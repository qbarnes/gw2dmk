#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "simbus.h"
#include "simctl.h"
#include "simdrive.h"
#include "simmedia.h"


static int
parse_unit(const char *s)
{
	if (!s || s[1] != '\0')
		return -1;

	if (s[0] >= '0' && s[0] <= '2')
		return s[0] - '0';

	if (s[0] >= 'a' && s[0] <= 'c')
		return s[0] - 'a';

	return -1;
}


static struct sim_drive *
unit_drive(struct sim_gw *gw, const char *s, int out, int *unitp)
{
	int	unit = parse_unit(s);

	if (unit == -1) {
		dprintf(out, "error: bad unit '%s'\n", s ? s : "");
		return NULL;
	}

	if (unitp)
		*unitp = unit;

	if (!gw->unit[unit]) {
		dprintf(out, "error: no drive at unit %d\n", unit);
		return NULL;
	}

	return gw->unit[unit];
}


static void
ctl_insert(struct sim_gw *gw, const char *us, const char *path, int out)
{
	struct sim_drive	*drv = unit_drive(gw, us, out, NULL);

	if (!drv)
		return;

	if (!path || !*path) {
		dprintf(out, "error: no file given\n");
		return;
	}

	if (drv->media) {
		dprintf(out, "error: drive has a diskette; eject first\n");
		return;
	}

	struct sim_media	*media = sim_media_load(path);

	if (!media) {
		dprintf(out, "error: cannot load '%s'\n", path);
		return;
	}

	drv->media = media;

	char	desc[256];

	media->ops->describe(media, desc, sizeof(desc));
	dprintf(out, "inserted: %s\n", desc);
}


static void
ctl_eject(struct sim_gw *gw, const char *us, int out)
{
	struct sim_drive	*drv = unit_drive(gw, us, out, NULL);

	if (!drv)
		return;

	if (!drv->media) {
		dprintf(out, "error: no diskette in drive\n");
		return;
	}

	if (sim_media_eject(drv->media) == -1)
		dprintf(out, "warning: failed to save media\n");
	else
		dprintf(out, "ejected\n");

	drv->media = NULL;
}


static void
ctl_wp(struct sim_gw *gw, const char *us, const char *val, int out)
{
	struct sim_drive	*drv = unit_drive(gw, us, out, NULL);

	if (!drv)
		return;

	if (val && !strcmp(val, "on"))
		drv->wp_force = true;
	else if (val && !strcmp(val, "off"))
		drv->wp_force = false;
	else {
		dprintf(out, "error: wp <unit> on|off\n");
		return;
	}

	dprintf(out, "write protect %s\n", val);
}


static void
ctl_status(struct sim_gw *gw, int out)
{
	dprintf(out, "model: %s   bus: %s\n", gw->model->name,
		sim_bus_name(gw->bus));

	for (int u = 0; u < SIM_MAX_UNITS; ++u) {
		struct sim_drive	*drv = gw->unit[u];

		if (!drv)
			continue;

		dprintf(out, "unit %d: %s (%d tracks, %d RPM)%s, "
			"cyl %d, motor %s%s\n",
			u, drv->type->desc, drv->tracks, drv->rpm,
			drv->fdadap ? " via FDADAP" : "",
			drv->cyl,
			sim_drive_spinning(drv) ? "on" : "off",
			drv->wp_force ? ", wp forced" : "");

		if (drv->media) {
			char	desc[256];

			drv->media->ops->describe(drv->media, desc,
						  sizeof(desc));
			dprintf(out, "        %s\n", desc);
		} else {
			dprintf(out, "        (no diskette)\n");
		}
	}
}


int
sim_ctl_command(struct sim_gw *gw, char *line, int out)
{
	const char	*sep = " \t\n";
	char		*save = NULL;
	char		*cmd  = strtok_r(line, sep, &save);

	if (!cmd)
		return 0;

	if (!strcmp(cmd, "insert")) {
		char	*us   = strtok_r(NULL, sep, &save);
		char	*path = strtok_r(NULL, sep, &save);

		ctl_insert(gw, us, path, out);
	} else if (!strcmp(cmd, "eject")) {
		ctl_eject(gw, strtok_r(NULL, sep, &save), out);
	} else if (!strcmp(cmd, "wp")) {
		char	*us  = strtok_r(NULL, sep, &save);
		char	*val = strtok_r(NULL, sep, &save);

		ctl_wp(gw, us, val, out);
	} else if (!strcmp(cmd, "status")) {
		ctl_status(gw, out);
	} else if (!strcmp(cmd, "help")) {
		dprintf(out, "commands: insert <unit> <file>, "
			"eject <unit>, wp <unit> on|off, status, "
			"help, quit\n");
	} else if (!strcmp(cmd, "quit") || !strcmp(cmd, "exit")) {
		return 1;
	} else {
		dprintf(out, "error: unknown command '%s' "
			"(try \"help\")\n", cmd);
	}

	return 0;
}
