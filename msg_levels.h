#ifndef MSG_LEVELS_H
#define MSG_LEVELS_H

#ifdef __cplusplus
extern "C" {
#endif


/*
 * Common message levels.
 */

#define MSG_QUIET	0
#define MSG_NORMAL	1
#define	MSG_SAMPLES	7
#define MSG_DEBUG	8

/*
 * Message levels exclusive to gw2dmk.
 */

#define	MSG_SUMMARY	MSG_NORMAL
#define	MSG_TSUMMARY	2
#define	MSG_ERRORS	3
#define	MSG_IDS		4
#define	MSG_HEX		5
#define	MSG_RAW		6

/*
 * Message levels exclusive to dmk2gw.
 *
 * Do not implement levels 5 or 6 due to MSG_{HEX,RAW}
 * hack in msg.c.
 */

#define	MSG_BYTES	2
#define	MSG_D2GSAMPLES	3


#ifdef __cplusplus
}
#endif

#endif
