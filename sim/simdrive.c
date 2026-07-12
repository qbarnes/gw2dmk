#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "simclock.h"
#include "simdrive.h"


static const struct sim_drive_type drive_types[] = {
	{ "8sd",    "8\" single-density",	8, 77, 360,   0, 1, true  },
	{ "8dd",    "8\" double-density",	8, 77, 360,   0, 2, true  },
	{ "525sd",  "5.25\" single-density",	5, 35, 300,   0, 1, false },
	{ "525dd",  "5.25\" double-density",	5, 40, 300,   0, 2, false },
	{ "525qd",  "5.25\" quad-density",	5, 80, 300,   0, 2, false },
	{ "525hd",  "5.25\" high-density",	5, 80, 360, 300, 2, false },
	{ "35dd",   "3.5\" double-density",	3, 80, 300,   0, 2, false },
	{ "35hd",   "3.5\" high-density",	3, 80, 300,   0, 2, false },
};

#define N_DRIVE_TYPES \
	(sizeof(drive_types) / sizeof(drive_types[0]))


const struct sim_drive_type *
sim_drive_type_find(const char *name)
{
	for (size_t i = 0; i < N_DRIVE_TYPES; ++i) {
		if (!strcmp(name, drive_types[i].name))
			return &drive_types[i];
	}

	return NULL;
}


void
sim_drive_type_list(void)
{
	for (size_t i = 0; i < N_DRIVE_TYPES; ++i) {
		const struct sim_drive_type *dt = &drive_types[i];

		printf("  %-7s %s: %d tracks, %d RPM",
		       dt->name, dt->desc, dt->tracks, dt->rpm);

		if (dt->rpm_alt)
			printf(" (or %d)", dt->rpm_alt);

		printf(", %d side%s\n", dt->sides,
		       dt->sides == 1 ? "" : "s");
	}
}


struct sim_drive *
sim_drive_new(const struct sim_drive_type *dt)
{
	struct sim_drive *drv = calloc(1, sizeof(*drv));

	if (!drv)
		return NULL;

	drv->type	 = dt;
	drv->tracks	 = dt->tracks;
	drv->rpm	 = dt->rpm;
	drv->sides	 = dt->sides;
	drv->fdadap	 = dt->needs_fdadap;
	drv->cyl	 = 0;
	drv->spin_ref_ns = sim_now_ns();

	return drv;
}


void
sim_drive_free(struct sim_drive *drv)
{
	if (!drv)
		return;

	if (drv->media)
		sim_media_eject(drv->media);

	free(drv);
}


bool
sim_drive_spinning(const struct sim_drive *drv)
{
	/* 8" drive spindles turn whenever the drive is powered. */
	if (drv->type->size == 8)
		return true;

	return drv->motor_on;
}


void
sim_drive_seek(struct sim_drive *drv, int cyl)
{
	if (cyl < 0)
		cyl = 0;
	else if (cyl >= drv->tracks)
		cyl = drv->tracks - 1;

	drv->cyl = cyl;
}


void
sim_drive_motor(struct sim_drive *drv, bool on)
{
	if (on && !drv->motor_on)
		drv->spin_ref_ns = sim_now_ns();

	drv->motor_on = on;
}


bool
sim_drive_wp(const struct sim_drive *drv)
{
	if (drv->wp_force)
		return true;

	return drv->media ? drv->media->wp : false;
}


int
sim_drive_media_track(const struct sim_drive *drv, int cyl)
{
	if (!drv->media)
		return -1;

	int	mtracks = drv->media->ops->tracks(drv->media);

	/*
	 * 48 tpi media (44 tracks or fewer) in a 96 tpi drive: each
	 * media track lies under two head positions.  8" drives are
	 * excluded; 77 tracks is their native spacing.
	 */

	if (drv->type->size != 8 && drv->tracks >= 77 && mtracks <= 44)
		return cyl / 2;

	return cyl;
}
