#include "secsize.h"


int
secsize(int sizecode, int encoding, unsigned int quirk)
{
	int	maxsize = 3;  /* hardcode for now */
	int	size;

	switch (encoding) {
	case MFM:
		/* 179x can only do sizes 128, 256, 512, 1024, and ignores
		 * higher-order bits.  If you need to read a 765-formatted
		 * disk with larger sectors, change maxsize with the -z
		 * command line option. */
		size = 128 << (sizecode % (maxsize + 1));
		break;

	case FM:
	default:
		/* WD1771 has two different encodings for sector size,
		 * depending on a bit in the read/write command that is not
		 * recorded on disk.  We guess IBM encoding if the size
		 * is <= maxsize, non-IBM if larger.  This doesn't really
		 * matter for demodulating the data bytes, only for checking
		 * the CRC.  */

		if (sizecode <= maxsize) {
			/* IBM */
			size = 128 << sizecode;
		} else {
			/* non-IBM */
			size = 16 * (sizecode ? sizecode : 256);
		}
		break;

	case RX02:
		size = 256 << (sizecode % (maxsize + 1));
		break;
	}

	if (quirk & QUIRK_EXTRA_DATA)
		size += 4;

	return size;
}
