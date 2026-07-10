/*
 * Common detection and initialization routines shared between
 * gw2dmk and dmk2gw.
 */

#include "gwdetect.h"


const char *
kind2desc(int kind)
{
	static const char *kinddesc[] = {
		"Undefined",
		"5.25\" SD/DD disk in 1.2MB drive",
		"5.25\" SD/DD disk in 360KB/720KB drive, or 3.5\" SD/DD disk",
		"5.25\" HD disk, or 8\" SD/DD disk",
		"3.5\" HD disk"
	};

	return (kind >= 1 && kind <= 4) ? kinddesc[kind] : kinddesc[0];
}


int
kind2densel(int kind)
{
	switch (kind) {
	case 1: /* FALL THRU */
	case 2:
		return DS_DD;

	case 3: /* FALL THRU */
	case 4:
		return DS_HD;
	};

	return -1;
}


/*
 * Find and open a GW on the device list.
 *
 * Return device descriptor and set "selected_dev" to point to
 * the device opened, or returns GW_DEVT_INVALID on failure.
 */

gw_devt
gw_openlist(const char **device_list, const char **selected_dev)
{
	gw_devt	gwfd = GW_DEVT_INVALID;

	for (const char **p = device_list; *p; ++p) {
		gwfd = gw_open(*p);

		if (gwfd != GW_DEVT_INVALID) {
			if (selected_dev)
				*selected_dev = *p;
			break;
		}
	}

	return gwfd;
}


static void
gw_scan_list_devs(const struct gw_scan_dev *devs, int cnt)
{
	for (int i = 0; i < cnt; ++i)
		msg_error("    %s  (serial '%s')\n",
			  devs[i].device, devs[i].serial);
}


/*
 * Open the scanned device at "devs[di]" and report it.
 *
 * Returns device descriptor and sets "selected_dev" to a copy of the
 * device name, or returns GW_DEVT_INVALID on failure.
 */

static gw_devt
gw_open_scanned_dev(const struct gw_scan_dev *devs, int di,
		    const char **selected_dev)
{
	gw_devt	gwfd = gw_open(devs[di].device);

	if (gwfd == GW_DEVT_INVALID) {
		msg_error("Failed open Greaseweazle ('%s').\n",
			  devs[di].device);
		return GW_DEVT_INVALID;
	}

	msg(MSG_NORMAL, "Using Greaseweazle at '%s' (serial '%s').\n",
	    devs[di].device, devs[di].serial);

	if (selected_dev) {
		char	*dev_copy = strdup(devs[di].device);

		if (!dev_copy) {
			msg_error("Failed to allocate memory for device "
				  "name.\n");
			gw_close(gwfd);
			return GW_DEVT_INVALID;
		}

		*selected_dev = dev_copy;
	}

	return gwfd;
}


/*
 * Find and open GW.
 *
 * If "device" is given, open it directly.  Otherwise scan the USB bus
 * for Greaseweazles; if "serial" is given, use the device whose USB
 * serial number matches exactly, else use the sole device found.  On
 * platforms without a scan backend, fall back to trying "device_list".
 *
 * Returns device file descriptor and sets "selected_dev" to device name
 * or returns GW_DEVT_INVALID on failure.
 */

gw_devt
gw_find_open_gw(const char *device,
		const char *serial,
		const char **device_list,
		const char **selected_dev)
{
	gw_devt gwfd;

	if (device) {
		gwfd = gw_open(device);

		if (gwfd == GW_DEVT_INVALID) {
			msg_error("Failed open Greaseweazle ('%s').\n", device);
			return GW_DEVT_INVALID;
		}

		if (selected_dev)
			*selected_dev = device;

		return gwfd;
	}

	struct gw_scan_dev	*devs;
	int			cnt = gw_scan(&devs);

	if (cnt == GW_SCAN_UNSUPPORTED) {
		if (serial) {
			msg_error("Greaseweazle scanning not supported on "
				  "this platform; use '-G <devname>'.\n");
			return GW_DEVT_INVALID;
		}

		gwfd = gw_openlist(device_list, selected_dev);

		if (gwfd == GW_DEVT_INVALID) {
			msg_error("Failed find Greaseweazle.  Use "
				  "'-G <devname>'.\n");
			return GW_DEVT_INVALID;
		}

		return gwfd;
	}

	if (cnt == GW_SCAN_ERROR) {
		msg_error("Failed to scan for Greaseweazles: %s.\n",
			  strerror(errno));
		return GW_DEVT_INVALID;
	}

	gwfd = GW_DEVT_INVALID;

	if (serial) {
		int	di;

		for (di = 0; di < cnt; ++di) {
			if (strcmp(devs[di].serial, serial) == 0)
				break;
		}

		if (di < cnt) {
			gwfd = gw_open_scanned_dev(devs, di, selected_dev);
		} else {
			msg_error("No Greaseweazle with serial '%s' "
				  "found.\n", serial);

			if (cnt > 0) {
				msg_error("Greaseweazles found:\n");
				gw_scan_list_devs(devs, cnt);
			}
		}
	} else if (cnt == 0) {
		msg_error("No Greaseweazle found.\n");
	} else if (cnt == 1) {
		gwfd = gw_open_scanned_dev(devs, 0, selected_dev);
	} else {
		msg_error("Multiple Greaseweazles found:\n");
		gw_scan_list_devs(devs, cnt);
		msg_error("Select one with '-Z <serial>' or "
			  "'-G <devname>'.\n");
	}

	gw_scan_free(devs, cnt);

	return gwfd;
}


/*
 * Initialize GW and drive.
 *
 * If "reset" is true, reset device upon initialization.
 *
 * Returns device descriptor or GW_DEVT_INVALID on failure.
 */

gw_devt
gw_init_gw(struct gw_fddrv *fdd, struct gw_info *gw_info, bool reset)
{
	gw_devt	gwfd     = fdd->gwfd;
	int	init_ret = gw_init(gwfd);

	if (init_ret != 0) {
		msg_error("Failed initialize Greaseweazle (%d).\n", init_ret);
		return GW_DEVT_INVALID;
	}

	int	cmd_ret;

	/* Put GW back to a better state if crashed on previous run. */
	if (reset) {
		cmd_ret = gw_reset(gwfd);

		if (cmd_ret != ACK_OKAY) {
			msg_error("Failed to reset Greaseweazle (%d).\n",
				  cmd_ret);
			return GW_DEVT_INVALID;
		}
	}

	cmd_ret = gw_get_info(gwfd, gw_info);

	if (cmd_ret != ACK_OKAY) {
		msg_error("Failed to get info from Greaseweazle (%d).\n",
			  cmd_ret);
		return GW_DEVT_INVALID;
	}

	cmd_ret = gw_set_bus_type(gwfd, fdd->bus);

	if (cmd_ret != ACK_OKAY) {
		msg_error("Failed to set bus type of Greaseweazle (%d).\n",
			  cmd_ret);
		return GW_DEVT_INVALID;
	}

	/*
	 * If other than default, set delays for step and settle.
	 */
	// XXX Is setting step and settle per drive or per GW?

	if (fdd->step_ms != -1 || fdd->settle_ms != -1) {
		struct gw_delay	gw_delay;

		cmd_ret = gw_get_params(gwfd, &gw_delay);

		if (cmd_ret != ACK_OKAY) {
			msg_error("Failed to get parameters of Greaseweazle "
				  "(%d).\n", cmd_ret);
			return GW_DEVT_INVALID;
		}

		if (fdd->step_ms != -1) {
			uint16_t	old_delay = gw_delay.step_delay;

			gw_delay.step_delay = fdd->step_ms * 1000;
			msg(MSG_NORMAL, "Changing step delay from %dms "
			    "to %dms.\n", (int)old_delay / 1000,
			    (int)gw_delay.step_delay / 1000);
		}

		if (fdd->settle_ms != -1) {
			uint16_t	old_settle = gw_delay.seek_settle;

			gw_delay.seek_settle = fdd->settle_ms;
			msg(MSG_NORMAL, "Changing settle delay from %dms "
			    "to %dms.\n", (int)old_settle,
			    (int)gw_delay.seek_settle);
		}

		cmd_ret = gw_set_params(gwfd, &gw_delay);

		if (cmd_ret != ACK_OKAY) {
			msg_error("Failed to set parameters of Greaseweazle "
				  "(%d).\n", cmd_ret);
			return GW_DEVT_INVALID;
		}

	}

	return gwfd;
}


/*
 * Detect drive connected to GW.
 */

int
gw_detect_drive(struct gw_fddrv *fdd)
{
	if (fdd->drive == -1)
		msg_fatal("Must specify drive with '-d <drive>' for now.\n");

	return 0;
}
