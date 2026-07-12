#include <stdlib.h>
#include <string.h>

#include "greaseweazle.h"
#include "gw.h"
#include "gwx.h"

#include "simbus.h"
#include "simclock.h"
#include "simdrive.h"
#include "simflux.h"
#include "simgw.h"
#include "simproto.h"

/* Cap on captured write-stream size (far beyond any real track). */
#define WRBUF_MAX	(4 * 1024 * 1024)

/* Wall time the firmware waits before giving up on an index pulse. */
#define NO_INDEX_MS	500


static uint16_t
get_le16(const uint8_t *p)
{
	return p[0] | (p[1] << 8);
}


static uint32_t
get_le32(const uint8_t *p)
{
	return p[0] | (p[1] << 8) | ((uint32_t)p[2] << 16) |
	       ((uint32_t)p[3] << 24);
}


static int
reply(struct sim_proto *sp, uint8_t ack, const void *payload, size_t cnt)
{
	uint8_t	hdr[2] = { sp->cbuf[0], ack };

	if (sim_pty_write_all(sp->pty, hdr, 2) == -1)
		return -1;

	if (ack == ACK_OKAY && payload && cnt)
		return sim_pty_write_all(sp->pty, payload, cnt);

	return 0;
}


void
sim_proto_init(struct sim_proto *sp, struct sim_gw *gw,
	       struct sim_pty *pty)
{
	memset(sp, 0, sizeof(*sp));

	sp->gw	= gw;
	sp->pty	= pty;

	sim_proto_reset(sp);
}


void
sim_proto_reset(struct sim_proto *sp)
{
	sp->state    = PROTO_CMD;
	sp->cbuf_cnt = 0;
	sp->wbuf_cnt = 0;
}


static int
do_get_info(struct sim_proto *sp)
{
	const struct sim_gw_model	*m = sp->gw->model;
	uint8_t				rbuf[32];

	memset(rbuf, 0, sizeof(rbuf));

	switch (sp->cbuf[2]) {
	case GETINFO_FIRMWARE:
		rbuf[0] = m->fw_major;
		rbuf[1] = m->fw_minor;
		rbuf[2] = 1;		/* is_main_firmware */
		rbuf[3] = m->max_cmd;
		rbuf[4] = m->sample_freq & 0xff;
		rbuf[5] = (m->sample_freq >> 8) & 0xff;
		rbuf[6] = (m->sample_freq >> 16) & 0xff;
		rbuf[7] = (m->sample_freq >> 24) & 0xff;
		rbuf[8] = m->hw_model;
		rbuf[9] = m->hw_submodel;
		rbuf[10] = m->usb_speed;
		break;

	case GETINFO_BW_STATS:
		/* Plausible fixed 8 Mbps min and max. */
		for (int i = 0; i < 24; i += 8) {
			rbuf[i]     = 0x40;	/* bytes = 1000000 */
			rbuf[i + 1] = 0x42;
			rbuf[i + 2] = 0x0f;
			rbuf[i + 4] = 0x40;	/* usecs = 1000000 */
			rbuf[i + 5] = 0x42;
			rbuf[i + 6] = 0x0f;
		}
		break;

	default:
		return reply(sp, ACK_BAD_COMMAND, NULL, 0);
	}

	return reply(sp, ACK_OKAY, rbuf, sizeof(rbuf));
}


static int
do_seek(struct sim_proto *sp)
{
	struct sim_gw	*gw  = sp->gw;
	int		cyl  = sp->cbuf[2];

	if (gw->sel_unit < 0)
		return reply(sp, ACK_NO_UNIT, NULL, 0);

	if (cyl >= GW_MAX_TRACKS)
		return reply(sp, ACK_BAD_CYLINDER, NULL, 0);

	struct sim_drive	*drv = sim_gw_sel_drive(gw);

	if (!drv) {
		/* No TRK0 sensor ever asserts on an empty position. */
		sim_sleep_ms(gw->delays.seek_settle);
		return reply(sp, ACK_NO_TRK0, NULL, 0);
	}

	int	fw_cyl = gw->fw_cyl[gw->sel_unit];
	int	steps  = (fw_cyl < 0) ? drv->tracks + cyl
				      : abs(fw_cyl - cyl);

	sim_sleep_us((uint64_t)steps * gw->delays.step_delay);
	sim_sleep_ms(gw->delays.seek_settle);

	gw->fw_cyl[gw->sel_unit] = cyl;
	sim_drive_seek(drv, cyl);

	return reply(sp, ACK_OKAY, NULL, 0);
}


static int
do_motor(struct sim_proto *sp)
{
	struct sim_gw	*gw   = sp->gw;
	int		unit  = sp->cbuf[2];
	int		on    = sp->cbuf[3];

	if (gw->bus == BUS_NONE)
		return reply(sp, ACK_NO_BUS, NULL, 0);

	if (unit < 0 || unit >= sim_bus_max_units(gw->bus))
		return reply(sp, ACK_BAD_UNIT, NULL, 0);

	struct sim_drive	*drv = gw->unit[unit];

	if (drv) {
		bool	was_spinning = sim_drive_spinning(drv);

		sim_drive_motor(drv, on != 0);

		if (on && !was_spinning)
			sim_sleep_ms(gw->delays.motor_delay);
	}

	return reply(sp, ACK_OKAY, NULL, 0);
}


static int
do_select(struct sim_proto *sp)
{
	struct sim_gw	*gw   = sp->gw;
	int		unit  = sp->cbuf[2];

	if (gw->bus == BUS_NONE)
		return reply(sp, ACK_NO_BUS, NULL, 0);

	if (unit < 0 || unit >= sim_bus_max_units(gw->bus))
		return reply(sp, ACK_BAD_UNIT, NULL, 0);

	gw->sel_unit = unit;
	sim_sleep_us(gw->delays.select_delay);

	return reply(sp, ACK_OKAY, NULL, 0);
}


static int
do_set_bus_type(struct sim_proto *sp)
{
	int	bus = sp->cbuf[2];

	if (bus != BUS_IBMPC && bus != BUS_SHUGART)
		return reply(sp, ACK_BAD_COMMAND, NULL, 0);

	sp->gw->bus = bus;

	return reply(sp, ACK_OKAY, NULL, 0);
}


static int
do_set_pin(struct sim_proto *sp)
{
	int	pin   = sp->cbuf[2];
	int	level = sp->cbuf[3];

	if (pin != 2)
		return reply(sp, ACK_BAD_PIN, NULL, 0);

	sp->gw->densel = level;

	return reply(sp, ACK_OKAY, NULL, 0);
}


static int
do_get_params(struct sim_proto *sp)
{
	struct gw_delay	*d  = &sp->gw->delays;
	int		nr  = sp->cbuf[3];
	uint8_t		rbuf[10];

	if (sp->cbuf[2] != PARAMS_DELAYS || nr > (int)sizeof(rbuf))
		return reply(sp, ACK_BAD_COMMAND, NULL, 0);

	uint16_t	v[5] = {
		d->select_delay, d->step_delay, d->seek_settle,
		d->motor_delay, d->auto_off
	};

	for (int i = 0; i < 5; ++i) {
		rbuf[i * 2]	= v[i] & 0xff;
		rbuf[i * 2 + 1]	= v[i] >> 8;
	}

	return reply(sp, ACK_OKAY, rbuf, nr);
}


static int
do_set_params(struct sim_proto *sp)
{
	struct gw_delay	*d = &sp->gw->delays;

	if (sp->cbuf[2] != PARAMS_DELAYS || sp->cbuf[1] != 3 + 10)
		return reply(sp, ACK_BAD_COMMAND, NULL, 0);

	const uint8_t	*p = &sp->cbuf[3];

	d->select_delay	= get_le16(p);
	d->step_delay	= get_le16(p + 2);
	d->seek_settle	= get_le16(p + 4);
	d->motor_delay	= get_le16(p + 6);
	d->auto_off	= get_le16(p + 8);

	return reply(sp, ACK_OKAY, NULL, 0);
}


/* Rotational position, in ticks past the index hole. */
static uint64_t
drive_phase(struct sim_drive *drv, uint64_t rev_ticks, uint32_t freq)
{
	if (sim_fast)
		return drv->phase_ticks % rev_ticks;

	double	sec = (sim_now_ns() - drv->spin_ref_ns) / 1e9;

	return (uint64_t)(sec * freq) % rev_ticks;
}


static int
do_read_flux(struct sim_proto *sp)
{
	struct sim_gw	*gw	   = sp->gw;
	uint32_t	freq	   = gw->model->sample_freq;
	uint32_t	max_ticks  = 0;
	unsigned	max_index  = 0;

	if (sp->cbuf[1] >= 6)
		max_ticks = get_le32(&sp->cbuf[2]);
	if (sp->cbuf[1] >= 8)
		max_index = get_le16(&sp->cbuf[6]);

	if (gw->sel_unit < 0)
		return reply(sp, ACK_NO_UNIT, NULL, 0);

	struct sim_drive	*drv = sim_gw_sel_drive(gw);

	if (!drv || !sim_drive_spinning(drv) || !drv->media) {
		/* No index pulses arrive; the read times out. */
		if (reply(sp, ACK_OKAY, NULL, 0) == -1)
			return -1;

		sim_sleep_ms(NO_INDEX_MS);
		gw->flux_status = ACK_NO_INDEX;

		return sim_pty_write_all(sp->pty, (uint8_t[]){ 0 }, 1);
	}

	struct sim_pulses	pul = { NULL, 0, 0 };
	struct sim_media	*media = drv->media;
	int			pr = 1;

	if (gw->head < drv->sides) {
		int	mtrack = sim_drive_media_track(drv, drv->cyl);

		pr = media->ops->track_pulses(media, mtrack, gw->head,
					      freq, drv->rpm,
					      &pul.p, &pul.cnt);
	}

	if (pr == 0) {
		sim_pulses_total(&pul);
	} else {
		free(pul.p);
		pul = (struct sim_pulses){ NULL, 0, 0 };

		if (sim_noise_pulses(freq, drv->rpm, &pul) == -1)
			return reply(sp, ACK_BAD_COMMAND, NULL, 0);
	}

	uint8_t		*stream	   = NULL;
	size_t		stream_cnt = 0;
	uint64_t	dur	   = 0;
	uint64_t	phase = drive_phase(drv, pul.total_ticks, freq);

	int	sr = sim_flux_stream(&pul, phase, max_index, max_ticks,
				     &stream, &stream_cnt, &dur);

	free(pul.p);

	if (sr == -1)
		return reply(sp, ACK_BAD_COMMAND, NULL, 0);

	drv->phase_ticks = (phase + dur) % pul.total_ticks;
	gw->flux_status	 = ACK_OKAY;

	int	ret = reply(sp, ACK_OKAY, NULL, 0);

	if (ret == 0) {
		sim_sleep_ticks(dur, freq);
		ret = sim_pty_write_all(sp->pty, stream, stream_cnt);
	}

	free(stream);

	return ret;
}


static int
do_write_flux(struct sim_proto *sp)
{
	struct sim_gw	*gw = sp->gw;

	if (gw->sel_unit < 0)
		return reply(sp, ACK_NO_UNIT, NULL, 0);

	struct sim_drive	*drv = sim_gw_sel_drive(gw);

	if (drv && drv->media && sim_drive_wp(drv))
		return reply(sp, ACK_WRPROT, NULL, 0);

	sp->wr_cue  = (sp->cbuf[1] > 2) ? sp->cbuf[2] != 0 : true;
	sp->wr_term = (sp->cbuf[1] > 3) ? sp->cbuf[3] != 0 : true;
	sp->wr_ok   = drv && drv->media && sim_drive_spinning(drv);

	sp->wbuf_cnt = 0;
	sp->state    = PROTO_WRSTREAM;

	return reply(sp, ACK_OKAY, NULL, 0);
}


/*
 * Parse a captured write stream into flux transition pulses.
 * Handles direct and two-byte tick codes, FLUXOP_SPACE, and
 * FLUXOP_ASTABLE (regular transitions filling the preceding space).
 * Returns 0 with a malloc'd pulse array, or -1 on error.
 */

static int
wrstream_pulses(const uint8_t *b, size_t cnt, uint32_t **out,
		size_t *out_cnt)
{
	uint32_t	*p   = NULL;
	size_t		n    = 0;
	size_t		len  = 0;
	uint64_t	ticks = 0;
	uint64_t	last  = 0;
	uint64_t	space_start   = 0;
	uint64_t	space_pending = 0;
	size_t		i = 0;

	while (i < cnt) {
		uint64_t	trans[1];
		size_t		ntrans = 0;
		uint64_t	astable_end = 0;
		uint32_t	astable_per = 0;

		uint8_t	c = b[i++];

		if (c == 255) {
			if (i + 5 > cnt)
				break;

			uint8_t		op = b[i++];
			uint32_t	v  = gw_read_28(&b[i]);

			i += 4;

			switch (op) {
			case FLUXOP_SPACE:
				if (space_pending == 0)
					space_start = ticks;

				space_pending += v;
				ticks	      += v;
				continue;

			case FLUXOP_ASTABLE:
				if (v == 0 || space_pending == 0)
					continue;

				astable_per = v;
				astable_end = space_start + space_pending;
				space_pending = 0;
				break;

			case FLUXOP_INDEX:
				continue;

			default:
				goto bad;
			}
		} else if (c < 250) {
			ticks	     += c;
			trans[0]      = ticks;
			ntrans	      = 1;
			space_pending = 0;
		} else {
			if (i >= cnt)
				break;

			ticks	     += 250 + (c - 250) * 255 + b[i++] - 1;
			trans[0]      = ticks;
			ntrans	      = 1;
			space_pending = 0;
		}

		uint64_t	t = ntrans ? trans[0]
					   : space_start + astable_per;

		for (;;) {
			if (n == len) {
				size_t	nlen = len ? len * 2 : 65536;
				uint32_t *np = realloc(p,
						nlen * sizeof(*np));

				if (!np)
					goto bad;

				p   = np;
				len = nlen;
			}

			if (t > last)
				p[n++] = t - last;

			last = t;

			if (ntrans)
				break;

			t += astable_per;

			if (t > astable_end)
				break;
		}
	}

	*out	 = p;
	*out_cnt = n;

	return 0;

bad:
	free(p);

	return -1;
}


/*
 * A complete write stream has arrived.  Decode it into the inserted
 * media, answer the host's synchronization read, and record the
 * outcome for CMD_GET_FLUX_STATUS.
 */

static int
wrstream_done(struct sim_proto *sp)
{
	struct sim_gw		*gw  = sp->gw;
	struct sim_drive	*drv = sim_gw_sel_drive(gw);

	sp->state = PROTO_CMD;

	if (!sp->wr_ok || !drv || !drv->media) {
		gw->flux_status = ACK_NO_INDEX;
	} else {
		struct sim_media	*media = drv->media;

		gw->flux_status = ACK_OKAY;

		if (media->ops->track_from_pulses &&
		    gw->head < drv->sides) {
			uint32_t	*pulses = NULL;
			size_t		cnt	= 0;

			if (wrstream_pulses(sp->wbuf, sp->wbuf_cnt,
					    &pulses, &cnt) == 0 &&
			    cnt > 0) {
				int	mtrack =
					sim_drive_media_track(drv,
							      drv->cyl);

				media->ops->track_from_pulses(media,
					mtrack, gw->head,
					gw->model->sample_freq,
					drv->rpm, pulses, cnt);
			}

			free(pulses);
		}

		/* Wait out the rotational time of the write. */
		sim_sleep_ms(60000 / drv->rpm);
	}

	/* Synchronize with the host (see gw_write_stream()). */
	return sim_pty_write_all(sp->pty, (uint8_t[]){ 0 }, 1);
}


static int
dispatch(struct sim_proto *sp)
{
	switch (sp->cbuf[0]) {
	case CMD_GET_INFO:
		return do_get_info(sp);

	case CMD_SEEK:
		return do_seek(sp);

	case CMD_HEAD:
		if (sp->cbuf[2] > 1)
			return reply(sp, ACK_BAD_COMMAND, NULL, 0);

		sp->gw->head = sp->cbuf[2];

		return reply(sp, ACK_OKAY, NULL, 0);

	case CMD_SET_PARAMS:
		return do_set_params(sp);

	case CMD_GET_PARAMS:
		return do_get_params(sp);

	case CMD_MOTOR:
		return do_motor(sp);

	case CMD_READ_FLUX:
		return do_read_flux(sp);

	case CMD_WRITE_FLUX:
		return do_write_flux(sp);

	case CMD_GET_FLUX_STATUS:
		return reply(sp, sp->gw->flux_status, NULL, 0);

	case CMD_SELECT:
		return do_select(sp);

	case CMD_DESELECT:
		sp->gw->sel_unit = -1;

		return reply(sp, ACK_OKAY, NULL, 0);

	case CMD_SET_BUS_TYPE:
		return do_set_bus_type(sp);

	case CMD_SET_PIN:
		return do_set_pin(sp);

	case CMD_RESET:
		sim_gw_reset(sp->gw);

		return reply(sp, ACK_OKAY, NULL, 0);

	default:
		return reply(sp, ACK_BAD_COMMAND, NULL, 0);
	}
}


static int
wrstream_byte(struct sim_proto *sp, uint8_t b)
{
	if (b == 0)
		return wrstream_done(sp);

	if (sp->wbuf_cnt == sp->wbuf_len) {
		if (sp->wbuf_len >= WRBUF_MAX) {
			/* Runaway stream; drop data but stay in sync. */
			return 0;
		}

		size_t	nlen = sp->wbuf_len ? sp->wbuf_len * 2 : 65536;
		uint8_t	*nb  = realloc(sp->wbuf, nlen);

		if (!nb)
			return 0;

		sp->wbuf     = nb;
		sp->wbuf_len = nlen;
	}

	sp->wbuf[sp->wbuf_cnt++] = b;

	return 0;
}


int
sim_proto_input(struct sim_proto *sp, const uint8_t *buf, size_t cnt)
{
	for (size_t i = 0; i < cnt; ++i) {
		if (sp->state == PROTO_WRSTREAM) {
			if (wrstream_byte(sp, buf[i]) == -1)
				return -1;

			continue;
		}

		sp->cbuf[sp->cbuf_cnt++] = buf[i];

		if (sp->cbuf_cnt < 2)
			continue;

		size_t	len = sp->cbuf[1];

		if (len < 2 || len > sizeof(sp->cbuf)) {
			/* Unrecoverable framing error; resync. */
			int	ret = reply(sp, ACK_BAD_COMMAND, NULL, 0);

			sp->cbuf_cnt = 0;

			if (ret == -1)
				return -1;

			continue;
		}

		if (sp->cbuf_cnt < len)
			continue;

		int	ret = dispatch(sp);

		sp->cbuf_cnt = 0;

		if (ret == -1)
			return -1;
	}

	return 0;
}
