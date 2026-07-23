#ifndef CFGFILE_H
#define CFGFILE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <getopt.h>
#include <stdbool.h>

/*
 * Startup configuration file support.
 *
 * A shared INI file provides default settings for the host tools.
 * Keys mirror the long command-line option names and are validated
 * against the tool's getopt_long() table; matching entries are
 * synthesized into an argv which the caller runs through its normal
 * argument parsing before the real command line, so the command line
 * overrides the file.
 */

/*
 * Scan argv for the config-file options ahead of getopt_long() (the
 * file must be loaded before normal parsing begins).  Recognizes
 * "-C FILE", "-CFILE", "--config FILE", "--config=FILE", and
 * "--noconfig".  Returns the "-C"/"--config" file path, or NULL if
 * not given, and sets *noconfig to true if "--noconfig" was given.
 * A missing file argument, a repeated "-C", or combining "-C" with
 * "--noconfig" is fatal.
 */
extern const char *cfg_scan_argv(int argc, char **argv, bool *noconfig);

/*
 * Return the malloc'd default config file path if such a file
 * exists, otherwise NULL.  Unix: $XDG_CONFIG_HOME/gw2dmk/gw2dmk.ini,
 * falling back to $HOME/.config/gw2dmk/gw2dmk.ini.  MS Windows:
 * %APPDATA%\gw2dmk\gw2dmk.ini.
 */
extern char *cfg_default_path(void);

/*
 * Parse the config file, applying the [global] section then the
 * [tool_section] section, and return a synthesized argument vector
 * via argv_out (argv[0] is the tool name; the vector is
 * NULL-terminated).  Returns the argument count.  Any syntax error,
 * unknown section, or setting not in longopts is fatal.
 *
 * The returned strings may end up referenced by parsed settings, so
 * they must remain live for the life of the process; they are never
 * freed.
 */
extern int cfg_load_argv(const char *path, const char *tool_section,
			 const struct option *longopts, char ***argv_out);

#ifdef __cplusplus
}
#endif

#endif
