/*
 * Validate secsize() for all encodings, size codes, and quirks.
 */

#include "secsize.h"

#include "test.h"


int
main(void)
{
	/* MFM: 128 << sizecode, modulo maxsize+1 as a 179x would. */
	CHECK_EQ(secsize(0, MFM, 3, 0), 128);
	CHECK_EQ(secsize(1, MFM, 3, 0), 256);
	CHECK_EQ(secsize(2, MFM, 3, 0), 512);
	CHECK_EQ(secsize(3, MFM, 3, 0), 1024);

	/* MFM: higher-order bits of the size code are ignored. */
	CHECK_EQ(secsize(4, MFM, 3, 0), 128);
	CHECK_EQ(secsize(7, MFM, 3, 0), 1024);
	CHECK_EQ(secsize(0xff, MFM, 3, 0), 1024);

	/* MFM: raising maxsize (-z) permits 765-style larger sectors. */
	CHECK_EQ(secsize(4, MFM, 4, 0), 2048);
	CHECK_EQ(secsize(5, MFM, 5, 0), 4096);

	/* MFM: smaller maxsize wraps sooner. */
	CHECK_EQ(secsize(2, MFM, 1, 0), 128);
	CHECK_EQ(secsize(3, MFM, 1, 0), 256);

	/* FM, IBM encoding: sizecode <= maxsize. */
	CHECK_EQ(secsize(0, FM, 3, 0), 128);
	CHECK_EQ(secsize(1, FM, 3, 0), 256);
	CHECK_EQ(secsize(2, FM, 3, 0), 512);
	CHECK_EQ(secsize(3, FM, 3, 0), 1024);

	/* FM, non-IBM (WD1771) encoding: sizecode > maxsize means
	 * 16 * sizecode bytes. */
	CHECK_EQ(secsize(4, FM, 3, 0), 64);
	CHECK_EQ(secsize(16, FM, 3, 0), 256);
	CHECK_EQ(secsize(255, FM, 3, 0), 4080);
	CHECK_EQ(secsize(1, FM, 0, 0), 16);

	/* MIXED falls into the FM/default case. */
	CHECK_EQ(secsize(1, MIXED, 3, 0), 256);

	/* RX02: 256 << sizecode, modulo maxsize+1. */
	CHECK_EQ(secsize(0, RX02, 3, 0), 256);
	CHECK_EQ(secsize(1, RX02, 3, 0), 512);
	CHECK_EQ(secsize(4, RX02, 3, 0), 256);

	/* DMK_QUIRK_EXTRA_DATA adds four extra data bytes. */
	CHECK_EQ(secsize(1, MFM, 3, DMK_QUIRK_EXTRA_DATA), 260);
	CHECK_EQ(secsize(0, FM, 3, DMK_QUIRK_EXTRA_DATA), 132);
	CHECK_EQ(secsize(0, RX02, 3, DMK_QUIRK_EXTRA_DATA), 260);

	/* Other quirk bits leave the size alone. */
	CHECK_EQ(secsize(1, MFM, 3, DMK_QUIRK_ID_CRC | DMK_QUIRK_PREMARK), 256);

	return test_exit("test_secsize");
}
