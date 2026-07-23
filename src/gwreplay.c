/*
 * Replay a Greaseweazle transaction logfile (as written via -U) in
 * place of a physical device.
 *
 * The logfile is a wire trace: every host-to-GW write is dumped
 * prefixed "-> ", every GW-to-host read prefixed "<- ", 16 "0x%02x"
 * tokens per line with continuation lines starting with 3 spaces.
 * The parser reconstructs the captured session into per-(cyl,head)
 * FIFOs of recorded flux streams, plus the recorded GET_INFO payload
 * (which carries the sample clock frequency).
 *
 * The responder half registers as a gw I/O backend and answers the
 * protocol commands of a replay run, serving the recorded flux
 * streams for CMD_READ_FLUX at the currently seeked position.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "misc.h"
#include "msg_levels.h"
#include "msg.h"
#include "greaseweazle.h"
#include "gw.h"
#include "gwx.h"
#include "gwreplay.h"


static uint32_t
le32_get(const uint8_t *p)
{
	return (uint32_t)p[0] |
	       ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) |
	       ((uint32_t)p[3] << 24);
}


static int
buf_append(uint8_t **buf, size_t *cnt, size_t *cap,
	   const uint8_t *src, size_t src_cnt)
{
	size_t	need = *cnt + src_cnt;

	if (need > *cap) {
		size_t	new_cap = *cap ? *cap : 4096;

		while (new_cap < need)
			new_cap *= 2;

		uint8_t	*new_buf = realloc(*buf, new_cap);

		if (!new_buf)
			return -1;

		*buf = new_buf;
		*cap = new_cap;
	}

	memcpy(*buf + *cnt, src, src_cnt);
	*cnt += src_cnt;

	return 0;
}


/*
 * Append a recorded flux stream (including its 0 terminator) to the
 * FIFO for (cyl,head).  Returns the new entry or NULL on failure.
 */

static struct gw_replay_flux *
flux_append(struct gw_replay_log *log, int cyl, int head,
	    const uint8_t *buf, size_t cnt)
{
	struct gw_replay_pos	*pos = &log->pos[cyl][head];

	if (!(pos->cnt % 8)) {
		struct gw_replay_flux *new_flux = realloc(pos->flux,
			(pos->cnt + 8) * sizeof(*pos->flux));

		if (!new_flux)
			return NULL;

		pos->flux = new_flux;
	}

	struct gw_replay_flux	*flux = &pos->flux[pos->cnt];

	flux->buf = malloc(cnt);

	if (!flux->buf)
		return NULL;

	memcpy(flux->buf, buf, cnt);
	flux->cnt    = cnt;
	flux->status = ACK_OKAY;

	++pos->cnt;
	++log->nstreams;

	return flux;
}


/*
 * Logfile parser.
 *
 * Host transfers are always whole command frames (gw_do_command
 * writes each command in a single gw_write), while responses are
 * chunked over multiple reads and must be concatenated.  A command's
 * complete response is on hand once the next host transfer (or EOF)
 * is seen, so resolution of a pending command is deferred until then.
 */

struct parse_state {
	struct gw_replay_log	*log;
	int			cur_cyl;
	int			cur_head;
	/* pending host command frame awaiting its response */
	uint8_t			pcmd[64];
	size_t			pcmd_cnt;
	int			pcyl, phead;
	/* accumulated response bytes for the pending command */
	uint8_t			*dbuf;
	size_t			dbuf_cnt, dbuf_cap;
	/* most recently appended flux stream, for GET_FLUX_STATUS */
	struct gw_replay_flux	*last_flux;
};


/*
 * Consume the accumulated response bytes for the pending command.
 * Returns 0 on success, -1 on memory failure.
 */

static int
resolve_pending(struct parse_state *ps)
{
	struct gw_replay_log	*log = ps->log;
	size_t			used = 0;

	if (ps->pcmd_cnt == 0) {
		if (ps->dbuf_cnt)
			++log->warnings;
		goto done;
	}

	if (ps->dbuf_cnt < 2) {
		/* Truncated exchange (e.g. interrupted capture). */
		++log->warnings;
		goto done;
	}

	uint8_t	cmd = ps->pcmd[0];
	uint8_t	ack = ps->dbuf[1];

	used = 2;

	if (ps->dbuf[0] != cmd) {
		++log->warnings;
		goto done;
	}

	switch (cmd) {
	case CMD_GET_INFO:
		if (ack == ACK_OKAY &&
		    ps->pcmd_cnt >= 3 &&
		    ps->pcmd[2] == GETINFO_FIRMWARE &&
		    ps->dbuf_cnt - used >= sizeof(log->getinfo)) {
			if (!log->have_getinfo) {
				memcpy(log->getinfo, &ps->dbuf[used],
				       sizeof(log->getinfo));
				log->sample_freq =
					le32_get(&log->getinfo[4]);
				log->have_getinfo = true;
			}
			used += sizeof(log->getinfo);
		} else if (ack == ACK_OKAY) {
			used = ps->dbuf_cnt;
		}
		break;

	case CMD_GET_PARAMS:
		if (ack == ACK_OKAY)
			used = ps->dbuf_cnt;
		break;

	case CMD_READ_FLUX:
		if (ack != ACK_OKAY)
			break;

		uint8_t	*term = memchr(&ps->dbuf[used], 0,
				       ps->dbuf_cnt - used);
		size_t	stream_cnt;
		uint8_t	*stream = &ps->dbuf[used];

		if (term) {
			stream_cnt = term - stream + 1;
		} else {
			/* Truncated capture; add the terminator. */
			stream_cnt = ps->dbuf_cnt - used;
			if (buf_append(&ps->dbuf, &ps->dbuf_cnt,
				       &ps->dbuf_cap,
				       (const uint8_t *)"", 1))
				return -1;
			stream = &ps->dbuf[used];
			++stream_cnt;
			++log->warnings;
		}

		if (stream_cnt > 1) {
			ps->last_flux = flux_append(log, ps->pcyl,
						    ps->phead,
						    stream, stream_cnt);
			if (!ps->last_flux)
				return -1;
		}

		used += stream_cnt;
		break;

	case CMD_GET_FLUX_STATUS:
		if (ps->last_flux)
			ps->last_flux->status = ack;
		break;

	default:
		break;
	}

	if (ps->dbuf_cnt > used)
		++log->warnings;

done:
	ps->pcmd_cnt = 0;
	ps->dbuf_cnt = 0;

	return 0;
}


/*
 * Process a complete host command frame: resolve the previous
 * command, then track drive position and set the new pending command.
 */

static int
host_frame(struct parse_state *ps, const uint8_t *frame, size_t cnt)
{
	if (resolve_pending(ps))
		return -1;

	if (cnt < 2 || cnt > sizeof(ps->pcmd) || frame[1] != cnt) {
		++ps->log->warnings;
		return 0;
	}

	switch (frame[0]) {
	case CMD_SEEK:
		if (frame[2] < GW_MAX_TRACKS)
			ps->cur_cyl = frame[2];
		else
			++ps->log->warnings;
		break;

	case CMD_HEAD:
		ps->cur_head = frame[2] & 1;
		break;

	default:
		break;
	}

	memcpy(ps->pcmd, frame, cnt);
	ps->pcmd_cnt = cnt;
	ps->pcyl  = ps->cur_cyl;
	ps->phead = ps->cur_head;

	return 0;
}


/*
 * Scan a logfile line's "0x%02x" tokens into bytes.
 * Returns the byte count, or -1 if the line is malformed.
 */

static int
hex_line(const char *p, uint8_t *bytes, int bytes_cap)
{
	int	cnt = 0;

	for (;;) {
		while (*p == ' ')
			++p;

		if (*p == '\n' || *p == '\0')
			return cnt;

		if (cnt == bytes_cap || p[0] != '0' || p[1] != 'x')
			return -1;

		char		*ep;
		unsigned long	v = strtoul(p + 2, &ep, 16);

		if (ep != p + 4 || v > 0xff)
			return -1;

		bytes[cnt++] = v;
		p = ep;
	}
}


/*
 * Parse a -U transaction logfile into "log", which must be zeroed by
 * the caller.  Returns 0 on success (parse errors are tolerated and
 * counted in log->warnings), or -1 on failure.
 */

int
gw_replay_parse(FILE *fp, struct gw_replay_log *log)
{
	struct parse_state	ps = { .log = log };
	char			line[512];
	uint8_t			bytes[16];
	uint8_t			hbuf[64];
	size_t			hbuf_cnt = 0;
	bool			in_host = false;
	int			ret = -1;

	while (fgets(line, sizeof(line), fp)) {
		bool	cont = !strncmp(line, "   ", 3) &&
			       (line[3] == '0');
		bool	host = !strncmp(line, "-> ", 3);
		bool	dev  = !strncmp(line, "<- ", 3);

		if (!cont && !host && !dev) {
			++log->warnings;
			continue;
		}

		int	cnt = hex_line(line + 3, bytes, sizeof(bytes));

		if (cnt < 0) {
			++log->warnings;
			continue;
		}

		if (!cont && in_host) {
			if (host_frame(&ps, hbuf, hbuf_cnt))
				goto fail;
			in_host = false;
			hbuf_cnt = 0;
		}

		if (host || (cont && in_host)) {
			if (hbuf_cnt + cnt <= sizeof(hbuf))
				memcpy(hbuf + hbuf_cnt, bytes, cnt);
			else
				++log->warnings;
			hbuf_cnt += cnt;
			in_host = true;
		} else {
			if (buf_append(&ps.dbuf, &ps.dbuf_cnt,
				       &ps.dbuf_cap, bytes, cnt))
				goto fail;
		}
	}

	if (in_host && host_frame(&ps, hbuf, hbuf_cnt))
		goto fail;

	if (resolve_pending(&ps))
		goto fail;

	ret = 0;
fail:
	free(ps.dbuf);

	return ret;
}


void
gw_replay_log_free(struct gw_replay_log *log)
{
	for (int cyl = 0; cyl < GW_MAX_TRACKS; ++cyl) {
		for (int head = 0; head < 2; ++head) {
			struct gw_replay_pos *pos = &log->pos[cyl][head];

			for (int i = 0; i < pos->cnt; ++i)
				free(pos->flux[i].buf);

			free(pos->flux);
		}
	}

	memset(log, 0, sizeof(*log));
}


/*
 * Responder: a virtual Greaseweazle registered as a gw I/O backend.
 * Commands are framed as on the wire (command byte, frame length);
 * whole responses are queued before the host reads, so the host's
 * exact-count reads and the flux drain loop behave as with a device.
 */

static struct gw_replay {
	bool			active;
	struct gw_replay_log	log;
	int			cur_cyl;
	int			cur_head;
	uint8_t			flux_status;
	uint8_t			cbuf[64];
	size_t			cbuf_cnt;
	uint8_t			*outq;
	size_t			outq_cnt, outq_off, outq_cap;
} rp;


static int
outq_push(const uint8_t *buf, size_t cnt)
{
	return buf_append(&rp.outq, &rp.outq_cnt, &rp.outq_cap, buf, cnt);
}


static int
push_ack(uint8_t cmd, uint8_t ack)
{
	return outq_push((uint8_t[]){ cmd, ack }, 2);
}


/*
 * Serve a synthetic empty revolution (two index marks a nominal
 * 300 RPM apart, no pulses) when the recorded passes are exhausted,
 * so detection paths see an unformatted track rather than an error.
 */

static int
push_empty_rev(void)
{
	uint32_t	rev_ticks = rp.log.sample_freq / 5;
	uint8_t		sbuf[16];
	int		i = 0;

	sbuf[i++] = 255;
	sbuf[i++] = FLUXOP_INDEX;
	gw_write_28(1, &sbuf[i]);
	i += 4;

	sbuf[i++] = 255;
	sbuf[i++] = FLUXOP_SPACE;
	gw_write_28(rev_ticks, &sbuf[i]);
	i += 4;

	sbuf[i++] = 255;
	sbuf[i++] = FLUXOP_INDEX;
	gw_write_28(1, &sbuf[i]);
	i += 4;

	sbuf[i++] = 0;

	return outq_push(sbuf, i);
}


static int
dispatch(const uint8_t *frame, size_t cnt)
{
	uint8_t	cmd = frame[0];

	switch (cmd) {
	case CMD_GET_INFO: {
		if (push_ack(cmd, ACK_OKAY))
			return -1;

		if (frame[2] == GETINFO_FIRMWARE)
			return outq_push(rp.log.getinfo,
					 sizeof(rp.log.getinfo));

		uint8_t	zeros[32] = { 0 };

		return outq_push(zeros, sizeof(zeros));
	}

	case CMD_GET_PARAMS: {
		if (push_ack(cmd, ACK_OKAY))
			return -1;

		uint8_t	pzeros[32] = { 0 };
		size_t	nr = frame[3] < sizeof(pzeros) ?
			     frame[3] : sizeof(pzeros);

		return outq_push(pzeros, nr);
	}

	case CMD_SEEK:
		if (frame[2] >= GW_MAX_TRACKS)
			return push_ack(cmd, ACK_BAD_CYLINDER);

		rp.cur_cyl = frame[2];

		return push_ack(cmd, ACK_OKAY);

	case CMD_HEAD:
		rp.cur_head = frame[2] & 1;

		return push_ack(cmd, ACK_OKAY);

	case CMD_READ_FLUX: {
		if (push_ack(cmd, ACK_OKAY))
			return -1;

		struct gw_replay_pos *pos =
			&rp.log.pos[rp.cur_cyl][rp.cur_head];

		if (pos->next < pos->cnt) {
			struct gw_replay_flux *flux =
				&pos->flux[pos->next++];

			rp.flux_status = flux->status;

			return outq_push(flux->buf, flux->cnt);
		}

		rp.flux_status = ACK_OKAY;

		return push_empty_rev();
	}

	case CMD_GET_FLUX_STATUS:
		return push_ack(cmd, rp.flux_status);

	case CMD_SET_PARAMS:
	case CMD_MOTOR:
	case CMD_SELECT:
	case CMD_DESELECT:
	case CMD_SET_BUS_TYPE:
	case CMD_SET_PIN:
	case CMD_RESET:
		return push_ack(cmd, ACK_OKAY);

	default:
		return push_ack(cmd, ACK_BAD_COMMAND);
	}
}


static ssize_t
rp_bwrite(void *ctx, const uint8_t *wbuf, size_t wbuf_cnt)
{
	for (size_t i = 0; i < wbuf_cnt; ++i) {
		if (rp.cbuf_cnt == sizeof(rp.cbuf)) {
			/* Overlong frame; drop a byte to resync. */
			memmove(rp.cbuf, rp.cbuf + 1, --rp.cbuf_cnt);
		}

		rp.cbuf[rp.cbuf_cnt++] = wbuf[i];

		while (rp.cbuf_cnt >= 2) {
			size_t	len = rp.cbuf[1];

			if (len < 2 || len > sizeof(rp.cbuf)) {
				memmove(rp.cbuf, rp.cbuf + 1,
					--rp.cbuf_cnt);
				continue;
			}

			if (rp.cbuf_cnt < len)
				break;

			if (dispatch(rp.cbuf, len) == -1) {
				errno = ENOMEM;
				return -1;
			}

			rp.cbuf_cnt -= len;
			memmove(rp.cbuf, rp.cbuf + len, rp.cbuf_cnt);
		}
	}

	return wbuf_cnt;
}


static ssize_t
rp_bread(void *ctx, uint8_t *rbuf, size_t rbuf_cnt)
{
	size_t	avail = rp.outq_cnt - rp.outq_off;

	if (avail == 0) {
		/* Host and responder are out of step. */
		errno = EIO;
		return -1;
	}

	size_t	cnt = rbuf_cnt < avail ? rbuf_cnt : avail;

	memcpy(rbuf, rp.outq + rp.outq_off, cnt);
	rp.outq_off += cnt;

	if (rp.outq_off == rp.outq_cnt)
		rp.outq_off = rp.outq_cnt = 0;

	return cnt;
}


static ssize_t
rp_bytes_waiting(void *ctx)
{
	return rp.outq_cnt - rp.outq_off;
}


static const struct gw_backend_ops gw_replay_ops = {
	.bread		= rp_bread,
	.bwrite		= rp_bwrite,
	.bytes_waiting	= rp_bytes_waiting,
};


/*
 * Install a parsed log as the active replay session, taking
 * ownership of its contents.  "log" itself is emptied.
 */

int
gw_replay_start_parsed(struct gw_replay_log *log)
{
	if (rp.active || !log->have_getinfo || log->nstreams == 0)
		return -1;

	rp.log = *log;
	memset(log, 0, sizeof(*log));

	rp.active      = true;
	rp.cur_cyl     = 0;
	rp.cur_head    = 0;
	rp.flux_status = ACK_OKAY;

	gw_set_backend(&gw_replay_ops, NULL);

	return 0;
}


int
gw_replay_start(const char *path)
{
	FILE	*fp = fopen(path, "r");

	if (!fp)
		return -1;

	struct gw_replay_log	log;

	memset(&log, 0, sizeof(log));

	int	ret = gw_replay_parse(fp, &log);

	fclose(fp);

	if (ret == 0)
		ret = gw_replay_start_parsed(&log);

	if (ret != 0) {
		gw_replay_log_free(&log);
		return -1;
	}

	if (rp.log.warnings)
		msg(MSG_ERRORS, "Replay log parsed with %d warning%s.\n",
		    rp.log.warnings, plu(rp.log.warnings));

	return 0;
}


void
gw_replay_finish(void)
{
	if (!rp.active)
		return;

	gw_set_backend(NULL, NULL);

	gw_replay_log_free(&rp.log);

	free(rp.outq);

	memset(&rp, 0, sizeof(rp));
}


bool
gw_replay_active(void)
{
	return rp.active;
}


enum gw_replay_avail
gw_replay_flux_avail(int cyl, int head)
{
	if (cyl < 0 || cyl >= GW_MAX_TRACKS || head < 0 || head > 1)
		return GW_REPLAY_NEVER;

	struct gw_replay_pos	*pos = &rp.log.pos[cyl][head];

	if (pos->cnt == 0)
		return GW_REPLAY_NEVER;

	return (pos->next < pos->cnt) ? GW_REPLAY_AVAIL
				      : GW_REPLAY_EXHAUSTED;
}
