#ifndef SIMFLUX_H
#define SIMFLUX_H

#include <stddef.h>
#include <stdint.h>

/*
 * Flux stream synthesis for CMD_READ_FLUX.
 *
 * A pulse train is one full revolution of inter-transition tick
 * counts; total_ticks is their sum and defines the revolution
 * period (and thus the RPM the host will measure).
 */

struct sim_pulses {
	uint32_t	*p;
	size_t		cnt;
	uint64_t	total_ticks;
};

/* Sum pulse ticks into total_ticks. */
extern void sim_pulses_total(struct sim_pulses *sp);

/*
 * Random flux for an unformatted surface (or no data on this head).
 * Fills one nominal revolution at "rpm".  Returns 0, or -1 on error.
 */
extern int sim_noise_pulses(uint32_t freq, int rpm,
			    struct sim_pulses *sp);

/*
 * Build a Greaseweazle read stream from the pulse train.
 *
 * "phase" is how far (in ticks) the media has rotated past the
 * index at stream start.  The stream ends after "max_index" index
 * pulses, or after "max_ticks" ticks if max_index is 0, and is
 * always terminated with a 0x00 byte.
 *
 * Returns 0 with a malloc'd stream in *out (caller frees) and the
 * stream's duration in ticks in *dur_ticks, or -1 on error.
 */
extern int sim_flux_stream(const struct sim_pulses *sp, uint64_t phase,
			   unsigned max_index, uint64_t max_ticks,
			   uint8_t **out, size_t *out_cnt,
			   uint64_t *dur_ticks);

#endif
