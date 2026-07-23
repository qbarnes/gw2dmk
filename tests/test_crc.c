/*
 * Validate the CCITT CRC-16 table and calc_crc1() against known vectors.
 */

#include "crc.h"

#include "test.h"


static uint16_t
crc_buf(uint16_t crc, const uint8_t *buf, size_t len)
{
	while (len--)
		crc = calc_crc1(crc, *buf++);

	return crc;
}


int
main(void)
{
	/* Standard CRC-16/CCITT-FALSE check value. */
	CHECK_EQ(crc_buf(0xffff, (const uint8_t *)"123456789", 9), 0x29b1);

	/* Empty input leaves the preset untouched. */
	CHECK_EQ(crc_buf(0xffff, NULL, 0), 0xffff);
	CHECK_EQ(crc_buf(0x0000, NULL, 0), 0x0000);

	/* Single bytes against the table directly. */
	CHECK_EQ(calc_crc1(0x0000, 0x00), 0x0000);
	CHECK_EQ(calc_crc1(0xffff, 0x00), 0xe1f0);

	/* CRC of the three MFM 0xa1 premark bytes from 0xffff.  The
	 * decoder hardcodes this value (0xcdb4) as its CRC preset when
	 * the premark is implied. */
	const uint8_t	premark[3] = { 0xa1, 0xa1, 0xa1 };

	CHECK_EQ(crc_buf(0xffff, premark, 3), 0xcdb4);

	/* An MFM sector ID field: a1 a1 a1 fe cyl side sec size.
	 * Appending the CRC of a message to the message must yield a
	 * CRC of 0; this is how the decoder validates IDs and data. */
	uint8_t	id[10] = { 0xa1, 0xa1, 0xa1, 0xfe, 0x05, 0x00, 0x01, 0x02 };

	uint16_t crc = crc_buf(0xffff, id, 8);

	id[8] = crc >> 8;
	id[9] = crc & 0xff;
	CHECK_EQ(crc_buf(0xffff, id, 10), 0x0000);

	/* Same property for an FM ID field (no premark). */
	uint8_t	fm_id[7] = { 0xfe, 0x11, 0x01, 0x0a, 0x00 };

	crc = crc_buf(0xffff, fm_id, 5);

	fm_id[5] = crc >> 8;
	fm_id[6] = crc & 0xff;
	CHECK_EQ(crc_buf(0xffff, fm_id, 7), 0x0000);

	return test_exit("test_crc");
}
