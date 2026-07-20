#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <getopt.h>
#include <ctype.h>
#include <signal.h>

#include "greaseweazle.h"
#include "msg_levels.h"
#include "msg.h"
#include "cmdutil.h"
#include "gw.h"
#include "gwx.h"
#include "gwhisto.h"
#include "dmk2gwcmdset.h"
#include "gwdetect.h"
#include "dmk.h"
#include "dmkx.h"

#if defined(WIN64) || defined(WIN32)
#include <windows.h>
#endif


const char version[] = VERSION;

static const struct option cmd_long_args[] = {
	{ "rateadj",	required_argument, NULL, 'a' },
	{ "drive",	required_argument, NULL, 'd' },
	{ "fill",	required_argument, NULL, 'f' },
	{ "ignore",	required_argument, NULL, 'g' },
	{ "hd",		required_argument, NULL, 'h' },
	{ "ipos",	required_argument, NULL, 'i' },
	{ "kind",	required_argument, NULL, 'k' },
	{ "len",	required_argument, NULL, 'l' },
	{ "steps",	required_argument, NULL, 'm' },
	{ "precomp",	required_argument, NULL, 'p' },
	{ "maxsides",	required_argument, NULL, 's' },
	{ "logfile",	required_argument, NULL, 'u' },
	{ "verbosity",	required_argument, NULL, 'v' },
	{ "testmode",	required_argument, NULL, 'y' },
	{ "device",	required_argument, NULL, 'G' },
	{ "stepdelay",	required_argument, NULL, 'T' },
	{ "gwlogfile",	required_argument, NULL, 'U' },
	/* Start of binary long options without single letter counterparts. */
	{ "gwdebug",	no_argument, NULL, 0 },
	{ "nogwdebug",	no_argument, NULL, 0 },
	{ "reset",	no_argument, NULL, 0 },
	{ "noreset",	no_argument, NULL, 0 },
	{ "reverse",	no_argument, NULL, 0 },
	{ "noreverse",	no_argument, NULL, 0 },
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
	.fdd.gwfd = GW_DEVT_INVALID,
	.fdd.device = NULL,
	.fdd.bus = BUS_IBMPC,
	.fdd.drive = 0,
	.fdd.kind = 0,
	.fdd.tracks = -1,
	.fdd.sides = -1,
	.fdd.steps = -1,
	.fdd.densel = DS_NOTSET,
	.fdd.step_ms = -1,
	.fdd.settle_ms = -1,
	.reset_on_init = true,
	.max_sides = 2,
	.hd = 4,
	.data_len = -1,
	.reverse_sides = false,
	.iam_pos = -1,
	.fill = 0,
	.rate_adj = 1.0,
	.ignore = 0,
	.dither = false,
	.gwdebug = false,
	.test_mode = -1,
	.scrn_verbosity = MSG_NORMAL,
	.file_verbosity = MSG_QUIET,
	.logfile = NULL,
	.devlogfile = NULL,
	.dmkfile = NULL,
};

static volatile gw_devt	cleanup_gwfd   = GW_DEVT_INVALID;
static volatile bool	exit_requested = false;
static volatile bool	writing_floppy = false;


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
  	u("                  1 = Print track and side being written\n");
  	u("                  2 = + dump bytes and encodings\n");
  	u("                  3 = + dump samples\n");
	u("  -k kind         Kind of floppy drive and media\n");
				for (int k = 0; k <= 4; ++k)
					u("                  %d = %s\n",
						k, kind2desc(k));
	u("  -m steps        Step multiplier, 1 or 2\n");
	u("  -s maxsides     Maximum number of sides, 1 or 2 [%d]\n",
				cmd_set->max_sides);
	u("  -T stp[,stl]    Step time");
				if (cmd_set->fdd.step_ms != -1)
					u(" [%u]", cmd_set->fdd.step_ms);
				u(" and head settling time");
				if (cmd_set->fdd.settle_ms != -1)
					u(" [%u]", cmd_set->fdd.settle_ms);
				u(" in ms\n");
	u("  -u logfile      Log output to the given file [%s]\n",
				cmd_set->logfile ? cmd_set->logfile : "none");
	u("  -U gwlogfile    Greaseweazle transaction logfile [%s]\n",
				cmd_set->devlogfile ? cmd_set->devlogfile :
				"none");
	u("  --[no]reset     Reset Greaseweazle upon initialization "
				"[%sreset]\n",
				cmd_set->reset_on_init ? "" : "no");

	u("\nThese values normally need not be changed:\n");
	u("  -p plo[,phi]    Write-precompensation range (ns) [%g,%g]\n",
				cmd_set->precomp_low, cmd_set->precomp_high);
	u("  -h HD           HD: 0=lo, 1=hi, 2=lo/hi, 3=hi/lo, 4=by kind "
				"[%d]\n", cmd_set->hd);
	u("  -l len          Use only first len bytes of track [%d]\n",
				cmd_set->data_len);
	u("  -g ign          Ignore first ign bytes of track [%d]\n",
				cmd_set->ignore);
	u("  -i ipos         Force IAM to ipos from track start; if -1, "
				"don't [%d]\n", cmd_set->iam_pos);
	u("  --[no]reverse   Reverse the sides or not [%sreverse]\n",
				cmd_set->reverse_sides ? "" : "no");
	u("  -f fill         Fill type [0x%x]\n", cmd_set->fill);
	u("                  0x0 = 0xff if in FM, 0x4e if in MFM\n");
	u("                  0x1 = erase only\n");
	u("                  0x2 = write very long transitions\n");
	u("                  0x3 = stop with no fill; leave old data intact\n");
	u("                  0x1nn = nn in FM\n");
	u("                  0x2nn = nn in MFM\n");
	u("  -a rate_adj     Data rate adjustment factor [%.1f]\n",
				cmd_set->rate_adj);
	u("  --[no]dither    Dither data rate [%sdither]\n",
				cmd_set->dither ? "" : "no");
	u("  --[no]gwdebug   Enable additional GW debugging [%sdebug]\n",
				cmd_set->gwdebug ? "" : "no");
	u("  -y testmode     Activate various test modes [%d]\n",
				cmd_set->test_mode);

	u("\n%s version %s\n\n", pgm_name, version);

	exit(EXIT_FAILURE);
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
			"a:d:f:g:h:i:k:l:m:p:s:u:v:y:G:T:U:",
			cmd_long_args, &lindex)) != -1) {

		switch(opt) {
		case 0:;
			const char *name = cmd_long_args[lindex].name;

			if (!strcmp(name, "dither")) {
				cmd_set->dither = true;
			} else if (!strcmp(name, "nodither")) {
				cmd_set->dither = false;
			} else if (!strcmp(name, "gwdebug")) {
				cmd_set->gwdebug = true;
			} else if (!strcmp(name, "nogwdebug")) {
				cmd_set->gwdebug = false;
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
			double rate_adj;
			const int aret = sscanf(optarg, "%lf", &rate_adj);
			if (aret != 1 || rate_adj < 0.0) goto err_usage;
			cmd_set->rate_adj = rate_adj;
			break;

		case 'd':
			if (parse_drive_arg(optarg, opt, &cmd_set->fdd))
				goto err_usage;
			break;

		case 'f':;
			const int fill = strtol_strict(optarg, 0, "'f'");
			if (fill < 0 || (fill > 3 && fill < 0x100) ||
			    fill > 0x2ff) goto err_usage;
			cmd_set->fill = fill;
			break;

		case 'g':;
			const int ign = strtol_strict(optarg, 10, "'g'");
			cmd_set->ignore = ign;
			break;

		case 'h':;
			const int hd = strtol_strict(optarg, 10, "'h'");

			if (hd < 0 || hd > 4) {
				msg_error("Option-argument to '%c' must "
					  "be 0 to 4.\n", opt);
				goto err_usage;
			}
			cmd_set->hd = hd;
			break;

		case 'i':;
			const int ipos = strtol_strict(optarg, 10, "'i'");
			cmd_set->iam_pos = ipos;
			break;

		case 'k':;
			const int kind = strtol_strict(optarg, 10, "'k'");

			if (kind >= 0 && kind <= 4) {
				cmd_set->fdd.kind = kind;
			} else {
				msg_error("Option-argument to '%c' must "
					  "be 0 or 4.\n", opt);
				goto err_usage;
			}
			break;

		case 'l':;
			const int datalen = strtol_strict(optarg, 10, "'l'");
			cmd_set->data_len = datalen;
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
			double precomplo, precomphi;
			const int pret = sscanf(optarg, "%lf, %lf",
				&precomplo, &precomphi);
			if (pret == 1)
				precomphi = precomplo;
			else if (pret != 2)
				goto err_usage;
			cmd_set->precomp_low  = precomplo;
			cmd_set->precomp_high = precomphi;
			break;

		case 's':;
			const int maxsides = strtol_strict(optarg, 10, "'s'");

			if (maxsides < 1 || maxsides > 2) {
				msg_error("Option-argument to '%c' must "
					  "be 1 or 2.\n", opt);
				goto err_usage;
			}
			cmd_set->max_sides = maxsides;
			break;

		case 'u':
			/* Always use malloc for allocation of logfile name
			 * so we always know we can free(). */
			cmd_set->logfile = strdup(optarg);
			if (!cmd_set->logfile)
				msg_fatal("Cannot allocate logfile name.\n");
			break;

		case 'v':;
			const int optav = strtol_strict(optarg, 10, "'v'");

			if (optav < 0 || optav > 99) goto err_usage;
			cmd_set->scrn_verbosity = optav % 10;
			cmd_set->file_verbosity = optav / 10;
			break;

		case 'y':;
			const int test_mode = strtol_strict(optarg, 0, "'y'");

			if (test_mode < 0 || test_mode > 0xff) {
				msg_error("Option-argument to '%c' must "
					  "be 0 to 0xff.\n", opt);
				goto err_usage;
			}
			cmd_set->test_mode = test_mode;
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

		default:  /* '?' */
			goto err_usage;
			break;
		}
	}

	if (optind != (argc-1))
		goto err_usage;

	if (cmd_set->ignore != 0 && cmd_set->iam_pos != -1) {
		u("Options -g -and -i cannot be used together\n");
		goto err_usage;
	}

	cmd_set->dmkfile = argv[optind];

	/* "Promote" verbosity of 3 to MSG_SAMPLES level. */
	if (cmd_set->scrn_verbosity % 10 == MSG_D2GSAMPLES)
		cmd_set->scrn_verbosity += MSG_SAMPLES - MSG_D2GSAMPLES;
	if (cmd_set->scrn_verbosity / 10 == MSG_D2GSAMPLES)
		cmd_set->file_verbosity += (MSG_SAMPLES - MSG_D2GSAMPLES) * 10;

	msg_scrn_set_level(cmd_set->scrn_verbosity);
	msg_file_set_level(cmd_set->file_verbosity);

	if (cmd_set->file_verbosity > MSG_QUIET) {
		if (!cmd_set->logfile) {
			const char *p = strrchr(cmd_set->dmkfile, '.');

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


/*
 * Reset the GW at exit.  Called via atexit() in main context; signal
 * handlers use the async-signal-safe gw_reset_async() instead.
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


#if defined(WIN64) || defined(WIN32)

BOOL WINAPI
handler(DWORD fdwCtrlType)
{
	if (writing_floppy && !exit_requested) {
		exit_requested = true;
		return TRUE;
	}

	if (cleanup_gwfd != GW_DEVT_INVALID)
		gw_reset_async(cleanup_gwfd);
	return FALSE;
}

#else

void
handler(int sig)
{
	if (writing_floppy && !exit_requested) {
		exit_requested = true;
		return;
	}

	struct sigaction sa_dfl = { .sa_handler = SIG_DFL };
	sigaction(sig, &sa_dfl, NULL);

	if (cleanup_gwfd != GW_DEVT_INVALID)
		gw_reset_async(cleanup_gwfd);
	raise(sig);
}

#endif




/*
 * Get RPM.
 *
 * Return -1.0 on error.
 */

static double
get_rpm(gw_devt gwfd, int drive, uint32_t sample_freq)
{
	nsec_type	clock_ns = 1000000000.0 / sample_freq;
	nsec_type	period_ns;

	int cmd_ret = gw_get_period_ns(gwfd, drive, clock_ns, &period_ns);

	if (cmd_ret != ACK_OKAY)
		return -1.0;

	return 60000000000.0 / period_ns;
}


struct t2gw_data {
	uint8_t		*tbuf;
	size_t		tbuf_len;
	size_t		tbuf_cnt;
	uint32_t 	nfa_thresh;
	uint32_t 	nfa_period;
};


static int
encode_t2gw(uint32_t ticks, void *data)
{
	if (ticks == 0)
		return 0;

	struct t2gw_data *t2gwd = (struct t2gw_data *)data;

	if (t2gwd->tbuf_cnt + GWCODE_MAX >= t2gwd->tbuf_len)
		return -1;

	t2gwd->tbuf_cnt += encode_ticks(ticks,
					t2gwd->nfa_thresh,
					t2gwd->nfa_period,
					&t2gwd->tbuf[t2gwd->tbuf_cnt]);

	return 0;
}


static int
write_track(struct cmd_settings *cmd_set,
	    struct dmk_track *dmkt,
	    struct extra_track_info *eti,
	    struct encode_bit *ebs)
{
	msg(MSG_NORMAL, "Track %d, side %d", eti->track, eti->side);
	msg(MSG_BYTES, "\n");
	msg(MSG_NORMAL, "\r");
	msg_scrn_flush();

	encode_bit_init(ebs, ebs->freq, ebs->mult);
	ebs->extra_bytes = eti->extra_bytes;

	uint32_t nfa_thresh = 150e-6 * ebs->freq + 0.5;   /* 150us */
	uint32_t nfa_period = 1.25e-6 * ebs->freq + 0.5;  /* 1.25us */

	/*
	 * Turn DMK track into a train of 8-bit encoded GW tick timings.
	 *
	 * We need to encode it as a single GW stream so
	 * gw_write_stream() can retry if needed.
	 */

	// XXX Just pick a large value for now.
	size_t tbuf_sz = sizeof(uint8_t) * 500000;
	uint8_t *tbuf  = malloc(tbuf_sz);
	if (!tbuf) msg_fatal("Failed to get buffer.\n");
	struct t2gw_data tes = { .tbuf     = tbuf,
				 .tbuf_len = tbuf_sz,
				 .tbuf_cnt = 0,
				 .nfa_thresh = nfa_thresh,
				 .nfa_period = nfa_period };

	struct dmk_encode_s des = {
		.encode_pulse = encode_t2gw,
		.pulse_data   = &tes
	};

	dmk2pulses(dmkt, eti, ebs, &des);

        /*
         * To finish the stream, emit a dummy final flux value and a
         * 0. The dummy is never written to disk because the write
         * is aborted immediately the final flux is loaded into the
         * WDATA timer. The dummy flux is sacrificial, ensuring that
         * the real final flux gets written in full.
	 */

	uint32_t	dummy = 100e-6 * ebs->freq + 0.5;

	tes.tbuf_cnt += encode_ticks(dummy, nfa_thresh, nfa_period,
				     &tes.tbuf[tes.tbuf_cnt]);

	/* Mark end of stream. */
	tes.tbuf[tes.tbuf_cnt++] = 0;

	if (cmd_set->gwdebug) {
		char	fns[25];
		snprintf(fns, sizeof(fns), "gwflux-%02d-%1d.bin",
				eti->track, eti->side);

		FILE *gwfp = fopen(fns, "wb");

		if (!gwfp) {
			msg_error("Failed to create debug file '%s': %s\n",
				  fns, strerror(errno));
		} else if (fwrite(tes.tbuf, tes.tbuf_cnt, 1, gwfp) != 1 ||
			   fclose(gwfp) == EOF) {
			msg_error("Failed to write debug file '%s': %s\n",
				  fns, strerror(errno));
		}
	}

	/*
	 * Write out 8-bit encoded GW timings to physical track.
	 */

	int cmd_ret = gw_seek(cmd_set->fdd.gwfd,
			      eti->track * cmd_set->fdd.steps);

	if (cmd_ret != ACK_OKAY) {
		msg_error("Failed to seek to track %d (%d).\n",
			  eti->track * cmd_set->fdd.steps, cmd_ret);
		free(tes.tbuf);
		return -1;
	}

	cmd_ret = gw_head(cmd_set->fdd.gwfd,
			  eti->side ^ cmd_set->reverse_sides);

	if (cmd_ret != ACK_OKAY) {
		msg_error("Failed to select side %d (%d).\n",
			  eti->side ^ cmd_set->reverse_sides, cmd_ret);
		free(tes.tbuf);
		return -1;
	}

	ssize_t wsret = gw_write_stream(cmd_set->fdd.gwfd, tes.tbuf,
					tes.tbuf_cnt, true, true, 5);

	free(tes.tbuf);

	if (wsret < 0) {
		if (wsret == -ACK_WRPROT)
			msg_fatal("Disk is write-protected.\n");

		msg_error("Failed to write track %d, side %d (status %zd).\n",
			  eti->track, eti->side, wsret);
		return -1;
	}

	return 0;
}


static void
dmk2gw(struct cmd_settings *cmd_set,
       uint32_t sample_freq,
       struct dmk_file *dmkf)
{
	int	tracks   = dmkf->header.ntracks;
	int	sides    = 2 - !!(dmkf->header.options & DMK_SSIDE_OPT);

	/*
	 * Extra bytes after the data CRC, if any.  secsize() accounts for
	 * extra bytes before the data CRC.
	 */

	int	extra_bytes = 0;

	if (dmkf->header.quirks & DMK_QUIRK_EXTRA_CRC) {
		extra_bytes = 6;
	} else if (dmkf->header.quirks & DMK_QUIRK_EXTRA) {
		/* unspecified, use 6 in case really DMK_QUIRK_EXTRA_CRC */
		extra_bytes = 6;
	}

	// XXX Should be dmkf->header.tracklen or dmkt->tracklen?
	struct extra_track_info	eti = {
		.track_len   = (cmd_settings.data_len < 0 ||
				cmd_settings.data_len >
				(dmkf->header.tracklen - DMK_TKHDR_SIZE)) ?
				 dmkf->header.tracklen :
				 DMK_TKHDR_SIZE + cmd_settings.data_len,
		.max_sides   = cmd_set->max_sides,
		.fmtimes     = 2 - !!(dmkf->header.options & DMK_SDEN_OPT),
		.iam_pos     = cmd_set->iam_pos,
		.rx02	     = !!(dmkf->header.options & DMK_RX02_OPT),
		.extra_bytes = extra_bytes,
		.fill	     = cmd_set->fill,
		.quirks	     = dmkf->header.quirks
	};

#if 0
	// XXX Need to finish this.
	mult = (kd->mfmshort / 2.0) * cwclock / rate_adj;
	if (hd == 4) {
		hd = kd->hd;
	}
#endif
	// XXX Need better cases here and documentation here.
	// Maybe use and better flesh out gw_media_encoding struct.
	// Hardcode for now.
	// Maybe used drive's actual RPM to better adjust stream rate?

	double ticks_per_us = sample_freq / 1000000.0;
	double rpm_adj, mult;
	switch (cmd_set->fdd.kind) {
	case 1:
		/* 5.25-inch SD/DD disk in 1.2MB drive */
		/* 360 RPM, MFM data 300 kHz */
		// 2us because I think len == 2 in encode_bit(), so 4us.
		mult = 2 * ticks_per_us;
		rpm_adj = 300.0 / 360.0;
		break;

	case 2:
		/* 5.25-inch SD/DD disk in 360KB/720KB drive, or
		 * 3.5-inch SD/DD disk */
		/* 300 RPM, MFM data 250 kHz */
		mult = 2 * ticks_per_us;
		rpm_adj = 1.0;
		break;

	case 3:
		/* 5.25-inch HD disk or 8-inch SD/DD disk */
		/* 360 RPM, MFM data 500 kHz */
		/* FALLTHRU */
	case 4:
		/* 3.5-inch HD disk */
		/* 300 RPM, MFM data 500 kHz */
		mult = ticks_per_us;
		rpm_adj = 1.0;
		break;

	default:
		msg_fatal("Expected value for kind of drive.\n");
	};

	/* Nominal DMK track length for the drive kind, used as the
	 * target length when filling out short tracks. */
	static const int fill_len_kind[] = { 0,
		DMKRD_TRACKLEN_5, DMKRD_TRACKLEN_5,
		DMKRD_TRACKLEN_8, DMKRD_TRACKLEN_3HD };
	static const int fill_len_kind_sd[] = { 0,
		DMKRD_TRACKLEN_5SD, DMKRD_TRACKLEN_5SD,
		DMKRD_TRACKLEN_8SD, DMKRD_TRACKLEN_3HD };

	eti.fill_len = (eti.fmtimes == 1 ?
			fill_len_kind_sd : fill_len_kind)[cmd_set->fdd.kind];

	struct encode_bit ebs;
	encode_bit_init(&ebs, sample_freq, mult * rpm_adj);

	/*
	 * Loop over tracks.
	 */

	writing_floppy = true;
	gw_motor(cmd_set->fdd.gwfd, cmd_set->fdd.drive, 1);
	// XXX Do we need to ensure proper rotational speed here?

	for (int t = 0; t < tracks; ++t) {
		eti.track   = t;
		eti.precomp = tracks > 1 ?
			((tracks - 1 - t) * cmd_set->precomp_low +
			    t * cmd_set->precomp_high) / (tracks - 1) :
			cmd_set->precomp_low;

		for (int s = 0; s < sides; ++s) {
			struct dmk_track *trkp =  &dmkf->track[t][s];
			eti.side = s;

			/* -h modes 2 and 3 vary density select by side:
			 * 2 = low/high, 3 = high/low. */
			if (cmd_set->hd == 2 || cmd_set->hd == 3) {
				int densel = ((cmd_set->hd == 2) == (s == 1)) ?
						DS_HD : DS_DD;

				if (gw_set_pin(cmd_set->fdd.gwfd, 2,
					       densel) != ACK_OKAY)
					msg_fatal("Failed to set density "
						  "select.\n");
			}

			if (cmd_set->test_mode >= 0 &&
			    cmd_set->test_mode <= 0xff) {
				/* When testing, fill with constant value
				 * instead of actual data. */
				memset(trkp->data, cmd_set->test_mode,
				       eti.track_len - DMK_TKHDR_SIZE);
			}

			if (write_track(cmd_set, trkp, &eti, &ebs) != 0)
				goto leave;

			if (exit_requested)
				goto leave;
		}
	}

leave:
	gw_motor(cmd_set->fdd.gwfd, cmd_set->fdd.drive, 0);
	writing_floppy = false;

	msg(MSG_NORMAL, "\n");
	msg_scrn_flush();
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

	msg(MSG_NORMAL, "%s version: %s\n", pgm, version);
	msg(MSG_NORMAL, "Command line:");

	for (int i = 0; i < argc; ++i)
		msg(MSG_NORMAL, " %s", argv[i]);
	msg(MSG_NORMAL, "\n");

	/*
	 * Stop spinning the drive when handling fatal signals
	 * XXX Is this even needed for GW with its motor timeout?
	 */

#if defined(WIN64) || defined(WIN32)
	if (!SetConsoleCtrlHandler(handler, TRUE))
		msg_fatal("SetConsoleCtrlHandler() failed.\n");
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
	 * Open and read in the DMK now.
	 *
	 * Use use its track length to guess about its kind of drive
	 * and media it'll need.
	 */

	FILE *dmkfp = fopen(cmd_settings.dmkfile, "rb");

	if (!dmkfp) {
		msg_fatal("Failed to open DMK file '%s': %s (%d)\n",
			  cmd_settings.dmkfile,
			  strerror(errno), errno);
	}


	struct dmk_file *dmkf = malloc(sizeof(struct dmk_file));

	if (!dmkf)
		msg_fatal("Malloc of dmkf failed.\n");

	if (fp2dmk(dmkfp, dmkf) != 0) {
		msg_fatal("File '%s' not in expected DMK format.\n",
			  cmd_settings.dmkfile);
	}

	//FILE *ndmkfp = fopen("aa.dmk", "w");
	//dmk2fp(dmkf, ndmkfp);
	//exit(0);

	/*
	 * GW detection and initialization.
	 */

	struct gw_info		gw_info;
	const char		*sdev;

	cmd_settings.fdd.gwfd = gw_find_open_gw(cmd_settings.fdd.device,
					cmd_settings.device_list, &sdev);

	cmd_settings.fdd.gwfd = gw_init_gw(&cmd_settings.fdd, &gw_info,
					   cmd_settings.reset_on_init);

	if (cmd_settings.fdd.gwfd == GW_DEVT_INVALID)
		msg_fatal("Failed to find or initialize Greaseweazle.\n");

	cleanup_gwfd = cmd_settings.fdd.gwfd;

	if (cmd_settings.fdd.drive == -1)
		gw_detect_drive(&cmd_settings.fdd);

	/* Select the drive with DD density for the RPM test below;
	 * the -h density selection is applied once the drive kind
	 * is known. */
	if (gw_setdrive(cmd_settings.fdd.gwfd, cmd_settings.fdd.drive,
			DS_DD) != ACK_OKAY) {
		msg_fatal("Failed to select and start drive.\n");
	}

	// XXX Write protected drive check here?

	/*
	 * Always test the RPM even if we "know" what it is.
	 * That way if a drive without media is selected, it will
	 * fault here instead of hanging on a write later.
	 */

	double rpm = get_rpm(cmd_settings.fdd.gwfd,
			     cmd_settings.fdd.drive,
			     gw_info.sample_freq);

	if (rpm == -1.0)
		msg_fatal("Cannot determine drive RPM.  Incorrect drive "
			  "or media not in drive?\n");

	int	kind = cmd_settings.fdd.kind;

	if (kind == 0) {
		if (rpm < 0.0)
			msg_fatal("Cannot determine drive RPM.\n");

		msg(MSG_NORMAL, "RPM: %.3f\n", rpm);

		int	hdrtrklen = dmkf->header.tracklen;

		// XXX use approx() here for rpm compares?
		if (rpm > 342.0 && rpm < 378.0) {
			/* about 360 RPM */
			if (approx04(hdrtrklen, DMKI_TRACKLEN_5) ||
			    approx04(hdrtrklen, DMKI_TRACKLEN_5SD)) {
				kind = 1;
			} else if (approx04(hdrtrklen, DMKI_TRACKLEN_8) ||
				   approx04(hdrtrklen, DMKI_TRACKLEN_8SD)) {
				kind = 3;
			}
		} else if (rpm > 285.0 && rpm < 315.0) {
			/* about 300 RPM */
			if (approx04(hdrtrklen, DMKI_TRACKLEN_5) ||
			    approx04(hdrtrklen, DMKI_TRACKLEN_5SD)) {
				kind = 2;
			} else if (approx04(hdrtrklen, DMKI_TRACKLEN_3HD)) {
				kind = 4;
			}
		}

		if (kind == 0)
			msg_fatal("Failed to guess drive kind; use -k.\n");

		msg(MSG_NORMAL, "Guessing %s\n", kind2desc(kind));
	}

	cmd_settings.fdd.kind = kind;

	/* Resolve the -h density selection now that the kind is known. */
	switch (cmd_settings.hd) {
	case 0:
		cmd_settings.fdd.densel = DS_DD;
		break;
	case 1:
		cmd_settings.fdd.densel = DS_HD;
		break;
	case 2:
	case 3:
		/* Varies by side; set in dmk2gw(). */
		cmd_settings.fdd.densel = DS_NOTSET;
		break;
	case 4:
		cmd_settings.fdd.densel = kind2densel(kind);
		break;
	}

	if (cmd_settings.fdd.densel != DS_NOTSET &&
	    gw_set_pin(cmd_settings.fdd.gwfd, 2,
		       cmd_settings.fdd.densel) != ACK_OKAY)
		msg_fatal("Failed to set density select.\n");

	if (cmd_settings.fdd.steps == -1)
		cmd_settings.fdd.steps = 1;

	dmk2gw(&cmd_settings, gw_info.sample_freq, dmkf);

	msg(MSG_NORMAL, "Done!\n");

	free(dmkf);

	/*
	 * Finish up and close out.
	 */

	if (gw_unsetdrive(cmd_settings.fdd.gwfd, cmd_settings.fdd.drive) !=
	    ACK_OKAY) {
		msg_error("Failed to stop and deselect drive.\n");
	}

	if (gw_close(cmd_settings.fdd.gwfd)) {
		msg_fatal("Failed to close Greaseweazle device: %s (%d)\n",
			  strerror(errno), errno);
	}

	cleanup_gwfd = GW_DEVT_INVALID;

#if defined(WIN64) || defined(WIN32)
	free((char *)cmd_settings.fdd.device);
#endif

	if (msg_fclose() == EOF) {
		msg_fatal("Failed to close message logging: %s (%d)\n",
			  strerror(errno), errno);
	}

	free((char *)cmd_settings.logfile);

	return EXIT_SUCCESS;
}
