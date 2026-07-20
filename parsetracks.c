/*
 * Parse a list of track-range option values of the form:
 *
 *   value[:track[/side][-[track2[/side2]]]][,...]
 *
 * applying "value" to the given [track][side] ranges of an option
 * matrix.  With no track given, "value" applies to all tracks and
 * sides.  With no end of range given after a "-", the range extends
 * to the end (last track, or side 1).
 */

#include <ctype.h>
#include <limits.h>
#include <stdlib.h>

#include "parsetracks.h"


static int
parse_num(const char **pp, int *val)
{
	if (!isdigit((unsigned char)**pp))
		return -1;

	char *ep;
	long  v = strtol(*pp, &ep, 10);

	if (v > INT_MAX)
		v = INT_MAX;

	*pp  = ep;
	*val = v;

	return 0;
}


static int
parse_side(const char **pp, int *val)
{
	if (**pp != '0' && **pp != '1')
		return -1;

	*val = *(*pp)++ - '0';

	return 0;
}


int
parse_tracks(const char *ss, int opt_matrix[GW_MAX_TRACKS][2])
{
	while (*ss != '\0') {
		int range_value    = -1;
		int range_track[2] = { -1, -1 };
		int range_side[2]  = { -1, -1 };
		int range_hyphen   = 0;

		if (parse_num(&ss, &range_value))
			return 1;

		if (*ss == ':') {
			++ss;

			if (parse_num(&ss, &range_track[0]))
				return 1;

			if (*ss == '/') {
				++ss;

				if (parse_side(&ss, &range_side[0]))
					return 1;
			}

			if (*ss == '-') {
				++ss;
				range_hyphen = 1;

				if (isdigit((unsigned char)*ss)) {
					parse_num(&ss, &range_track[1]);

					if (*ss == '/') {
						++ss;

						if (parse_side(&ss,
							      &range_side[1]))
							return 1;
					}
				}
			}
		}

		if (*ss == ',')
			++ss;
		else if (*ss != '\0')
			return 1;

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

		/* Bounds check track and side values from user. */

		if (range_track[0] < 0 || range_track[0] >= GW_MAX_TRACKS ||
		    range_track[1] < 0 || range_track[1] >= GW_MAX_TRACKS ||
		    range_side[0]  < 0 || range_side[0]  > 1 ||
		    range_side[1]  < 0 || range_side[1]  > 1)
			return 3;

		/* Ensure tracks and sides are in order. */

		if (range_track[1] < range_track[0] ||
		    (range_track[1] == range_track[0] &&
		     range_side[1] < range_side[0]))
			return 3;

		/* Set the opt_matrix for the range given. */

		for (int tm = range_track[0] * 2 + range_side[0];
		     tm <= range_track[1] * 2 + range_side[1]; ++tm) {
			opt_matrix[tm / 2][tm % 2] = range_value;
		}
	}

	return 0;
}
