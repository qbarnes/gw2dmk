/*
 * Validate histogram collection from a synthetic Greaseweazle flux
 * stream and the peak analysis used for autodetecting media.
 */

#include "gwhisto.h"
#include "gwx.h"

#include "test.h"


#define FREQ	24000000u

/*
 * Synthesize one revolution of 250 kbps MFM-like flux at 300.003 RPM:
 * equal numbers of 96, 144, and 192 tick intervals bracketed by two
 * index marks.  432 ticks per cycle * 11111 cycles = 4799952 ticks,
 * just a hair under the 4800000 ticks of a perfect 300 RPM rev.
 */

#define CYCLES		11111
#define CYCLE_TICKS	(96 + 144 + 192)
#define TOTAL_TICKS	(CYCLES * CYCLE_TICKS)


static size_t
synth_stream(uint8_t *buf)
{
	size_t	n = 0;

	buf[n++] = 255;
	buf[n++] = FLUXOP_INDEX;
	gw_write_28(0, &buf[n]); n += 4;

	for (int i = 0; i < CYCLES; ++i) {
		buf[n++] = 96;
		buf[n++] = 144;
		buf[n++] = 192;
	}

	buf[n++] = 255;
	buf[n++] = FLUXOP_INDEX;
	gw_write_28(0, &buf[n]); n += 4;

	buf[n++] = 0;

	return n;
}


int
main(void)
{
	struct histogram	histo;

	histo_init(3, 1, 0, FREQ, TICKS_PER_BUCKET, &histo);

	CHECK_EQ(histo.track, 3);
	CHECK_EQ(histo.side, 1);
	CHECK_EQ(histo.revs, 1);	/* revs of 0 becomes 1 */
	CHECK_EQ(histo.sample_freq, FREQ);
	CHECK_NEAR(histo.ticks_per_bucket, TICKS_PER_BUCKET, 0.0);

	static uint8_t	buf[3 * CYCLES + 32];

	size_t	n = synth_stream(buf);

	CHECK_EQ(flux2histo(buf, n, &histo), 0);

	CHECK_EQ(histo.revs, 1);
	CHECK_EQ(histo.total_ticks, TOTAL_TICKS);

	/* All samples land in exactly three buckets. */
	int	b96  = (int)(96 / TICKS_PER_BUCKET);
	int	b144 = (int)(144 / TICKS_PER_BUCKET);
	int	b192 = (int)(192 / TICKS_PER_BUCKET);

	CHECK_EQ(histo.data[b96], CYCLES);
	CHECK_EQ(histo.data[b144], CYCLES);
	CHECK_EQ(histo.data[b192], CYCLES);
	CHECK_EQ(histo.data_overflow, 0);

	unsigned int	total = 0;

	for (int i = 0; i < HIST_BUCKETS; ++i)
		total += histo.data[i];

	CHECK_EQ(total, 3 * CYCLES);

	/* Peak analysis: three clean MFM peaks. */
	struct histo_analysis	ha;

	histo_analysis_init(&ha);
	histo_analyze(&histo, &ha);

	CHECK_EQ(ha.peaks, 3);
	CHECK_NEAR(ha.peak[0], (double)b96, 0.001);
	CHECK_NEAR(ha.peak[1], (double)b144, 0.001);
	CHECK_NEAR(ha.peak[2], (double)b192, 0.001);
	CHECK_NEAR(ha.rpm, 300.003, 0.01);

	/* Bucket quantization limits precision; just require the
	 * estimates to be in the right neighborhood of 250 kHz. */
	CHECK(ha.bit_rate_khz > 230.0 && ha.bit_rate_khz < 280.0);
	CHECK(ha.data_clock_khz > 230.0 && ha.data_clock_khz < 280.0);

	/* An FM-like stream with two peaks. */
	struct histogram	histo2;

	histo_init(0, 0, 1, FREQ, TICKS_PER_BUCKET, &histo2);

	size_t	n2 = 0;

	buf[n2++] = 255;
	buf[n2++] = FLUXOP_INDEX;
	gw_write_28(0, &buf[n2]); n2 += 4;

	for (int i = 0; i < 1000; ++i) {
		buf[n2++] = 96;
		buf[n2++] = 192;
	}

	buf[n2++] = 255;
	buf[n2++] = FLUXOP_INDEX;
	gw_write_28(0, &buf[n2]); n2 += 4;
	buf[n2++] = 0;

	CHECK_EQ(flux2histo(buf, n2, &histo2), 0);

	struct histo_analysis	ha2;

	histo_analysis_init(&ha2);
	histo_analyze(&histo2, &ha2);

	CHECK_EQ(ha2.peaks, 2);
	CHECK_NEAR(ha2.peak[0], (double)b96, 0.001);
	CHECK_NEAR(ha2.peak[1], (double)b192, 0.001);

	/* A stream with no index mark at all fails. */
	struct histogram	histo3;

	histo_init(0, 0, 1, FREQ, TICKS_PER_BUCKET, &histo3);

	uint8_t	buf3[] = { 96, 144, 192, 0 };

	CHECK_EQ(flux2histo(buf3, sizeof(buf3), &histo3), -1);

	return test_exit("test_gwhisto");
}
