/*
 * Internal contract between gwscan.c and its per-platform backends.
 * To support a new platform, add it to GW_SCAN_HAVE_BACKEND below and
 * provide a gwscan_<os>.c implementing gw_scan_backend().
 */

#ifndef GWSCAN_IMPL_H
#define GWSCAN_IMPL_H

#include "gwscan.h"

#if linux || defined(WIN64) || defined(WIN32)
#define GW_SCAN_HAVE_BACKEND	1
#endif

/*
 * Implemented by exactly one gwscan_*.c per platform.  "*devs" is NULL
 * on entry.  Appends found devices to "*devs" and returns the count,
 * or GW_SCAN_ERROR with errno set and *devs freed and NULLed.
 */
extern int gw_scan_backend(struct gw_scan_dev **devs);

/*
 * Append a device to the array (strdups both strings).  Returns the
 * new count, or GW_SCAN_ERROR with errno set on allocation failure,
 * in which case the whole array is freed and "*devs" set to NULL.
 */
extern int gw_scan_append(struct gw_scan_dev **devs, int cnt,
			  const char *device, const char *serial);

#endif
