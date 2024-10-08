#ifndef GWMEDIA_H
#define GWMEDIA_H

#include <stdint.h>
#include <math.h>

#include "gwhisto.h"


struct gw_media_encoding {
	double	rpm;		// XXX Needed?  Right place?
	double	data_clock;	// XXX Needed?  Right place?
	double	bit_rate;	// XXX Needed?  Right place?
	int	fmthresh;
	int	mfmthresh0;
	int	mfmthresh1;
	int	mfmthresh2;
	double	mfmshort;
	double	thresh_adj;
	double	postcomp;
};


extern void media_encoding_init(struct gw_media_encoding *gme,
				uint32_t sample_freq, double fm_bitcell_us);

extern void media_encoding_init_from_histo(struct gw_media_encoding *gme,
					   const struct histo_analysis *histo,
					   uint32_t sample_freq);

#endif
