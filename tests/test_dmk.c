/*
 * Validate the DMK file format layer: header and track file I/O,
 * on-disk byte layout, track rotation, and whole-file round trips.
 */

#include "dmk.h"

#include "test.h"


static struct dmk_file	dmkf;
static struct dmk_file	dmkf2;


static void
test_header_init(void)
{
	struct dmk_header	h;

	memset(&h, 0xa5, sizeof(h));
	dmk_header_init(&h, 40, DMKRD_TRACKLEN_5);

	CHECK_EQ(h.writeprot, 0);
	CHECK_EQ(h.ntracks, 40);
	CHECK_EQ(h.tracklen, DMKRD_TRACKLEN_5);
	CHECK_EQ(h.options, 0);
	CHECK_EQ(h.real_format, 0);
}


/*
 * The DMK header must be written in its defined on-disk layout with
 * little-endian multibyte fields, independent of host endianness.
 */

static void
test_header_layout(void)
{
	struct dmk_header	h;
	FILE			*fp = tmpfile();

	CHECK(fp != NULL);
	if (!fp)
		return;

	dmk_header_init(&h, 40, 0x1980);
	h.options = DMK_SDEN_OPT | DMK_SSIDE_OPT;
	h.quirks  = DMK_QUIRK_PREMARK | DMK_QUIRK_ID_CRC;

	CHECK(dmk_header_fwrite(&h, fp));

	uint8_t	raw[DMK_HDR_SIZE];

	fseek(fp, 0, SEEK_SET);
	CHECK_EQ(fread(raw, 1, sizeof(raw), fp), DMK_HDR_SIZE);

	CHECK_EQ(raw[DMK_WRITEPROT], 0x00);
	CHECK_EQ(raw[DMK_NTRACKS], 40);
	CHECK_EQ(raw[DMK_TRACKLEN], 0x80);	/* LE low byte */
	CHECK_EQ(raw[DMK_TRACKLEN + 1], 0x19);	/* LE high byte */
	CHECK_EQ(raw[DMK_OPTIONS], DMK_SDEN_OPT | DMK_SSIDE_OPT);
	CHECK_EQ(raw[5], DMK_QUIRK_PREMARK | DMK_QUIRK_ID_CRC);

	for (int i = 6; i < DMK_HDR_SIZE; ++i)
		CHECK_EQ(raw[i], 0);

	/* Read back and compare.  dmk_header_fread() does not seek; the
	 * caller must position the stream at the header first. */
	struct dmk_header	h2;

	memset(&h2, 0, sizeof(h2));
	fseek(fp, 0, SEEK_SET);
	CHECK(dmk_header_fread(&h2, fp));

	CHECK_EQ(h2.writeprot, h.writeprot);
	CHECK_EQ(h2.ntracks, h.ntracks);
	CHECK_EQ(h2.tracklen, h.tracklen);
	CHECK_EQ(h2.options, h.options);
	CHECK_EQ(h2.quirks, h.quirks);
	CHECK_EQ(h2.real_format, h.real_format);

	fclose(fp);
}


static void
test_track_file_offset(void)
{
	struct dmk_header	h;

	dmk_header_init(&h, 40, 0x1900);

	/* Double-sided: 2 tracks of data per cylinder. */
	CHECK_EQ(dmk_track_file_offset(&h, 0, 0), DMK_HDR_SIZE);
	CHECK_EQ(dmk_track_file_offset(&h, 0, 1), DMK_HDR_SIZE + 0x1900);
	CHECK_EQ(dmk_track_file_offset(&h, 1, 0), DMK_HDR_SIZE + 2 * 0x1900);
	CHECK_EQ(dmk_track_file_offset(&h, 5, 1),
		 DMK_HDR_SIZE + 11 * 0x1900);

	/* Single-sided: 1 track of data per cylinder. */
	h.options |= DMK_SSIDE_OPT;
	CHECK_EQ(dmk_track_file_offset(&h, 0, 0), DMK_HDR_SIZE);
	CHECK_EQ(dmk_track_file_offset(&h, 1, 0), DMK_HDR_SIZE + 0x1900);
	CHECK_EQ(dmk_track_file_offset(&h, 5, 0), DMK_HDR_SIZE + 5 * 0x1900);
}


static void
test_track_roundtrip(void)
{
	struct dmk_header	h;
	static struct dmk_track	trk, trk2;
	FILE			*fp = tmpfile();

	CHECK(fp != NULL);
	if (!fp)
		return;

	dmk_header_init(&h, 2, DMKI_TRACKLEN_5SD);

	memset(&trk, 0, sizeof(trk));
	trk.track_len = h.tracklen;
	trk.idam_offset[0] = (DMK_TKHDR_SIZE + 10) | DMK_DDEN_FLAG;
	trk.idam_offset[1] = (DMK_TKHDR_SIZE + 500) | DMK_EXTRA_FLAG;

	for (int i = 0; i < h.tracklen - DMK_TKHDR_SIZE; ++i)
		trk.data[i] = (i * 7) & 0xff;

	CHECK_EQ(dmk_track_fseek(&h, 1, 0, fp), 0);
	CHECK(dmk_track_fwrite(&h, &trk, fp));

	memset(&trk2, 0, sizeof(trk2));
	CHECK_EQ(dmk_track_fseek(&h, 1, 0, fp), 0);
	CHECK(dmk_track_fread(&h, &trk2, fp));

	/* DMK_EXTRA_FLAG is internal state and must not hit the file. */
	CHECK_EQ(trk2.idam_offset[0], (DMK_TKHDR_SIZE + 10) | DMK_DDEN_FLAG);
	CHECK_EQ(trk2.idam_offset[1], DMK_TKHDR_SIZE + 500);
	CHECK_EQ(trk2.idam_offset[2], 0);

	CHECK(memcmp(trk.data, trk2.data,
		     h.tracklen - DMK_TKHDR_SIZE) == 0);

	fclose(fp);
}


static void
test_data_rotate(void)
{
	static struct dmk_track	trk;

	memset(&trk, 0, sizeof(trk));
	trk.track_len = DMK_TKHDR_SIZE + 100;
	trk.idam_offset[0] = (DMK_TKHDR_SIZE + 10) | DMK_DDEN_FLAG;
	trk.idam_offset[1] = DMK_TKHDR_SIZE + 50;

	for (int i = 0; i < 100; ++i)
		trk.data[i] = i;

	/* NULL hole pointer: no change. */
	dmk_data_rotate(&trk, NULL);
	CHECK_EQ(trk.data[0], 0);

	/* Hole at the start of data: no change. */
	dmk_data_rotate(&trk, trk.data);
	CHECK_EQ(trk.data[0], 0);
	CHECK_EQ(trk.idam_offset[0], (DMK_TKHDR_SIZE + 10) | DMK_DDEN_FLAG);

	/* Hole out of range: no change. */
	dmk_data_rotate(&trk, trk.data + 100);
	CHECK_EQ(trk.data[0], 0);

	/* Rotate so the hole (offset 30) becomes the start of data. */
	dmk_data_rotate(&trk, trk.data + 30);

	CHECK_EQ(trk.track_len, DMK_TKHDR_SIZE + 100);
	CHECK_EQ(trk.data[0], 30);
	CHECK_EQ(trk.data[69], 99);
	CHECK_EQ(trk.data[70], 0);
	CHECK_EQ(trk.data[99], 29);

	/* IDAM at +50 rotates to the front at +20; IDAM at +10 wraps
	 * around to +80 keeping its density flag. */
	CHECK_EQ(trk.idam_offset[0], DMK_TKHDR_SIZE + 20);
	CHECK_EQ(trk.idam_offset[1], (DMK_TKHDR_SIZE + 80) | DMK_DDEN_FLAG);
	CHECK_EQ(trk.idam_offset[2], 0);

	/* All IDAMs before the hole: all wrap by the tail size. */
	memset(&trk, 0, sizeof(trk));
	trk.track_len = DMK_TKHDR_SIZE + 100;
	trk.idam_offset[0] = DMK_TKHDR_SIZE + 5;

	for (int i = 0; i < 100; ++i)
		trk.data[i] = i;

	dmk_data_rotate(&trk, trk.data + 30);
	CHECK_EQ(trk.idam_offset[0], DMK_TKHDR_SIZE + 75);
}


static void
test_track_length_optimal(void)
{
	static struct dmk_file	f;

	memset(&f, 0, sizeof(f));
	dmk_header_init(&f.header, 2, DMKRD_TRACKLEN_MAX);

	f.track[0][0].track_len = 100;
	f.track[0][1].track_len = 200;
	f.track[1][0].track_len = 150;
	f.track[1][1].track_len = 50;

	CHECK_EQ(dmk_track_length_optimal(&f), 200);

	/* Single-sided: side 1 lengths are ignored. */
	f.header.options |= DMK_SSIDE_OPT;
	CHECK_EQ(dmk_track_length_optimal(&f), 150);

	/* Tracks beyond ntracks are ignored. */
	f.header.options = 0;
	f.header.ntracks = 1;
	CHECK_EQ(dmk_track_length_optimal(&f), 200);
}


static void
test_file_roundtrip(void)
{
	FILE	*fp = tmpfile();

	CHECK(fp != NULL);
	if (!fp)
		return;

	memset(&dmkf, 0, sizeof(dmkf));
	dmk_header_init(&dmkf.header, 3, DMKI_TRACKLEN_5SD);

	int	datalen = dmkf.header.tracklen - DMK_TKHDR_SIZE;

	for (int t = 0; t < dmkf.header.ntracks; ++t) {
		for (int s = 0; s < DMK_SIDES; ++s) {
			struct dmk_track	*trk = &dmkf.track[t][s];

			trk->track_len = dmkf.header.tracklen;
			trk->idam_offset[0] = DMK_TKHDR_SIZE | DMK_DDEN_FLAG;

			for (int i = 0; i < datalen; ++i)
				trk->data[i] = (t * 31 + s * 17 + i) & 0xff;
		}
	}

	CHECK_EQ(dmk2fp(&dmkf, fp), 0);

	/* File size must be header plus ntracks * sides tracks. */
	fseek(fp, 0, SEEK_END);
	CHECK_EQ(ftell(fp), DMK_HDR_SIZE +
		 3 * DMK_SIDES * dmkf.header.tracklen);

	memset(&dmkf2, 0, sizeof(dmkf2));
	CHECK_EQ(fp2dmk(fp, &dmkf2), 0);

	CHECK_EQ(dmkf2.header.ntracks, dmkf.header.ntracks);
	CHECK_EQ(dmkf2.header.tracklen, dmkf.header.tracklen);
	CHECK_EQ(dmkf2.header.options, dmkf.header.options);

	for (int t = 0; t < dmkf.header.ntracks; ++t) {
		for (int s = 0; s < DMK_SIDES; ++s) {
			CHECK_EQ(dmkf2.track[t][s].idam_offset[0],
				 DMK_TKHDR_SIZE | DMK_DDEN_FLAG);
			CHECK(memcmp(dmkf2.track[t][s].data,
				     dmkf.track[t][s].data, datalen) == 0);
		}
	}

	fclose(fp);
}


static void
test_file_sanity(void)
{
	FILE	*fp = tmpfile();

	CHECK(fp != NULL);
	if (!fp)
		return;

	/* Bogus writeprot byte must be rejected.  dmk_header_fwrite() does
	 * not seek, so rewind before each header we lay down at offset 0. */
	struct dmk_header	h;

	dmk_header_init(&h, 0, DMKI_TRACKLEN_5SD);
	h.writeprot = 0x55;
	rewind(fp);
	CHECK(dmk_header_fwrite(&h, fp));
	CHECK_EQ(fp2dmk(fp, &dmkf2), -1);

	/* Nonzero real_format field must be rejected. */
	dmk_header_init(&h, 0, DMKI_TRACKLEN_5SD);
	h.real_format = 0x12345678;
	rewind(fp);
	CHECK(dmk_header_fwrite(&h, fp));
	CHECK_EQ(fp2dmk(fp, &dmkf2), -1);

	/* Write-protected but otherwise valid file is accepted. */
	dmk_header_init(&h, 0, DMKI_TRACKLEN_5SD);
	h.writeprot = 0xff;
	rewind(fp);
	CHECK(dmk_header_fwrite(&h, fp));
	CHECK_EQ(fp2dmk(fp, &dmkf2), 0);

	fclose(fp);
}


int
main(void)
{
	test_header_init();
	test_header_layout();
	test_track_file_offset();
	test_track_roundtrip();
	test_data_rotate();
	test_track_length_optimal();
	test_file_roundtrip();
	test_file_sanity();

	return test_exit("test_dmk");
}
