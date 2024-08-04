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
#include "gwcmdsettings.h"
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
	{
		"alternate",
		required_argument,
		NULL,
		'a'
	},
	{
		"drive",
		required_argument,
		NULL,
		'd'
	},
	{
		"encoding",
		required_argument,
		NULL,
		'e'
	},
	{
		"fmthresh",
		required_argument,
		NULL,
		'f'
	},
	{
		"ignore",
		required_argument,
		NULL,
		'g'
	},
	{
		"ipos",
		required_argument,
		NULL,
		'i'
	},
	{
		"kind",
		required_argument,
		NULL,
		'k'
	},
	{
		"dmktracklen",
		required_argument,
		NULL,
		'l'
	},
	{
		"steps",
		required_argument,
		NULL,
		'm'
	},
	{
		"reverse",
		required_argument,
		NULL,
		'r'
	},
	{
		"sides",
		required_argument,
		NULL,
		's'
	},
	{	"tracks",
		required_argument,
		NULL,
		't'
	},
	{
		"logfile",
		required_argument,
		NULL,
		'u'
	},
	{
		"verbosity",
		required_argument,
		NULL,
		'v'
	},
	{
		"fmtimes",
		required_argument,
		NULL,
		'w'
	},
	{
		"tracks",
		required_argument,
		NULL,
		'x'
	},
	{
		"device",
		required_argument,
		NULL,
		'G'
	},
	{
		"menu",
		required_argument,
		NULL,
		'M'
	},
	{
		"stepdelay",
		required_argument,
		NULL,
		'T'
	},
	{
		"gwlogfile",
		required_argument,
		NULL,
		'U'
	},
	{
		"mfmthresh1",
		required_argument,
		NULL,
		'1'
	},
	{
		"mfmthresh2",
		required_argument,
		NULL,
		'2'
	},
	/* Start of long options only that are booleans. */
	{
		"hd",
		no_argument,
		NULL,
		0
	},
	{
		"dd",
		no_argument,
		NULL,
		0
	},
	{
		"hole",
		no_argument,
		NULL,
		0
	},
	{
		"nohole",
		no_argument,
		NULL,
		0
	},
	{
		"join",
		no_argument,
		NULL,
		0
	},
	{
		"nojoin",
		no_argument,
		NULL,
		0
	},
	{
		"compat",
		no_argument,
		NULL,
		0
	},
	{
		"nocompat",
		no_argument,
		NULL,
		0
	},
	{
		"dmkopt",
		no_argument,
		NULL,
		0
	},
	{
		"nodmkopt",
		no_argument,
		NULL,
		0
	},
	{
		"usehisto",
		no_argument,
		NULL,
		0
	},
	{
		"nousehisto",
		no_argument,
		NULL,
		0
	},
	{
		"force",
		no_argument,
		NULL,
		0
	},
	{
		"noforce",
		no_argument,
		NULL,
		0
	},
	{ 0, 0, 0, 0 }
};

struct cmd_settings cmd_settings = {
	.device_list = (const char *[]){
#if linux
		"/dev/greaseweazle",
		"/dev/ttyACM0",
#elif defined(WIN64) || defined(WIN32)
		"\\\\.\\COM3",
#endif
		NULL }, 
	.device = NULL,
	.gwfd = GW_DEVT_INVALID,
	.bus = BUS_IBMPC,
	.drive = 0,
	.kind = -1,
	.tracks = -1,
	.guess_tracks = false,
	.sides = -1,
	.guess_sides = false,
	.steps = -1,
	.guess_steps = false,
	.step_ms = -1,
	.settle_ms = -1,
	.check_compat_sides = true,
	.forcewrite = false,
	.use_histo = true,
	.usr_encoding = MIXED,
	.densel = DS_NOTSET,
	.reverse_sides = false,
	.hole = true,
	.alternate = 0,
	.iam_ipos = -1,
	.fmtimes = 2,
	.usr_fmthresh = -1,
	.usr_mfmthresh1 = -1,
	.usr_mfmthresh2 = -1,
	.ignore = 0,
	.join_sectors = true,
	.menu_intr_enabled = false,
	.menu_err_enabled = false,
	.scrn_verbosity = MSG_TSUMMARY,
	.file_verbosity = MSG_QUIET,
	.usr_dmktracklen = 0,
	.logfile = NULL,
	.devlogfile = NULL,
	.dmkfile = NULL,
	.gme.rpm = 0.0,
	.gme.data_clock = 0.0,
	.gme.pulse_rate = 0.0,
	.gme.fmthresh = 0,
	.gme.mfmthresh1 = 0,
	.gme.mfmthresh2 = 0,
	.gme.mfmshort = 0.0,
	.gme.thresh_adj = 0.0,
	.min_sectors = {[0 ... DMK_MAX_TRACKS-1] = {[0 ... 1] = 0}},
	.retries     = {[0 ... DMK_MAX_TRACKS-1] = {[0 ... 1] = 4}},
	.min_retries = {[0 ... DMK_MAX_TRACKS-1] = {[0 ... 1] = 0}}
};

static volatile bool menu_requested = false;
static volatile bool exit_requested = false;
static volatile bool reading_floppy = false;


static void
fatal_bad_number(const char *name)
{
	msg_fatal(EXIT_FAILURE, "%s requires a numeric argument.\n", name);
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
usage(const char *pgm_name, struct cmd_settings *cmd_set)
{
	fprintf(stderr, "Usage: %s [options] file.dmk\n\n"
		" Options [settings in brackets]:\n", pgm_name);

	fprintf(stderr, "  -G device       Greaseweazle device [%s]\n",
			cmd_set->device ? cmd_set->device :
			cmd_set->device_list[0]);
	fprintf(stderr, "  -d drive        Drive unit {a,b,0,1,2} [%c]\n",
			cmd_set->bus == BUS_SHUGART ?
			cmd_set->drive + '0' : cmd_set->drive + 'a');
	fprintf(stderr, "  -v verbosity    Amount of output [%d]\n",
			(cmd_set->file_verbosity * 10) +
			cmd_set->scrn_verbosity);
  	fprintf(stderr, "                  0 = No output\n");
  	fprintf(stderr, "                  1 = Summary of disk\n");
  	fprintf(stderr, "                  2 = + summary of each track\n");
  	fprintf(stderr, "                  3 = + individual errors\n");
  	fprintf(stderr, "                  4 = + track IDs and DAMs\n");
  	fprintf(stderr, "                  5 = + hex data and event flags\n");
  	fprintf(stderr, "                  6 = like 4, but with raw data "
			"too\n");
  	fprintf(stderr, "                  7 = like 5, but with Greaseweazle "
			"samples too\n");
  	fprintf(stderr, "                  21 = level 2 to logfile, 1 to "
			"screen, etc.\n");
	fprintf(stderr, "  -u logfile      Log output to the given file [%s]"
			"\n", cmd_set->logfile ? cmd_set->logfile : "none");
	fprintf(stderr, "  -U gwlogfile    Greaseweazle transaction logfile "
			"[%s]\n", cmd_set->devlogfile ? cmd_set->devlogfile :
			"none");
	//fprintf(stderr, "  -R logfile      Replay logfile\n");
	fprintf(stderr, "  -M {i,e,d}      Menu control [d]\n");
	fprintf(stderr, "                  i = Interrupt (^C) invokes "
			"menu\n");
	fprintf(stderr, "                  e = Errors equals retries invokes "
			"menu\n");
	fprintf(stderr, "                  d = Disables invoking menu\n");
	fprintf(stderr, "  --[no]hole      Use or not index hole for track "
			"start [%shole]\n", cmd_set->hole ? "" : "no");
	fprintf(stderr, "  --[no]join      Join or not sectors between retries "
			"[%sjoin]\n", cmd_set->join_sectors ? "" : "no");
	fprintf(stderr, "  --[no]compat    Compare or not sides for "
			"incompatible formats [%scompat]\n",
				cmd_set->check_compat_sides ? "" : "no");
	fprintf(stderr, "  --[no]dmkopt    Optimize or not DMK track length "
			"[%sdmkopt]\n", cmd_set->dmkopt ? "" : "no");
	fprintf(stderr, "  --[no]usehisto  Use histogram or not for "
			"autotuning of thresholds [%susehisto]\n",
			cmd_set->use_histo ? "" : "no");
	fprintf(stderr, "  --[no]force     Force or not to overwrite "
			"existing DMK output file [%sforce]\n",
			cmd_set->forcewrite ? "" : "no");

	fprintf(stderr, "\n Options to manually set values that are normally "
			"autodetected:\n");
	fprintf(stderr, "  -k kind         Kind of floppy drive and media\n");
	for (int k = 1; k <= 4; ++k)
		fprintf(stderr, "                  %d = %s\n", k, kind2desc(k));
	fprintf(stderr, "  -m steps        Step multiplier, 1 or 2\n");
	fprintf(stderr, "  -t tracks       Number of tracks per side\n");
	fprintf(stderr, "  -s sides        Number of sides, 1 or 2\n");
	fprintf(stderr, "  -e encoding     0 = mixed, 1 = FM (SD), 2 = MFM "
			"(DD or HD), 3 = RX02 [%d]\n", cmd_set->usr_encoding);
	fprintf(stderr, "  --dd|--hd       Density Select, pin 2\n");
	fprintf(stderr, "  -x max_retry    Max retries on errors [%d]\n",
			cmd_set->retries[0][0]);
	fprintf(stderr, "  -X min_retry    Min retries even if no errors "
			"[%d]\n", cmd_set->min_retries[0][0]);
	fprintf(stderr, "  -S min_sector   Min sector count [%d]\n",
			cmd_set->min_sectors[0][0]);
	// XXX -z
	fprintf(stderr, "  -r reverse      0 = normal, 1 = reverse sides "
			"[%d]\n", cmd_set->reverse_sides);
	fprintf(stderr, "  -q quirk        Bitmap of support for some format "
			"quirks [0x%02x]\n", cmd_set->quirk);
	fprintf(stderr, "                  0x01 = ID CRCs omit a1 a1 a1 "
			"premark\n");
	fprintf(stderr, "                  0x02 = Data CRCs omit a1 a1 a1 "
			"premark\n");
	fprintf(stderr, "                  0x04 = Third a1 a1 a1 premark "
			"isn't missing clock\n");
	fprintf(stderr, "                  0x08 = Extra bytes (data only) "
			"after data CRC\n");
	fprintf(stderr, "                  0x10 = Extra bytes (4 data + 2 "
			"CRC) after data CRC\n");
	fprintf(stderr, "                  0x20 = 4 extra data bytes before "
			"data CRC\n");
	fprintf(stderr, "                  0x40 = FM IAM can have either "
			"0xd7 or 0xc7 clock\n");
	fprintf(stderr, "                  0x80 = Extra MFM clocks may be "
			"present\n");
	fprintf(stderr, "  -w fmtimes      Write FM bytes 1 or 2 times "
			"[%d]\n", cmd_set->fmtimes);
	fprintf(stderr, "  -l bytes        DMK track length\n");

	fprintf(stderr, "\n Rarely used fine-tuning options:\n");
	fprintf(stderr, "  -f threshold    FM threshold for short vs. long\n");
	fprintf(stderr, "  -1 threshold    MFM threshold for short vs. "
			"medium\n");
	fprintf(stderr, "  -2 threshold    MFM threshold for medium vs. "
			"long\n");
	fprintf(stderr, "  -T stp[,stl]    Step time");
	if (cmd_set->step_ms != -1)
		fprintf(stderr, " [%u]", cmd_set->step_ms);
	fprintf(stderr, " and head settling time");
	if (cmd_set->settle_ms != -1)
		fprintf(stderr, " [%u]", cmd_set->settle_ms);
	fprintf(stderr, " in ms\n");
	fprintf(stderr, "  -g ign          Ignore first ign bytes of track "
			"[%d]\n", cmd_set->ignore);
	fprintf(stderr, "  -i ipos         Force IAM to ipos from track "
			"start; if -1, don't [%d]\n", cmd_set->iam_ipos);
	fprintf(stderr, "  -a alternate    Alternate even/odd tracks on "
			"retries with -m2 [%d]\n", cmd_set->alternate);
	fprintf(stderr, "                  0 = always even\n");
	fprintf(stderr, "                  1 = always odd\n");
	fprintf(stderr, "                  2 = even, then odd\n");
	fprintf(stderr, "                  3 = odd, then even\n");

	fprintf(stderr, "\n%s version %s\n\n", pgm_name, version);

	exit(EXIT_FAILURE);
}


enum menu_action {
	MENU_NOCHANGE,
	MENU_QUIT,
	MENU_NORETRY,
	MENU_NEWRETRIES
	};


enum menu_action
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
		case 'c':
			return MENU_NOCHANGE;

		case 'q':
			return MENU_QUIT;

		case 'g':
			return MENU_NORETRY;

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
 * Returns:
 *   0 - Success
 *   1 - regcomp failure (internal error)
 *   2 - Parse list failure (internal error)
 *   3 - Track or side parameter out of range (user error)
 */

int
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
			"a:d:e:f:g:i:k:l:m:q:r:s:t:u:v:w:x:G:M:S:T:U:X:1:2:",
			cmd_long_args, &lindex)) != -1) {

		switch(opt) {
		case 0:;
			const char *name = cmd_long_args[lindex].name;

			if (!strcmp(name, "hd")) {
				cmd_set->densel = DS_HD;
			} else if (!strcmp(name, "dd")) {
				cmd_set->densel = DS_DD;
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
			if (optarg[1]) goto d_err;

			const int loarg = tolower(optarg[0]);

			switch(loarg) {
			case '0':
			case '1':
			case '2':
				cmd_set->bus = BUS_SHUGART;
				cmd_set->drive = loarg - '0';
				break;
			case 'a':
			case 'b':
				cmd_set->bus = BUS_IBMPC;
				cmd_set->drive = loarg - 'a';
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
			const int ign = strtol_strict(optarg, 10, "'i'");
			cmd_set->ignore = ign;
			break;

		case 'i':;
			const int ipos = strtol_strict(optarg, 10, "'i'");
			cmd_set->iam_ipos = ipos;
			break;

		case 'k':;
			const int kind = strtol_strict(optarg, 10, "'k'");

			if (kind >= 1 && kind <= 4) {
				cmd_set->kind = kind;
			} else {
				msg_error("Option-argument to '%c' must "
					  "be 1 or 4.\n", opt);
				goto err_usage;
			}
			break;

		case 'l':;
			const int dmktracklen =
					strtol_strict(optarg, 10, "'l'");
			if (dmktracklen < 0 || dmktracklen > 0x4000)
				goto err_usage;
			cmd_set->usr_dmktracklen = dmktracklen;
			break;

		case 'm':;
			const int steps = strtol_strict(optarg, 10, "'m'");

			if (steps >= 1 && steps <= 2) {
				cmd_set->steps = steps;
			} else {
				msg_error("Option-argument to '%c' must "
					  "be 1 or 2.\n", opt);
				goto err_usage;
			}
			break;

		case 'q':;
			const int quirk = strtol_strict(optarg, 0, "'q'");
			if (quirk & ~QUIRK_ALL) goto err_usage;
			if (((quirk & QUIRK_EXTRA) != 0) +
			    ((quirk & QUIRK_EXTRA_CRC) != 0) +
			    ((quirk & QUIRK_EXTRA_DATA) != 0) > 1) {
				msg_fatal(1, "Quirks %#x, %#x, and %#x "
				"cannot be used together.\n",
				QUIRK_EXTRA, QUIRK_EXTRA_CRC, QUIRK_EXTRA_DATA);
			}
				cmd_set->quirk = quirk;
			break;

		case 'r':;
			const int reverse = strtol_strict(optarg, 0, "'r'");
			if (reverse >= 0 && reverse <= 1) {
				cmd_set->reverse_sides = reverse;
			} else {
				msg_error("Option-argument to '%c' must "
					  "be 0 or 1.\n", opt);
				goto err_usage;
			}
			break;

		case 's':;
			const int sides = strtol_strict(optarg, 10, "'s'");

			if (sides >= 1 && sides <= 2) {
				cmd_set->sides = sides;
			} else {
				msg_error("Option-argument to '%c' must "
					  "be 1 or 2.\n", opt);
				goto err_usage;
			}
			break;

		case 't':;
			const int tracks = strtol_strict(optarg, 10, "'t'");

			if (tracks >= 0 && tracks <= GW_MAX_TRACKS) {
				cmd_set->tracks = tracks;
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

				if (optav >= 100) {
					goto err_usage;
				} else if (optav >= 10) {
					cmd_set->scrn_verbosity = optav % 10;
					cmd_set->file_verbosity = optav / 10;
				} else {
					cmd_set->scrn_verbosity = optav;
					cmd_set->file_verbosity = optav;
				}
			}

			break;

		case 'w':;
			const int fmtm = strtol_strict(optarg, 10, "'w'");
			if (fmtm < 1 || fmtm > 2) goto err_usage;
			cmd_set->fmtimes = fmtm;
			break;

		case 'x':
			if (parse_tracks(optarg, cmd_set->retries))
				goto err_usage;

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
				msg_fatal(EXIT_FAILURE,
					  "Cannot allocate device name.\n");
			strcpy(ds, optarg);
			cmd_set->device = ds;
#else
			cmd_set->device = optarg;
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
				cmd_set->settle_ms = settle_ms;
				/* FALLTHRU */
			case 1:
				if (step_ms > 65) goto err_usage;
				cmd_set->step_ms = step_ms;
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
				msg_fatal(EXIT_FAILURE,
					  "Cannot allocate log file name.\n");

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


struct imark_data {
	uint32_t	total_ticks;	// Only count ticks between index holes
	unsigned int	revs_seen;
	uint32_t	index[2];
};


static int
imark_fn(uint32_t imark, void *data)
{
	struct imark_data *idata = (struct imark_data *)data;

	idata->index[0] = idata->index[1];
	idata->index[1] = imark;

	if  (idata->index[0] != ~0) {
		idata->total_ticks += idata->index[1] - idata->index[0];
		++idata->revs_seen;
	}

	return 0;
}


struct pulse_data {
	struct gw_media_encoding	*gme;
	struct flux2dmk_sm		*flux2dmk;
	struct imark_data		imd;
};


static int
pulse_fn(uint32_t pulse, void *data)
{
	struct pulse_data	*pdata = (struct pulse_data *)data;

	uint8_t **track_hole_pp = &pdata->flux2dmk->dtsm.track_hole_p;

	/* Only note the first index hole encountered. */
	if ((*track_hole_pp == NULL) && (pdata->imd.index[1] != ~0))
		*track_hole_pp = pdata->flux2dmk->dtsm.track_data_p;

	// XXX For now, block decoding stream until hole seen.
	// Change when we can abort the stream in progress without waiting
	// for a full rotation and move on.
	if (!pdata->flux2dmk->fdec.use_hole || *track_hole_pp)
		gwflux_decode(pulse, pdata->gme, pdata->flux2dmk);

	return pdata->flux2dmk->dtsm.dmk_full ? 1 : 0;
}


static void
dmk_file_init(struct dmk_file *dmkf, struct cmd_settings *cmds)
{
	dmk_header_init(&dmkf->header, 0, DMK_TRACKLEN_MAX);

	memset(dmkf->track, 0, sizeof(struct dmk_track));
}


static FILE *
dmkfile2fp(const char *dmkfile, bool fail_if_exists)
{
	int oflags = O_RDWR | O_CREAT | O_TRUNC | (fail_if_exists ? O_EXCL : 0);

	int fd = open(dmkfile, oflags, 0644);

	if (fd == -1)
		return NULL;

	return fdopen(fd, "w");
}


/*
 * Read the track and side.
 *
 *    -2	Attempt restart.
 *    -1	Side count changed to 1 (cmd_set->sides=1).
 *     0	Everything good.
 *     1+	Error.
 */

// XXX Too many args.  Rethink.
static int
read_track(gw_devt gwfd,
	   struct cmd_settings *cmd_set,
	   uint32_t sample_freq,
	   struct dmk_file *dmkf,
	   struct dmk_disk_stats *dds,
	   int track,
	   int side)
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

	int headpos = track * cmd_set->steps;

	if (cmd_set->steps == 2) {
		headpos += cmd_set->alternate & 1;

		if ((retry > 0) && (cmd_set->alternate & 2))
			headpos ^= 1;
	}

	gw_seek(gwfd, headpos);
	// error checking

	gw_head(gwfd, side ^ cmd_set->reverse_sides);
	// error checking


	fdecoder_init(&flux2dmk.fdec, sample_freq, cmd_set->usr_encoding);

	flux2dmk.fdec.use_hole      = cmd_set->hole;
	flux2dmk.fdec.quirk         = cmd_set->quirk;
	flux2dmk.fdec.reverse_sides = cmd_set->reverse_sides;
	flux2dmk.fdec.awaiting_iam  = (cmd_set->iam_ipos >= 0) ? true : false;

	dmk_track_sm_init(&flux2dmk.dtsm,
			  dds,
			  &dmkf->header,
			  &dmkf->track[track][side],
			  &dts);

	flux2dmk.dtsm.dmk_iam_pos    = cmd_set->iam_ipos;
	flux2dmk.dtsm.dmk_ignore     = cmd_set->ignore;
	flux2dmk.dtsm.accum_sectors  = cmd_set->join_sectors;

	uint8_t *fbuf = 0;
	ssize_t bytes_read = gw_read_stream(gwfd, 1, 0, &fbuf);

	if (bytes_read == -1) {
		msg(MSG_ERRORS, "gw_read_stream() failure\n");
		free(fbuf);
		return 1;
	}

	struct pulse_data pdata = { &cmd_set->gme, &flux2dmk,
				    { 0, 0, { ~0, ~0 } } };
	struct gw_decode_stream_s gwds = { 0, -1,
					   imark_fn, &pdata.imd,
					   pulse_fn, &pdata };

	ssize_t dsv = gw_decode_stream(fbuf, bytes_read, &gwds);

	if (dsv == -1) {
		msg(MSG_ERRORS, "Decode error from stream\n");
		free(fbuf);
		return 2;
	} else if (dsv < bytes_read) {
		// XXX Just not handling this at present, but warn.
		msg(MSG_ERRORS, "Leftover bytes in stream! "
				"(%d out of %d bytes unparsed, status %d)\n",
				(int)(bytes_read - dsv), (int)bytes_read,
				gwds.status);
	}

	free(fbuf);

	gw_decode_flush(&flux2dmk);

	if (flux2dmk.fdec.use_hole && flux2dmk.dtsm.track_hole_p) {
		dmk_data_rotate(&flux2dmk.dtsm.trk_working,
				flux2dmk.dtsm.track_hole_p);
	}

	merge_sectors(flux2dmk.dtsm.trk_merged,
		      flux2dmk.dtsm.trk_merged_stats,
		      &flux2dmk.dtsm.trk_working,
		      &flux2dmk.dtsm.trk_working_stats);

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
	    cmd_set->sides == 2 &&
	    track == 0 &&
	    dts.good_sectors > 0) {
		if (side == 1 &&
		    flux2dmk.fdec.prev_secsize != 512 &&
		    flux2dmk.fdec.secsize == 512) {
			msg(MSG_NORMAL, "[Incompatible formats detected "
			    "between sides; restarting single-sided]\n");
			cmd_set->sides = 1;
			return -1;
		}
	}

	/*
	 * Guess sides check.
	 */

	if (cmd_set->guess_sides && side == 1) {
		cmd_set->guess_sides = false;

		if (dts.good_sectors == 0) {
			cmd_set->sides = 1;
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

		if (cmd_set->steps == 1) {
			if ((track & 1) && (dts.good_sectors == 0)) {
				msg(MSG_NORMAL,
				    "[double-stepping apparently needed; "
				    "restarting]\n");

				cmd_set->guess_steps = 0;
				cmd_set->steps = 2;

				if (cmd_set->guess_tracks) {
					cmd_set->tracks =
						GUESS_TRACKS /
							cmd_set->steps;
				}

				return -2;
			}
		} else {
			if (dts.good_sectors && track > 0 &&
			    flux2dmk.fdec.cyl_seen == track * 2) {
				msg(MSG_NORMAL,
				    "[single-stepping apparently needed; "
				    "restarting]\n");

				cmd_set->steps = 1;
				if (cmd_set->guess_tracks) {
					cmd_set->tracks =
						GUESS_TRACKS /
							cmd_set->steps;
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
		return 0;
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

		gw_motor(cmd_set->gwfd, cmd_set->drive, 0);

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
			msg_fatal(1, "Bad return value from menu().\n");
			break;
		}

		gw_motor(cmd_set->gwfd, cmd_set->drive, 1);

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

	return 0;
}


static void
gw2dmk(gw_devt gwfd,
       struct cmd_settings *cmd_set,
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
		    cmd_set->sides, plu(cmd_set->sides),
		    cmd_set->tracks,
		    (cmd_set->steps == 1) ? "single" : "double",
        	    encoding_name(cmd_set->usr_encoding));
	}

	dmk_file_init(dmkf, cmd_set);

	struct dmk_disk_stats dds;
	dmk_disk_stats_init(&dds);

	int tracks = cmd_set->tracks;
	if (dmkf->header.ntracks < tracks)
		dmkf->header.ntracks = tracks;

	int sides = cmd_set->sides;
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

	for (int h = 0; h < tracks; ++h) {

		for (int s = 0; s < sides; ++s) {
			int rtv = read_track(gwfd, cmd_set, sample_freq,
					     dmkf, &dds, h, s);

			switch (rtv) {
			case -2: goto restart;
			case -1:
				sides = cmd_set->sides;
				dmkf->header.options |= DMK_SSIDE_OPT;
				break;

			case 0:  break;
			default: goto leave;
			}

			if (exit_requested)
				goto leave;
		}
	}

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

	return;

leave:
	reading_floppy = false;
}


/*
 * Function cleanup() may be called from signal context, so only
 * async-signal-safe functions should be invoked.
 */

void
cleanup(void)
{
	// XXX This doesn't work.  Need to specifically handle out of
	// band commands to workaround commands in progress.
	// But may not be strictly be needed any more anyway since
	// drive times out and powers down on its own.
	//if (cmd_settings.gwfd != GW_DEVT_INVALID)
	//	gw_reset(cmd_settings.gwfd);
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
		msg_fatal(EXIT_FAILURE,
			  "Failure to allocate memory for message prefix.\n");
	}

	if (atexit(cleanup))
		msg_fatal(EXIT_FAILURE, "Can't establish atexit() call.\n");

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
		msg_fatal(EXIT_FAILURE, "DMK file '%s' exists.\n"
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
			msg_fatal(EXIT_FAILURE, "sigaction failed for "
				  "signal %d.\n", sigs[s]);
	}
#endif

	/*
	 * GW info and media detection initialization.
	 */

	struct gw_info	gw_info;

	cmd_settings.gwfd = gw_detect_init_all(&cmd_settings, &gw_info);

	if (cmd_settings.gwfd == GW_DEVT_INVALID)
		msg_fatal(EXIT_FAILURE, "Failed to find or initialize GW.\n");

	/*
	 * Read the disk into memory and make a DMK from it.
	 * "dmkf" can't be on the stack because MSW doesn't like it.
	 */

	struct dmk_file *dmkf = malloc(sizeof(struct dmk_file));

	if (!dmkf)
		msg_fatal(EXIT_FAILURE, "Malloc of dmkf failed.\n");

	gw2dmk(cmd_settings.gwfd, &cmd_settings, gw_info.sample_freq, dmkf);

	/*
	 * Optimize the DMK if needed and save it.
	 */

	if (cmd_settings.usr_dmktracklen) {
		dmkf->header.tracklen = cmd_settings.usr_dmktracklen;
	} else {
		dmkf->header.tracklen = 
			(uint16_t[]){ 0, DMK_TRACKLEN_5, DMK_TRACKLEN_5,
				      DMK_TRACKLEN_8, DMK_TRACKLEN_3HD
				    }[cmd_settings.kind];

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

	FILE *dmkfp = dmkfile2fp(cmd_settings.dmkfile,
				 !cmd_settings.forcewrite);

	if (!dmkfp) {
		msg_fatal(EXIT_FAILURE, "Failed to open DMK file '%s'%s: "
			  "%s (%d)\n",
			  cmd_settings.dmkfile,
			  cmd_settings.forcewrite ? " " : " exclusively",
			  strerror(errno), errno);
	}

	dmk2fp(dmkf, dmkfp);

	if (fclose(dmkfp) == EOF) {
		msg_fatal(EXIT_FAILURE, "Failed to close DMK file '%s': "
			  "%s (%d)\n",
			  cmd_settings.dmkfile, strerror(errno), errno);
	}

	msg(MSG_NORMAL, "done!\n");

	free(dmkf);

	/*
	 * Finish up and close out.
	 */

	gw_unsetdrive(cmd_settings.gwfd, cmd_settings.drive);

	if (gw_close(cmd_settings.gwfd)) {
		msg_fatal(EXIT_FAILURE, "Failed to close GW device: %s (%d)\n",
			  strerror(errno), errno);
	}

	if (msg_fclose() == EOF) {
		msg_fatal(EXIT_FAILURE, "Failed to close message logging: "
			  "%s (%d)\n", strerror(errno), errno);
	}

	return EXIT_SUCCESS;
}
