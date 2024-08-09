#ifndef MISC_H
#define MISC_H

#ifdef __cplusplus
extern "C" {
#endif


#if !defined(WIN64) && !defined(WIN32)
#define min(a,b) \
	({ __typeof__ (a) _a = (a); \
	   __typeof__ (b) _b = (b); \
	   _a < _b ? _a : _b; })

#define max(a,b) \
	({ __typeof__ (a) _a = (a); \
	   __typeof__ (b) _b = (b); \
	   _a > _b ? _a : _b; })
#endif


#define COUNT_OF(x) ((sizeof(x)/sizeof(0[x])) / \
			((size_t)(!(sizeof(x) % sizeof(0[x])))))


#define MEMBER_SIZE(type, member) sizeof(((type *)0)->member)


#if linux
#include <endian.h>
#elif defined(WIN64) || defined(WIN32)
  #if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
    /* These can be considered no-ops. */
    #define htole16(x) ((uint16_t)(x))
    #define htole32(x) ((uint32_t)(x))
    #define le16toh(x) ((uint16_t)(x))
    #define le32toh(x) ((uint32_t)(x))
  #else
    #error "Need macros for BE MSW."
  #endif
#endif


inline const char *
plu(int val)
{
	return (val == 1) ? "" : "s";
}


#ifdef __cplusplus
}
#endif

#endif
