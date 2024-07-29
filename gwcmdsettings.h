#ifndef CMD_SETTINGS_H
#define CMD_SETTINGS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "greaseweazle.h"
#include "msg_levels.h"
#include "msg.h"
#include "gwmedia.h"
#include "dmk.h"

/* Density select */
// Should this go somewhere else?
enum {
	DS_NOTSET = -1,	/* Defaults to DD */
	DS_HD     = 0,
	DS_DD     = 1
};

/*
 * XXX Including this file in any other C file than gw2dmk.c and maybe
 * gwdetect.c probably indicates an abstraction inversion that
 * should be dealt with.
 */

/*
 * Variable settings that come from command line arguments.
 */

// XXX Should gme be pulled out and passed as a separate parameter?

struct cmd_settings {
	const char		**device_list;
	const char		*device;
	gw_devt			gwfd;
	int			bus;
	int			drive;
	int			kind;
	int			tracks;
	bool			guess_tracks;
	int			sides;
	bool			guess_sides;
	int			steps;
	bool			guess_steps;
	bool			check_compat_sides;
	enum dmk_encoding_mode	usr_encoding;
	int			densel;
	bool			reverse_sides;
	bool			hole;
	bool			alternate;
	unsigned int		quirk;
	int			iam_ipos;
	int			fmtimes;
	int			ignore;
	bool			join_sectors;
	volatile bool		menu_intr_enabled;
	bool			menu_err_enabled;
	int			scrn_verbosity;
	int			file_verbosity;
	bool			dmkopt;
	int			usr_dmktracklen;
	const char		*logfile;
	const char		*devlogfile;
	const char		*dmkfile;
	struct gw_media_encoding	gme;
	int			min_sectors[DMK_MAX_TRACKS][2];
	int			retries[DMK_MAX_TRACKS][2];
	int			min_retries[DMK_MAX_TRACKS][2];
};


#ifdef __cplusplus
}
#endif

#endif
