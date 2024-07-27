#include "dmk.h"


void
dmk_disk_stats_init(struct dmk_disk_stats *dds)
{
	*dds = (struct dmk_disk_stats){};
}


void
dmk_track_stats_init(struct dmk_track_stats *dts)
{
	*dts = (struct dmk_track_stats){};
}


void
dmk_header_init(struct dmk_header *dmkh,
		uint8_t tracks,
		uint16_t tracklen)
{
	*dmkh = (struct dmk_header){
		.writeprot   = 0,
		.ntracks     = tracks,
		.tracklen    = tracklen,
		.options     = 0,
		.real_format = 0
	};
}


int
dmk_header_fwrite(FILE *fp, struct dmk_header *dmkh)
{
	// XXX need error checking

	fseek(fp, 0, SEEK_SET);
	fwrite(&dmkh->writeprot, sizeof(dmkh->writeprot), 1, fp);
	fwrite(&dmkh->ntracks, sizeof(dmkh->ntracks), 1, fp);

	uint16_t tracklen = htole16(dmkh->tracklen);
	fwrite(&tracklen, sizeof(tracklen), 1, fp);

	fwrite(&dmkh->options, sizeof(dmkh->options), 1, fp);
	fwrite(dmkh->padding, sizeof(dmkh->padding), 1, fp);

	uint32_t format = htole32(dmkh->real_format);
	fwrite(&format, sizeof(format), 1, fp);

	return 0;
}


long
dwk_track_file_offset(struct dmk_header *dmkh, int track, int side)
{
	return DMK_HDR_SIZE +
		((track * (2 - !!(dmkh->options & DMK_SSIDE_OPT))) + side) *
			dmkh->tracklen;
}


int
dmk_track_fseek(FILE *fp,
		struct dmk_header *dmkh,
		int track, int side)
{
	return fseek(fp, dwk_track_file_offset(dmkh, track, side), SEEK_SET);
}


size_t
dmk_track_fwrite(FILE *fp,
		 struct dmk_header *dmkh,
		 struct dmk_track *trk)
{
	size_t	fwsz = 0;

	for (int i = 0; i < DMK_MAX_SECTORS; ++i) {
		uint16_t	idam_off = htole16(trk->idam_offset[i]);

		fwsz += fwrite(&idam_off, sizeof(idam_off), 1, fp);
	}

	return fwsz + fwrite(trk->track + DMK_TKHDR_SIZE,
				dmkh->tracklen - DMK_TKHDR_SIZE, 1, fp);
}


void
dmk_data_rotate(struct dmk_track *trk, uint8_t *data_hole)
{
	if (!data_hole)
		return;

	uint16_t data_len      = trk->track_len - DMK_TKHDR_SIZE;
	uint8_t	*data_end      = trk->data + data_len;
	uint16_t rotate_amount = data_hole - trk->data;
	uint16_t rotate_size   = data_end - data_hole;

	if ((data_hole < trk->data) || (data_hole >= data_end))
		return;

	if (rotate_amount == 0)
		return;

	struct dmk_track	tmp_trk = { .track_len = trk->track_len };

	/*
	 * Rotate the data.
	 */

	for (int i = 0; i < data_len; ++i)
		tmp_trk.data[i] = trk->data[(i + rotate_amount) % data_len];

	/*
	 * Rotate and adjust the IDAM pointers to account for
	 * the data rotation.
	 */

	int idam_rotate = 0;

	for (int i = 0; i < DMK_MAX_SECTORS && trk->idam_offset[i]; ++i) {
		if ((trk->idam_offset[i] & DMK_IDAMP_BITS) - DMK_TKHDR_SIZE >=
		    rotate_amount) {
			idam_rotate = i;
			break;
		}
	}

	if (idam_rotate == 0) {
		for (int i = 0;
		     i < DMK_MAX_SECTORS && trk->idam_offset[i]; ++i) {
			tmp_trk.idam_offset[i] =
				trk->idam_offset[i] + rotate_size;
		}
	} else {
		int	i = idam_rotate;

		for (; i < DMK_MAX_SECTORS && trk->idam_offset[i]; ++i) {
			tmp_trk.idam_offset[i - idam_rotate] =
				trk->idam_offset[i] - rotate_amount;
		}

		int	idam_moved = i - idam_rotate;

		for (int i = 0; i < idam_rotate; ++i) {
			tmp_trk.idam_offset[i + idam_moved] =
				trk->idam_offset[i] + rotate_size;
		}
	}

	//memcpy(trk, &tmp_trk,
	//		offsetof(track) + DMK_TKHDR_SIZE + data_len);
	*trk = tmp_trk;
}


/*
 * Return the optimal DMK header track length for the entire DMK.
 */

uint16_t
dmk_track_length_optimal(const struct dmk_file *dmkf)
{
	uint16_t	max_trk_len = 0;

	int sides = 2 - !!(dmkf->header.options & DMK_SSIDE_OPT);

	for (int t = 0; t < dmkf->header.ntracks; ++t) {
		for (int s = 0; s < sides; ++s) {
			uint16_t	trk_len = dmkf->track[t][s].track_len;

			if (trk_len > max_trk_len)
				max_trk_len = trk_len;
		}
	}

	return max_trk_len;
}


/*
 * Write out the dmk_file data structure as a DMK to file stream fp.
 */

int
dmk2fp(struct dmk_file *dmkf, FILE *fp)
{
	// XXX Error checking

	fseek(fp, 0, SEEK_SET);

	dmk_header_fwrite(fp, &dmkf->header);

	int sides = 2 - !!(dmkf->header.options & DMK_SSIDE_OPT);

	for (int t = 0; t < dmkf->header.ntracks; ++t) {
		for (int s = 0; s < sides; ++s) {
			dmk_track_fwrite(fp, &dmkf->header, &dmkf->track[t][s]);
		}
	}

	return 0;
}


#if 0
uint8_t
dmk_tracks_get

uint8_t
dmk_tracks_set

dmk_track_length_get

dmk_track_length_set

uint8_t
dmk_option_flags_get

uint8_t
dmk_option_flags_set

dmk_track_write

dmk_track_read

dmk_track_merge
#endif
