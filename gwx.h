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


/*
 * Values for "status":
 *   negative: error (stop)
 *   0       : continue
 *   positive: done (stop)
 */

struct gw_decode_stream_s {
	uint32_t	ticks;
	int		status;
	int		(*decoded_imark)(uint32_t ticks, void *data);
	void		*imark_data;
	int		(*decoded_pulse)(uint32_t ticks, void *data);
	void		*pulse_data;
};


// XXX Need to change due to POSIX violation.
typedef double nanoseconds_t;

extern int gw_setdrive(gw_devt gwfd, int drive, int densel);

extern int gw_unsetdrive(gw_devt gwfd, int drive);

extern int gw_get_bandwidth(gw_devt gwfd, double *min_bw, double *max_bw);

extern ssize_t gw_read_stream(gw_devt gwfd, int revs, int ticks,
			      uint8_t **fbuf);

extern ssize_t gw_decode_stream(const uint8_t *fbuf, size_t fbuf_cnt,
				struct gw_decode_stream_s *gwds);

extern int gw_get_period_ns(gw_devt gwfd, int drive, nanoseconds_t clock_ns,
				nanoseconds_t *period_ns);


static inline uint32_t
gw_read_28(const uint8_t *p)
{
	return( ((p[0] & 0xfe) >> 1) |
		((p[1] & 0xfe) << 6) |
		((p[2] & 0xfe) << 13) |
		((p[3] & 0xfe) << 20) );
}


#ifdef __cplusplus
}
#endif

#endif
