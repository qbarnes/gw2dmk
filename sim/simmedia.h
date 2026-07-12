#ifndef SIMMEDIA_H
#define SIMMEDIA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Media abstraction.
 *
 * A media instance represents one "diskette".  Backends implement the
 * ops table to translate between their on-file format and trains of
 * flux transition pulses (tick counts at the Greaseweazle sample
 * clock) for one full revolution.
 *
 * The only backend for now is DMK (simdmk.c); the vtable exists so
 * other formats can be added without touching the device model.
 */

struct sim_media;

struct sim_media_ops {
	const char	*name;

	/* Load media from a file.  Returns NULL on failure. */
	struct sim_media *(*load)(const char *path);

	/* Flush any modified track data back to the file. */
	int		(*save)(struct sim_media *media);

	/* Free the media instance. */
	void		(*unload)(struct sim_media *media);

	/*
	 * Produce the flux pulse train for one full revolution of
	 * the given track and side, as read by a drive rotating at
	 * "rpm" and sampled at "freq" ticks per second.
	 *
	 * On success returns 0 and a malloc'd array of inter-
	 * transition tick counts the caller must free.  Returns 1 if
	 * the track/side is unformatted (caller synthesizes noise),
	 * or -1 on error.
	 */
	int		(*track_pulses)(struct sim_media *media,
					int track, int side,
					uint32_t freq, int rpm,
					uint32_t **pulses, size_t *cnt);

	/*
	 * Decode a flux pulse train (one write pass) back into the
	 * media's track storage.  Returns 0 on success, -1 on error.
	 * May be NULL if the backend is read-only.
	 */
	int		(*track_from_pulses)(struct sim_media *media,
					     int track, int side,
					     uint32_t freq, int rpm,
					     const uint32_t *pulses,
					     size_t cnt);

	/* Number of formatted tracks per side. */
	int		(*tracks)(struct sim_media *media);

	/* One-line description for "status" output. */
	void		(*describe)(struct sim_media *media, char *buf,
				    size_t buflen);
};

struct sim_media {
	const struct sim_media_ops	*ops;
	char				*path;
	bool				wp;	/* write-protected */
	bool				dirty;
};

/* Load media from "path", choosing a backend by file extension. */
extern struct sim_media *sim_media_load(const char *path);

/* Save (if dirty) and unload. Returns 0, or -1 if the save failed. */
extern int sim_media_eject(struct sim_media *media);

#endif
