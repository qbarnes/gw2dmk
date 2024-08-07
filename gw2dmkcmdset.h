/*
 * XXX Including this file in any other C file than gw2dmk.c and maybe
 * gwdetect.c probably indicates an abstraction inversion that
 * should be dealt with.
 */

#ifndef GW2DMKCMDSET_H
#define GW2DMKCMDSET_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "greaseweazle.h"
#include "msg_levels.h"
#include "msg.h"
#include "gwmedia.h"
#include "dmk.h"
#include "gwfddrv.h"

/*
 * Variable settings that come from command line arguments.
 */

// XXX Should fdd and gme be pulled out and
// passed as a separate parameters?

struct cmd_settings {
	struct gw_fddrv		fdd;
	const char		**device_list;
	const char		*usr_device;
	//gw_devt			gwfd;
	int			usr_bus;
	int			usr_drive;
	int			usr_kind;
	int			usr_tracks;
	int			usr_sides;
	int			usr_steps;
	int			usr_densel;
	int			usr_step_ms;
	int			usr_settle_ms;
	bool			guess_tracks;
	bool			guess_sides;
	bool			guess_steps;
	bool			check_compat_sides;
	bool			forcewrite;
	bool			use_histo;
	enum dmk_encoding_mode	usr_encoding;
	bool			reverse_sides;
	bool			hole;
	bool			alternate;
	unsigned int		quirk;
	int			iam_ipos;
	int			fmtimes;
	int			usr_fmthresh;
	int			usr_mfmthresh1;
	int			usr_mfmthresh2;
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
