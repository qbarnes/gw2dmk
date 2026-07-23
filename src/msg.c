#include "msg.h"


static int file_msg_level       = 0;
static int scrn_msg_level       = 0;
static const char *msg_prefix   = NULL;
static const char *msg_filename = NULL;
static FILE *msg_file           = NULL;


int
msg_scrn_get_level()
{
	return scrn_msg_level;
}


int
msg_scrn_set_level(int new_msg_level)
{
	if (new_msg_level < 0)
		return -1;

	scrn_msg_level = new_msg_level;

	return scrn_msg_level;
}


int
msg_file_get_level()
{
	return file_msg_level;
}


int
msg_file_set_level(int new_msg_level)
{
	if (new_msg_level < 0)
		return -1;

	file_msg_level = new_msg_level;

	return file_msg_level;
}


const char *
msg_get_filename()
{
	return msg_filename;
}


const char *
msg_error_prefix(const char *pf)
{
	if (msg_prefix)
		free((void *)msg_prefix);

	msg_prefix = pf ? strdup(pf) : NULL;

	return msg_prefix;
}


int
msg_fclose()
{
	if (!msg_file)
		return 0;

	free((void *)msg_filename);
	msg_filename = NULL;

	msg_error_prefix(NULL);  // Should this be called here?

	FILE *f = msg_file;
	msg_file = NULL;

	return fclose(f);
}


FILE *
msg_fopen(const char *filename)
{
	msg_fclose();

	const char *fn = strdup(filename);

	if (!fn)
		return NULL;

	FILE *f = fopen(filename, "w");

	if (f) {
		msg_filename = fn;
		msg_file = f;
	} else {
		free((void *)fn);
	}

	return f;
}


int
msg_scrn_flush()
{
	return fflush(stdout);
}


int
msg_verror(const char *fmt, va_list ap)
{
	int ret = 0;

	msg_scrn_flush();

	if (msg_prefix)
		ret = fprintf(stderr, "%s: ", msg_prefix);

	if (ret != -1) {
		int ret2 = vfprintf(stderr, fmt, ap);
		ret = (ret2 != -1) ? ret + ret2 : -1;
	}

	return ret;
}


int
msg_error(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	int ret = msg_verror(fmt, args);
	va_end(args);

	return ret;
}


void
msg_fatal_ec(int exit_code, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	msg_verror(fmt, args);
	va_end(args);

	exit(exit_code);
}


void
msg_fatal(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	msg_verror(fmt, args);
	va_end(args);

	exit(EXIT_FAILURE);
}


#include "msg_levels.h" // Violate inheritance levels until the
			// violation in msg_vfprintf() is understood.
void
msg_vfprintf(int msg_level, FILE *scrn, const char *fmt, va_list ap)
{
	if (msg_level <= scrn_msg_level &&
	    !(msg_level == MSG_RAW && scrn_msg_level != MSG_RAW) &&
	    !(msg_level == MSG_HEX && scrn_msg_level == MSG_RAW)) {
		va_list	aq;

		va_copy(aq, ap);
		vfprintf(scrn, fmt, aq);
		va_end(aq);
	}


	if (msg_file && (msg_level <= file_msg_level) &&
	    !(msg_level == MSG_RAW && file_msg_level != MSG_RAW) &&
	    !(msg_level == MSG_HEX && file_msg_level == MSG_RAW)) {
		vfprintf(msg_file, fmt, ap);
	}
}


void
msg_vprintf(int msg_level, const char *fmt, va_list ap)
{
	msg_vfprintf(msg_level, stdout, fmt, ap);
}


/* Log a message. */
void
msg(int level, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	msg_vprintf(level, fmt, args);
	va_end(args);
}
