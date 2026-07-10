/*
 * Scan for Greaseweazle devices on the USB bus.
 */

#ifndef GWSCAN_H
#define GWSCAN_H

#ifdef __cplusplus
extern "C" {
#endif


struct gw_scan_dev {
	char	*device;	/* "/dev/ttyACM0" or "\\\\.\\COM5" */
	char	*serial;	/* USB serial string; "" if absent */
};

#define GW_SCAN_ERROR		(-1)	/* scan failed; see errno */
#define GW_SCAN_UNSUPPORTED	(-2)	/* no backend on this platform */

/*
 * Scan for Greaseweazles.  On success, returns the count of devices
 * found (>= 0) and sets "*devs" to a malloc'd array of that many
 * entries (NULL if count is 0), sorted by device name.  Caller frees
 * with gw_scan_free().  On failure, returns GW_SCAN_ERROR or
 * GW_SCAN_UNSUPPORTED.
 */
extern int gw_scan(struct gw_scan_dev **devs);

extern void gw_scan_free(struct gw_scan_dev *devs, int cnt);

#ifdef __cplusplus
}
#endif

#endif
