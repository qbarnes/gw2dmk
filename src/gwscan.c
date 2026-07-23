/*
 * Scan for Greaseweazle devices on the USB bus.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "gwscan_impl.h"


int
gw_scan_append(struct gw_scan_dev **devs, int cnt,
	       const char *device, const char *serial)
{
	struct gw_scan_dev	*ndevs;
	char			*ndevice, *nserial;

	ndevs = realloc(*devs, (cnt + 1) * sizeof *ndevs);

	if (!ndevs)
		goto fail;

	*devs = ndevs;

	ndevice = strdup(device);
	nserial = strdup(serial);

	if (!ndevice || !nserial) {
		free(ndevice);
		free(nserial);
		goto fail;
	}

	ndevs[cnt].device = ndevice;
	ndevs[cnt].serial = nserial;

	return cnt + 1;

fail:
	gw_scan_free(*devs, cnt);
	*devs = NULL;
	return GW_SCAN_ERROR;
}


void
gw_scan_free(struct gw_scan_dev *devs, int cnt)
{
	for (int i = 0; i < cnt; ++i) {
		free(devs[i].device);
		free(devs[i].serial);
	}

	free(devs);
}


#ifdef GW_SCAN_HAVE_BACKEND
static int
scan_dev_cmp(const void *a, const void *b)
{
	const struct gw_scan_dev	*da = a;
	const struct gw_scan_dev	*db = b;

	return strcmp(da->device, db->device);
}
#endif


int
gw_scan(struct gw_scan_dev **devs)
{
#ifdef GW_SCAN_HAVE_BACKEND
	*devs = NULL;

	int	cnt = gw_scan_backend(devs);

	if (cnt < 0)
		return cnt;

	/* Enumeration order is nondeterministic; sort for stable output. */
	if (cnt > 1)
		qsort(*devs, cnt, sizeof **devs, scan_dev_cmp);

	return cnt;
#else
	*devs = NULL;
	return GW_SCAN_UNSUPPORTED;
#endif
}
