#ifndef GWDETECT_H
#define GWDETECT_H

#ifdef __cplusplus
extern "C" {
#endif

// All needed?
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <math.h>

#include "gw.h"
#include "gwx.h"
#include "gwcmdsettings.h"
#include "gwmedia.h"
#include "gwhisto.h"


extern const char *kind2desc(int kind);

extern gw_devt gw_find_gw(const char **device_list);

extern int gw_detect_drive(gw_devt gwfd, struct cmd_settings *cmd_settings);

extern int gw_detect_drive_kind(gw_devt gwfd,
				const struct gw_info *gw_info,
				struct cmd_settings *cmd_settings);

extern int gw_detect_tracks(gw_devt gwfd, struct cmd_settings *cmd_settings);

extern int gw_detect_sides(gw_devt gwfd,
			   const struct gw_info *gw_info,
			   struct cmd_settings *cmd_settings);

extern int gw_detect_steps(gw_devt gwfd, struct cmd_settings *cmd_settings);

extern gw_devt gw_detect_init_all(struct cmd_settings *cmd_settings,
				  struct gw_info *gw_info);


#ifdef __cplusplus
}
#endif

#endif
