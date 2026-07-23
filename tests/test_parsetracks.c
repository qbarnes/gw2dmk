/*
 * Validate parse_tracks(): the -t/--tracks range-list parser shared by
 * the front-end tools.  A track/side matrix cell holds the value from
 * the last range covering it, or -1 if no range mentions it.
 *
 * Range syntax exercised: bare value (whole disk), "v:track",
 * "v:lo-hi", per-side "v:t/s", open-ended "v:lo-", and comma-joined
 * lists.  Return codes: 0 success, 1 parse error, 3 out-of-range.
 */

#include "parsetracks.h"

#include "test.h"


static void
reset(int m[GW_MAX_TRACKS][2])
{
	memset(m, 0xff, sizeof(int[GW_MAX_TRACKS][2]));	/* every cell -1 */
}


/* A bare value with no track spec covers the entire disk, both sides. */

static void
test_whole_disk(void)
{
	int	m[GW_MAX_TRACKS][2];

	reset(m);
	CHECK_EQ(parse_tracks("7", m), 0);
	CHECK_EQ(m[0][0], 7);
	CHECK_EQ(m[0][1], 7);
	CHECK_EQ(m[GW_MAX_TRACKS - 1][0], 7);
	CHECK_EQ(m[GW_MAX_TRACKS - 1][1], 7);
}


/* "v:lo-hi" covers an inclusive track range, both sides. */

static void
test_track_range(void)
{
	int	m[GW_MAX_TRACKS][2];

	reset(m);
	CHECK_EQ(parse_tracks("3:10-12", m), 0);
	CHECK_EQ(m[9][1], -1);		/* just below the range */
	CHECK_EQ(m[10][0], 3);
	CHECK_EQ(m[11][1], 3);
	CHECK_EQ(m[12][0], 3);
	CHECK_EQ(m[13][0], -1);		/* just above the range */
}


/* Per-side endpoints: "v:t0/s0-t1/s1" walks the linear track*2+side
 * index, so a range can start and end on specific sides. */

static void
test_per_side(void)
{
	int	m[GW_MAX_TRACKS][2];

	reset(m);
	CHECK_EQ(parse_tracks("2:5/1-6/0", m), 0);
	CHECK_EQ(m[5][0], -1);		/* side 0 of track 5 excluded */
	CHECK_EQ(m[5][1], 2);
	CHECK_EQ(m[6][0], 2);
	CHECK_EQ(m[6][1], -1);		/* side 1 of track 6 excluded */
}


/* Open-ended "v:lo-" runs to the last track; comma joins ranges, and a
 * later range overwrites cells set by an earlier one. */

static void
test_open_ended_and_list(void)
{
	int	m[GW_MAX_TRACKS][2];

	reset(m);
	CHECK_EQ(parse_tracks("9:40-,4:0-9", m), 0);
	CHECK_EQ(m[0][0], 4);
	CHECK_EQ(m[9][1], 4);
	CHECK_EQ(m[10][0], -1);		/* gap between the two ranges */
	CHECK_EQ(m[40][0], 9);
	CHECK_EQ(m[GW_MAX_TRACKS - 1][1], 9);
}


/* User-error inputs must be rejected with the documented codes. */

static void
test_errors(void)
{
	int	m[GW_MAX_TRACKS][2];

	reset(m);

	CHECK_EQ(parse_tracks("1:88", m), 3);		/* track >= max */
	CHECK_EQ(parse_tracks("5:3-2", m), 3);		/* hi < lo */
	CHECK_EQ(parse_tracks("x", m), 1);		/* not a number */
	CHECK_EQ(parse_tracks("4:1/2", m), 1);		/* side not 0/1 */
	CHECK_EQ(parse_tracks("4:1/0-2/1x", m), 1);	/* trailing junk */
}


int
main(void)
{
	test_whole_disk();
	test_track_range();
	test_per_side();
	test_open_ended_and_list();
	test_errors();

	return test_exit("test_parsetracks");
}
