#ifndef MISC_H
#define MISC_H

#ifdef __cplusplus
extern "C" {
#endif


#include <stdbool.h>


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


/*
 * Return 1 if a is approximately equal to b (within tolerance).
 */

#define approx(a, b, tol) _Generic((a), \
	int: approx_int, \
	float: approx_float, \
	double: approx_double, \
	default: approx_default \
)(a,b, tol)

/* Could these approx_* functions below be replaced with macro expansion? */

inline bool
approx_int(int a, int b, double tol)
{
	int diff = (a > b) ? (a - b) : (b - a);
	return ((double)diff / (double) b) < tol;
}


inline bool
approx_float(float a, float b, double tol)
{
	int diff = (a > b) ? (a - b) : (b - a);
	return ((double)diff / (double) b) < tol;
}


inline bool
approx_double(double a, double b, double tol)
{
	int diff = (a > b) ? (a - b) : (b - a);
	return (diff / b) < tol;
}


inline bool
approx_default(void *a, void *b, double tol)
{
	return false;  /* Unsupported type */
}


/*
 * Return 1 if a is approximately equal to b (within 4% of b)
 */

#define approx04(a, b) _Generic((a), \
	int: approx04_int, \
	float: approx04_float, \
	double: approx04_double, \
	default: approx04_default \
)(a,b)

inline bool
approx04_int(int a, int b)
{
	return approx(a, b, 0.04);
}


inline bool
approx04_float(float a, float b)
{
	return approx(a, b, 0.04);
}


inline bool
approx04_double(double a, double b)
{
	return approx(a, b, 0.04);
}


inline bool
approx04_default(void *a, void *b)
{
	return NULL;  /* Unsupported type */
}


#ifdef __cplusplus
}
#endif

#endif
