#ifndef GWDECODE_H
#define GWDECODE_H

#ifdef __cplusplus
extern "C" {
#endif

// All needed?
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>

#include "crc.h"
#include "gw.h"
#include "gwx.h"
#include "gwmedia.h"
#include "dmk.h"
#include "msg_levels.h"
#include "msg.h"


/* Bit assignments in quirks byte */
#define QUIRK_ID_CRC		0x01
#define QUIRK_DATA_CRC		0x02
#define QUIRK_PREMARK		0x04
#define QUIRK_EXTRA		0x08
#define QUIRK_EXTRA_CRC		0x10
#define QUIRK_EXTRA_DATA	0x20
#define QUIRK_IAM		0x40
#define QUIRK_MFM_CLOCK		0x80
#define QUIRK_ALL		0xff


/*
 * Flux decoder
 *
 * XXX Anything converting flux to bytes goes here.
 */

struct fdecoder
{
	uint32_t	sample_freq;
	uint32_t	gw_ticks;

	uint64_t	accum;
	uint64_t	taccum;		/* Transform accumulator for RX02 */
	int		bit_cnt;
	int		premark;
	unsigned int	quirk;
	int		reverse_sides;

	int		usr_encoding;	/* From user args. */
	int		first_encoding;	/* Encoding to try on the next track */
	int		cur_encoding;
	int		mark_after;
	int		sizecode;
	int		secsize;
	int		prev_secsize;

	bool		awaiting_iam;
	bool		awaiting_dam;

	/* bit counter, >0 if we may be in a write splice */
	int		write_splice;

	int		backward_am;
	int		flippy;
	int		index_edge;

	int		use_hole;	/* From user args. */

	uint8_t		curcyl;
	uint8_t		cyl_seen;
	uint8_t		cyl_prev_seen;

	uint16_t	crc;

	int		ibyte;		/* Index byte count     */
	int		dbyte;		/* Data byte count      */
	int		ebyte;		/* Extra CRC byte count */
};


/*
 * DMK state machine
 *
 * Anything above byte level goes here.
 */

struct dmk_track_sm {
	struct dmk_disk_stats	*dds;
	struct dmk_header	*header;
	struct dmk_track	*trk_merged;
	struct dmk_track_stats	*trk_merged_stats;

	uint16_t		*idam_p;
	uint8_t			*track_data_p;
	uint8_t			*track_hole_p;

	struct dmk_track	trk_working;
	struct dmk_track_stats	trk_working_stats;

	int			valid_id;

	int			dmk_ignored;
	int			dmk_full;

	/* From user args. */
	int			dmk_iam_pos;
	int			dmk_ignore;
	int			accum_sectors;

};


struct flux2dmk_sm {
	struct fdecoder		fdec;
	struct dmk_track_sm	dtsm;
};


extern const char *encoding_name(int encoding);

extern void fdecoder_init(struct fdecoder *fdec, uint32_t sample_freq);

extern void dmk_track_sm_init(struct dmk_track_sm *dtsm,
			      struct dmk_disk_stats *dds,
			      struct dmk_header *dmkh,
			      struct dmk_track *trk_merged,
			      struct dmk_track_stats *trk_merged_stats);

extern void gwflux_decode(uint32_t pulse,
			  struct gw_media_encoding *gme, 
			  struct flux2dmk_sm  *fdec);

extern void gw_decode_flush(struct flux2dmk_sm *f2dsm);

extern void gw_post_process_track(struct flux2dmk_sm *f2dsm);


#ifdef __cplusplus
}
#endif

#endif
