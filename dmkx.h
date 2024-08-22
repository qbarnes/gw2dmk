#ifndef DMKX_H
#define DMKX_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
//#include <stdio.h>
//#include <string.h>

#include "dmk.h"
#include "msg.h"
#include "msg_levels.h"
#include "secsize.h"
#include "gwencode.h"	// XXX abstraction violation?


struct encode_bit {
	uint32_t	freq;
	double		mult;
	double		precomp;	/* ns */
	double		prev_err;
	double		prev_adj;
	double		adj;
	bool		dither;
	int		len;
	int		next_len;
	int		extra_bytes;
};


struct extra_track_info {
	int		track;
	int		track_len;
	int		side;
	int		max_sides;
	int		fmtimes;
	int		iam_pos;
	int		rx02;
	int		extra_bytes;
	int		fill;
	uint8_t		quirks;
	double		precomp;
};


extern void encode_bit_init(struct encode_bit *ebs, uint32_t freq, double mult);

extern int dmk2pulses(struct dmk_track *dmkt, struct extra_track_info *eti,
		      struct encode_bit *ebs, struct dmk_encode_s *des);

#ifdef __cplusplus
}
#endif

#endif
