/*
 * Startup configuration file support.  See cfgfile.h for an
 * overview.
 *
 * File format:
 *   - Sections: [global], [gw2dmk], [dmk2gw], [gwhist] (names are
 *     case-insensitive; anything else is fatal).
 *   - "key = value" lines; keys are long option names of the running
 *     tool (case-sensitive).
 *   - Comments start with ';' or '#' at the start of a line or after
 *     whitespace.
 *   - Values may be double-quoted to protect leading/trailing spaces
 *     or comment characters.
 *   - Options that take no argument use boolean values
 *     (true/yes/on/1 or false/no/off/0); false selects the paired
 *     negative option ("join = false" acts as --nojoin, "hd = false"
 *     as --dd).
 */

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "msg.h"
#include "cfgfile.h"


#define CFG_LINE_MAX	1024

static const char *const cfg_sections[] =
		{ "global", "gw2dmk", "dmk2gw", "gwhist" };


struct argv_buf {
	char	**argv;
	int	argc;
	int	cap;
};


static void
argv_push(struct argv_buf *ab, char *arg)
{
	if (ab->argc == ab->cap) {
		ab->cap  = ab->cap ? ab->cap * 2 : 16;
		ab->argv = realloc(ab->argv, ab->cap * sizeof(*ab->argv));

		if (!ab->argv)
			msg_fatal("Cannot allocate config arguments.\n");
	}

	ab->argv[ab->argc++] = arg;
}


static char *
skip_ws(char *s)
{
	while (*s == ' ' || *s == '\t')
		++s;

	return s;
}


static void
trim_trailing_ws(char *s)
{
	size_t	len = strlen(s);

	while (len && (s[len-1] == ' ' || s[len-1] == '\t'))
		s[--len] = '\0';
}


/* Truncate at a comment: ';' or '#' at the start or after whitespace. */

static void
cut_comment(char *s)
{
	for (char *p = s; *p; ++p) {
		if ((*p == ';' || *p == '#') &&
		    (p == s || p[-1] == ' ' || p[-1] == '\t')) {
			*p = '\0';
			return;
		}
	}
}


/* Returns 1 for true, 0 for false, -1 if not a boolean. */

static int
parse_bool(const char *s)
{
	if (!strcasecmp(s, "true") || !strcasecmp(s, "yes") ||
	    !strcasecmp(s, "on") || !strcmp(s, "1"))
		return 1;

	if (!strcasecmp(s, "false") || !strcasecmp(s, "no") ||
	    !strcasecmp(s, "off") || !strcmp(s, "0"))
		return 0;

	return -1;
}


static const struct option *
find_opt(const struct option *longopts, const char *name)
{
	for (const struct option *o = longopts; o->name; ++o) {
		if (!strcmp(o->name, name))
			return o;
	}

	return NULL;
}


/* Find the option that turns "name" off ("no" pairing, or hd<->dd). */

static const struct option *
find_negation(const struct option *longopts, const char *name)
{
	if (!strcmp(name, "hd"))
		return find_opt(longopts, "dd");

	if (!strcmp(name, "dd"))
		return find_opt(longopts, "hd");

	if (!strncmp(name, "no", 2))
		return find_opt(longopts, name + 2);

	char	buf[64];

	if (strlen(name) + 3 > sizeof(buf))
		return NULL;

	sprintf(buf, "no%s", name);

	return find_opt(longopts, buf);
}


/*
 * Match the config-file option: the short form "-C" ("-C file" or
 * "-Cfile") or the long form "--config", including the unambiguous
 * abbreviations getopt_long() accepts.  Returns the attached value
 * via valp, or NULL if the value is a separate argument.
 */

static bool
is_config_opt(const char *arg, const char **valp)
{
	if (arg[0] != '-')
		return false;

	/* Short form: -C or -Cvalue (everything after -C is the value). */
	if (arg[1] == 'C') {
		*valp = arg[2] ? arg + 2 : NULL;
		return true;
	}

	/* Long form: --config[=value] and unambiguous abbreviations. */
	if (arg[1] != '-' || !arg[2])
		return false;

	const char	*p  = arg + 2;
	const char	*eq = strchr(p, '=');
	size_t		n   = eq ? (size_t)(eq - p) : strlen(p);

	if (n == 0 || n > strlen("config") || strncmp(p, "config", n))
		return false;

	*valp = eq ? eq + 1 : NULL;

	return true;
}


/*
 * Match the "--noconfig" option, including the unambiguous
 * abbreviations getopt_long() accepts.  It takes no value.
 */

static bool
is_noconfig_opt(const char *arg)
{
	if (arg[0] != '-' || arg[1] != '-' || !arg[2])
		return false;

	const char	*p = arg + 2;
	size_t		n = strlen(p);

	if (n == 0 || n > strlen("noconfig") || strncmp(p, "noconfig", n))
		return false;

	return true;
}


const char *
cfg_scan_argv(int argc, char **argv, bool *noconfig)
{
	const char	*path = NULL;
	bool		seen_config = false;

	*noconfig = false;

	for (int i = 1; i < argc; ++i) {
		const char	*val;

		if (!strcmp(argv[i], "--"))
			break;

		if (is_noconfig_opt(argv[i])) {
			*noconfig = true;
			continue;
		}

		if (!is_config_opt(argv[i], &val))
			continue;

		if (seen_config)
			msg_fatal("Option '-C' ('--config') given more than "
				  "once.  Only one config file may be "
				  "specified.\n");

		seen_config = true;

		if (val) {
			path = val;
		} else {
			if (i + 1 == argc)
				msg_fatal("Option '-C' ('--config') requires "
					  "a file argument.\n");

			path = argv[++i];
		}
	}

	if (seen_config && *noconfig)
		msg_fatal("Options '-C' ('--config') and '--noconfig' are "
			  "mutually exclusive.\n");

	return path;
}


char *
cfg_default_path(void)
{
	char		*path;

#if defined(WIN64) || defined(WIN32)
	static const char	sub[] = "\\gw2dmk\\gw2dmk.ini";
	const char	*base = getenv("APPDATA");

	if (!base || !*base)
		return NULL;
#else
	static const char	xdg_sub[]  = "/gw2dmk/gw2dmk.ini";
	static const char	home_sub[] = "/.config/gw2dmk/gw2dmk.ini";
	const char	*sub  = xdg_sub;
	const char	*base = getenv("XDG_CONFIG_HOME");

	if (!base || !*base) {
		base = getenv("HOME");
		sub  = home_sub;

		if (!base || !*base)
			return NULL;
	}
#endif

	path = malloc(strlen(base) + strlen(sub) + 1);

	if (!path)
		msg_fatal("Cannot allocate config file path.\n");

	sprintf(path, "%s%s", base, sub);

	if (access(path, F_OK) != 0) {
		free(path);
		return NULL;
	}

	return path;
}


int
cfg_load_argv(const char *path, const char *tool_section,
	      const struct option *longopts, char ***argv_out)
{
	FILE	*fp = fopen(path, "r");

	if (!fp)
		msg_fatal("Cannot open config file '%s': %s\n",
			  path, strerror(errno));

	enum { SEC_NONE, SEC_GLOBAL, SEC_TOOL, SEC_SKIP } sec = SEC_NONE;

	/* [global] settings apply before [tool] ones, whatever the
	 * order of the sections in the file. */
	struct argv_buf	gargs = { NULL, 0, 0 };
	struct argv_buf	targs = { NULL, 0, 0 };

	char	line[CFG_LINE_MAX];
	int	lineno = 0;

	while (fgets(line, sizeof(line), fp)) {
		++lineno;

		size_t	len = strlen(line);

		if (len == sizeof(line) - 1 && line[len-1] != '\n')
			msg_fatal("%s:%d: line too long.\n", path, lineno);

		while (len && (line[len-1] == '\n' || line[len-1] == '\r'))
			line[--len] = '\0';

		char	*p = skip_ws(line);

		if (!*p || *p == ';' || *p == '#')
			continue;

		if (*p == '[') {
			char	*q = strchr(p, ']');

			if (!q)
				msg_fatal("%s:%d: missing ']'.\n",
					  path, lineno);

			*q = '\0';

			char	*name = skip_ws(p + 1);

			trim_trailing_ws(name);

			char	*rest = skip_ws(q + 1);

			cut_comment(rest);
			trim_trailing_ws(rest);

			if (*rest)
				msg_fatal("%s:%d: unexpected text after "
					  "section name.\n", path, lineno);

			if (!strcasecmp(name, "global")) {
				sec = SEC_GLOBAL;
			} else if (!strcasecmp(name, tool_section)) {
				sec = SEC_TOOL;
			} else {
				sec = SEC_SKIP;

				bool	known = false;

				for (size_t i = 0;
				     i < sizeof(cfg_sections) /
					 sizeof(cfg_sections[0]); ++i) {
					if (!strcasecmp(name,
							cfg_sections[i]))
						known = true;
				}

				if (!known)
					msg_fatal("%s:%d: unknown section "
						  "'[%s]'.\n",
						  path, lineno, name);
			}

			continue;
		}

		char	*eq = strchr(p, '=');

		if (!eq)
			msg_fatal("%s:%d: expected 'setting = value'.\n",
				  path, lineno);

		*eq = '\0';
		trim_trailing_ws(p);

		if (!*p)
			msg_fatal("%s:%d: missing setting name.\n",
				  path, lineno);

		char	*val = skip_ws(eq + 1);

		if (*val == '"') {
			char	*q = strchr(val + 1, '"');

			if (!q)
				msg_fatal("%s:%d: unterminated quote.\n",
					  path, lineno);

			*q  = '\0';
			val = val + 1;

			char	*rest = skip_ws(q + 1);

			cut_comment(rest);
			trim_trailing_ws(rest);

			if (*rest)
				msg_fatal("%s:%d: unexpected text after "
					  "quoted value.\n", path, lineno);
		} else {
			cut_comment(val);
			trim_trailing_ws(val);
		}

		/* Keys in other tools' sections cannot be validated
		 * by this tool; skip them. */
		if (sec == SEC_SKIP)
			continue;

		if (sec == SEC_NONE)
			msg_fatal("%s:%d: setting before any [section].\n",
				  path, lineno);

		if (!strcmp(p, "config"))
			msg_fatal("%s:%d: 'config' is not allowed in a "
				  "config file.\n", path, lineno);

		const struct option	*o = find_opt(longopts, p);

		if (!o)
			msg_fatal("%s:%d: unknown setting '%s' for %s.\n",
				  path, lineno, p, tool_section);

		struct argv_buf	*ab = (sec == SEC_GLOBAL) ? &gargs : &targs;
		char		*arg;

		if (o->has_arg == required_argument) {
			if (!*val)
				msg_fatal("%s:%d: setting '%s' requires a "
					  "value.\n", path, lineno, p);

			size_t	alen = strlen(p) + strlen(val) + 4;

			arg = malloc(alen);

			if (!arg)
				msg_fatal("Cannot allocate config "
					  "arguments.\n");

			snprintf(arg, alen, "--%s=%s", p, val);
		} else if (o->has_arg == no_argument) {
			int	bv = parse_bool(val);

			if (bv == -1)
				msg_fatal("%s:%d: setting '%s' requires a "
					  "boolean value (true/false).\n",
					  path, lineno, p);

			if (!bv) {
				o = find_negation(longopts, p);

				if (!o || o->has_arg != no_argument)
					msg_fatal("%s:%d: setting '%s' "
						  "cannot be disabled.\n",
						  path, lineno, p);
			}

			size_t	alen = strlen(o->name) + 3;

			arg = malloc(alen);

			if (!arg)
				msg_fatal("Cannot allocate config "
					  "arguments.\n");

			snprintf(arg, alen, "--%s", o->name);
		} else {
			msg_fatal("%s:%d: setting '%s' is not supported "
				  "in a config file.\n", path, lineno, p);
		}

		argv_push(ab, arg);
	}

	if (ferror(fp))
		msg_fatal("Error reading config file '%s'.\n", path);

	fclose(fp);

	char	**argv = malloc((1 + gargs.argc + targs.argc + 1) *
				sizeof(*argv));

	if (!argv)
		msg_fatal("Cannot allocate config arguments.\n");

	int	argc = 0;

	argv[argc] = strdup(tool_section);

	if (!argv[argc])
		msg_fatal("Cannot allocate config arguments.\n");

	++argc;

	for (int i = 0; i < gargs.argc; ++i)
		argv[argc++] = gargs.argv[i];

	for (int i = 0; i < targs.argc; ++i)
		argv[argc++] = targs.argv[i];

	argv[argc] = NULL;

	free(gargs.argv);
	free(targs.argv);

	*argv_out = argv;

	return argc;
}
