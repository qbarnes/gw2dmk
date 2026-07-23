#ifndef CRC_H
#define CRC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>


/* Recompute the CRC with len bytes appended. */
static inline uint16_t
calc_crc1(uint16_t crc, uint8_t c)
{
	extern const uint16_t crc16_table[256];

	return (crc << 8) ^ crc16_table[(crc >> 8) ^ c];
}


#ifdef __cplusplus
}
#endif

#endif
