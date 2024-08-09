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
			if (*selected_dev)
				*selected_dev = *p;
			break;
		}
	}

	return gwfd;
}


/*
 * Find and open GW.
 *
 * Returns device file descriptor and sets "selected_dev" to device name
 * or returns GW_DEVT_INVALID on failure.
 */

gw_devt
gw_find_open_gw(const char *device,
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

		if (*selected_dev)
			*selected_dev = device;
	} else {
		gwfd = gw_openlist(device_list, selected_dev);

		if (gwfd == GW_DEVT_INVALID) {
			msg_error("Failed find Greaseweazle.  Use '-G'.\n");
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
		msg_fatal(EXIT_FAILURE,
			  "Must specify drive with '-d' for now.\n");

	return 0;
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
			msg(MSG_TSUMMARY, "Changing step delay from %dms "
			    "to %dms.\n", (int)old_delay / 1000,
			    (int)gw_delay.step_delay / 1000);
		}

		if (fdd->settle_ms != -1) {
			uint16_t	old_settle = gw_delay.seek_settle;

			gw_delay.seek_settle = fdd->settle_ms;
			msg(MSG_TSUMMARY, "Changing settle delay from %dms "
			    "to %dms.\n", (int)old_settle,
			    (int)gw_delay.seek_settle);
		}

		cmd_ret = gw_set_params(gwfd, &gw_delay);

		if (cmd_ret != ACK_OKAY) {
			msg_error("Failed to get parameters of Greaseweazle "
				  "(%d).\n", cmd_ret);
			return GW_DEVT_INVALID;
		}

	}

	return gwfd;
}


/*
 * "histo" must be prepped before calling.
 */

void
gw_get_histo_analysis(gw_devt gwfd,
		      struct histogram *histo,
		      struct histo_analysis *ha)
{
	int ret = collect_histo_from_track(gwfd, histo);

	if (ret)
		msg_fatal(EXIT_FAILURE,
			  "Couldn't collect histogram (%d)\n", ret);
	
	histo_analysis_init(ha);
	histo_analyze(histo, ha);
	histo_show(MSG_SAMPLES, histo, ha);
}


/*
 * Detect kind of drive.
 *
 * Returns GW drive set and with vars fdd->kind and fdd->densel set.
 */

int
gw_detect_drive_kind(struct gw_fddrv *fdd,
		     const struct gw_info *gw_info,
		     struct histo_analysis *ha)
{
	if (fdd->drive == -1)
		msg_fatal(EXIT_FAILURE,
			  "Drive should have previously been set.\n");

	int	densel = fdd->densel;

redo_kind:;
	int	kind   = fdd->kind;

	/* If densel not set, assume it's DS_DD for now.  Redo if
	 * guess is wrong. */
	gw_setdrive(fdd->gwfd, fdd->drive, densel == DS_HD ? DS_HD : DS_DD);

	struct histogram	histo;

	histo_init(0, 0, 1, gw_info->sample_freq, TICKS_PER_BUCKET, &histo);

	gw_get_histo_analysis(fdd->gwfd, &histo, ha);

	if (histo.data_overflow > 25)	/* 25 is arbitrary */
		msg_fatal(EXIT_FAILURE, "Track 0 side 0 is unformatted.\n");

	double rpm   = ha->rpm;
	double brate = ha->bit_rate_khz;

	if (rpm > 270.0 && rpm < 330.0) {
		/* 300 RPM */
		if (brate > 225.0 && brate < 287.5) {
			/* Bit rate 250 kHz */
			kind = 2;
		} else if (brate > 450.0 && brate < 575.5) {
			/* Bit rate 500 kHz */
			kind = 4;
		}
	} else if (rpm > 330.0 && rpm < 396.0) {
		/* 360 RPM */
		if (brate > 270.0 && brate < 345.0) {
			/* Bit rate 300 kHz */
			kind = 1;
		} else if (brate > 450.0 && brate < 575.0) {
			kind = 3;
		}
	}

	if (kind == -1) {
		msg_fatal(EXIT_FAILURE,
			  "Failed to detect media type.\n"
			  "  Bit rate:    %7.3f kHz\n"
			  "  Drive speed: %7.3f RPM\n", brate, rpm);
	}

	if (densel == DS_NOTSET) {
		densel = kind2densel(kind);

		/* If density changed, redo in case it changes the RPM. */
		if (densel == DS_HD)
			goto redo_kind;
	}

	fdd->kind   = kind;
	fdd->densel = densel;

	msg(MSG_NORMAL, "Detected %s\n", kind2desc(fdd->kind));
	msg(MSG_TSUMMARY, "    (bit rate %.1f kHz, rpm %.1f, "
			  "density select %s)\n",
			  brate, rpm,
			  fdd->densel == DS_HD ? "HD" : "DD");

	return 0;
}


int
gw_detect_sides(struct gw_fddrv *fdd,
		const struct gw_info *gw_info)
{
	if (fdd->drive == -1)
		msg_fatal(EXIT_FAILURE,
			  "Drive should have previously been set.\n");

	struct histogram	histo;

	histo_init(0, 1, 1, gw_info->sample_freq, TICKS_PER_BUCKET, &histo);

	struct histo_analysis	ha;

	gw_get_histo_analysis(fdd->gwfd, &histo, &ha);

	fdd->sides = (histo.data_overflow > 25) ? 1 : 2;

	return 0;
}
