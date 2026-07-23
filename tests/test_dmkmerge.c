/*
 * Validate merge_sectors(): replacing bad sectors in the current read
 * with good copies from previous reads of the same track.
 *
 * Synthetic track layout (MFM-style, no byte doubling):
 *   16 byte preamble, then two 64-byte sectors.  Sector numbers live
 *   at IDAM offset + 3.
 */

#include "dmkmerge.h"

#include "test.h"


#define PRE_LEN		16
#define SEC_LEN		64
#define SEC1_OFF	(DMK_TKHDR_SIZE + PRE_LEN)
#define SEC2_OFF	(SEC1_OFF + SEC_LEN)
#define TRK_LEN		(SEC2_OFF + SEC_LEN)


static void
build_track(struct dmk_track *trk, struct dmk_track_stats *stats,
	    int sec1_bad, int sec2_bad, uint8_t fill)
{
	memset(trk, 0, sizeof(*trk));
	dmk_track_stats_init(stats);

	trk->track_len = TRK_LEN;

	trk->idam_offset[0] = SEC1_OFF | DMK_DDEN_FLAG |
				(sec1_bad ? DMK_EXTRA_FLAG : 0);
	trk->idam_offset[1] = SEC2_OFF | DMK_DDEN_FLAG |
				(sec2_bad ? DMK_EXTRA_FLAG : 0);

	memset(trk->data, 0x4e, PRE_LEN);

	for (int s = 0; s < 2; ++s) {
		uint8_t	*sec = trk->track + SEC1_OFF + s * SEC_LEN;

		memset(sec, fill, SEC_LEN);
		sec[0] = 0xfe;
		sec[1] = 0;		/* cyl */
		sec[2] = 0;		/* side */
		sec[3] = s + 1;		/* sector number */
		sec[4] = 1;		/* size code */
	}

	stats->good_sectors = 2 - sec1_bad - sec2_bad;
	stats->errcount = sec1_bad + sec2_bad;
}


/* A clean read replaces any previous merge result outright. */

static void
test_clean_read(void)
{
	static struct dmk_track		working, merged;
	struct dmk_track_stats		wstats, mstats;

	build_track(&working, &wstats, 0, 0, 0xaa);
	build_track(&merged, &mstats, 1, 1, 0xbb);
	mstats.reused_sectors = 1;

	merge_sectors(&merged, &mstats, &working, &wstats);

	CHECK_EQ(mstats.errcount, 0);
	CHECK_EQ(mstats.good_sectors, 2);
	CHECK_EQ(mstats.reused_sectors, 0);
	CHECK(memcmp(merged.track, working.track, TRK_LEN) == 0);
}


/* A bad sector in the current read is repaired from the previous
 * merged track when the latter has a good copy. */

static void
test_repair(void)
{
	static struct dmk_track		working, merged;
	struct dmk_track_stats		wstats, mstats;

	/* Previous read: sector 1 bad, sector 2 good. */
	build_track(&merged, &mstats, 1, 0, 0xbb);

	/* Current read: sector 1 good, sector 2 bad. */
	build_track(&working, &wstats, 0, 1, 0xaa);

	merge_sectors(&merged, &mstats, &working, &wstats);

	CHECK_EQ(mstats.errcount, 0);
	CHECK_EQ(mstats.reused_sectors, 1);

	/* Preamble and sector 1 from the current read. */
	CHECK(memcmp(merged.track + DMK_TKHDR_SIZE,
		     working.track + DMK_TKHDR_SIZE, PRE_LEN) == 0);
	CHECK(memcmp(merged.track + SEC1_OFF,
		     working.track + SEC1_OFF, SEC_LEN) == 0);
	CHECK_EQ(merged.track[SEC1_OFF + 5], 0xaa);

	/* Sector 2 replaced with the previous read's good copy. */
	CHECK_EQ(merged.track[SEC2_OFF], 0xfe);
	CHECK_EQ(merged.track[SEC2_OFF + 3], 2);
	CHECK_EQ(merged.track[SEC2_OFF + 5], 0xbb);

	/* IDAM offsets: same layout, no error flags surviving. */
	CHECK_EQ(merged.idam_offset[0], SEC1_OFF | DMK_DDEN_FLAG);
	CHECK_EQ(merged.idam_offset[1], SEC2_OFF | DMK_DDEN_FLAG);
	CHECK_EQ(merged.idam_offset[2], 0);
}


/* When the previous merge result is at least as good with fewer
 * repairs, it is kept over a worse current read. */

static void
test_keep_previous(void)
{
	static struct dmk_track		working, merged;
	struct dmk_track_stats		wstats, mstats;

	/* Previous read: perfect. */
	build_track(&merged, &mstats, 0, 0, 0xbb);

	/* Current read: both sectors bad. */
	build_track(&working, &wstats, 1, 1, 0xaa);

	merge_sectors(&merged, &mstats, &working, &wstats);

	CHECK_EQ(mstats.errcount, 0);
	CHECK_EQ(mstats.reused_sectors, 0);

	/* Previous track contents untouched. */
	CHECK_EQ(merged.track[SEC1_OFF + 5], 0xbb);
	CHECK_EQ(merged.track[SEC2_OFF + 5], 0xbb);
	CHECK_EQ(merged.idam_offset[0], SEC1_OFF | DMK_DDEN_FLAG);
	CHECK_EQ(merged.idam_offset[1], SEC2_OFF | DMK_DDEN_FLAG);
}


/* With no usable replacements, the current read is kept as-is. */

static void
test_no_replacement(void)
{
	static struct dmk_track		working, merged;
	struct dmk_track_stats		wstats, mstats;

	/* Previous read: both sectors bad, too. */
	build_track(&merged, &mstats, 1, 1, 0xbb);

	/* Current read: sector 2 bad. */
	build_track(&working, &wstats, 0, 1, 0xaa);

	merge_sectors(&merged, &mstats, &working, &wstats);

	CHECK_EQ(mstats.errcount, 1);
	CHECK_EQ(mstats.reused_sectors, 0);
	CHECK_EQ(merged.track[SEC1_OFF + 5], 0xaa);
	CHECK_EQ(merged.track[SEC2_OFF + 5], 0xaa);
	CHECK(merged.idam_offset[1] & DMK_EXTRA_FLAG);
}


int
main(void)
{
	test_clean_read();
	test_repair();
	test_keep_previous();
	test_no_replacement();

	return test_exit("test_dmkmerge");
}
