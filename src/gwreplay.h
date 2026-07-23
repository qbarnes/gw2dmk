#ifndef GWREPLAY_H
#define GWREPLAY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "gw.h"

/*
 * Replay a Greaseweazle transaction logfile (written via -U) in place
 * of a physical device.  The logfile is parsed into per-(cyl,head)
 * FIFOs of recorded flux streams, and a protocol responder registered
 * as a gw backend serves them to the unmodified read paths.
 */

/* Sentinel device handle; never touched by a syscall. */
#if defined(WIN64) || defined(WIN32)
#define GW_REPLAY_DEVT	((gw_devt)(intptr_t)-3)
#else
#define GW_REPLAY_DEVT	((gw_devt)-3)
#endif


struct gw_replay_flux {
	uint8_t		*buf;		/* raw stream incl. 0 terminator */
	size_t		cnt;
	uint8_t		status;		/* recorded GET_FLUX_STATUS ack */
};

struct gw_replay_pos {
	struct gw_replay_flux	*flux;
	int			cnt;
	int			next;	/* consumption cursor */
};

struct gw_replay_log {
	bool		have_getinfo;
	uint8_t		getinfo[32];	/* recorded GET_INFO payload */
	uint32_t	sample_freq;
	int		nstreams;
	int		warnings;
	struct gw_replay_pos	pos[GW_MAX_TRACKS][2];
};

enum gw_replay_avail {
	GW_REPLAY_AVAIL,		/* an unconsumed stream remains */
	GW_REPLAY_EXHAUSTED,		/* streams recorded, all consumed */
	GW_REPLAY_NEVER			/* nothing recorded there */
};


extern int gw_replay_parse(FILE *fp, struct gw_replay_log *log);

extern void gw_replay_log_free(struct gw_replay_log *log);

extern int gw_replay_start_parsed(struct gw_replay_log *log);

extern int gw_replay_start(const char *path);

extern void gw_replay_finish(void);

extern bool gw_replay_active(void);

extern enum gw_replay_avail gw_replay_flux_avail(int cyl, int head);

#ifdef __cplusplus
}
#endif

#endif
