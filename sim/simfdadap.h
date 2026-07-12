#ifndef SIMFDADAP_H
#define SIMFDADAP_H

#include <stdbool.h>

#include "simdrive.h"

/*
 * FDADAP 50-to-34 pin adapter.
 *
 * The FDADAP (D Bit) adapts an 8" drive's 50-pin Shugart interface
 * to the 34-pin bus: it passes drive select, step, direction, side
 * select, and read/write data straight through; it derives TG43
 * (write-current reduction) from the head position; and it maps the
 * 34-pin motor/ready lines onto the 8" head-load and ready signals.
 *
 * Functionally, at the level the Greaseweazle host tools can
 * observe, the adapter is a validity gate plus pass-through: an 8"
 * drive must sit behind one, and everything else must not.  The
 * hooks below exist so future 8" quirks (head load timing, REDWC,
 * two-sided sensing) have a home without disturbing the bus layer.
 */

/* May this drive be attached to the 34-pin bus as configured? */
extern bool sim_fdadap_attach_ok(const struct sim_drive *drv,
				 char *errbuf, int errbuflen);

/* TG43 write-current reduction line; ignored by the simulation. */
extern bool sim_fdadap_tg43(const struct sim_drive *drv);

#endif
