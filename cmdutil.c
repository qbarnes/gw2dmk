/*
 * Command-line parsing and usage-output helpers shared by the
 * gw2dmk, dmk2gw, and gwhist front ends.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "cmdutil.h"


void
fatal_bad_number(const char *name)
{
	msg_fatal("%s requires a numeric argument.\n", name);
}


/*
 * Like strtol, but exit with a fatal error message if there are any
 * invalid characters or the string is empty.
 */

long int
strtol_strict(const char *nptr, int base, const char *name)
{
	char *endptr;

	long int res = strtol(nptr, &endptr, base);
	if (*nptr == '\0' || *endptr != '\0')
		fatal_bad_number(name);

	return res;
}


int
vu(const char *fmt, va_list ap)
{
	return vfprintf(stderr, fmt, ap);
}


int
u(const char *fmt, ...)
{
	va_list	args;

	va_start(args, fmt);
	int ret = vu(fmt, args);
	va_end(args);

	return ret;
}


/*
 * Parse a -d drive argument {0,1,2,a,b}, setting the bus type and
 * drive unit.
 */

int
parse_drive_arg(const char *arg, int opt, struct gw_fddrv *fdd)
{
	const int loarg = (arg[0] && arg[1]) ? -1 : tolower(arg[0]);

	switch (loarg) {
	case '0':
	case '1':
	case '2':
		fdd->bus   = BUS_SHUGART;
		fdd->drive = loarg - '0';
		break;

	case 'a':
	case 'b':
		fdd->bus   = BUS_IBMPC;
		fdd->drive = loarg - 'a';
		break;

	default:
		msg_error("Option-argument to '%c' must "
			  "be 0, 1, 2, a, or b.\n", opt);
		return -1;
	}

	return 0;
}


/*
 * Parse a -G device argument.
 */

int
parse_device_arg(const char *arg, struct gw_fddrv *fdd)
{
#if defined(WIN32) || defined(WIN64)
	/* If the user specified a 2 digit COM device
	 * without the '\\\\.\\' prefix, prefix their string
	 * with it.  Always malloc the string on MSW so we
	 * always know we can free() if it needed on this OS.
	 */

	char *ds;
	if (strncasecmp("COM", arg, 3) == 0 &&
	    isdigit(arg[3]) &&
	    isdigit(arg[4]) &&
	    !arg[5]) {
		ds = malloc(4 + 5 + 1);
		if (ds) {
			strcpy(ds, "\\\\.\\");
			strcpy(ds + 4, arg);
		}
	} else {
		ds = strdup(arg);
	}
	if (!ds)
		msg_fatal("Cannot allocate device name.\n");
	fdd->device = ds;
#else
	fdd->device = arg;
#endif

	return 0;
}


/*
 * Parse a -T step[,settle] delay argument in ms.
 */

int
parse_stepdelay_arg(const char *arg, struct gw_fddrv *fdd)
{
	unsigned int step_ms, settle_ms;
	int sfn = sscanf(arg, "%u,%u", &step_ms, &settle_ms);

	switch (sfn) {
	case 2:
		if (settle_ms > 65000)
			return -1;
		fdd->settle_ms = settle_ms;
		/* FALLTHRU */
	case 1:
		if (step_ms > 65)
			return -1;
		fdd->step_ms = step_ms;
		break;
	default:
		return -1;
	}

	return 0;
}
