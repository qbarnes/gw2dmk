#ifndef SIMDRIVE_H
#define SIMDRIVE_H

#include <stdbool.h>
#include <stdint.h>

#include "simmedia.h"

/*
 * Emulated floppy drive.
 *
 * A drive type describes the mechanism: media size, stepper range,
 * rotational speed(s), and heads.  A drive instance adds mutable
 * state: head position, motor, inserted media.
 *
 * Default rotational speeds:
 *   8"        360 RPM (fixed; spindle turns whenever powered)
 *   5.25" SD  300 RPM
 *   5.25" DD  300 RPM
 *   5.25" HD  360 RPM (300 selectable, as on dual-speed drives)
 *   3.5"  DD  300 RPM
 *   3.5"  HD  300 RPM
 */

struct sim_drive_type {
	const char	*name;		/* e.g. "525dd" */
	const char	*desc;
	int		size;		/* media size: 8, 5, or 3 */
	int		tracks;		/* default track count */
	int		rpm;		/* default RPM */
	int		rpm_alt;	/* alternate RPM, or 0 */
	int		sides;		/* default heads */
	bool		needs_fdadap;	/* 8" behind a 50<->34 adapter */
};

struct sim_drive {
	const struct sim_drive_type	*type;
	int		tracks;
	int		rpm;
	int		sides;
	bool		fdadap;		/* attached via FDADAP */
	bool		wp_force;	/* user-forced write protect */

	int		cyl;		/* physical head position */
	bool		motor_on;
	uint64_t	spin_ref_ns;	/* rotation phase reference */
	uint64_t	phase_ticks;	/* fast-mode rotation cursor */

	struct sim_media	*media;	/* NULL = no diskette */
};

extern const struct sim_drive_type *sim_drive_type_find(const char *name);

extern void sim_drive_type_list(void);

/* Create a drive instance of the given type. Returns NULL on error. */
extern struct sim_drive *sim_drive_new(const struct sim_drive_type *dt);

extern void sim_drive_free(struct sim_drive *drv);

/* Is the media rotating?  8" spindles turn whenever powered. */
extern bool sim_drive_spinning(const struct sim_drive *drv);

/* Step the head to "cyl", clamped to the drive's physical range. */
extern void sim_drive_seek(struct sim_drive *drv, int cyl);

extern void sim_drive_motor(struct sim_drive *drv, bool on);

/* Is the inserted media write-protected (or the drive forced so)? */
extern bool sim_drive_wp(const struct sim_drive *drv);

/*
 * Map a physical cylinder to a media track number.  Handles 48 tpi
 * media (~40 tracks or fewer) inserted in a 96 tpi (80 track)
 * drive, where each media track spans two head positions.
 */
extern int sim_drive_media_track(const struct sim_drive *drv, int cyl);

#endif
