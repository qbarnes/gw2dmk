#ifndef GWHISTO_H
#define GWHISTO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "msg_levels.h"
#include "msg.h"
#include "gw.h"
#include "gwx.h"


#define	HIST_BUCKETS		128
#define TICKS_PER_BUCKET	5.0843808
#define	HIST_MAX_PEAKS		3


// XXX Should I move track, side, revs out?
struct histogram {
	uint8_t		track;
	uint8_t		side;
	uint32_t	revs;
	uint32_t	sample_freq;
	uint32_t	total_ticks;	// Only count ticks between index holes
	double		ticks_per_bucket;
	uint32_t	data[HIST_BUCKETS];
	uint32_t	data_overflow;
};


struct histo_analysis {
	int		peaks;
	double		peak[HIST_MAX_PEAKS];
	double		ps[HIST_MAX_PEAKS];
	double		std_dev[HIST_MAX_PEAKS];
	double		pulse_rate_khz;
	double		data_clock_khz;
	double		rpm;
};


extern void histo_init(uint8_t track, uint8_t side, uint8_t revs,
			uint32_t sample_freq, double ticks_per_bucket,
			struct histogram *histo);

extern void histo_analysis_init(struct histo_analysis *ha);

void histo_analyze(const struct histogram *histo, struct histo_analysis *ha);

extern int histo_show(int msg_level, const struct histogram *histo,
			const struct histo_analysis *ha);

extern int flux2histo(const uint8_t *fbuf, size_t bytes_read,
			struct histogram *histo);

extern int collect_histo_from_track(gw_devt gwfd, struct histogram *histo);


#ifdef __cplusplus
}               
#endif

#endif
