#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <getopt.h>
#include <ctype.h>

#include "gw.h"
#include "gwx.h"
#include "gwhisto.h"
#include "msg_levels.h"
#include "msg.h"
#include "gwfddrv.h"
#include "cmdutil.h"
#include "gwdetect.h"
#include "cfgfile.h"


static const struct option cmd_long_args[] = {
	{ "config",	 required_argument, NULL, 'C' },
	{ "drive",	 required_argument, NULL, 'd' },
	{ "revs",	 required_argument, NULL, 'r' },
	{ "side",	 required_argument, NULL, 's' },
	{ "track",	 required_argument, NULL, 't' },
	{ "logfile",	 required_argument, NULL, 'u' },
	{ "verbosity",	 required_argument, NULL, 'v' },
	{ "bustype",	 required_argument, NULL, 'B' },
	{ "device",	 required_argument, NULL, 'G' },
	{ "stepdelay",	 required_argument, NULL, 'T' },
	{ "gwlogfile",	 required_argument, NULL, 'U' },
	{ "serial",	 required_argument, NULL, 'Z' },
	/* Start of binary long options without single letter counterparts. */
	{ "noconfig",	 no_argument, NULL, 0 },
	{ "hd",		 no_argument, NULL, 0 },
	{ "dd",		 no_argument, NULL, 0 },
	{ "reset",	 no_argument, NULL, 0 },
	{ "noreset",	 no_argument, NULL, 0 },
	{ 0, 0, 0, 0 }
};

/* Only used on platforms without a USB scan backend. */
static const char *device_list[] = {
#if linux
	"/dev/greaseweazle",
	"/dev/ttyACM0",
#elif defined(WIN64) || defined(WIN32)
	"\\\\.\\COM3",
#endif
	NULL
};

static struct cmd_settings {
	struct gw_fddrv	fdd;
	int		track;
	int		side;
	int		revs;
	bool		reset_on_init;
	int		scrn_verbosity;
	int		file_verbosity;
	const char	*logfile;
	const char	*devlogfile;
} cmd_settings = {
	.fdd.device = NULL,
	.fdd.serial = NULL,
	.fdd.bus = BUS_IBMPC,
	.fdd.drive = -1,
	.fdd.kind = -1,
	.fdd.tracks = -1,
	.fdd.sides = -1,
	.fdd.steps = -1,
	.fdd.densel = DS_NOTSET,
	.fdd.step_ms = -1,
	.fdd.settle_ms = -1,
	.track = 0,
	.side = 0,
	.revs = 1,
	.reset_on_init = true,
	.scrn_verbosity = MSG_NORMAL,
	.file_verbosity = MSG_QUIET,
	.logfile = "gw.log",
	.devlogfile = NULL
};


static void
usage(const char *pgm_name)
{
	msg_fatal("Usage: %s [-G device] [-Z serial] [-d drive] "
		  "[-B bustype] [-t track] [-s side] [-r revs] "
		  "[--dd|--hd] [-T stp[,stl]] --[no]reset "
		  "[-v verbosity] [-u logfile] [-U gwlogfile]\n",
		  pgm_name);
}


static void
parse_args(int argc, char **argv, struct cmd_settings *cmd_set,
	   const char *cfgfile)
{

	int	opt;
	int	lindex = 0;
	int	opt_bus = BUS_NONE;
	bool	opt_d_given = false;

	optind = 0;	/* Reset getopt state; parse_args runs twice. */

	while ((opt = getopt_long(argc, argv, "d:r:s:t:u:v:B:C:G:T:U:Z:",
		cmd_long_args, &lindex)) != -1) {

		switch(opt) {
		case 'C':
			/* Config file: already handled by cfg_scan_argv(). */
			break;

		case 0:;
			const char *name = cmd_long_args[lindex].name;

			if (!strcmp(name, "noconfig")) {
				/* Handled by cfg_scan_argv(). */
			} else if (!strcmp(name, "hd")) {
				cmd_set->fdd.densel = DS_HD;
			} else if (!strcmp(name, "dd")) {
				cmd_set->fdd.densel = DS_DD;
			} else if (!strcmp(name, "reset")) {
				cmd_set->reset_on_init = true;
			} else if (!strcmp(name, "noreset")) {
				cmd_set->reset_on_init = false;
			} else {
				goto err_usage;
			}
			break;

		case 'd':
			if (parse_drive_arg(optarg, opt, &cmd_set->fdd))
				goto err_usage;

			opt_d_given = true;
			break;

		case 'r':;
			const int revs = strtol_strict(optarg, 10, "'r'");

			if (revs > 0) {
				cmd_set->revs = revs;
			} else {
				msg_error("Option-argument to '%c' must "
					  "be greater than 0.\n", opt);
				goto err_usage;
			}
			break;

		case 's':;
			const int side = strtol_strict(optarg, 10, "'s'");

			if (side >= 0 && side <= 1) {
				cmd_set->side = side;
			} else {
				msg_error("Option-argument to '%c' must "
					  "be 0 or 1.\n", opt);
				goto err_usage;
			}
			break;

		case 't':;
			const int track = strtol_strict(optarg, 10, "'t'");

			if (track >= 0 && track < GW_MAX_TRACKS) {
				cmd_set->track = track;
			} else {
				msg_error("Option-argument to '%c' must "
					  "be between 0 and %d.\n",
					  opt, GW_MAX_TRACKS - 1);
				goto err_usage;
			}
			break;

		case 'u':
			cmd_set->logfile = optarg;
			break;

		case 'v':;
			const char	vstr[] = "'v'";
			char		*p, *endptr;
			int		optav;

			if ((p = strchr(optarg, ':'))) {
				optav = strtol(optarg, &endptr, 10);

				if ((optarg == endptr) || (endptr != p))
					fatal_bad_number(vstr);

				cmd_set->scrn_verbosity =
						strtol_strict(p+1, 10, vstr);
				cmd_set->file_verbosity = optav;

			} else {
				optav = strtol_strict(optarg, 10, vstr);

				if (optav >= 10) {
					cmd_set->scrn_verbosity = optav % 10;
					cmd_set->file_verbosity = optav / 10;
				} else {
					cmd_set->scrn_verbosity = optav;
					cmd_set->file_verbosity = optav;
				}
			}

			break;

		case 'B':
			if (parse_bustype_arg(optarg, opt, &opt_bus))
				goto err_usage;
			break;

		case 'G':
			if (parse_device_arg(optarg, &cmd_set->fdd))
				goto err_usage;
			break;

		case 'T':
			if (parse_stepdelay_arg(optarg, &cmd_set->fdd))
				goto err_usage;
			break;

		case 'U':
			cmd_set->devlogfile = optarg;
			break;

		case 'Z':
			cmd_set->fdd.serial = optarg;
			break;

		default:  /* '?' */
			goto err_usage;
			break;
		}
	}

	if (opt_bus != BUS_NONE) {
		if (opt_d_given && cmd_set->fdd.bus != opt_bus) {
			msg_error("Option '-B' bus type conflicts with the "
				  "bus type implied by '-d'.\n");
			goto err_usage;
		}

		cmd_set->fdd.bus = opt_bus;
	}

	if (cmd_set->fdd.device && cmd_set->fdd.serial) {
		msg_error("Options '-G' and '-Z' are mutually exclusive.\n");
		goto err_usage;
	}

	/* The rest applies only after the command line is parsed. */
	if (cfgfile)
		return;

	if (optind != argc)
		goto err_usage;

	msg_scrn_set_level(cmd_set->scrn_verbosity);
	msg_file_set_level(cmd_set->file_verbosity);

	if ((cmd_set->file_verbosity > MSG_QUIET) && cmd_set->logfile) {
		if (!msg_fopen(cmd_set->logfile)) {
			msg_error("Failed to open log file '%s': %s\n",
				  cmd_set->logfile, strerror(errno));
			goto err_usage;
		}
	}

	if (cmd_set->devlogfile) {
		FILE *dlfp = fopen(cmd_set->devlogfile, "w");

		if (!dlfp) {
			msg_error("Failed to open device log file '%s': %s\n",
				  cmd_set->devlogfile, strerror(errno));
			goto err_usage;
		}

		if (!gw_set_logfp(dlfp)) {
			msg_error("Failed to set device log file '%s': %s\n",
				  cmd_set->devlogfile, strerror(errno));
			goto err_usage;
		}
	}

	return;

err_usage:
	if (cfgfile)
		msg_error("(while processing config file '%s')\n", cfgfile);
	usage(argv[0]);
}


int
main(int argc, char **argv)
{
	const char *slash = strrchr(argv[0], '/');
	const char *pgm   = slash ? slash + 1 : argv[0];

	if (!msg_error_prefix(pgm)) {
		msg_error("Failure to allocate memory for message prefix.\n");
		return EXIT_FAILURE;
	}

	bool		noconfig = false;
	const char	*cfgpath = cfg_scan_argv(argc, argv, &noconfig);

	if (!noconfig) {
		if (!cfgpath)
			cfgpath = cfg_default_path();

		if (cfgpath) {
			char	**cargv;
			int	cargc = cfg_load_argv(cfgpath, "gwhist",
						      cmd_long_args, &cargv);

			if (cargc > 1)
				parse_args(cargc, cargv, &cmd_settings, cfgpath);
		}
	}

	parse_args(argc, argv, &cmd_settings, NULL);

	const char *sdev = NULL;

	gw_devt gwfd = gw_find_open_gw(cmd_settings.fdd.device,
				       cmd_settings.fdd.serial,
				       device_list, &sdev);

	if (gwfd == GW_DEVT_INVALID)
		return EXIT_FAILURE;

	cmd_settings.fdd.gwfd = gwfd;

	gw_init(gwfd);

	if (cmd_settings.reset_on_init)
		gw_reset(gwfd);

	gw_set_bus_type(gwfd, cmd_settings.fdd.bus);

	if (cmd_settings.fdd.drive == -1 &&
	    gw_detect_drive(&cmd_settings.fdd, false))
		return EXIT_FAILURE;

	struct gw_info	gw_info;

	int cmd_ret = gw_get_info(gwfd, &gw_info);

	if (cmd_ret != ACK_OKAY) {
		// error handling
		return EXIT_FAILURE;
	}

	if (gw_setdrive(gwfd, cmd_settings.fdd.drive,
			cmd_settings.fdd.densel == DS_HD ?
			DS_HD : DS_DD) != ACK_OKAY) {
		msg_fatal("Failed to select and start drive.\n");
	}

	msg(MSG_NORMAL, "Reading track %d, side %d...\n",
		cmd_settings.track, cmd_settings.side);

	struct histogram	histo;

	histo_init(cmd_settings.track, cmd_settings.side, cmd_settings.revs,
			gw_info.sample_freq, TICKS_PER_BUCKET, &histo);

	int chft_ret = collect_histo_from_track(gwfd, &histo);

	if (chft_ret > 0) {
		msg_fatal("%s (%d)%s\n", gw_cmd_ack(chft_ret), chft_ret,
			  chft_ret == ACK_NO_INDEX ?
			  " [Is diskette in drive?]" : "");
	} else if (chft_ret < 0) {
		msg_fatal("Couldn't collect histogram.  Internal error.\n");
	}

	struct histo_analysis	ha;

	histo_analysis_init(&ha);
	histo_analyze(&histo, &ha);
	histo_show(MSG_NORMAL, &histo, &ha);

	if (gw_unsetdrive(gwfd, cmd_settings.fdd.drive) != ACK_OKAY)
		msg_error("Failed to stop and deselect drive.\n");

	gw_close(gwfd);

	// error checking
	msg_fclose();

	return EXIT_SUCCESS;
}
