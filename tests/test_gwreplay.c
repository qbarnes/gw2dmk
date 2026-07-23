/*
 * Validate the replay module: parsing of -U transaction logfiles into
 * per-(cyl,head) flux stream FIFOs, and the protocol responder that
 * serves them back through the gw I/O backend.
 */

#include "gwreplay.h"
#include "gwx.h"

#include "test.h"


/*
 * A small synthetic capture: GET_INFO (payload split across chunked
 * reads and a continuation line), a RESET, then two READ_FLUX passes
 * at cyl 2 head 1, the second with a non-OKAY flux status.
 */
static const char basic_log[] =
	"-> 0x00 0x03 0x00\n"
	"<- 0x00 0x00\n"
	"<- 0x01 0x06 0x01 0x16 0x00 0xa2 0x4a 0x04 0x07 0x04 0x00 0x00"
	" 0xd8 0x00 0x00 0x01\n"
	"   0x80 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00"
	" 0x00 0x00 0x00 0x00\n"
	"-> 0x10 0x02\n"
	"<- 0x10 0x00\n"
	"-> 0x02 0x03 0x02\n"
	"<- 0x02 0x00\n"
	"-> 0x03 0x03 0x01\n"
	"<- 0x03 0x00\n"
	"-> 0x07 0x08 0x00 0x00 0x00 0x00 0x02 0x00\n"
	"<- 0x07 0x00\n"
	"<- 0x32 0x33\n"
	"<- 0x34 0x00\n"
	"-> 0x09 0x02\n"
	"<- 0x09 0x00\n"
	"-> 0x07 0x08 0x00 0x00 0x00 0x00 0x02 0x00\n"
	"<- 0x07 0x00\n"
	"<- 0x41 0x42 0x43 0x00\n"
	"-> 0x09 0x02\n"
	"<- 0x09 0x04\n";


static int
parse_string(const char *text, struct gw_replay_log *log)
{
	FILE	*fp = fmemopen((void *)text, strlen(text), "r");
	int	ret = gw_replay_parse(fp, log);

	fclose(fp);

	return ret;
}


static void
test_parse_basic(void)
{
	struct gw_replay_log	log;

	memset(&log, 0, sizeof(log));
	CHECK_EQ(parse_string(basic_log, &log), 0);

	CHECK(log.have_getinfo);
	CHECK_EQ(log.sample_freq, 72000000);
	CHECK_EQ(log.getinfo[0], 0x01);
	CHECK_EQ(log.getinfo[16], 0x80);
	CHECK_EQ(log.warnings, 0);
	CHECK_EQ(log.nstreams, 2);

	CHECK_EQ(log.pos[2][1].cnt, 2);
	CHECK_EQ(log.pos[0][0].cnt, 0);

	const struct gw_replay_flux	*flux = &log.pos[2][1].flux[0];

	CHECK_EQ(flux->cnt, 4);
	CHECK(!memcmp(flux->buf, "\x32\x33\x34\x00", 4));
	CHECK_EQ(flux->status, 0);

	flux = &log.pos[2][1].flux[1];
	CHECK_EQ(flux->cnt, 4);
	CHECK(!memcmp(flux->buf, "\x41\x42\x43\x00", 4));
	CHECK_EQ(flux->status, 4);

	gw_replay_log_free(&log);
}


static void
test_parse_truncated(void)
{
	/* Capture interrupted mid-stream: no 0 terminator, no status. */
	static const char trunc_log[] =
		"-> 0x02 0x03 0x01\n"
		"<- 0x02 0x00\n"
		"-> 0x07 0x08 0x00 0x00 0x00 0x00 0x02 0x00\n"
		"<- 0x07 0x00\n"
		"<- 0x55 0x56\n";

	struct gw_replay_log	log;

	memset(&log, 0, sizeof(log));
	CHECK_EQ(parse_string(trunc_log, &log), 0);

	CHECK_EQ(log.pos[1][0].cnt, 1);

	const struct gw_replay_flux	*flux = &log.pos[1][0].flux[0];

	CHECK_EQ(flux->cnt, 3);
	CHECK(!memcmp(flux->buf, "\x55\x56\x00", 3));
	CHECK(log.warnings > 0);

	gw_replay_log_free(&log);
}


static void
test_parse_junk(void)
{
	/* Junk lines and malformed transfers are warned about and
	 * skipped without derailing the surrounding exchanges. */
	static const char junk_log[] =
		"gw2dmk version: 0.0.1\n"
		"-> 0x02 0x03 0x03\n"
		"<- 0x02 0x00\n"
		"-> 0x07 0x08 0x00 0x00 0x00 0x00 0x02 0x00\n"
		"<- 0x07 0x00\n"
		"<- 0x99 0x00\n"
		"-> 0xzz\n";

	struct gw_replay_log	log;

	memset(&log, 0, sizeof(log));
	CHECK_EQ(parse_string(junk_log, &log), 0);

	CHECK_EQ(log.pos[3][0].cnt, 1);
	CHECK_EQ(log.pos[3][0].flux[0].cnt, 2);
	CHECK(log.warnings >= 2);

	gw_replay_log_free(&log);
}


/*
 * Drive the responder through the public gw I/O entry points, as
 * gw2dmk would: command writes, exact-count ack reads, and the
 * 1-byte-then-bytes-waiting flux drain pattern.
 */

static int
run_command(const uint8_t *cmd, size_t cmd_cnt, uint8_t *ack)
{
	if (gw_write(GW_REPLAY_DEVT, cmd, cmd_cnt) != (ssize_t)cmd_cnt)
		return -1;

	if (gw_read(GW_REPLAY_DEVT, ack, 2) != 2)
		return -1;

	return 0;
}


static ssize_t
drain_flux(uint8_t *buf, size_t buf_cap)
{
	size_t	cnt = 0;

	do {
		if (cnt == buf_cap ||
		    gw_read(GW_REPLAY_DEVT, buf + cnt, 1) != 1)
			return -1;

		++cnt;

		ssize_t	nrd = gw_bytes_waiting(GW_REPLAY_DEVT);

		if (nrd < 0 || cnt + nrd > buf_cap)
			return -1;

		if (nrd > 0) {
			if (gw_read(GW_REPLAY_DEVT, buf + cnt, nrd) != nrd)
				return -1;
			cnt += nrd;
		}
	} while (buf[cnt - 1] != 0);

	return cnt;
}


static void
test_responder(void)
{
	struct gw_replay_log	log;

	memset(&log, 0, sizeof(log));
	CHECK_EQ(parse_string(basic_log, &log), 0);
	CHECK_EQ(gw_replay_start_parsed(&log), 0);
	CHECK(gw_replay_active());

	uint8_t	ack[2];
	uint8_t	buf[64];

	/* GET_INFO returns the recorded 32-byte payload. */
	CHECK_EQ(run_command((uint8_t[]){ 0x00, 3, 0 }, 3, ack), 0);
	CHECK_EQ(ack[0], 0x00);
	CHECK_EQ(ack[1], 0);
	CHECK_EQ(gw_bytes_waiting(GW_REPLAY_DEVT), 32);
	CHECK_EQ(gw_read(GW_REPLAY_DEVT, buf, 32), 32);
	CHECK_EQ(buf[0], 0x01);
	CHECK_EQ(buf[16], 0x80);

	/* Seek cyl 2, head 1, then read both recorded passes. */
	CHECK_EQ(run_command((uint8_t[]){ 0x02, 3, 2 }, 3, ack), 0);
	CHECK_EQ(ack[1], 0);
	CHECK_EQ(run_command((uint8_t[]){ 0x03, 3, 1 }, 3, ack), 0);
	CHECK_EQ(ack[1], 0);

	CHECK_EQ(gw_replay_flux_avail(2, 1), GW_REPLAY_AVAIL);
	CHECK_EQ(gw_replay_flux_avail(0, 0), GW_REPLAY_NEVER);

	const uint8_t	read_flux[] = { 0x07, 8, 0, 0, 0, 0, 2, 0 };

	CHECK_EQ(run_command(read_flux, sizeof(read_flux), ack), 0);
	CHECK_EQ(ack[1], 0);
	CHECK_EQ(drain_flux(buf, sizeof(buf)), 4);
	CHECK(!memcmp(buf, "\x32\x33\x34\x00", 4));
	CHECK_EQ(run_command((uint8_t[]){ 0x09, 2 }, 2, ack), 0);
	CHECK_EQ(ack[1], 0);

	CHECK_EQ(run_command(read_flux, sizeof(read_flux), ack), 0);
	CHECK_EQ(drain_flux(buf, sizeof(buf)), 4);
	CHECK(!memcmp(buf, "\x41\x42\x43\x00", 4));
	CHECK_EQ(run_command((uint8_t[]){ 0x09, 2 }, 2, ack), 0);
	CHECK_EQ(ack[1], 4);	/* recorded non-OKAY status */

	CHECK_EQ(gw_replay_flux_avail(2, 1), GW_REPLAY_EXHAUSTED);

	/* Exhausted: a synthetic empty revolution is served. */
	CHECK_EQ(run_command(read_flux, sizeof(read_flux), ack), 0);
	CHECK_EQ(ack[1], 0);

	ssize_t	cnt = drain_flux(buf, sizeof(buf));

	CHECK_EQ(cnt, 19);
	CHECK_EQ(buf[0], 255);
	CHECK_EQ(buf[1], 1);			/* FLUXOP_INDEX */
	CHECK_EQ(buf[7], 2);			/* FLUXOP_SPACE */
	CHECK_EQ(gw_read_28(&buf[8]), 72000000 / 5);
	CHECK_EQ(buf[cnt - 1], 0);
	CHECK_EQ(run_command((uint8_t[]){ 0x09, 2 }, 2, ack), 0);
	CHECK_EQ(ack[1], 0);

	/* Unknown commands are rejected, housekeeping ones acked. */
	CHECK_EQ(run_command((uint8_t[]){ 0x08, 2 }, 2, ack), 0);
	CHECK_EQ(ack[1], 1);			/* ACK_BAD_COMMAND */
	CHECK_EQ(run_command((uint8_t[]){ 0x10, 2 }, 2, ack), 0);
	CHECK_EQ(ack[1], 0);

	gw_replay_finish();
	CHECK(!gw_replay_active());
}


int
main(void)
{
	test_parse_basic();
	test_parse_truncated();
	test_parse_junk();
	test_responder();

	return test_exit("test_gwreplay");
}
