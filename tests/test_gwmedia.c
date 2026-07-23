/*
 * Validate media encoding threshold computation, both from nominal
 * bit cell times and from histogram analysis results.
 */

#include "gwmedia.h"

#include "test.h"


/*
 * 24 MHz sample clock, 4 us FM bit cell (300 RPM 5.25" DD media):
 * FM bit cell is 96 ticks, MFM half cell 48 ticks.
 */

#define FREQ		24000000u
#define FM_CELL_US	4.0
#define FM_CELL_TICKS	96.0


static void
test_init_nominal(void)
{
	struct gw_media_encoding	gme;

	media_encoding_init(&gme, FREQ, FM_CELL_US);

	CHECK_EQ(gme.fmthresh, 144);	/* 1.5 * FM cell */
	CHECK_EQ(gme.mfmthresh0, 72);	/* 1.5 * MFM half cell */
	CHECK_EQ(gme.mfmthresh1, 120);	/* 2.5 * MFM half cell */
	CHECK_EQ(gme.mfmthresh2, 168);	/* 3.5 * MFM half cell */
	CHECK_NEAR(gme.mfmshort, 48.0, 0.001);
	CHECK_NEAR(gme.thresh_adj, 0.0, 0.0);
	CHECK_NEAR(gme.postcomp, 0.5, 0.0);

	/* 500 kbps MFM (HD) at a 72 MHz sample clock. */
	media_encoding_init(&gme, 72000000, 2.0);

	CHECK_EQ(gme.fmthresh, 216);
	CHECK_EQ(gme.mfmthresh1, 180);
	CHECK_EQ(gme.mfmthresh2, 252);
	CHECK_NEAR(gme.mfmshort, 72.0, 0.001);
}


/*
 * MFM media shows three histogram peaks at 2, 3, and 4 half cells
 * (96, 144, and 192 ticks here).  Thresholds must land midway
 * between adjacent peaks.
 */

static void
test_init_from_histo_mfm(void)
{
	struct gw_media_encoding	gme;
	struct histo_analysis		ha;

	histo_analysis_init(&ha);
	ha.peaks = 3;
	ha.peak[0] = 2.0 * FM_CELL_TICKS / 2.0 / TICKS_PER_BUCKET;
	ha.peak[1] = 3.0 * FM_CELL_TICKS / 2.0 / TICKS_PER_BUCKET;
	ha.peak[2] = 4.0 * FM_CELL_TICKS / 2.0 / TICKS_PER_BUCKET;
	ha.ps[0] = ha.ps[1] = ha.ps[2] = 1000.0;
	ha.rpm = 300.0;
	ha.bit_rate_khz = 250.0;
	ha.data_clock_khz = 250.0;

	media_encoding_init_from_histo(&gme, &ha, FREQ);

	CHECK_EQ(gme.fmthresh, 144);
	CHECK_EQ(gme.mfmthresh1, 120);
	CHECK_EQ(gme.mfmthresh2, 168);
	CHECK_EQ(gme.mfmthresh0, 72);
	CHECK_NEAR(gme.mfmshort, 48.0, 0.001);
	CHECK_NEAR(gme.rpm, 300.0, 0.0);
	CHECK_NEAR(gme.bit_rate, 250.0, 0.0);
	CHECK_NEAR(gme.data_clock, 250.0, 0.0);
}


/*
 * FM media shows two peaks, at the FM cell and twice the FM cell
 * (96 and 192 ticks here).  The synthesized middle peak at 144
 * ticks must produce the same thresholds as the MFM case.
 */

static void
test_init_from_histo_fm(void)
{
	struct gw_media_encoding	gme;
	struct histo_analysis		ha;

	histo_analysis_init(&ha);
	ha.peaks = 2;
	ha.peak[0] = FM_CELL_TICKS / TICKS_PER_BUCKET;
	ha.peak[1] = 2.0 * FM_CELL_TICKS / TICKS_PER_BUCKET;
	ha.ps[0] = ha.ps[1] = 1000.0;

	media_encoding_init_from_histo(&gme, &ha, FREQ);

	CHECK_EQ(gme.fmthresh, 144);
	CHECK_EQ(gme.mfmthresh1, 120);
	CHECK_EQ(gme.mfmthresh2, 168);
	CHECK_EQ(gme.mfmthresh0, 72);
	CHECK_NEAR(gme.mfmshort, 48.0, 0.001);
}


int
main(void)
{
	test_init_nominal();
	test_init_from_histo_mfm();
	test_init_from_histo_fm();

	return test_exit("test_gwmedia");
}
