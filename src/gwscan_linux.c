/*
 * Greaseweazle USB scan backend for Linux using sysfs.
 */

#include "gwscan_impl.h"

#if linux

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "greaseweazle.h"


/*
 * Read a sysfs attribute into "buf", stripping the trailing newline.
 * Returns 0 on success, -1 on failure.
 */

static int
read_sysfs_attr(const char *tty, const char *attr, char *buf, size_t buflen)
{
	char	path[PATH_MAX];
	int	pret;

	/*
	 * The "device" symlink points at the USB interface node; the
	 * kernel resolves the symlink before applying "..", landing on
	 * the USB device node which owns idVendor/idProduct/serial.
	 */
	pret = snprintf(path, sizeof path, "/sys/class/tty/%s/device/../%s",
			tty, attr);

	if (pret < 0 || (size_t)pret >= sizeof path)
		return -1;

	FILE	*fp = fopen(path, "r");

	if (!fp)
		return -1;

	if (!fgets(buf, buflen, fp)) {
		fclose(fp);
		return -1;
	}

	fclose(fp);

	buf[strcspn(buf, "\n")] = '\0';

	return 0;
}


int
gw_scan_backend(struct gw_scan_dev **devs)
{
	DIR	*dirp = opendir("/sys/class/tty");

	if (!dirp)
		return GW_SCAN_ERROR;

	int	cnt = 0;

	for (struct dirent *de; (de = readdir(dirp)); ) {
		/* Greaseweazles always present as CDC-ACM on Linux. */
		if (strncmp(de->d_name, "ttyACM", 6) != 0)
			continue;

		char	buf[256];

		if (read_sysfs_attr(de->d_name, "idVendor",
				    buf, sizeof buf) ||
		    strtoul(buf, NULL, 16) != GREASEWEAZLE_VID)
			continue;

		if (read_sysfs_attr(de->d_name, "idProduct",
				    buf, sizeof buf) ||
		    strtoul(buf, NULL, 16) != GREASEWEAZLE_PID)
			continue;

		char	serial[256];

		if (read_sysfs_attr(de->d_name, "serial",
				    serial, sizeof serial))
			serial[0] = '\0';

		char	devname[NAME_MAX + 6];

		snprintf(devname, sizeof devname, "/dev/%s", de->d_name);

		cnt = gw_scan_append(devs, cnt, devname, serial);

		if (cnt == GW_SCAN_ERROR)
			break;
	}

	closedir(dirp);

	return cnt;
}

#endif
