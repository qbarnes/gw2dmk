#include "gwmedia.h"


void
media_encoding_init(struct gw_media_encoding *gme, uint32_t sample_freq,
		    double mult)
{
	/*
	 * Assume FM is 4us and 8us and MFM is 4us, 6us, and 8us.
	 */

	double p4us = sample_freq * 4000.0 / 1e9 * mult;
	double p6us = sample_freq * 6000.0 / 1e9 * mult;
	double p8us = sample_freq * 8000.0 / 1e9 * mult;

	*gme = (struct gw_media_encoding){
		.rpm	    = 0.0,
		.data_clock = 0.0,
		.pulse_rate = 0.0,
		.fmthresh   = (int)round((p4us + p8us) / 2.0),
		.mfmthresh1 = (int)round((p4us + p6us) / 2.0),
		.mfmthresh2 = (int)round((p6us + p8us) / 2.0),
		.mfmshort   = 0.0,
		.thresh_adj = 0.0
	};
}


/*
 * Use a histogram analysis to determine more optimal pulse timings.
 */

void
media_encoding_init_from_histo(struct gw_media_encoding *gme,
			       const struct histo_analysis *ha,
			       uint32_t sample_freq)
{

	media_encoding_init(gme, sample_freq, 1.0);

	gme->rpm	= ha->rpm;
	gme->data_clock = ha->data_clock_khz;
	gme->pulse_rate = ha->pulse_rate_khz;

	if (ha->peaks == 2) {
		double	p6g = (ha->peak[0] + ha->peak[1]) / 2.0;

		gme->fmthresh   = (int)round(p6g * TICKS_PER_BUCKET);

		gme->mfmthresh1 = (int)round((ha->peak[0] + p6g) *
						TICKS_PER_BUCKET / 2.0);

		gme->mfmthresh2 = (int)round((p6g + ha->peak[1]) *
						TICKS_PER_BUCKET / 2.0);
	} else {
		gme->fmthresh   = (int)round((ha->peak[0] + ha->peak[2]) *
						TICKS_PER_BUCKET / 2.0);

		gme->mfmthresh1 = (int)round((ha->peak[0] + ha->peak[1]) *
						TICKS_PER_BUCKET / 2.0);

		gme->mfmthresh2 = (int)round((ha->peak[1] + ha->peak[2]) *
						TICKS_PER_BUCKET / 2.0);
	}
}
