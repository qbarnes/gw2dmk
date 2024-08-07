/*
 * Greaseweazle floppy disk drive.
 */

#ifndef GWFDDRV_H
#define GWFDDRV_H

#ifdef __cplusplus
extern "C" {
#endif

#include "gw.h"

/* Density select */
enum {
	DS_NOTSET = -1,	/* Defaults to DD */
	DS_HD     = 0,
	DS_DD     = 1
};


struct gw_fddrv {
	gw_devt		gwfd;
	const char	*device;
	int		bus;
	int		drive;
	int		kind;
	int		tracks;
	int		sides;
	int		steps;
	int		densel;
	int		step_ms;
	int		settle_ms;
};

#ifdef __cplusplus
}
#endif

#endif
