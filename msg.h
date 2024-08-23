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
int msg_verror(const char *fmt, va_list ap) __attribute__((format(printf,1,0)));

int msg_error(const char *fmt, ...) __attribute__((format(printf,1,2)));

void msg_fatal_ec(int exit_code, const char *fmt, ...)
					__attribute__((format(printf,2,3)))
					__attribute__((noreturn));

void msg_fatal(const char *fmt, ...)
					__attribute__((format(printf,1,2)))
					__attribute__((noreturn));

void msg(int level, const char *fmt, ...)
					__attribute__((format(printf,2,3)));
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

extern int msg_verror(const char *fmt, va_list ap);

extern int msg_error(const char *fmt, ...);

extern void msg_fatal_ec(int exit_code, const char *fmt, ...);

extern void msg_fatal(const char *fmt, ...);

extern void msg(int level, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif
