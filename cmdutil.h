#ifndef CMDUTIL_H
#define CMDUTIL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>

#include "msg.h"
#include "gwfddrv.h"


/*
 * Command-line parsing and usage-output helpers shared by the
 * gw2dmk, dmk2gw, and gwhist front ends.
 */

extern void fatal_bad_number(const char *name) MSG_NORETURN;

extern long int strtol_strict(const char *nptr, int base, const char *name);

extern int vu(const char *fmt, va_list ap) MSG_PRINTF(1, 0);

extern int u(const char *fmt, ...) MSG_PRINTF(1, 2);

/*
 * Option-argument parsers.  Each returns 0 on success and -1 on a
 * bad argument (after printing any error message); the caller should
 * then show its usage.
 */

extern int parse_drive_arg(const char *arg, int opt, struct gw_fddrv *fdd);

extern int parse_device_arg(const char *arg, struct gw_fddrv *fdd);

extern int parse_stepdelay_arg(const char *arg, struct gw_fddrv *fdd);


#ifdef __cplusplus
}
#endif

#endif
