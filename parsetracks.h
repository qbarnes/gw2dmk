#ifndef PARSETRACKS_H
#define PARSETRACKS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "gw.h"


/*
 * Parse a list of track ranges.
 *
 * Returns:
 *   0 - Success
 *   1 - Parse failure (user error)
 *   3 - Track or side parameter out of range (user error)
 */

extern int parse_tracks(const char *ss, int opt_matrix[GW_MAX_TRACKS][2]);


#ifdef __cplusplus
}
#endif

#endif
