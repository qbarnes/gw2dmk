#ifndef GWDETECT_H
#define GWDETECT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "gw.h"
#include "gwx.h"
#include "gwmedia.h"
#include "gwfddrv.h"
#include "gwhisto.h"
#include "msg_levels.h"


extern const char *kind2desc(int kind);

extern int kind2densel(int kind);

extern gw_devt gw_openlist(const char **device_list, const char **selected_dev);

extern gw_devt gw_find_open_gw(const char *device, const char **device_list,
			       const char **selected_dev);

extern gw_devt gw_init_gw(struct gw_fddrv *fdd, struct gw_info *gw_info,
			  bool reset);

extern int gw_detect_drive(struct gw_fddrv *fdd);


#ifdef __cplusplus
}
#endif

#endif
