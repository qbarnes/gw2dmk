#ifndef DMK_MERGE_H
#define DMK_MERGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

#include "msg.h"
#include "msg_levels.h"
#include "dmk.h"


extern void
merge_sectors(struct dmk_track *trk_merged,
              struct dmk_track_stats *trk_merged_stats,
              struct dmk_track *trk_working,
              struct dmk_track_stats *trk_working_stats);


#ifdef __cplusplus
}
#endif

#endif
