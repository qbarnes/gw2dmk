#ifndef DMK_H
#define DMK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "misc.h"


/* Some constants for DMK format */
#define DMK_WRITEPROT		0
#define DMK_NTRACKS		1
#define DMK_TRACKLEN		2
#define DMK_TRACKLEN_SIZE	2
#define DMK_OPTIONS		4
#define DMK_FORMAT		0x0c
#define DMK_FORMAT_SIZE		4
#define DMK_HDR_SIZE		0x10
#define DMK_TKHDR_SIZE		0x80    /* Space reserved for IDAM pointers */

#define DMK_SIDES		2
#define DMK_MAX_SECTORS		64
#define DMK_MAX_TRACKS		88


/* Conventional track lengths used by DMK's emulator, plus one for
 * 3.5" HD.  These are okay for images formatted by the emulator, but
 * they are a little too small for cw2dmk when reading a real disk,
 * because the real disk may have been written in a drive whose motor
 * was 1% slow or so.  The "nominal" value in the comments is the
 * ideal track length assuming that the drive motor and the data clock
 * are both exactly at their specified speeds.
 */

#define DMKI_TRACKLEN_5SD  0x0cc0 /* DMK_TKHDR_SIZE + 3136 (nominal 3125) */
#define DMKI_TRACKLEN_5    0x1900 /* DMK_TKHDR_SIZE + 6272 (nominal 6250) */
#define DMKI_TRACKLEN_8SD  0x14e0 /* DMK_TKHDR_SIZE + 5216 (nominal 5208) */
#define DMKI_TRACKLEN_8    0x2940 /* DMK_TKHDR_SIZE + 10432 (nominal 10416) */
#define DMKI_TRACKLEN_3HD  0x3180 /* DMK_TKHDR_SIZE + 12544 (nominal 12500) */


/* Track lengths for reads from media.  Allows for 2% slower than nominal
 * drive.  The values are rounded up to a multiple of 32 as the DMK
 * values were (just for paranoia's sake, in case someone was
 * depending on that).  The user will still have to specify a longer
 * track explicitly to read Atari 800 disks made in a 288 RPM (4% slow) drive.
 */

#define DMKRD_TRACKLEN_5SD  0x0d00 /* DMK_TKHDR_SIZE + 3200 (nominal 3125) */
#define DMKRD_TRACKLEN_5    0x1980 /* DMK_TKHDR_SIZE + 6400 (nominal 6250) */
#define DMKRD_TRACKLEN_8SD  0x1560 /* DMK_TKHDR_SIZE + 5344 (nominal 5208) */
#define DMKRD_TRACKLEN_8    0x2A40 /* DMK_TKHDR_SIZE + 10688 (nominal 10416) */
#define DMKRD_TRACKLEN_3HD  0x3260 /* DMK_TKHDR_SIZE + 12768 (nominal 12500) */
#define DMKRD_TRACKLEN_MIN  DMKRD_TRACKLEN_5SD
#define DMKRD_TRACKLEN_MAX  DMKRD_TRACKLEN_3HD


/* Bit assignments in options */
#define DMK_SSIDE_OPT	0x10
#define DMK_RX02_OPT	0x20    /* DMK extension, set if -e3 */
#define DMK_SDEN_OPT	0x40
#define DMK_IGNDEN_OPT	0x80	/* Obsolete flag, unused */

/* Bit assignments in IDAM pointers */
#define DMK_DDEN_FLAG	0x8000
#define DMK_EXTRA_FLAG	0x4000  /* unused */
#define DMK_IDAMP_BITS	0x3fff

// XXX Should these get a DMK_ prefix?
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


enum dmk_encoding_mode {
	MIXED  = 0,
	FM     = 1,
	MFM    = 2,
	RX02   = 3,
	N_ENCS
};


struct dmk_disk_stats {
	int		retries_total;
	int		good_sectors_total;

	int		errcount_total;
	int		enc_count_total[N_ENCS];

	int		err_tracks;
	int		good_tracks;

	bool		flippy;
};


struct dmk_track_stats {
	int		good_sectors;

	int		errcount;
	int		bad_sectors;
	int		reused_sectors;

	int		enc_sec[DMK_MAX_SECTORS];
	int		enc_count[N_ENCS];
};


struct dmk_header {
	uint8_t		writeprot;
	uint8_t		ntracks;
	uint16_t	tracklen;
	uint8_t		options;
	uint8_t		quirks;
	uint8_t		padding[6];
	uint32_t	real_format;
};


struct dmk_track {
	uint16_t	track_len;

	/* Anonymous union to let us avoid casting. */
	// XXX Eventually work at eliminating use of "track".
	union {
		/* Worst case working size, use track_len for actual size. */
		uint8_t		track[DMKRD_TRACKLEN_MAX];
		struct {
			uint16_t	idam_offset[DMK_MAX_SECTORS];
			uint8_t		data[DMKRD_TRACKLEN_MAX -
						DMK_TKHDR_SIZE];
		};
	};
};


struct dmk_file {
	struct dmk_header	header;
	struct dmk_track	track[DMK_MAX_TRACKS][DMK_SIDES];
};


extern void dmk_disk_stats_init(struct dmk_disk_stats *dds);

extern void dmk_track_stats_init(struct dmk_track_stats *dts);

extern void dmk_header_init(struct dmk_header *dmkh, uint8_t tracks,
			    uint16_t tracklen);

extern bool dmk_header_fread(struct dmk_header *dmkh, FILE *fp);

extern bool dmk_header_fwrite(const struct dmk_header *dmkh, FILE *fp);

extern long dwk_track_file_offset(struct dmk_header *dmkh, int track, int side);

int dmk_track_fseek(struct dmk_header *dmkh, int track, int side, FILE *fp);

extern bool dmk_track_fread(const struct dmk_header *dmkh,
			    struct dmk_track *trk, FILE *fp);

extern bool dmk_track_fwrite(const struct dmk_header *dmkh,
			     const struct dmk_track *trk, FILE *fp);

extern void dmk_data_rotate(struct dmk_track *trk, uint8_t *data_hole);

extern uint16_t dmk_track_length_optimal(const struct dmk_file *dmkf);

extern int fp2dmk(FILE *fp, struct dmk_file *dmkf);

extern int dmk2fp(struct dmk_file *dmkf, FILE *fp);


#ifdef __cplusplus
}
#endif

#endif
