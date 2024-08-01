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
	int	r = -2;

	switch (kind) {
	case -1:
		r = -1;
		break;
	case 1:
	case 2:
		r = DS_DD;
		break;
	case 3:
	case 4:
		r = DS_HD;
		break;
	};

	return r;
}


/*
 * Detect GW, open and return file descriptor.
 *
 * Return device descriptor or GW_DEVT_INVALID on failure.
 */

gw_devt
gw_find_gw(const char **device_list)
{
	gw_devt	gwfd = GW_DEVT_INVALID;

	for (const char **p = device_list; *p; ++p) {
		gwfd = gw_open(*p);

		if (gwfd != GW_DEVT_INVALID) {
			msg(MSG_TSUMMARY,
			    "Found Greaseweazle using '%s'.\n", *p);
			break;
		}

	}

	if (gwfd == GW_DEVT_INVALID) {
		msg_fatal(EXIT_FAILURE,
			  "Failed to find Greaseweazle's device.\n");
	}

	return gwfd;
}


int
gw_detect_drive(gw_devt gwfd, struct cmd_settings *cmd_set)
{
	if (cmd_set->drive == -1)
		msg_fatal(EXIT_FAILURE,
			  "Must specify drive with '-d' for now.\n");

	return 0;
}


/*
 * Detect kind of drive.
 *
 * Returns with cmd_set->kind and cmd_set->densel set.
 */

int
gw_detect_drive_kind(gw_devt gwfd,
		     const struct gw_info *gw_info,
		     struct cmd_settings *cmd_set)
{
	if (cmd_set->drive == -1)
		msg_fatal(EXIT_FAILURE,
			  "Drive should have previously been set.\n");

	struct histogram	histo;

	int	densel = cmd_set->densel;

redo_kind:;
	int	kind   = cmd_set->kind;

	histo_init(0, 0, 1, gw_info->sample_freq, TICKS_PER_BUCKET, &histo);

	/* If densel not set, assume it's DS_DD for now.  Redo if
	 * guess is wrong. */
	gw_setdrive(cmd_set->gwfd, cmd_set->drive,
		    densel == DS_HD ? DS_HD : DS_DD);

	int chft_ret = collect_histo_from_track(gwfd, &histo);

	if (chft_ret)
		msg_fatal(EXIT_FAILURE,
			  "Couldn't collect histogram (%d)\n", chft_ret);

	struct histo_analysis	ha;

	histo_analysis_init(&ha);
	histo_analyze(&histo, &ha);
	histo_show(MSG_SAMPLES, &histo, &ha);

	if (histo.data_overflow > 25)	/* 25 is arbitrary */
		msg_fatal(EXIT_FAILURE, "Track 0 side 0 is unformatted.\n");

	double rpm   = ha.rpm;
	double prate = ha.pulse_rate_khz;

	if (rpm > 270.0 && rpm < 330.0) {
		/* 300 RPM */
		if (prate > 225.0 && prate < 287.5) {
			/* Pulse rate 250 kHz */
			kind = 2;
		} else if (prate > 450.0 && prate < 575.5) {
			/* Pulse rate 500 kHz */
			kind = 4;
		}
	} else if (rpm > 330.0 && rpm < 396.0) {
		/* 360 RPM */
		if (prate > 270.0 && prate < 345.0) {
			/* Pulse rate 300 kHz */
			kind = 1;
		} else if (prate > 450.0 && prate < 575.0) {
			kind = 3;
		}
	}

	if (kind == -1) {
		msg_fatal(EXIT_FAILURE,
			  "Failed to detect media type.\n"
			  "  Pulse clock: %f kHz\n"
			  "  Drive speed: %f RPM\n", prate, rpm);
	}

	if (densel == DS_NOTSET) {
		densel = kind2densel(kind);

		/* If density changed, redo in case it changes the RPM. */
		if (densel == DS_HD)
			goto redo_kind;
	}

	cmd_set->kind   = kind;
	cmd_set->densel = densel;

	// XXX Should a command line option control using this?
	// -1, -2 and -f should disable.
	media_encoding_init_from_histo(&cmd_set->gme, &ha,
					gw_info->sample_freq);

	msg(MSG_NORMAL, "Detected %s\n", kind2desc(cmd_set->kind));
	msg(MSG_TSUMMARY, "    (pulse rate %.1f kHz, rpm %.1f, "
			  "density select %s)\n",
			  prate, rpm,
			  cmd_set->densel == DS_HD ? "HD" : "DD");

	return 0;
}


int
gw_detect_sides(gw_devt gwfd,
		const struct gw_info *gw_info,
		struct cmd_settings *cmd_set)
{
	if (cmd_set->drive == -1)
		msg_fatal(EXIT_FAILURE,
			  "Drive should have previously been set.\n");

	struct histogram	histo;

	histo_init(0, 1, 1, gw_info->sample_freq, TICKS_PER_BUCKET, &histo);

	int chft_ret = collect_histo_from_track(gwfd, &histo);

	if (chft_ret)
		msg_fatal(EXIT_FAILURE,
			  "Couldn't collect histogram (%d)\n", chft_ret);

	struct histo_analysis	ha;

	histo_analysis_init(&ha);
	histo_analyze(&histo, &ha);
	histo_show(MSG_SAMPLES, &histo, &ha);

	cmd_set->sides = (histo.data_overflow > 25) ? 1 : 2;

	if (cmd_set->gme.rpm == 0.0) {
		media_encoding_init_from_histo(&cmd_set->gme, &ha,
						gw_info->sample_freq);
	}

	return 0;
}


int
gw_detect_steps(gw_devt gwfd, struct cmd_settings *cmd_set)
{
	cmd_set->steps = (cmd_set->kind == 1) ? 2 : 1;
	cmd_set->guess_steps = true;

	return 0;
}


int
gw_detect_tracks(gw_devt gwfd, struct cmd_settings *cmd_set)
{
	if (cmd_set->steps == -1)
		msg_fatal(EXIT_FAILURE,
			  "Steps should have previously been set.\n");

	cmd_set->tracks = GW_MAX_TRACKS / cmd_set->steps;
	cmd_set->guess_tracks = true;

	return 0;
}


gw_devt
gw_detect_init_all(struct cmd_settings *cmd_set,
		   struct gw_info *gw_info)
{
	// XXX Check returns.

	if (cmd_set->device) {
		cmd_set->gwfd = gw_open(cmd_set->device);
		if (cmd_set->gwfd == GW_DEVT_INVALID) {
			msg_error("Failed open Greaseweazle ('%s').\n",
				  cmd_set->device);
			return GW_DEVT_INVALID;
		}
	} else {
		cmd_set->gwfd = gw_find_gw(cmd_set->device_list);
		if (cmd_set->gwfd == GW_DEVT_INVALID) {
			msg_error("Failed find Greaseweazle.  Use '-G'.\n");
			return GW_DEVT_INVALID;
		}
	}

	int init_ret = gw_init(cmd_set->gwfd);

	if (init_ret != 0) {
		msg_error("Failed initialize Greaseweazle (%d).\n", init_ret);
		return GW_DEVT_INVALID;
	}

	// Get us back into a saner state if crashed on last run.
	int cmd_ret = gw_reset(cmd_set->gwfd);

	if (cmd_ret != ACK_OKAY) {
		msg_error("Failed to reset Greaseweazle (%d).\n", cmd_ret);
		return GW_DEVT_INVALID;
	}

	cmd_ret = gw_get_info(cmd_set->gwfd, gw_info);

	if (cmd_ret != ACK_OKAY) {
		msg_error("Failed to get info from Greaseweazle (%d).\n",
			  cmd_ret);
		return GW_DEVT_INVALID;
	}

	media_encoding_init(&cmd_set->gme, gw_info->sample_freq);

	gw_set_bus_type(cmd_set->gwfd, cmd_set->bus);

	if (cmd_set->drive == -1)
		gw_detect_drive(cmd_set->gwfd, cmd_set);

	if (cmd_set->kind != -1 && cmd_set->densel == DS_NOTSET)
		cmd_set->densel = kind2densel(cmd_set->kind);

	if (cmd_set->kind == -1)
		gw_detect_drive_kind(cmd_set->gwfd, gw_info, cmd_set);

	gw_setdrive(cmd_set->gwfd, cmd_set->drive, cmd_set->densel);

	if (cmd_set->sides == -1)
		gw_detect_sides(cmd_set->gwfd, gw_info, cmd_set);

	if (cmd_set->steps == -1)
		gw_detect_steps(cmd_set->gwfd, cmd_set);

	if (cmd_set->tracks == -1)
		gw_detect_tracks(cmd_set->gwfd, cmd_set);

	return cmd_set->gwfd;
}
