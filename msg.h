#ifndef MSG_H
#define MSG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#ifdef __GNUC__
#define MSG_PRINTF(fmt_idx, arg_idx) \
			__attribute__((format(printf, fmt_idx, arg_idx)))
#define MSG_NORETURN	__attribute__((noreturn))
#else
#define MSG_PRINTF(fmt_idx, arg_idx)
#define MSG_NORETURN
#endif


extern int msg_scrn_get_level(void);

extern int msg_scrn_set_level(int new_msg_level);

extern int msg_file_get_level(void);

extern int msg_file_set_level(int new_msg_level);

extern const char *msg_get_filename(void);

extern FILE *msg_fopen(const char *filename);

extern const char *msg_error_prefix(const char *pf);

extern int msg_fclose(void);

extern int msg_scrn_flush();

extern int msg_verror(const char *fmt, va_list ap) MSG_PRINTF(1, 0);

extern int msg_error(const char *fmt, ...) MSG_PRINTF(1, 2);

extern void msg_fatal_ec(int exit_code, const char *fmt, ...)
					MSG_PRINTF(2, 3) MSG_NORETURN;

extern void msg_fatal(const char *fmt, ...) MSG_PRINTF(1, 2) MSG_NORETURN;

extern void msg(int level, const char *fmt, ...) MSG_PRINTF(2, 3);

#ifdef __cplusplus
}
#endif

#endif
