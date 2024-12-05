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
#include "gwdetect.h"


static const struct option cmd_long_args[] = {
	{ "drive",	 required_argument, NULL, 'd' },
	{ "revs",	 required_argument, NULL, 'r' },
	{ "side",	 required_argument, NULL, 's' },
	{ "track",	 required_argument, NULL, 't' },
	{ "logfile",	 required_argument, NULL, 'u' },
	{ "verbosity",	 required_argument, NULL, 'v' },
	{ "device",	 required_argument, NULL, 'G' },
	{ "stepdelay",	 required_argument, NULL, 'T' },
	{ "gwlogfile",	 required_argument, NULL, 'U' },
	/* Start of binary long options without single letter counterparts. */
	{ "hd",		 no_argument, NULL, 0 },
	{ "dd",		 no_argument, NULL, 0 },
	{ "reset",	 no_argument, NULL, 0 },
	{ "noreset",	 no_argument, NULL, 0 },
	{ 0, 0, 0, 0 }
};

#if defined(WIN64) || defined(WIN32)
static const char gwsd_def[] = "\\\\.\\COM3";
#else
static const char gwsd_def[] = "/dev/ttyACM0";
#endif

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
	.fdd.device = gwsd_def,
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
fatal_bad_number(const char *name)
{
	msg_fatal("%s requires a numeric argument.\n", name);
}


/*
 * Like strtol, but exit with a fatal error message if there are any
 * invalid characters or the string is empty.
 */

static long int
strtol_strict(const char *nptr, int base, const char *name)
{
	char *endptr;

	long int res = strtol(nptr, &endptr, base);
	if (*nptr == '\0' || *endptr != '\0')
		fatal_bad_number(name);

	return res;
}


static void
usage(const char *pgm_name)
{
	msg_fatal("Usage: %s [-G device] [-d drive] "
		  "[-t track] [-s side] [-r revs] "
		  "[--dd|--hd] [-T stp[,stl]] --[no]reset "
		  "[-v verbosity] [-u logfile] [-U gwlogfile]\n",
		  pgm_name);
}


static void
parse_args(int argc, char **argv, struct cmd_settings *cmd_set)
{

	int	opt;
	int	lindex = 0;

	while ((opt = getopt_long(argc, argv, "d:r:s:t:u:v:G:T:U:",
		cmd_long_args, &lindex)) != -1) {

		switch(opt) {
		case 0:;
			const char *name = cmd_long_args[lindex].name;

			if (!strcmp(name, "hd")) {
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
			if (optarg[1]) goto d_err;

			const int loarg = tolower(optarg[0]);

			switch(loarg) {
			case '0':
			case '1':
			case '2':
				cmd_set->fdd.bus = BUS_SHUGART;
				cmd_set->fdd.drive = loarg - '0';
				break;
			case 'a':
			case 'b':
				cmd_set->fdd.bus = BUS_IBMPC;
				cmd_set->fdd.drive = loarg - 'a';
				break;
			default: d_err:
				msg_error("Option-argument to '%c' must "
					  "be 0, 1, 2, a, or b.\n", opt);
				goto err_usage;
			}
			break;

		case 'r':;
			const int revs = strtol_strict(optarg, 10, "'r'");

			if (revs >= 0) {
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

			if (track >= 0 && track <= GW_MAX_TRACKS) {
				cmd_set->track = track;
			} else {
				msg_error("Option-argument to '%c' must "
					  "be between 0 and %d.\n",
					  opt, GW_MAX_TRACKS);
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

		case 'G':;
#if defined(WIN32) || defined(WIN64)
			/* If the user specified a 2 digit COM device
			 * without the '\\\\.\\' prefix, prefix their string
			 * with it.  Always malloc the string on MSW so we
			 * always know we can free() if it needed on this OS.
			 */

			char *ds;
			if (strncasecmp("COM", optarg, 3) == 0 &&
			    isdigit(optarg[3]) &&
			    isdigit(optarg[4]) &&
			    !optarg[5]) {
				ds = malloc(4 + 5 + 1);
				if (ds) {
					strcpy(ds, "\\\\.\\");
					strcpy(ds + 4, optarg);
				}
			} else {
				ds = strdup(optarg);
			}
			if (!ds)
				msg_fatal("Cannot allocate device name.\n");
			cmd_set->fdd.device = ds;
#else
			cmd_set->fdd.device = optarg;
#endif
			break;

		case 'T':;
			unsigned int step_ms, settle_ms;
			int sfn = sscanf(optarg, "%u,%u", &step_ms, &settle_ms);

			switch (sfn) {
			case 2:
				if (settle_ms > 65000) goto err_usage;
				cmd_set->fdd.settle_ms = settle_ms;
				/* FALLTHRU */
			case 1:
				if (step_ms > 65) goto err_usage;
				cmd_set->fdd.step_ms = step_ms;
				break;
			default:
				goto err_usage;
				break;
			}
			break;

		case 'U':
			cmd_set->devlogfile = optarg;
			break;

		default:  /* '?' */
			goto err_usage;
			break;
		}
	}

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

	parse_args(argc, argv, &cmd_settings);

	const char *gwsd = cmd_settings.fdd.device;

	gw_devt gwfd = gw_open(gwsd);

	if (gwfd == GW_DEVT_INVALID)
		msg_fatal("Failed to open Greaseweazle's serial device "
			  "'%s': %s.\n", gwsd, strerror(errno));

	gw_init(gwfd);

	if (cmd_settings.reset_on_init)
		gw_reset(gwfd);

	if (cmd_settings.fdd.drive == -1)
		gw_detect_drive(&cmd_settings.fdd);

	struct gw_info	gw_info;

	int cmd_ret = gw_get_info(gwfd, &gw_info);

	if (cmd_ret != ACK_OKAY) {
		// error handling
		return EXIT_FAILURE;
	}

	gw_set_bus_type(gwfd, cmd_settings.fdd.bus);

	gw_setdrive(gwfd, cmd_settings.fdd.drive,
		    cmd_settings.fdd.densel == DS_HD ? DS_HD : DS_DD);

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

	gw_unsetdrive(gwfd, cmd_settings.fdd.drive);

	gw_close(gwfd);

	// error checking
	msg_fclose();

	return EXIT_SUCCESS;
}
