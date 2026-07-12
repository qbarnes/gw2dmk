#include <stdlib.h>
#include <string.h>

#include "greaseweazle.h"
#include "gwx.h"

#include "simflux.h"


void
sim_pulses_total(struct sim_pulses *sp)
{
	uint64_t	total = 0;

	for (size_t i = 0; i < sp->cnt; ++i)
		total += sp->p[i];

	sp->total_ticks = total;
}


int
sim_noise_pulses(uint32_t freq, int rpm, struct sim_pulses *sp)
{
	uint64_t	rev_ticks = (uint64_t)(freq * 60.0 / rpm);

	/* Random transitions 2us..30us apart. */
	uint32_t	min_t   = 2e-6 * freq;
	uint32_t	range_t = 28e-6 * freq;

	size_t		len = rev_ticks / min_t + 1;
	uint32_t	*p  = malloc(len * sizeof(*p));

	if (!p)
		return -1;

	static uint32_t	seed = 20260711;

	uint64_t	total = 0;
	size_t		cnt   = 0;

	while (total < rev_ticks && cnt < len) {
		seed = seed * 1664525 + 1013904223;	/* LCG */

		uint32_t t = min_t + (uint64_t)seed * range_t / 4294967296u;

		if (total + t > rev_ticks)
			t = rev_ticks - total;

		if (t == 0)
			break;

		p[cnt++] = t;
		total	+= t;
	}

	sp->p		= p;
	sp->cnt		= cnt;
	sp->total_ticks	= total;

	return 0;
}


/* Dynamically growing output stream. */
struct sbuf {
	uint8_t		*b;
	size_t		cnt;
	size_t		len;
};


static int
sbuf_room(struct sbuf *sb, size_t need)
{
	if (sb->cnt + need <= sb->len)
		return 0;

	size_t	nlen = sb->len ? sb->len * 2 : 65536;

	while (nlen < sb->cnt + need)
		nlen *= 2;

	uint8_t	*nb = realloc(sb->b, nlen);

	if (!nb)
		return -1;

	sb->b	= nb;
	sb->len	= nlen;

	return 0;
}


/*
 * Emit one inter-transition interval.  Same encoding as the
 * firmware's read streams: direct byte, two-byte extended, or a
 * FLUXOP_SPACE followed by a final short pulse.  (FLUXOP_ASTABLE is
 * a write-stream opcode; it never appears in read streams.)
 */

static int
sbuf_pulse(struct sbuf *sb, uint64_t ticks)
{
	if (sbuf_room(sb, 8) == -1)
		return -1;

	if (ticks == 0)
		return 0;

	if (ticks < 250) {
		sb->b[sb->cnt++] = ticks;
	} else if (ticks < 250 + 5 * 255) {
		sb->b[sb->cnt++] = 250 + (ticks - 250) / 255;
		sb->b[sb->cnt++] = 1 + (ticks - 250) % 255;
	} else {
		sb->b[sb->cnt++] = 255;
		sb->b[sb->cnt++] = FLUXOP_SPACE;
		gw_write_28(ticks - 249, &sb->b[sb->cnt]);
		sb->cnt += 4;
		sb->b[sb->cnt++] = 249;
	}

	return 0;
}


static int
sbuf_index(struct sbuf *sb, uint64_t ticks_to_index)
{
	if (sbuf_room(sb, 6) == -1)
		return -1;

	sb->b[sb->cnt++] = 255;
	sb->b[sb->cnt++] = FLUXOP_INDEX;
	gw_write_28(ticks_to_index, &sb->b[sb->cnt]);
	sb->cnt += 4;

	return 0;
}


int
sim_flux_stream(const struct sim_pulses *sp, uint64_t phase,
		unsigned max_index, uint64_t max_ticks,
		uint8_t **out, size_t *out_cnt, uint64_t *dur_ticks)
{
	const uint64_t	rev = sp->total_ticks;

	if (rev == 0 || sp->cnt == 0)
		return -1;

	if (max_index == 0 && max_ticks == 0)
		max_index = 2;

	phase %= rev;

	struct sbuf	sb = { NULL, 0, 0 };

	/*
	 * Walk transitions in unrolled time.  "last" is the absolute
	 * angle of the previous transition (or of stream start);
	 * "base"+"cum" track the current revolution.  Index pulses
	 * occur at every multiple of "rev" and do not advance the
	 * sample cursor.
	 */

	uint64_t	base = 0;
	uint64_t	cum  = 0;
	size_t		j    = 0;

	/* Skip transitions at or before the starting phase. */
	while (j < sp->cnt && cum + sp->p[j] <= phase)
		cum += sp->p[j++];

	uint64_t	last	   = phase;
	uint64_t	next_index = rev;
	unsigned	idx	   = 0;

	for (;;) {
		if (j == sp->cnt) {
			base += rev;
			cum   = 0;
			j     = 0;
		}

		uint64_t	t_abs = base + cum + sp->p[j];

		if (max_index && next_index <= t_abs) {
			if (sbuf_index(&sb, next_index - last) == -1)
				goto err;

			if (++idx >= max_index)
				break;

			next_index += rev;
			continue;
		}

		if (sbuf_pulse(&sb, t_abs - last) == -1)
			goto err;

		last = t_abs;
		cum += sp->p[j];
		++j;

		if (max_index == 0 && last - phase >= max_ticks)
			break;
	}

	if (sbuf_room(&sb, 1) == -1)
		goto err;

	sb.b[sb.cnt++] = 0;	/* end of stream */

	*out	   = sb.b;
	*out_cnt   = sb.cnt;
	*dur_ticks = last - phase;

	return 0;

err:
	free(sb.b);

	return -1;
}
