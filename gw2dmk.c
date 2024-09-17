#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <getopt.h>
#include <ctype.h>
#include <regex.h>
#include <limits.h>
#include <signal.h>

#include "greaseweazle.h"
#include "msg_levels.h"
#include "msg.h"
#include "gw.h"
#include "gwx.h"
#include "gwhisto.h"
#include "gwfddrv.h"
#include "gw2dmkcmdset.h"
#include "gwdetect.h"
#include "gwdecode.h"
#include "dmk.h"
#include "dmkmerge.h"

#if defined(WIN64) || defined(WIN32)
#include <windows.h>
#endif


#define	GUESS_TRACKS	GW_MAX_TRACKS


const char version[] = VERSION;

static const struct option cmd_long_args[] = {
	{ "alternate",	 required_argument, NULL, 'a' },
	{ "drive",	 required_argument, NULL, 'd' },
	{ "encoding",	 required_argument, NULL, 'e' },
	{ "fmthresh",	 required_argument, NULL, 'f' },
	{ "ignore",	 required_argument, NULL, 'g' },
	{ "ipos",	 required_argument, NULL, 'i' },
	{ "kind",	 required_argument, NULL, 'k' },
	{ "dmktracklen", required_argument, NULL, 'l' },
	{ "steps",	 required_argument, NULL, 'm' },
	{ "postcomp",	 required_argument, NULL, 'p' },
	{ "sides",	 required_argument, NULL, 's' },
	{ "tracks",	 required_argument, NULL, 't' },
	{ "logfile",	 required_argument, NULL, 'u' },
	{ "verbosity",	 required_argument, NULL, 'v' },
	{ "fmtimes",	 required_argument, NULL, 'w' },
	{ "maxtries",	 required_argument, NULL, 'x' },
	{ "device",	 required_argument, NULL, 'G' },
	{ "menu",	 required_argument, NULL, 'M' },
	{ "stepdelay",	 required_argument, NULL, 'T' },
	{ "gwlogfile",	 required_argument, NULL, 'U' },
	{ "mfmthresh1",	 required_argument, NULL, '1' },
	{ "mfmthresh2",	 required_argument, NULL, '2' },
	/* Start of binary long options without single letter counterparts. */
	{ "hd",		 no_argument, NULL, 0 },
	{ "dd",		 no_argument, NULL, 0 },
	{ "hole",	 no_argument, NULL, 0 },
	{ "nohole",	 no_argument, NULL, 0 },
	{ "join",	 no_argument, NULL, 0 },
	{ "nojoin",	 no_argument, NULL, 0 },
	{ "compat",	 no_argument, NULL, 0 },
	{ "nocompat",	 no_argument, NULL, 0 },
	{ "dmkopt",	 no_argument, NULL, 0 },
	{ "nodmkopt",	 no_argument, NULL, 0 },
	{ "usehisto",	 no_argument, NULL, 0 },
	{ "nousehisto",	 no_argument, NULL, 0 },
	{ "force",	 no_argument, NULL, 0 },
	{ "noforce",	 no_argument, NULL, 0 },
	{ "reset",	 no_argument, NULL, 0 },
	{ "noreset",	 no_argument, NULL, 0 },
	{ "reverse",	 no_argument, NULL, 0 },
	{ "noreverse",	 no_argument, NULL, 0 },
	{ 0, 0, 0, 0 }
};

static struct cmd_settings cmd_settings = {
	.device_list = (const char *[]){
#if linux
		"/dev/greaseweazle",
		"/dev/ttyACM0",
#elif defined(WIN64) || defined(WIN32)
		"\\\\.\\COM3",
#endif
		NULL }, 
	.fdd.gwfd = GW_DEVT_INVALID,
	.fdd.device = NULL,
	.fdd.bus = BUS_IBMPC,
	.fdd.drive = 0,
	.fdd.kind = -1,
	.fdd.tracks = -1,
	.fdd.sides = -1,
	.fdd.steps = -1,
	.fdd.densel = DS_NOTSET,
	.fdd.step_ms = -1,
	.fdd.settle_ms = -1,
	.guess_tracks = false,
	.guess_sides = false,
	.guess_steps = false,
	.check_compat_sides = true,
	.reset_on_init = true,
	.forcewrite = false,
	.use_histo = false,
	.usr_encoding = MIXED,
	.reverse_sides = false,
	.hole = true,
	.alternate = 0,
	.iam_pos = -1,
	.fmtimes = 2,
	.usr_fmthresh = -1,
	.usr_mfmthresh1 = -1,
	.usr_mfmthresh2 = -1,
	.usr_postcomp = 0.5,
	.ignore = 0,
	.join_sectors = true,
	.menu_intr_enabled = false,
	.menu_err_enabled = false,
	.scrn_verbosity = MSG_TSUMMARY,
	.file_verbosity = MSG_QUIET,
	.dmkopt = false,
	.usr_dmktracklen = 0,
	.logfile = NULL,
	.devlogfile = NULL,
	.dmkfile = NULL,
	.gme.rpm = 0.0,
	.gme.data_clock = 0.0,
	.gme.bit_rate = 0.0,
	.gme.fmthresh = 0,
	.gme.mfmthresh1 = 0,
	.gme.mfmthresh2 = 0,
	.gme.mfmshort = 0.0,
	.gme.thresh_adj = 0.0,
	.min_sectors = {[0 ... DMK_MAX_TRACKS-1] = {[0 ... 1] = 0}},
	.retries     = {[0 ... DMK_MAX_TRACKS-1] = {[0 ... 1] = 4}},
	.min_retries = {[0 ... DMK_MAX_TRACKS-1] = {[0 ... 1] = 0}}
};

static volatile gw_devt cleanup_gwfd   = GW_DEVT_INVALID;
static volatile bool    menu_requested = false;
static volatile bool    exit_requested = false;
static volatile bool    reading_floppy = false;


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


#ifdef __GNUC__
static int vu(const char *fmt, va_list ap) __attribute__((format(printf,1,0)));
static int u(const char *fmt, ...)	   __attribute__((format(printf,1,2)));
#endif

static int
vu(const char *fmt, va_list ap)
{
	return vfprintf(stderr, fmt, ap);
}

static int
u(const char *fmt, ...)
{
	va_list	args;

	va_start(args, fmt);
	int ret = vu(fmt, args);
	va_end(args);

	return ret;
}

static void
usage(const char *pgm_name, struct cmd_settings *cmd_set)
{
	u("Usage: %s [options] file.dmk\n\n"
		" Options [settings in brackets]:\n", pgm_name);

	u("  -G device       Greaseweazle device [%s]\n",
				cmd_set->fdd.device ? cmd_set->fdd.device :
				cmd_set->device_list[0]);
	u("  -d drive        Drive unit {a,b,0,1,2} [%c]\n",
				cmd_set->fdd.bus == BUS_SHUGART ?
				cmd_set->fdd.drive + '0' :
				cmd_set->fdd.drive + 'a');
	u("  -v verbosity    Amount of output [%d]\n",
				(cmd_set->file_verbosity * 10) +
				cmd_set->scrn_verbosity);
	u("                  0 = No output\n");
	u("                  1 = Summary of disk\n");
	u("                  2 = + summary of each track\n");
	u("                  3 = + individual errors\n");
	u("                  4 = + track IDs and DAMs\n");
	u("                  5 = + hex data and event flags\n");
	u("                  6 = like 4, but with raw data too\n");
  	u("                  7 = like 5, but with Greaseweazle samples too\n");
  	u("                  21 = level 2 to logfile, 1 to screen, etc.\n");
	u("  -u logfile      Log output to the given file [%s]\n",
				cmd_set->logfile ? cmd_set->logfile : "none");
	u("  -U gwlogfile    Greaseweazle transaction logfile [%s]\n",
				cmd_set->devlogfile ? cmd_set->devlogfile :
				"none");
	//u("  -R logfile      Replay logfile\n");
	u("  -M {i,e,d}      Menu control [d]\n");
	u("                  i = Interrupt (^C) invokes menu\n");
	u("                  e = Errors equals retries invokes menu\n");
	u("                  d = Disables invoking menu\n");
	u("  --[no]hole      Use or not index hole for track start [%shole]\n",
				cmd_set->hole ? "" : "no");
	u("  --[no]join      Join or not sectors between retries [%sjoin]\n",
				cmd_set->join_sectors ? "" : "no");
	u("  --[no]compat    Compare or not sides for incompatible formats "
				"[%scompat]\n",
				cmd_set->check_compat_sides ? "" : "no");
	u("  --[no]dmkopt    Optimize or not DMK track length [%sdmkopt]\n",
				cmd_set->dmkopt ? "" : "no");
	u("  --[no]usehisto  Use histogram or not for autotuning of thresholds "
				"[%susehisto]\n",
				cmd_set->use_histo ? "" : "no");
	u("  --[no]force     Force or not to overwrite existing DMK output "
				"file [%sforce]\n",
				cmd_set->forcewrite ? "" : "no");
	u("  --[no]reset     Reset GW upon initialization [%sreset]\n",
				cmd_set->reset_on_init ? "" : "no");

	u("\n Options to manually set values that are normally "
			"autodetected:\n");
	u("  -k kind         Kind of floppy drive and media\n");
		for (int k = 1; k <= 4; ++k)
			u("                  %d = %s\n", k, kind2desc(k));
	u("  -m steps        Step multiplier, 1 or 2\n");
	u("  -t tracks       Number of tracks per side\n");
	u("  -s sides        Number of sides, 1 or 2\n");
	u("  -e encoding     0 = mixed, 1 = FM (SD), 2 = MFM (DD or HD), "
				"3 = RX02 [%d]\n", cmd_set->usr_encoding);
	u("  --dd|--hd       Density Select, pin 2\n");
	u("  -x max_retry    Max retries on errors [%d]\n",
				cmd_set->retries[0][0]);
	u("  -X min_retry    Min retries even if no errors [%d]\n",
				cmd_set->min_retries[0][0]);
	u("  -S min_sector   Min sector count [%d]\n",
				cmd_set->min_sectors[0][0]);
	u("  --[no]reverse   Reverse sides or not [%sreverse]\n",
				cmd_set->reverse_sides ? "" : "no");
	// XXX -z
	u("  -q quirk        Bitmap of support for some format quirks "
				"[0x%02x]\n", cmd_set->quirk);
	u("                  0x01 = ID CRCs omit a1 a1 a1 premark\n");
	u("                  0x02 = Data CRCs omit a1 a1 a1 premark\n");
	u("                  0x04 = Third a1 a1 a1 premark isn't missing "
				"clock\n");
	u("                  0x08 = Extra bytes (data only) after data CRC\n");
	u("                  0x10 = Extra bytes (4 data + 2 CRC) after data "
				"CRC\n");
	u("                  0x20 = 4 extra data bytes before data CRC\n");
	u("                  0x40 = FM IAM can have either 0xd7 or 0xc7 "
				"clock\n");
	u("                  0x80 = Extra MFM clocks may be present\n");
	u("  -w fmtimes      Write FM bytes 1 or 2 times [%d]\n",
				cmd_set->fmtimes);
	u("  -l bytes        DMK track length\n");

	u("\n Rarely used fine-tuning options:\n");
	u("  -f threshold    FM threshold for short vs. long\n");
	u("  -1 threshold    MFM threshold for short vs. medium\n");
	u("  -2 threshold    MFM threshold for medium vs. long\n");
	u("  -p postcomp     Amount of read-postcompensation (0.0-1.0) "
				"[%.2f]\n", cmd_set->usr_postcomp);
	u("  -T stp[,stl]    Step time");
		if (cmd_set->fdd.step_ms != -1)
			u(" [%u]", cmd_set->fdd.step_ms);
		u(" and head settling time");
		if (cmd_set->fdd.settle_ms != -1)
			u(" [%u]", cmd_set->fdd.settle_ms);
		u(" in ms\n");
	u("  -g ign          Ignore first ign bytes of track [%d]\n",
				cmd_set->ignore);
	u("  -i ipos         Force IAM to ipos from track start; if -1, "
				"don't [%d]\n", cmd_set->iam_pos);
	u("  -a alternate    Alternate even/odd tracks on retries with -m2 "
				"[%d]\n", cmd_set->alternate);
	u("                  0 = always even\n");
	u("                  1 = always odd\n");
	u("                  2 = even, then odd\n");
	u("                  3 = odd, then even\n");

	u("\n%s version %s\n\n", pgm_name, version);

	exit(EXIT_FAILURE);
}


enum menu_action {
	MENU_NOCHANGE,
	MENU_QUIT,
	MENU_NORETRY,
	MENU_NEWRETRIES
};


static enum menu_action
menu(int failing, int retries[GW_MAX_TRACKS][2])
{
	printf("\n*** Reading paused. ***");

	do {
		printf("\nWould you like to (c)ontinue, (q)uit, "
		       "%schange # of (r)etries%s:\n",
		       failing ? "" : "or ",
		       failing ? ", or\n(g)ive up retrying and continue on "
		       "with the data as read" : "");
		fflush(stdout);

		char inc;
		int ret = scanf(" %c", &inc);
		if (ret != 1)
			continue;
		switch (inc) {
		case 'c': return MENU_NOCHANGE;
		case 'q': return MENU_QUIT;
		case 'g': return MENU_NORETRY;
		case 'r':
			do {
				printf("New retry limit? ");
				fflush(stdout);

				int rnum;
				ret = scanf(" %d", &rnum);

				if (ret == 1 && rnum >= 0) {
					for (int i = 0; i < GW_MAX_TRACKS;
					     ++i) {
						retries[i][0] = rnum;
						retries[i][1] = rnum;
					}

					printf("Retry limit is now %d.\n",
					       rnum);

					return MENU_NEWRETRIES;
				}

				printf("Invalid number.\n");

			} while (1);
			break;

		default:
			printf("Input character '%c' unrecognized.\n", inc);
			break;
		}

	} while (1);
}


/*
 * Parse a list of track ranges.
 *
 * Returns:
 *   0 - Success
 *   1 - regcomp failure (internal error)
 *   2 - Parse list failure (internal error)
 *   3 - Track or side parameter out of range (user error)
 */

static int
parse_tracks(const char *ss, int opt_matrix[GW_MAX_TRACKS][2])
{
	int err = 0;
	regex_t regex;
	static const char re[] =
	    "^([0-9]+)" "(:[0-9]+((/[01])?(-([0-9]+(/[01])?)?)?)?)?" "(,|$)";

	if (regcomp(&regex, re, REG_EXTENDED | REG_NEWLINE))
		return 1;

	while (!err && *ss != '\0') {
		regmatch_t pmatch[9];

		if (regexec(&regex, ss, COUNT_OF(pmatch), pmatch, 0)) {
			err = 1;
			break;
		}

		int range_value    = -1;
		int range_track[2] = { -1, -1 };
		int range_side[2]  = { -1, -1 };
		int range_hyphen   = 0;

		regoff_t old_soff = INT_MAX;

		for (int i = 1; i < COUNT_OF(pmatch); ++i) {
			regoff_t soff = pmatch[i].rm_so;
			regoff_t eoff = pmatch[i].rm_eo;
			const char *p = ss + soff;
			char *eip;

			if (soff == -1)
				break;

			/*
			 * Skip if its a null entry or if it's an
			 * entry that's a repeat.
			 */

			if (soff == eoff || soff == old_soff)
				continue;

			if (range_value == -1) {
				range_value = strtol(p, &eip, 0);
			} else if (range_track[0] == -1 && p[0] == ':') {
				range_track[0] = strtol(&p[1], &eip, 0);
			} else if (range_side[0] == -1 && p[0] == '/') {
				range_side[0] = strtol(&p[1], &eip, 0);
			} else if (p[0] == '-') {
				range_hyphen = 1;
			} else if (range_track[1] == -1) {
				range_track[1] = strtol(&p[0], &eip, 0);
			} else if (range_side[1] == -1 && p[0] == '/') {
				range_side[1] = strtol(&p[1], &eip, 0);
			} else if (p[0] == ',') {
				break;
			} else {
				err = 2;
				break;
			}

			old_soff = soff;
		}

		if (err)
			break;

		if (range_track[0] == -1) {
			range_track[0] = 0;
			range_side[0]  = 0;
			range_track[1] = GW_MAX_TRACKS - 1;
			range_side[1]  = 1;
		} else {
			if (range_side[0] == -1)
				range_side[0] = 0;

			if (range_track[1] == -1)
				range_track[1] =
				    range_hyphen ? GW_MAX_TRACKS -
				    1 : range_track[0];

			if (range_side[1] == -1)
				range_side[1] =
				    range_hyphen ? 1 : range_side[0];
		}

		/* Check to make sure tracks and sides are in order. */

		if (range_track[1] < range_track[0] ||
		    (range_track[1] == range_track[0] &&
		     range_side[1] < range_side[0])) {
			err = 3;
			break;
		}

		/* Now set the opt_matrix for the range given. */

		for (int tm = range_track[0] * 2 + range_side[0];
		     tm <= range_track[1] * 2 + range_side[1]; ++tm) {
			opt_matrix[tm / 2][tm % 2] = range_value;
		}

		ss += pmatch[0].rm_eo;
	}

	return err;
}


static void
parse_args(int argc,
	   char **argv,
	   const char *pgm_name,
	   struct cmd_settings *cmd_set)
{

	int	opt;
	int	lindex = 0;

	while ((opt = getopt_long(argc, argv,
			"a:d:e:f:g:i:k:l:m:p:q:s:t:u:v:w:x:G:M:S:T:U:X:1:2:",
			cmd_long_args, &lindex)) != -1) {

		switch(opt) {
		case 0:;
			const char *name = cmd_long_args[lindex].name;

			if (!strcmp(name, "hd")) {
				cmd_set->fdd.densel = DS_HD;
			} else if (!strcmp(name, "dd")) {
				cmd_set->fdd.densel = DS_DD;
			} else if (!strcmp(name, "hole")) {
				cmd_set->hole = true;
			} else if (!strcmp(name, "nohole")) {
				cmd_set->hole = false;
			} else if (!strcmp(name, "join")) {
				cmd_set->join_sectors = true;
			} else if (!strcmp(name, "nojoin")) {
				cmd_set->join_sectors = false;
			} else if (!strcmp(name, "compat")) {
				cmd_set->check_compat_sides = true;
			} else if (!strcmp(name, "nocompat")) {
				cmd_set->check_compat_sides = false;
			} else if (!strcmp(name, "dmkopt")) {
				cmd_set->dmkopt = true;
			} else if (!strcmp(name, "nodmkopt")) {
				cmd_set->dmkopt = false;
			} else if (!strcmp(name, "usehisto")) {
				cmd_set->use_histo = true;
			} else if (!strcmp(name, "nousehisto")) {
				cmd_set->use_histo = false;
			} else if (!strcmp(name, "force")) {
				cmd_set->forcewrite = true;
			} else if (!strcmp(name, "noforce")) {
				cmd_set->forcewrite = false;
			} else if (!strcmp(name, "reset")) {
				cmd_set->reset_on_init = true;
			} else if (!strcmp(name, "noreset")) {
				cmd_set->reset_on_init = false;
			} else if (!strcmp(name, "reverse")) {
				cmd_set->reverse_sides = true;
			} else if (!strcmp(name, "noreverse")) {
				cmd_set->reverse_sides = false;
			} else {
				goto err_usage;
			}
			break;

		case 'a':;
			const int alt = strtol_strict(optarg, 10, "'a'");
			if (alt < 0 || alt > 3) goto err_usage;
			cmd_set->alternate = alt;
			break;

		case 'd':
			if (optarg[0] && optarg[1]) goto d_err;

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

		case 'e':;
			const int uenc = strtol_strict(optarg, 10, "'e'");
			if (uenc < FM || uenc > RX02) goto err_usage;
			cmd_set->usr_encoding = uenc;
			break;

		case 'f':;
			const int fmthr = strtol_strict(optarg, 10, "'f'");
			if (fmthr < 1) goto err_usage;
			cmd_set->usr_fmthresh = fmthr;
			break;

		case 'g':;
			const int ign = strtol_strict(optarg, 10, "'g'");
			cmd_set->ignore = ign;
			break;

		case 'i':;
			const int ipos = strtol_strict(optarg, 10, "'i'");
			cmd_set->iam_pos = ipos;
			break;

		case 'k':;
			const int kind = strtol_strict(optarg, 10, "'k'");

			if (kind >= 1 && kind <= 4) {
				cmd_set->fdd.kind = kind;
			} else {
				msg_error("Option-argument to '%c' must "
					  "be 1 or 4.\n", opt);
				goto err_usage;
			}
			break;

		case 'l':;
			const int dmktracklen =
					strtol_strict(optarg, 10, "'l'");
			if (dmktracklen < 0 || dmktracklen >= 0x4000)
				goto err_usage;
			cmd_set->usr_dmktracklen = dmktracklen;
			break;

		case 'm':;
			const int steps = strtol_strict(optarg, 10, "'m'");

			if (steps >= 1 && steps <= 2) {
				cmd_set->fdd.steps = steps;
			} else {
				msg_error("Option-argument to '%c' must "
					  "be 1 or 2.\n", opt);
				goto err_usage;
			}
			break;

		case 'p':;
			double postcomp;
			const int pret = sscanf(optarg, "%lf", &postcomp);

			if (pret != 1 || postcomp < 0.0 || postcomp > 1.0)
				goto err_usage;
			cmd_set->usr_postcomp = postcomp;
			break;

		case 'q':;
			const int quirk = strtol_strict(optarg, 0, "'q'");
			if (quirk & ~QUIRK_ALL) goto err_usage;

			if (((quirk & QUIRK_EXTRA) != 0) +
			    ((quirk & QUIRK_EXTRA_CRC) != 0) +
			    ((quirk & QUIRK_EXTRA_DATA) != 0) > 1) {
				msg_fatal("Quirks %#x, %#x, and %#x "
				"cannot be used together.\n",
				QUIRK_EXTRA, QUIRK_EXTRA_CRC, QUIRK_EXTRA_DATA);
			}
			cmd_set->quirk = quirk;
			break;

		case 's':;
			const int sides = strtol_strict(optarg, 10, "'s'");

			if (sides < 1 || sides > 2) {
				msg_error("Option-argument to '%c' must "
					  "be 1 or 2.\n", opt);
				goto err_usage;
			}
			cmd_set->fdd.sides = sides;
			break;

		case 't':;
			const int tracks = strtol_strict(optarg, 10, "'t'");

			if (tracks < 0 || tracks > GW_MAX_TRACKS) {
				msg_error("Option-argument to '%c' must "
					  "be between 0 and %d.\n",
					  opt, GW_MAX_TRACKS);
				goto err_usage;
			}
			cmd_set->fdd.tracks = tracks;
			break;

		case 'u':
			cmd_set->logfile = optarg;
			break;

		case 'v':;
			const int optav = strtol_strict(optarg, 10, "'v'");
			if (optav < 0 || optav > 99) goto err_usage;
			cmd_set->scrn_verbosity = optav % 10;
			cmd_set->file_verbosity = optav / 10;
			break;

		case 'w':;
			const int fmtm = strtol_strict(optarg, 10, "'w'");
			if (fmtm < 1 || fmtm > 2) goto err_usage;
			cmd_set->fmtimes = fmtm;
			break;

		case 'x':
			if (parse_tracks(optarg, cmd_set->retries))
				goto err_usage;
			break;

		case 'G':;
#if defined(WIN32) || defined(WIN64)
			/* If the user specified the COM device without the
			 * '\\\\.\\' prefix, prefix their string with it.
			 * Always malloc the string on MSW so we always
			 * know we can free() if it needed on this OS. */

			size_t gstrsz = strlen(optarg) + 1;
			char *ds;
			if (strncasecmp("COM", optarg, 3) == 0) {
				ds = malloc(gstrsz + 4);
				strcpy(ds, "\\\\.\\");
				ds += 4;
			} else {
				ds = malloc(gstrsz);
			}
			if (!ds)
				msg_fatal("Cannot allocate device name.\n");
			strcpy(ds, optarg);
			cmd_set->fdd.device = ds;
#else
			cmd_set->fdd.device = optarg;
#endif
			break;

		case 'M':
			if (!strcmp(optarg, "i")) {
				cmd_set->menu_intr_enabled = true;
			} else if (!strcmp(optarg, "e")) {
				cmd_set->menu_err_enabled = true;
			} else if (!strcmp(optarg, "d")) {
				cmd_set->menu_intr_enabled = false;
				cmd_set->menu_err_enabled = false;
			} else {
				goto err_usage;
			}
			break;

		case 'S':
			if (parse_tracks(optarg, cmd_set->min_sectors))
				goto err_usage;
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

		case 'X':
			if (parse_tracks(optarg, cmd_set->min_retries))
				goto err_usage;
			break;

		case '1':;
			const int mfmthr1 = strtol_strict(optarg, 10, "'1'");
			if (mfmthr1 < 1) goto err_usage;
			cmd_set->usr_mfmthresh1 = mfmthr1;
			break;

		case '2':;
			const int mfmthr2 = strtol_strict(optarg, 10, "'2'");
			if (mfmthr2 < 1) goto err_usage;
			cmd_set->usr_mfmthresh2 = mfmthr2;
			break;

		default:  /* '?' */
			goto err_usage;
			break;
		}
	}

	if (optind != (argc-1))
		goto err_usage;

	cmd_set->dmkfile = argv[optind];

	msg_scrn_set_level(cmd_set->scrn_verbosity);
	msg_file_set_level(cmd_set->file_verbosity);

	if (cmd_set->file_verbosity > MSG_QUIET) {
		if (!cmd_set->logfile) {
			char *p = strrchr(cmd_set->dmkfile, '.');

			size_t dmk_len = p ? p - cmd_set->dmkfile :
					     strlen(cmd_set->dmkfile);

			char *s1 = malloc(dmk_len + 5);
			if (!s1)
				msg_fatal("Cannot allocate log file name.\n");

			sprintf(s1, "%.*s.log", (int)dmk_len, cmd_set->dmkfile);
			cmd_set->logfile = s1;
		}

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
	usage(pgm_name, cmd_set);
}


static int
imark_fn(uint32_t imark, void *data)
{
	return gwflux_decode_index(imark, (struct flux2dmk_sm *)data);
}


struct pulse_data {
	struct gw_media_encoding	*gme;
	struct flux2dmk_sm		*flux2dmk;
};


static int
pulse_fn(uint32_t pulse, void *data)
{
	struct pulse_data	*pdata = (struct pulse_data *)data;

	return gwflux_decode_pulse(pulse, pdata->gme, pdata->flux2dmk);
}


static void
dmk_file_init(struct dmk_file *dmkf)
{
	dmk_header_init(&dmkf->header, 0, DMKRD_TRACKLEN_MAX);

	memset(dmkf->track, 0, sizeof(struct dmk_track));
}


/*
 * Open a file for writing and return a file pointer.
 *
 * If fail_if_exists is true, ensure file does not exist prior to open.
 */

static FILE *
fopenwx(const char *file, bool fail_if_exists)
{
	int oflags = O_RDWR | O_CREAT | O_TRUNC | (fail_if_exists ? O_EXCL : 0);

	int fd = open(file, oflags, 0644);

	if (fd == -1)
		return NULL;

	return fdopen(fd, "wb");
}


/*
 * Read the track and side.
 *
 *    -2	Attempt restart.
 *    -1	Side count changed to 1 (cmd_set->fdd.sides=1).
 *     0	Everything good, continue reading next track.
 *     1	Everything good, but done reading this disk.
 *     2+	Error.
 */

// XXX Too many args.  Rethink.
static int
read_track(struct cmd_settings *cmd_set,
	   uint32_t sample_freq,
	   struct dmk_file *dmkf,
	   struct dmk_disk_stats *dds,
	   int track,
	   int side,
	   int *first_encoding)
{
	struct dmk_track_stats	dts;
	dmk_track_stats_init(&dts);

	struct flux2dmk_sm flux2dmk;

	int retry = 0;

retry:
	msg(MSG_TSUMMARY, "Track %d, side %d, pass %d",
	    track, side, retry + 1);
	if (retry)
		msg(MSG_TSUMMARY, "/%d", cmd_set->retries[track][side] + 1);
	msg(MSG_TSUMMARY, ":");
	msg_scrn_flush();

	int headpos = track * cmd_set->fdd.steps;

	if (cmd_set->fdd.steps == 2) {
		headpos += cmd_set->alternate & 1;

		if ((retry > 0) && (cmd_set->alternate & 2))
			headpos ^= 1;
	}

	gw_seek(cmd_set->fdd.gwfd, headpos);
	// error checking

	gw_head(cmd_set->fdd.gwfd, side ^ cmd_set->reverse_sides);
	// error checking

	fdecoder_init(&flux2dmk.fdec, sample_freq);

	flux2dmk.fdec.usr_encoding   = cmd_set->usr_encoding;
	flux2dmk.fdec.first_encoding = *first_encoding;
	flux2dmk.fdec.cur_encoding   = *first_encoding;
	flux2dmk.fdec.use_hole       = cmd_set->hole;
	flux2dmk.fdec.quirk          = cmd_set->quirk;
	flux2dmk.fdec.reverse_sides  = cmd_set->reverse_sides;
	flux2dmk.fdec.awaiting_iam   = (cmd_set->iam_pos >= 0) ? true : false;

	dmk_track_sm_init(&flux2dmk.dtsm,
			  dds,
			  &dmkf->header,
			  &dmkf->track[track][side],
			  &dts);

	flux2dmk.dtsm.dmk_iam_pos    = cmd_set->iam_pos;
	flux2dmk.dtsm.dmk_ignore     = cmd_set->ignore;
	flux2dmk.dtsm.accum_sectors  = cmd_set->join_sectors;

	uint8_t *fbuf = 0;
	ssize_t bytes_read = gw_read_stream(cmd_set->fdd.gwfd, 1, 0, &fbuf);

	if (bytes_read < 0) {
		int	gwerr = (int)-bytes_read;

		free(fbuf);
		msg(MSG_ERRORS, "gw_read_stream() failure: %s (%d)%s\n",
		    gw_cmd_ack(gwerr), gwerr,
		    gwerr == ACK_NO_INDEX ?  " [Is diskette in drive?]" : "");
		return 2;
	}

	struct pulse_data pdata = { &cmd_set->gme, &flux2dmk };
	struct gw_decode_stream_s gwds = {
					  .ds_ticks = 0,
					  .ds_last_pulse = 0,
					  .ds_status = -1,
					  .decoded_imark = imark_fn,
					  .imark_data = &flux2dmk,
					  .decoded_space = NULL,
					  .space_data = NULL,
					  .decoded_pulse = pulse_fn,
					  .pulse_data = &pdata
					 };

	ssize_t dsv = gw_decode_stream(fbuf, bytes_read, &gwds);

	if (dsv == -1) {
		msg(MSG_ERRORS, "Decode error from stream\n");
		free(fbuf);
		return 3;
	} else if (dsv < bytes_read) {
		// XXX Just not handling this at present, but warn.
		msg(MSG_ERRORS, "Leftover bytes in stream! "
				"(%d out of %d bytes unparsed, status %d)\n",
				(int)(bytes_read - dsv), (int)bytes_read,
				gwds.ds_status);
	}

	msg(MSG_HEX, "[end of data] ");

	free(fbuf);

	gw_decode_flush(&flux2dmk);

	if (flux2dmk.fdec.use_hole && flux2dmk.dtsm.track_hole_p) {
		dmk_data_rotate(&flux2dmk.dtsm.trk_working,
				flux2dmk.dtsm.track_hole_p);
	}

	if (flux2dmk.dtsm.accum_sectors) {
		merge_sectors(flux2dmk.dtsm.trk_merged,
			      flux2dmk.dtsm.trk_merged_stats,
			      &flux2dmk.dtsm.trk_working,
			      &flux2dmk.dtsm.trk_working_stats);
	} else {
		*flux2dmk.dtsm.trk_merged = flux2dmk.dtsm.trk_working;
		*flux2dmk.dtsm.trk_merged_stats =
					flux2dmk.dtsm.trk_working_stats;
	}

	gw_post_process_track(&flux2dmk);

	/*
	 * Flippy check.
	 */

	if (track == 0 && side == 1 &&
	    dts.good_sectors == 0 &&
	    flux2dmk.fdec.backward_am >= 9 &&
	    flux2dmk.fdec.backward_am > dts.errcount) {
		msg(MSG_ERRORS, "[possibly a flippy disk] ");
		flux2dmk.fdec.flippy = 1;
	}

	/*
	 * Check for incompatible formats.
	 */

	if (cmd_set->check_compat_sides &&
	    cmd_set->fdd.sides == 2 &&
	    track == 0 &&
	    dts.good_sectors > 0) {
		if (side == 1 &&
		    flux2dmk.fdec.prev_secsize != 512 &&
		    flux2dmk.fdec.secsize == 512) {
			msg(MSG_NORMAL, "[Incompatible formats detected "
			    "between sides; restarting single-sided]\n");
			cmd_set->fdd.sides = 1;
			return -1;
		}
	}

	/*
	 * Guess sides check.
	 */

	if (cmd_set->guess_sides && side == 1) {
		cmd_set->guess_sides = false;

		if (dts.good_sectors == 0) {
			cmd_set->fdd.sides = 1;
			msg(MSG_NORMAL, "[apparently single-sided]\n");
			return -1;
		}
	}

	/*
	 * Guess steps check.
	 */

	if (cmd_set->guess_steps) {
		if (track == 3)
			cmd_set->guess_steps = 0;

		if (cmd_set->fdd.steps == 1) {
			if ((track & 1) && (dts.good_sectors == 0)) {
				msg(MSG_NORMAL,
				    "[double-stepping apparently needed; "
				    "restarting]\n");

				cmd_set->guess_steps = 0;
				cmd_set->fdd.steps = 2;

				if (cmd_set->guess_tracks) {
					cmd_set->fdd.tracks =
						GUESS_TRACKS /
							cmd_set->fdd.steps;
				}

				return -2;
			}
		} else {
			if (dts.good_sectors && track > 0 &&
			    flux2dmk.fdec.cyl_seen == track * 2) {
				msg(MSG_NORMAL,
				    "[single-stepping apparently needed; "
				    "restarting]\n");

				cmd_set->fdd.steps = 1;
				if (cmd_set->guess_tracks) {
					cmd_set->fdd.tracks =
						GUESS_TRACKS /
							cmd_set->fdd.steps;
				}

				return -2;
			}
		}
	}

	/*
	 * Guess tracks check.
	 */

	if (cmd_set->guess_tracks &&
	    (track == 35 || track >= 40) &&
	    (dts.good_sectors == 0 ||
	     (side == 0 &&
	      flux2dmk.fdec.cyl_seen == flux2dmk.fdec.cyl_prev_seen) ||
	      (side == 0 && track >= 80 &&
	       flux2dmk.fdec.cyl_seen == track/2))) {
		msg(MSG_NORMAL, "[apparently only %d tracks; done]\n", track);
		dmkf->header.ntracks = track;
		return 1;
	}

	/*
	 * Check for errors and other problems during the track read.
	 */

	bool failing =
		((dts.errcount > 0) ||
		(retry < cmd_set->min_retries[track][side]) ||
		(dts.good_sectors < cmd_set->min_sectors[track][side])) &&
		(retry < cmd_set->retries[track][side]);

	/* Generally just reporting on the latest read. */
	if (failing) {
		if (cmd_set->min_sectors[track][side] &&
		    (dts.good_sectors !=
		     cmd_set->min_sectors[track][side])) {
			msg(MSG_TSUMMARY, "[%d/%d",
			    dts.good_sectors,
			    cmd_set->min_sectors[track][side]);
		} else {
			msg(MSG_TSUMMARY, "[%d", dts.good_sectors);
		}
		msg(MSG_TSUMMARY, " good, %d error%s]\n",
		    dts.errcount, plu(dts.errcount));
	}

	/*
	 * Handle an interactive user interrupt.
	 */

	if (menu_requested ||
	    (cmd_set->menu_err_enabled &&
	     retry >= cmd_set->retries[track][side])) {

		gw_motor(cmd_set->fdd.gwfd, cmd_set->fdd.drive, 0);

		msg_scrn_flush();
		switch (menu(failing, cmd_set->retries)) {
		case MENU_NOCHANGE:
			break;

		case MENU_QUIT:
			exit_requested = true;
			goto leave;

		case MENU_NORETRY:
			failing = false;
			break;

		case MENU_NEWRETRIES:
			if (retry >= cmd_set->retries[track][side])
				failing = false;
			break;
		default:
			msg_fatal("Bad return value from menu().\n");
			break;
		}

		gw_motor(cmd_set->fdd.gwfd, cmd_set->fdd.drive, 1);

		menu_requested = 0;
	}

	if (failing && ++retry)
		goto retry;

leave:;
	/*
	 * Report stats on current track read.
	 */

	int	min_sector_cnt = cmd_set->min_sectors[track][side];
	int	reused_sectors = flux2dmk.dtsm.trk_merged_stats->reused_sectors;

	if (min_sector_cnt && (dts.good_sectors != min_sector_cnt))
		msg(MSG_TSUMMARY, " %d/%d", dts.good_sectors, min_sector_cnt);
	else
		msg(MSG_TSUMMARY, " %d", dts.good_sectors);

	msg(MSG_TSUMMARY, " good sector%s", plu(dts.good_sectors));

	if (reused_sectors > 0)
		msg(MSG_TSUMMARY, " (%d reused)", reused_sectors);

	msg(MSG_TSUMMARY, ", %d error%s\n", dts.errcount, plu(dts.errcount));
	msg(MSG_IDS, "\n");

	/*
	 * Update disk stats.
	 */

	dds->retries_total      += retry;
	dds->good_sectors_total += dts.good_sectors;
	dds->errcount_total     += dts.errcount;

	for (int i = 0; i < N_ENCS; ++i)
		dds->enc_count_total[i] += dts.enc_count[i];

	if (dts.errcount > 0) {
		dds->err_tracks++;
	} else if (dts.good_sectors) {
		dds->good_tracks++;
	}

	if (flux2dmk.fdec.flippy)
		dds->flippy = true;

	*first_encoding = flux2dmk.fdec.first_encoding;

	return 0;
}


static void
gw2dmk(struct cmd_settings *cmd_set,
       uint32_t sample_freq,
       struct dmk_file *dmkf)
{
restart:
	if (cmd_set->guess_sides ||
	    cmd_set->guess_steps ||
	    cmd_set->guess_tracks) {
		msg(MSG_NORMAL,
		    "Trying %d side%s, %d tracks/side, %s stepping, "
		    "%s encoding\n",
		    cmd_set->fdd.sides, plu(cmd_set->fdd.sides),
		    cmd_set->fdd.tracks,
		    (cmd_set->fdd.steps == 1) ? "single" : "double",
        	    encoding_name(cmd_set->usr_encoding));
	}

	dmk_file_init(dmkf);

	struct dmk_disk_stats dds;
	dmk_disk_stats_init(&dds);

	int tracks = cmd_set->fdd.tracks;
	if (dmkf->header.ntracks < tracks)
		dmkf->header.ntracks = tracks;

	int sides = cmd_set->fdd.sides;
	if (sides == 1)
		dmkf->header.options |= DMK_SSIDE_OPT;

	if (cmd_set->fmtimes == 1)
		dmkf->header.options |= DMK_SDEN_OPT;

	if (cmd_set->usr_encoding == RX02)
		dmkf->header.options |= DMK_RX02_OPT;

	/*
	 * Loop over tracks.
	 */

	reading_floppy = true;

	int first_encoding =
		(cmd_set->usr_encoding == RX02) ? FM : cmd_set->usr_encoding;

	for (int h = 0; h < tracks; ++h) {

		for (int s = 0; s < sides; ++s) {
			int rtv = read_track(cmd_set, sample_freq,
					     dmkf, &dds, h, s,
					     &first_encoding);

			switch (rtv) {
			case -2:
				goto restart;

			case -1:
				sides = cmd_set->fdd.sides;
				dmkf->header.options |= DMK_SSIDE_OPT;
				break;

			case 0:
				break;

			case 1:  /* FALL THRU */
			default:
				goto leave;
			}

			if (exit_requested)
				goto leave;
		}
	}

leave:
	reading_floppy = false;

	msg(MSG_SUMMARY, "\nTotals:\n");

	msg(MSG_SUMMARY,
	    "%d good track%s, %d good sector%s (%d FM + %d MFM + %d RX02)\n",
	    dds.good_tracks, plu(dds.good_tracks),
	    dds.good_sectors_total, plu(dds.good_sectors_total),
	    dds.enc_count_total[FM],
	    dds.enc_count_total[MFM],
	    dds.enc_count_total[RX02]);

	msg(MSG_SUMMARY, "%d bad track%s, %d unrecovered error%s, %d retr%s\n",
	    dds.err_tracks, plu(dds.err_tracks),
	    dds.errcount_total, plu(dds.errcount_total),
	    dds.retries_total, (dds.retries_total == 1) ? "y" : "ies");

	if (dds.flippy) {
		msg(MSG_SUMMARY,
		    "Possibly a flippy disk; check reverse side too\n");
	}
}


/*
 * "histo" must be init'd before calling.
 */

static void
gw_get_histo_analysis(gw_devt gwfd,
		      struct histogram *histo,
		      struct histo_analysis *ha)
{
	int ret = collect_histo_from_track(gwfd, histo);

	if (ret > 0) {
		msg_fatal("%s (%d)%s\n", gw_cmd_ack(ret), ret,
			  ret == ACK_NO_INDEX ?
			  " [Is diskette in drive?]" : "");
	} else if (ret < 0) {
		msg_fatal("Couldn't collect histogram.  Internal error.\n");
	}
	
	histo_analysis_init(ha);
	histo_analyze(histo, ha);
	histo_show(MSG_SAMPLES, histo, ha);
}


/*
 * Detect kind of drive using its media.
 *
 * Returns GW drive set and with vars fdd->kind and fdd->densel set.
 */

static int
gw_detect_drive_kind(struct gw_fddrv *fdd,
		     const struct gw_info *gw_info,
		     struct histo_analysis *ha)
{
	if (fdd->drive == -1)
		msg_fatal("Drive should have previously been set.\n");

	int	densel = fdd->densel;

redo_kind:;
	int	kind   = fdd->kind;

	/* If densel not set, assume it's DS_DD for now.  Redo if
	 * guess is wrong. */
	gw_setdrive(fdd->gwfd, fdd->drive, densel == DS_HD ? DS_HD : DS_DD);

	struct histogram	histo;

	histo_init(0, 0, 1, gw_info->sample_freq, TICKS_PER_BUCKET, &histo);

	gw_get_histo_analysis(fdd->gwfd, &histo, ha);

	// XXX Could we use ha->peaks == 0 here?
	if (histo.data_overflow > 25)	/* 25 is arbitrary */
		msg_fatal("Track 0 side 0 is unformatted.\n");

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
		msg_fatal("Failed to detect media type.\n"
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


static int
gw_detect_sides(struct gw_fddrv *fdd,
		const struct gw_info *gw_info)
{
	if (fdd->drive == -1)
		msg_fatal("Drive should have previously been set.\n");

	struct histogram	histo;

	histo_init(0, 1, 1, gw_info->sample_freq, TICKS_PER_BUCKET, &histo);

	struct histo_analysis	ha;

	gw_get_histo_analysis(fdd->gwfd, &histo, &ha);

	// XXX Could we use ha->peaks == 0 here?
	fdd->sides = (histo.data_overflow > 25) ? 1 : 2;

	return 0;
}


/*
 * Function cleanup() may be called from signal context, so only
 * async-signal-safe functions should be invoked.
 */

void
cleanup(void)
{
	// XXX Need to drain the device and handle out of band
	// commands to workaround commands in progress.  But
	// for now, just reset several times and not check
	// return codes.

	if (cleanup_gwfd != GW_DEVT_INVALID) {
		gw_reset(cleanup_gwfd);
		gw_reset(cleanup_gwfd);
		gw_reset(cleanup_gwfd);
	}
}


void
handler(int sig)
{
	if (sig == SIGINT && cmd_settings.menu_intr_enabled &&
	    reading_floppy && !menu_requested) {
		menu_requested = true;
		return;
	}

	if (reading_floppy && !exit_requested) {
		exit_requested = true;
		return;
	}

#if defined(WIN64) || defined(WIN32)
	signal(sig, SIG_DFL);
#else
	struct sigaction sa_dfl = { .sa_handler = SIG_DFL };
	sigaction(sig, &sa_dfl, NULL);
#endif

	cleanup();
	raise(sig);
}


int
main(int argc, char **argv)
{
#if defined(WIN64) || defined(WIN32)
	const char *slash = strrchr(argv[0], '\\');
#else
	const char *slash = strrchr(argv[0], '/');
#endif
	const char *pgm   = slash ? slash + 1 : argv[0];

	if (!msg_error_prefix(pgm)) {
		msg_fatal("Failure to allocate memory for message prefix.\n");
	}

	if (atexit(cleanup))
		msg_fatal("Can't establish atexit() call.\n");

	parse_args(argc, argv, pgm, &cmd_settings);

	msg(MSG_TSUMMARY, "%s version: %s\n", pgm, version);
	msg(MSG_ERRORS, "Command line:");

	for (int i = 0; i < argc; ++i)
		msg(MSG_ERRORS, " %s", argv[i]);
	msg(MSG_ERRORS, "\n");

	/*
	 * Ensure DMK file doesn't yet exist if force writing not active.
	 */

	if (!cmd_settings.forcewrite &&
	    access(cmd_settings.dmkfile, F_OK) == 0) {
		msg_fatal("DMK file '%s' exists.\n"
		"    (Use the --force option if you want to ignore this "
		"check.\n", cmd_settings.dmkfile);
	}

	/*
	 * Stop spinning the drive when handling fatal signals
	 * XXX Is this even needed for GW with its motor timeout?
	 */

#if defined(WIN64) || defined(WIN32)
	signal(SIGINT, handler);
	signal(SIGTERM, handler);
#else
	int sigs[] = { SIGHUP, SIGINT, SIGQUIT, SIGPIPE, SIGTERM };
	struct sigaction sa_def = { .sa_handler = handler,
				    .sa_flags = SA_RESETHAND };
	struct sigaction sa_int = { .sa_handler = handler };

	for (int s = 0; s < COUNT_OF(sigs); ++s) {
		const struct sigaction *sa_hndlr =
				(sigs[s] == SIGINT) ? &sa_int : &sa_def;	

		if (sigaction(sigs[s], sa_hndlr, 0) == -1)
			msg_fatal("sigaction failed for signal %d.\n", sigs[s]);
	}
#endif

	/*
	 * GW info and media detection initialization.
	 */

	struct gw_info	gw_info;
	const char	*sdev;

	cmd_settings.fdd.gwfd = gw_find_open_gw(cmd_settings.fdd.device,
					cmd_settings.device_list, &sdev);

	cmd_settings.fdd.gwfd = gw_init_gw(&cmd_settings.fdd, &gw_info,
					   cmd_settings.reset_on_init);

	if (cmd_settings.fdd.gwfd == GW_DEVT_INVALID)
		msg_fatal("Failed to find or initialize GW.\n");

	cleanup_gwfd = cmd_settings.fdd.gwfd;

	if (cmd_settings.fdd.drive == -1)
		gw_detect_drive(&cmd_settings.fdd);

	/*
	 * Detect drive kind and characteristics.
	 */

	struct histo_analysis	ha;
	bool			have_ha = false;

	if (cmd_settings.fdd.kind == -1) {
		gw_detect_drive_kind(&cmd_settings.fdd, &gw_info, &ha);
		have_ha = true;
	} else {
		if (cmd_settings.fdd.densel == DS_NOTSET)
			cmd_settings.fdd.densel =
					kind2densel(cmd_settings.fdd.kind);

		gw_setdrive(cmd_settings.fdd.gwfd, cmd_settings.fdd.drive,
			    cmd_settings.fdd.densel);
	}

	struct gw_media_encoding	*gme = &cmd_settings.gme;

	if (cmd_settings.use_histo) {
		if (!have_ha) {
			struct histogram	histo;

			histo_init(0, 0, 1, gw_info.sample_freq,
				   TICKS_PER_BUCKET, &histo);

			gw_get_histo_analysis(cmd_settings.fdd.gwfd, &histo,
					      &ha);
		}

		media_encoding_init_from_histo(gme, &ha,
					       gw_info.sample_freq);

		if (gme->fmthresh == 0)
			msg_fatal("No data collected for histogram.\n");
	} else {
		media_encoding_init(gme, gw_info.sample_freq,
				    (double[]){ 4.0 * 300.0/360.0, 4.0,
				    2.0, 2.0 }[cmd_settings.fdd.kind-1]);
	}

	gme->postcomp = cmd_settings.usr_postcomp;

	msg(MSG_TSUMMARY, "Thresholds");
	if (cmd_settings.use_histo)
		msg(MSG_TSUMMARY, " from histogram");
	msg(MSG_TSUMMARY, ": FM = %d, MFM = {%d,%d}\n",
			  gme->fmthresh,
			  gme->mfmthresh1,
			  gme->mfmthresh2);

	if (cmd_settings.fdd.sides == -1) {
		gw_detect_sides(&cmd_settings.fdd, &gw_info);
		cmd_settings.guess_sides = true;
	}

	if (cmd_settings.fdd.steps == -1) {
		cmd_settings.fdd.steps = (cmd_settings.fdd.kind == 1) ? 2 : 1;
		cmd_settings.guess_steps = true;
	}

	if (cmd_settings.fdd.tracks == -1) {
		cmd_settings.fdd.tracks =
					GW_MAX_TRACKS / cmd_settings.fdd.steps;
		cmd_settings.guess_tracks = true;
	}

	/*
	 * If given, override thresholds.
	 */

	if (cmd_settings.usr_fmthresh != -1) {
		int	old_fmthr = cmd_settings.gme.fmthresh;

		cmd_settings.gme.fmthresh = cmd_settings.usr_fmthresh;

		msg(MSG_TSUMMARY, "Overriding FM threshold: was %d, now %d\n",
		    old_fmthr, cmd_settings.gme.fmthresh);
	}

	if (cmd_settings.usr_mfmthresh1 != -1 ||
	    cmd_settings.usr_mfmthresh2 != -1) {
		int	old_mfmthr1 = cmd_settings.gme.mfmthresh1;
		int	old_mfmthr2 = cmd_settings.gme.mfmthresh2;

		if (cmd_settings.usr_mfmthresh1 != -1) {
			cmd_settings.gme.mfmthresh1 =
				cmd_settings.usr_mfmthresh1;
		}

		if (cmd_settings.usr_mfmthresh2 != -1) {
			cmd_settings.gme.mfmthresh2 =
				cmd_settings.usr_mfmthresh2;
		}

		msg(MSG_TSUMMARY, "Overriding MFM thresholds: were [%d, %d], "
		    "now [%d, %d]\n", old_mfmthr1, old_mfmthr2,
		    cmd_settings.gme.mfmthresh1, cmd_settings.gme.mfmthresh2);
	}

	/*
	 * Read the disk into memory and make a DMK from it.
	 * "dmkf" can't be on the stack because MSW doesn't like it.
	 */

	struct dmk_file *dmkf = malloc(sizeof(struct dmk_file));

	if (!dmkf)
		msg_fatal("Malloc of dmkf failed.\n");

	gw2dmk(&cmd_settings, gw_info.sample_freq, dmkf);

	/*
	 * Optimize the DMK if needed and save it.
	 */

	if (cmd_settings.usr_dmktracklen) {
		dmkf->header.tracklen = cmd_settings.usr_dmktracklen;
	} else {
		dmkf->header.tracklen = 
			(uint16_t[]){ 0, DMKRD_TRACKLEN_5, DMKRD_TRACKLEN_5,
				      DMKRD_TRACKLEN_8, DMKRD_TRACKLEN_3HD
				    }[cmd_settings.fdd.kind];

		if (cmd_settings.dmkopt) {
			uint16_t	old_tracklen = dmkf->header.tracklen;

			dmkf->header.tracklen = dmk_track_length_optimal(dmkf);
		
			msg(MSG_ERRORS, "DMK track length optimized from %d "
					"to %d.\n", old_tracklen,
					dmkf->header.tracklen);
		}
	}

	msg(MSG_NORMAL, "Writing DMK...");
	msg_scrn_flush();

	FILE *dmkfp = fopenwx(cmd_settings.dmkfile, !cmd_settings.forcewrite);

	if (!dmkfp) {
		msg_fatal("Failed to open DMK file '%s'%s: %s (%d)\n",
			  cmd_settings.dmkfile,
			  cmd_settings.forcewrite ? " " : " exclusively",
			  strerror(errno), errno);
	}

	dmk2fp(dmkf, dmkfp);

	if (fclose(dmkfp) == EOF) {
		msg_fatal("Failed to close DMK file '%s': %s (%d)\n",
			  cmd_settings.dmkfile, strerror(errno), errno);
	}

	msg(MSG_NORMAL, "done!\n");

	free(dmkf);

	/*
	 * Finish up and close out.
	 */

	gw_unsetdrive(cmd_settings.fdd.gwfd, cmd_settings.fdd.drive);

	if (gw_close(cmd_settings.fdd.gwfd)) {
		msg_fatal("Failed to close GW device: %s (%d)\n",
			  strerror(errno), errno);
	}

	if (msg_fclose() == EOF) {
		msg_fatal("Failed to close message logging: %s (%d)\n",
			  strerror(errno), errno);
	}

	cleanup_gwfd = GW_DEVT_INVALID;

	return EXIT_SUCCESS;
}
