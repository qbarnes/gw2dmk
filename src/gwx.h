#ifndef GWX_H
#define GWX_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#if !defined(WIN64) && !defined(WIN32)
#include <sys/ioctl.h>
#include <termios.h>
#endif

#include "greaseweazle.h"
#include "gw.h"


/* Maximum size of a tick timing pulse encoded as an 8-bit sequence for GW. */
#define GWCODE_MAX	11


/*
 * Values for "status":
 *   negative: error (stop)
 *   0       : continue
 *   positive: done (stop)
 */

struct gw_decode_stream_s {
	uint32_t	ds_ticks;
	uint32_t	ds_last_pulse;
	int		ds_status;
	int		(*decoded_imark)(uint32_t ticks, void *data);
	void		*imark_data;
	int		(*decoded_space)(uint32_t ticks, void *data);
	void		*space_data;
	int		(*decoded_pulse)(uint32_t ticks, void *data);
	void		*pulse_data;
};


/*
 * Values for return from (*encoded_pulse):
 *   negative: error (stop)
 *   0       : continue
 */

struct gw_encode_stream_s {
	int		(*encoded_pulse)(uint8_t *byte, int byte_cnt,
					 void *data);
	void		*pulse_data;
};


extern int gw_setdrive(gw_devt gwfd, int drive, int densel);

extern int gw_unsetdrive(gw_devt gwfd, int drive);

extern int gw_get_bandwidth(gw_devt gwfd, double *min_bw, double *max_bw);

extern ssize_t gw_read_stream(gw_devt gwfd, int revs, int ticks,
			      uint8_t **fbuf);

extern ssize_t gw_decode_stream(const uint8_t *fbuf, size_t fbuf_cnt,
				struct gw_decode_stream_s *gwds);

extern int gw_get_period_ns(gw_devt gwfd, int drive, nsec_type clock_ns,
				nsec_type *period_ns);

extern ssize_t gw_write_stream(gw_devt gwfd, const uint8_t *enbuf,
			       size_t enbuf_cnt, bool cue_at_index,
			       bool terminate_at_index, int retries);

extern int encode_ticks(uint32_t ticks, uint32_t nfa_thresh,
			uint32_t nfa_period, uint8_t sbuf[GWCODE_MAX]);


static inline uint32_t
gw_read_28(const uint8_t *p)
{
	return ((p[0] & 0xfe) >>  1) |
	       ((p[1] & 0xfe) <<  6) |
	       ((p[2] & 0xfe) << 13) |
	       ((p[3] & 0xfe) << 20);
}


static inline void
gw_write_28(uint32_t val, uint8_t *p)
{
	p[0] = ((val <<  1) & 0xff) | 1;
	p[1] = ((val >>  6) & 0xff) | 1;
	p[2] = ((val >> 13) & 0xff) | 1;
	p[3] = ((val >> 20) & 0xff) | 1;
}


#ifdef __cplusplus
}
#endif

#endif
