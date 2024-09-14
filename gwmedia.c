#include "gwmedia.h"


static void
media_encoding_init_base(struct gw_media_encoding *gme)
{
	*gme = (struct gw_media_encoding){
		.rpm	    = 0.0,
		.data_clock = 0.0,
		.bit_rate   = 0.0,
		.fmthresh   = 0.0,
		.mfmthresh0 = 0.0,
		.mfmthresh1 = 0.0,
		.mfmthresh2 = 0.0,
		.mfmshort   = 0.0,
		.thresh_adj = 0.0,
		.postcomp   = 0.5
	};
}


void
media_encoding_init(struct gw_media_encoding *gme, uint32_t sample_freq,
		    double fm_bitcell_us)
{
	media_encoding_init_base(gme);

	double fm_bitcell_clks  = fm_bitcell_us * sample_freq / 1e6;
	double mfm_bitcell_clks = fm_bitcell_clks / 2.0;

	gme->fmthresh   = (int)round(fm_bitcell_clks * 1.5);
	gme->mfmthresh0 = (int)round(mfm_bitcell_clks * 1.5);
	gme->mfmthresh1 = (int)round(mfm_bitcell_clks * 2.5);
	gme->mfmthresh2 = (int)round(mfm_bitcell_clks * 3.5);
	gme->mfmshort   = (int)round(mfm_bitcell_clks);
}


/*
 * Use a histogram analysis to determine more optimal bit timings.
 */

void
media_encoding_init_from_histo(struct gw_media_encoding *gme,
			       const struct histo_analysis *ha,
			       uint32_t sample_freq)
{

	media_encoding_init_base(gme);

	gme->rpm	= ha->rpm;
	gme->data_clock = ha->data_clock_khz;
	gme->bit_rate   = ha->bit_rate_khz;

	if (ha->peaks == 2) {
		double	p6g = (ha->peak[0] + ha->peak[1]) / 2.0;

		gme->fmthresh   = (int)round(p6g * TICKS_PER_BUCKET);

		gme->mfmthresh1 = (int)round((ha->peak[0] + p6g) *
						TICKS_PER_BUCKET / 2.0);

		gme->mfmthresh2 = (int)round((p6g + ha->peak[1]) *
						TICKS_PER_BUCKET / 2.0);

		gme->mfmshort   = (ha->peak[0] + p6g) * TICKS_PER_BUCKET / 5.0;
	} else {
		gme->fmthresh   = (int)round((ha->peak[0] + ha->peak[2]) *
						TICKS_PER_BUCKET / 2.0);

		gme->mfmthresh1 = (int)round((ha->peak[0] + ha->peak[1]) *
						TICKS_PER_BUCKET / 2.0);

		gme->mfmthresh2 = (int)round((ha->peak[1] + ha->peak[2]) *
						TICKS_PER_BUCKET / 2.0);

		gme->mfmshort   = (ha->peak[0] + ha->peak[1]) *
						TICKS_PER_BUCKET / 5.0;
	}

	gme->mfmthresh0 = (int)round(gme->mfmthresh1 * 0.6);
}
