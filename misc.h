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


inline
const char *plu(int val)
{
	return (val == 1) ? "" : "s";
}


#ifdef __cplusplus
}
#endif

#endif
