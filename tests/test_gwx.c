/*
 * Validate the Greaseweazle flux stream codec: 28-bit value packing,
 * gw_decode_stream() opcode parsing, and encode_ticks() round trips.
 */

#include "gwx.h"

#include "test.h"


#define MAX_EVENTS	64

struct events {
	int		npulses;
	uint32_t	pulse[MAX_EVENTS];
	int		nimarks;
	uint32_t	imark[MAX_EVENTS];
	int		nspaces;
	uint32_t	space[MAX_EVENTS];
	int		pulse_status;	/* status for pulse callback */
};


static int
pulse_cb(uint32_t ticks, void *data)
{
	struct events	*ev = (struct events *)data;

	if (ev->npulses < MAX_EVENTS)
		ev->pulse[ev->npulses++] = ticks;

	return ev->pulse_status;
}


static int
imark_cb(uint32_t ticks, void *data)
{
	struct events	*ev = (struct events *)data;

	if (ev->nimarks < MAX_EVENTS)
		ev->imark[ev->nimarks++] = ticks;

	return 0;
}


static int
space_cb(uint32_t ticks, void *data)
{
	struct events	*ev = (struct events *)data;

	if (ev->nspaces < MAX_EVENTS)
		ev->space[ev->nspaces++] = ticks;

	return 0;
}


static ssize_t
decode(const uint8_t *buf, size_t cnt, struct events *ev)
{
	struct gw_decode_stream_s	gwds = {
		.ds_ticks      = 0,
		.ds_last_pulse = 0,
		.ds_status     = -1,
		.decoded_imark = imark_cb,
		.imark_data    = ev,
		.decoded_space = space_cb,
		.space_data    = ev,
		.decoded_pulse = pulse_cb,
		.pulse_data    = ev
	};

	return gw_decode_stream(buf, cnt, &gwds);
}


static void
test_rw28(void)
{
	static const uint32_t	vals[] = {
		0, 1, 249, 250, 0x1234567, 0x0fffffff
	};

	for (int i = 0; i < (int)(sizeof(vals)/sizeof(vals[0])); ++i) {
		uint8_t	buf[4];

		gw_write_28(vals[i], buf);
		CHECK_EQ(gw_read_28(buf), vals[i]);

		/* Protocol requires the low bit set in every byte. */
		for (int j = 0; j < 4; ++j)
			CHECK(buf[j] & 1);
	}
}


static void
test_decode_pulses(void)
{
	struct events	ev = {};

	/* Single-byte pulses (1-249 ticks) and stream terminator. */
	const uint8_t	buf1[] = { 10, 20, 249, 0 };

	CHECK_EQ(decode(buf1, sizeof(buf1), &ev), 4);
	CHECK_EQ(ev.npulses, 3);
	CHECK_EQ(ev.pulse[0], 10);
	CHECK_EQ(ev.pulse[1], 20);
	CHECK_EQ(ev.pulse[2], 249);

	/* Two-byte pulses: 250 + (c - 250) * 255 + next - 1 ticks. */
	struct events	ev2 = {};
	const uint8_t	buf2[] = { 250, 1, 254, 255, 0 };

	CHECK_EQ(decode(buf2, sizeof(buf2), &ev2), 5);
	CHECK_EQ(ev2.npulses, 2);
	CHECK_EQ(ev2.pulse[0], 250);
	CHECK_EQ(ev2.pulse[1], 1524);
}


static void
test_decode_ops(void)
{
	/* FLUXOP_SPACE accumulates into the next pulse. */
	struct events	ev = {};
	uint8_t		buf[16];
	int		n = 0;

	buf[n++] = 255;
	buf[n++] = FLUXOP_SPACE;
	gw_write_28(1000, &buf[n]); n += 4;
	buf[n++] = 5;
	buf[n++] = 0;

	CHECK_EQ(decode(buf, n, &ev), n);
	CHECK_EQ(ev.nspaces, 1);
	CHECK_EQ(ev.space[0], 1000);
	CHECK_EQ(ev.npulses, 1);
	CHECK_EQ(ev.pulse[0], 1005);

	/* FLUXOP_INDEX reports the index offset from current ticks and
	 * does not advance the tick counter. */
	struct events	ev2 = {};

	n = 0;
	buf[n++] = 3;
	buf[n++] = 255;
	buf[n++] = FLUXOP_INDEX;
	gw_write_28(7, &buf[n]); n += 4;
	buf[n++] = 4;
	buf[n++] = 0;

	CHECK_EQ(decode(buf, n, &ev2), n);
	CHECK_EQ(ev2.nimarks, 1);
	CHECK_EQ(ev2.imark[0], 10);	/* 3 ticks + 7 offset */
	CHECK_EQ(ev2.npulses, 2);
	CHECK_EQ(ev2.pulse[0], 3);
	CHECK_EQ(ev2.pulse[1], 4);

	/* Unknown flux opcode is an error. */
	struct events	ev3 = {};

	n = 0;
	buf[n++] = 255;
	buf[n++] = 9;
	gw_write_28(1, &buf[n]); n += 4;
	buf[n++] = 0;

	CHECK_EQ(decode(buf, n, &ev3), -1);
}


static void
test_decode_partial(void)
{
	/* A truncated multibyte sequence consumes only complete
	 * sequences; the caller is expected to retry with more data. */
	struct events	ev = {};
	const uint8_t	buf1[] = { 10, 250 };

	CHECK_EQ(decode(buf1, sizeof(buf1), &ev), 1);
	CHECK_EQ(ev.npulses, 1);

	struct events	ev2 = {};
	const uint8_t	buf2[] = { 5, 255, FLUXOP_INDEX, 1, 1 };

	CHECK_EQ(decode(buf2, sizeof(buf2), &ev2), 1);
	CHECK_EQ(ev2.npulses, 1);
	CHECK_EQ(ev2.nimarks, 0);

	/* A positive callback status stops the decode early. */
	struct events	ev3 = { .pulse_status = 1 };
	const uint8_t	buf3[] = { 10, 20, 30, 0 };

	CHECK_EQ(decode(buf3, sizeof(buf3), &ev3), 1);
	CHECK_EQ(ev3.npulses, 1);
}


static void
test_encode_ticks(void)
{
	uint8_t	sbuf[GWCODE_MAX];

	/* Short pulses encode as a single byte. */
	CHECK_EQ(encode_ticks(1, 0x10000000, 0, sbuf), 1);
	CHECK_EQ(sbuf[0], 1);
	CHECK_EQ(encode_ticks(249, 0x10000000, 0, sbuf), 1);
	CHECK_EQ(sbuf[0], 249);

	/* Two-byte encoding boundaries. */
	CHECK_EQ(encode_ticks(250, 0x10000000, 0, sbuf), 2);
	CHECK_EQ(sbuf[0], 250);
	CHECK_EQ(sbuf[1], 1);

	CHECK_EQ(encode_ticks(1524, 0x10000000, 0, sbuf), 2);
	CHECK_EQ(sbuf[0], 254);
	CHECK_EQ(sbuf[1], 255);

	/* Everything encode_ticks() produces (below the no-flux-area
	 * threshold) must decode back to the same tick count. */
	static const uint32_t	ticks[] = {
		1, 100, 249, 250, 251, 504, 505, 1000,
		1524, 1525, 2000, 65536, 1000000
	};

	for (int i = 0; i < (int)(sizeof(ticks)/sizeof(ticks[0])); ++i) {
		struct events	ev = {};
		uint8_t		buf[GWCODE_MAX + 1];

		int cnt = encode_ticks(ticks[i], 0x10000000, 0, buf);

		CHECK(cnt > 0 && cnt <= GWCODE_MAX);

		buf[cnt] = 0;
		CHECK_EQ(decode(buf, cnt + 1, &ev), cnt + 1);
		CHECK_EQ(ev.npulses, 1);
		CHECK_EQ(ev.pulse[0], ticks[i]);
	}
}


int
main(void)
{
	test_rw28();
	test_decode_pulses();
	test_decode_ops();
	test_decode_partial();
	test_encode_ticks();

	return test_exit("test_gwx");
}
