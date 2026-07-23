/*
 * Validate the flux decoder by synthesizing flux transitions for
 * complete FM and MFM tracks and running them through the full
 * pulse -> bit -> byte -> DMK track pipeline.
 */

#include "gwdecode.h"
#include "gwmedia.h"

#include "test.h"


/* Not in gwdecode.h, but external for testing. */
extern int mfm_valid_clock(uint64_t accum);


/*
 * 24 MHz sample clock, 4 us FM bit cell (250 kbps MFM, 125 kbps FM).
 * All flux timing is generated in MFM half cells of 2 us = 48 ticks.
 */

#define SAMPLE_FREQ	24000000u
#define HALFCELL_TICKS	48


struct fluxgen {
	struct gw_media_encoding	*gme;
	struct flux2dmk_sm		*f2dsm;
	int				slots_since_pulse;
	uint32_t			total_ticks;
	int				last_mfm_bit;
};


/*
 * Advance time by one half cell; emit a flux transition if bit is 1.
 */

static void
fg_slot(struct fluxgen *fg, int bit)
{
	++fg->slots_since_pulse;
	fg->total_ticks += HALFCELL_TICKS;

	if (bit) {
		gwflux_decode_pulse(fg->slots_since_pulse * HALFCELL_TICKS,
				    fg->gme, fg->f2dsm);
		fg->slots_since_pulse = 0;
	}
}


/*
 * Emit one MFM byte: data bits interleaved with clock bits that are 1
 * only between two 0 data bits.
 */

static void
mfm_byte(struct fluxgen *fg, uint8_t byte)
{
	for (int i = 7; i >= 0; --i) {
		int	d = (byte >> i) & 1;

		fg_slot(fg, !(fg->last_mfm_bit | d));
		fg_slot(fg, d);
		fg->last_mfm_bit = d;
	}
}


/*
 * Emit a raw 16-bit MFM clock/data pattern (for premarks with
 * missing clocks).
 */

static void
mfm_raw16(struct fluxgen *fg, uint16_t pattern)
{
	for (int i = 15; i >= 0; --i)
		fg_slot(fg, (pattern >> i) & 1);

	fg->last_mfm_bit = pattern & 1;
}


static void
mfm_fill(struct fluxgen *fg, uint8_t byte, int count)
{
	while (count--)
		mfm_byte(fg, byte);
}


/*
 * Emit one FM byte with an explicit clock pattern.  Each FM bit
 * window is a clock then a data bit, each two half cells wide.
 */

static void
fm_byte_clocked(struct fluxgen *fg, uint8_t data, uint8_t clock)
{
	for (int i = 7; i >= 0; --i) {
		fg_slot(fg, (clock >> i) & 1);
		fg_slot(fg, 0);
		fg_slot(fg, (data >> i) & 1);
		fg_slot(fg, 0);
	}
}


static void
fm_fill(struct fluxgen *fg, uint8_t byte, int count)
{
	while (count--)
		fm_byte_clocked(fg, byte, 0xff);
}


static uint16_t
crc_buf(uint16_t crc, const uint8_t *buf, size_t len)
{
	while (len--)
		crc = calc_crc1(crc, *buf++);

	return crc;
}


/*
 * Decode state shared by the track tests.
 */

static struct flux2dmk_sm	f2dsm;
static struct dmk_track		trk_merged;
static struct dmk_track_stats	trk_merged_stats;
static struct dmk_disk_stats	dds;
static struct dmk_header	header;
static struct gw_media_encoding	gme;


static struct fluxgen
decode_setup(void)
{
	dmk_disk_stats_init(&dds);
	dmk_track_stats_init(&trk_merged_stats);
	dmk_header_init(&header, 1, DMKRD_TRACKLEN_5);
	memset(&trk_merged, 0, sizeof(trk_merged));

	fdecoder_init(&f2dsm.fdec, SAMPLE_FREQ);
	dmk_track_sm_init(&f2dsm.dtsm, &dds, &header,
			  &trk_merged, &trk_merged_stats);

	media_encoding_init(&gme, SAMPLE_FREQ, 4.0);

	/* Index hole at the start of the track. */
	gwflux_decode_index(0, &f2dsm);

	return (struct fluxgen){ .gme = &gme, .f2dsm = &f2dsm };
}


static void
decode_finish(struct fluxgen *fg)
{
	gwflux_decode_index(fg->total_ticks, &f2dsm);
	gw_decode_flush(&f2dsm);

	*f2dsm.dtsm.trk_merged = f2dsm.dtsm.trk_working;
	*f2dsm.dtsm.trk_merged_stats = f2dsm.dtsm.trk_working_stats;

	gw_post_process_track(&f2dsm);
}


static void
mfm_sector(struct fluxgen *fg, uint8_t cyl, uint8_t side, uint8_t sec,
	   const uint8_t *data, int sizecode)
{
	const uint8_t	id[8] = { 0xa1, 0xa1, 0xa1, 0xfe,
				  cyl, side, sec, sizecode };
	int		datalen = 128 << sizecode;
	uint16_t	crc;

	mfm_fill(fg, 0x00, 12);
	for (int i = 0; i < 3; ++i)
		mfm_raw16(fg, 0x4489);

	for (int i = 3; i < 8; ++i)
		mfm_byte(fg, id[i]);

	crc = crc_buf(0xffff, id, 8);
	mfm_byte(fg, crc >> 8);
	mfm_byte(fg, crc & 0xff);

	mfm_fill(fg, 0x4e, 22);
	mfm_fill(fg, 0x00, 12);

	for (int i = 0; i < 3; ++i)
		mfm_raw16(fg, 0x4489);

	mfm_byte(fg, 0xfb);

	crc = calc_crc1(crc_buf(0xffff, id, 3), 0xfb);
	for (int i = 0; i < datalen; ++i) {
		mfm_byte(fg, data[i]);
		crc = calc_crc1(crc, data[i]);
	}
	mfm_byte(fg, crc >> 8);
	mfm_byte(fg, crc & 0xff);

	mfm_fill(fg, 0x4e, 24);
}


static void
test_mfm_track(void)
{
	static uint8_t	data1[256], data2[256];

	for (int i = 0; i < 256; ++i) {
		data1[i] = i;
		data2[i] = 255 - i;
	}

	struct fluxgen	fg = decode_setup();

	mfm_fill(&fg, 0x4e, 32);
	mfm_sector(&fg, 5, 0, 1, data1, 1);
	mfm_sector(&fg, 5, 0, 2, data2, 1);
	mfm_fill(&fg, 0x4e, 32);

	decode_finish(&fg);

	const struct dmk_track_stats	*ts = &trk_merged_stats;

	CHECK_EQ(ts->good_sectors, 2);
	CHECK_EQ(ts->errcount, 0);
	CHECK_EQ(ts->enc_count[MFM], 2);
	CHECK_EQ(ts->enc_count[FM], 0);
	CHECK_EQ(f2dsm.fdec.first_encoding, MFM);
	CHECK_EQ(f2dsm.fdec.cyl_seen, 5);

	/* Two IDAMs, both flagged double density, no error flags. */
	const struct dmk_track	*trk = &trk_merged;

	CHECK(trk->idam_offset[0] & DMK_DDEN_FLAG);
	CHECK(trk->idam_offset[1] & DMK_DDEN_FLAG);
	CHECK(!(trk->idam_offset[0] & DMK_EXTRA_FLAG));
	CHECK(!(trk->idam_offset[1] & DMK_EXTRA_FLAG));
	CHECK_EQ(trk->idam_offset[2], 0);

	/* The IDAM pointers must land on the 0xfe ID address marks. */
	static const uint8_t	secnum[2] = { 1, 2 };

	for (int s = 0; s < 2; ++s) {
		int	off = trk->idam_offset[s] & DMK_IDAMP_BITS;

		CHECK(off >= DMK_TKHDR_SIZE && off < trk->track_len);
		CHECK_EQ(trk->track[off], 0xfe);
		CHECK_EQ(trk->track[off + 1], 5);	/* cyl */
		CHECK_EQ(trk->track[off + 2], 0);	/* side */
		CHECK_EQ(trk->track[off + 3], secnum[s]);
		CHECK_EQ(trk->track[off + 4], 1);	/* size code */
	}

	/* Sector data bytes follow the DAM in the DMK track. */
	int	off = trk->idam_offset[0] & DMK_IDAMP_BITS;
	const uint8_t	*p = &trk->track[off];
	const uint8_t	*end = trk->track + trk->track_len;

	while (p < end && *p != 0xfb)
		++p;

	CHECK(p < end - 256);
	if (p < end - 256)
		CHECK(memcmp(p + 1, data1, 256) == 0);
}


static void
test_fm_track(void)
{
	static uint8_t	data[128];

	for (int i = 0; i < 128; ++i)
		data[i] = (i * 3) & 0xff;

	struct fluxgen	fg = decode_setup();

	const uint8_t	id[5] = { 0xfe, 2, 0, 3, 0 };
	uint16_t	crc;

	fm_fill(&fg, 0xff, 16);
	fm_fill(&fg, 0x00, 6);

	fm_byte_clocked(&fg, 0xfe, 0xc7);
	for (int i = 1; i < 5; ++i)
		fm_byte_clocked(&fg, id[i], 0xff);

	crc = crc_buf(0xffff, id, 5);
	fm_byte_clocked(&fg, crc >> 8, 0xff);
	fm_byte_clocked(&fg, crc & 0xff, 0xff);

	fm_fill(&fg, 0xff, 11);
	fm_fill(&fg, 0x00, 6);

	fm_byte_clocked(&fg, 0xfb, 0xc7);

	crc = calc_crc1(0xffff, 0xfb);
	for (int i = 0; i < 128; ++i) {
		fm_byte_clocked(&fg, data[i], 0xff);
		crc = calc_crc1(crc, data[i]);
	}
	fm_byte_clocked(&fg, crc >> 8, 0xff);
	fm_byte_clocked(&fg, crc & 0xff, 0xff);

	fm_fill(&fg, 0xff, 16);

	decode_finish(&fg);

	const struct dmk_track_stats	*ts = &trk_merged_stats;

	CHECK_EQ(ts->good_sectors, 1);
	CHECK_EQ(ts->errcount, 0);
	CHECK_EQ(ts->enc_count[FM], 1);
	CHECK_EQ(ts->enc_count[MFM], 0);
	CHECK_EQ(f2dsm.fdec.cyl_seen, 2);

	/* FM sector in a double-density-sized DMK: every byte is
	 * written twice and the IDAM is not flagged DD. */
	const struct dmk_track	*trk = &trk_merged;

	CHECK(!(trk->idam_offset[0] & DMK_DDEN_FLAG));
	CHECK(!(trk->idam_offset[0] & DMK_EXTRA_FLAG));
	CHECK_EQ(trk->idam_offset[1], 0);

	int	off = trk->idam_offset[0] & DMK_IDAMP_BITS;

	CHECK(off >= DMK_TKHDR_SIZE && off < trk->track_len);
	CHECK_EQ(trk->track[off], 0xfe);
	CHECK_EQ(trk->track[off + 1], 0xfe);
	CHECK_EQ(trk->track[off + 2], 2);	/* cyl, doubled */
	CHECK_EQ(trk->track[off + 3], 2);
	CHECK_EQ(trk->track[off + 6], 3);	/* sec, doubled */
	CHECK_EQ(trk->track[off + 7], 3);
}


/*
 * A corrupted data CRC must be counted as an error, not a good
 * sector, and the IDAM flagged for the sector merge logic.
 */

static void
test_mfm_bad_crc(void)
{
	static uint8_t	data[256];

	memset(data, 0xdb, sizeof(data));

	struct fluxgen	fg = decode_setup();

	mfm_fill(&fg, 0x4e, 32);

	/* Emit a sector but corrupt the data CRC. */
	const uint8_t	id[8] = { 0xa1, 0xa1, 0xa1, 0xfe, 1, 0, 1, 1 };
	uint16_t	crc;

	mfm_fill(&fg, 0x00, 12);
	for (int i = 0; i < 3; ++i)
		mfm_raw16(&fg, 0x4489);
	for (int i = 3; i < 8; ++i)
		mfm_byte(&fg, id[i]);
	crc = crc_buf(0xffff, id, 8);
	mfm_byte(&fg, crc >> 8);
	mfm_byte(&fg, crc & 0xff);

	mfm_fill(&fg, 0x4e, 22);
	mfm_fill(&fg, 0x00, 12);
	for (int i = 0; i < 3; ++i)
		mfm_raw16(&fg, 0x4489);
	mfm_byte(&fg, 0xfb);
	crc = calc_crc1(crc_buf(0xffff, id, 3), 0xfb);
	for (int i = 0; i < 256; ++i) {
		mfm_byte(&fg, data[i]);
		crc = calc_crc1(crc, data[i]);
	}
	mfm_byte(&fg, (crc >> 8) ^ 0xff);	/* corrupted */
	mfm_byte(&fg, crc & 0xff);

	mfm_fill(&fg, 0x4e, 32);

	decode_finish(&fg);

	CHECK_EQ(trk_merged_stats.good_sectors, 0);
	CHECK_EQ(trk_merged_stats.errcount, 1);
	CHECK(trk_merged.idam_offset[0] & DMK_EXTRA_FLAG);
}


static void
test_encoding_name(void)
{
	CHECK(strcmp(encoding_name(MIXED), "autodetect") == 0);
	CHECK(strcmp(encoding_name(FM), "FM") == 0);
	CHECK(strcmp(encoding_name(MFM), "MFM") == 0);
	CHECK(strcmp(encoding_name(RX02), "RX02") == 0);
	CHECK(strcmp(encoding_name(N_ENCS), "unknown") == 0);
	CHECK(strcmp(encoding_name(-1), "unknown") == 0);
}


static void
test_mfm_clock(void)
{
	/* 0x00 data byte with proper clocks. */
	CHECK(mfm_valid_clock(0xaaaa));

	/* 0xff data byte with proper (absent) clocks. */
	CHECK(mfm_valid_clock(0x5555));

	/* All ones: clock and data bits can't both be set. */
	CHECK(!mfm_valid_clock(0xffff));

	/* 0xa1 premark: its missing clock violates the clock rules. */
	CHECK(!mfm_valid_clock(0x4489));
}


int
main(void)
{
	test_encoding_name();
	test_mfm_clock();
	test_mfm_track();
	test_fm_track();
	test_mfm_bad_crc();

	return test_exit("test_gwdecode");
}
