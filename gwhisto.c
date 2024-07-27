#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <math.h>

#include "gwhisto.h"


void
histo_init(uint8_t track, uint8_t side, uint8_t revs,
		uint32_t sample_freq,
		double ticks_per_bucket,
		struct histogram *histo)
{
	// Should there be a check on the max bucket size?
	*histo = (struct histogram){
		.track = track,
		.side  = side,
		.revs  = revs ? revs : 1,
		.sample_freq = sample_freq,
		.total_ticks = 0,
		.ticks_per_bucket = ticks_per_bucket,
		.data_overflow = 0
	};
}


void
histo_analysis_init(struct histo_analysis *ha)
{
	*ha = (struct histo_analysis){};
}


void
histo_analyze(const struct histogram *histo,
	      struct histo_analysis *ha)
{
	/*
	 * Find 2-3 peaks in the data.
	 */

	unsigned int data_total_cnt = 0;
	for (int i = 0; i < COUNT_OF(histo->data); ++i)
		data_total_cnt += histo->data[i];

	unsigned int min_threshold =
		(int)(0.06 * data_total_cnt / COUNT_OF(histo->data));

	int max_width = (int)(COUNT_OF(histo->data) / 5.3);

	int j = 0, i = 0;
	for (; j < HIST_MAX_PEAKS; ++j) {
		int pwidth = 0, psamps = 0, psampsw = 0;

		for (;(histo->data[i] < min_threshold) &&
			(i < COUNT_OF(histo->data)); ++i);

		for (; (histo->data[i] >= min_threshold) &&
				(i < COUNT_OF(histo->data)); ++i) {
			++pwidth;
			psamps  += histo->data[i];
			psampsw += histo->data[i] * i;
		}

		if ((pwidth == 0) || (pwidth > max_width)) {
			/* Not a real peak */
			break;
		} else {
			ha->peak[j]    = (double)psampsw / psamps;
			ha->ps[j]      = psamps;
			ha->std_dev[j] = 0.0;

			for (int ii = i - pwidth; ii < i; ++ii) {
				ha->std_dev[j] += histo->data[ii] *
					pow((double)ii - ha->peak[j], 2);
			}
			ha->std_dev[j] = sqrt(ha->std_dev[j] /
						((double)psamps - 1));
		}
	}

	switch ((ha->peaks = j)) {
	case 2:
		/* FM encoding */
		ha->data_clock_khz = 
			histo->sample_freq / 1000.0 / histo->ticks_per_bucket /
			((ha->peak[0] / 0.5 * ha->ps[0] +
			  ha->peak[1] / 1.0 * ha->ps[1])
			/ (ha->ps[0] + ha->ps[1]) + 1.0);
		break;

	case 3:
		/* MFM encoding */
		ha->data_clock_khz = 
			histo->sample_freq / 1000.0 / histo->ticks_per_bucket /
			((ha->peak[0] / 1.0 * ha->ps[0] +
			ha->peak[1] / 1.5 * ha->ps[1] +
			ha->peak[2] / 2.0 * ha->ps[2])
			/ (ha->ps[0] + ha->ps[1] + ha->ps[2]) + 1.0);
		break;
	}

	ha->rpm = 60.0 * histo->sample_freq * histo->revs / histo->total_ticks;
}


int
histo_show(int msg_level,
	   const struct histogram *histo,
	   const struct histo_analysis *ha)
{
	msg(msg_level, "Track %d, side %d, revolutions %d\n",
			histo->track, histo->side, histo->revs);
	for (int i = 0; i < COUNT_OF(histo->data); i += 8) {
		msg(msg_level,
			"%3d: %06d %06d %06d %06d %06d %06d %06d %06d\n", i,
			histo->data[i],   histo->data[i+1],
			histo->data[i+2], histo->data[i+3],
			histo->data[i+4], histo->data[i+5],
			histo->data[i+6], histo->data[i+7]);
	}

	msg(msg_level, "Size of buckets: %.3fns\n", 1000000000.0 *
				histo->ticks_per_bucket / histo->sample_freq);
	msg(msg_level, "Last bucket is less than: %.3fus\n",
				1000000.0 * histo->ticks_per_bucket *
				COUNT_OF(histo->data) / histo->sample_freq);
	msg(msg_level, "Entries exceeding last bucket: %u\n",
				histo->data_overflow);

	for (int j = 0; j < ha->peaks; ++j) {
		msg(msg_level, "Peak %d: Mean %.05f, SD %.05f\n",
			   j, ha->peak[j], ha->std_dev[j]);
	}

	msg(msg_level, "Average drive speed:   %.3f RPM\n",
			60.0 * histo->sample_freq * histo->revs /
			histo->total_ticks);

	msg(msg_level, "%s data clock approx: %.3f kHz\n",
		ha->peaks == 2 ? "FM" : "MFM", ha->data_clock_khz);

	return 0;
}


struct decode_imark_data {
	uint32_t	total_ticks;	// Only count ticks between index holes
	unsigned int	revs_seen;
	uint32_t	index[2];
};


static int
imark_fn(uint32_t imark, void *data)
{
	struct decode_imark_data *dmd = (struct decode_imark_data *)data;

	dmd->index[0] = dmd->index[1];
	dmd->index[1] = imark;

	if  (dmd->index[0] != ~0) {
		dmd->total_ticks += dmd->index[1] - dmd->index[0];
		++dmd->revs_seen;
	}

	return 0;
}


static int
pulse_fn(uint32_t ticks, void *data)
{
	struct histogram	*histo = (struct histogram *)data;
	const unsigned int	buckets = COUNT_OF(histo->data);
	uint32_t		bucket = (int)(ticks / histo->ticks_per_bucket);

	if (bucket > buckets)
		++histo->data_overflow;
	else if (ticks > 0)
		++histo->data[bucket];
		
	return 0;
}


int
flux2histo(const uint8_t *fbuf, size_t bytes_read, struct histogram *histo)
{
	histo->total_ticks = 0;
	histo->data_overflow = 0;
	memset(histo->data, 0, sizeof(histo->data));

	struct decode_imark_data idata = { 0, 0, { ~0, ~0 } };
	struct gw_decode_stream_s gwds =
				{ 0, -1, imark_fn, &idata, pulse_fn, histo };

	gw_decode_stream(fbuf, bytes_read, &gwds);

	histo->total_ticks = idata.total_ticks;
	histo->revs        = idata.revs_seen;

	// Should we check to ensure revs_seen and histo->revs are equal?

	if (idata.index[1] == ~0) {
		// error handling
		return -1;
	}

	return 0;
}


int
collect_histo_from_track(gw_devt gwfd, struct histogram *histo)
{
	uint8_t	*fbuf = 0;

	gw_seek(gwfd, histo->track);
	// error checking

	gw_head(gwfd, histo->side);
	// error checking

	int rd_ret = gw_read_stream(gwfd, histo->revs, 0, &fbuf);

	if (rd_ret == -1) {
		// error handling
		return rd_ret;
	}

	int f2hret = flux2histo(fbuf, rd_ret, histo);

	free(fbuf);

	return f2hret ? f2hret : 0;
}
