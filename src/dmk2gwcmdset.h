/*
 * XXX Including this file in any other C file than dmk2gw.c and maybe
 * gwdetect.c probably indicates an abstraction inversion that
 * should be dealt with.
 */

#ifndef DMK2GWCMDSET_H
#define DMK2GWCMDSET_H

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

// XXX Should fdd be pulled out and passed as a separate parameter?

struct cmd_settings {
	const char		**device_list;
	struct gw_fddrv		fdd;
	bool			reset_on_init;
	int			max_sides;
	int			hd;
	int			data_len;
	bool			reverse_sides;
	int			iam_pos;
	int			fill;
	double			rate_adj;
	double			precomp_low;
	double			precomp_high;
	int			ignore;
	bool			dither;
	bool			gwdebug;
	int			test_mode;
	int			scrn_verbosity;
	int			file_verbosity;
	const char		*logfile;
	const char		*devlogfile;
	const char		*dmkfile;
};


#ifdef __cplusplus
}
#endif

#endif
