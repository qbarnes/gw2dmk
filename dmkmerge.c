#include "dmkmerge.h"


/*
 * Get pointer to sector N in rotational order.
 */

static uint8_t *
dmk_get_phys_sector(uint8_t *track, int n)
{
	if (n < 0 || n >= DMK_MAX_SECTORS)
		return NULL;

	uint16_t off = ((uint16_t *) track)[n] & DMK_IDAMP_BITS;

	/* IDAM offsets skip over IDAM offset table so can never be 0. */
	if (off == 0)
		return NULL;

	/* Filter out bogus offset.  The mininum valid sector size here
	 * is really only avoiding very bad situations. */
	if (off > DMK_TRACKLEN_MAX - 10)
		return NULL;

	return track + off;
}


/*
 * Get length of sector N in rotational order.
 */

#if 0
#include <stdio.h>
#endif

static int
dmk_get_phys_sector_len(uint8_t *track, int n, int tracklen)
{
	uint8_t *s0 = dmk_get_phys_sector(track, n);
	uint8_t *s1 = dmk_get_phys_sector(track, n + 1);

	if (!s0)
		return -1;

	if (s1) {
		if (s0 >= s1) {
#if 0
			printf("\nphysical sector misordering from %d to %d .. "
			       "off %d off %d max %d!\n",
			       n, n + 1, s0 - track, s1 - track,
			       DMK_TRACKLEN_MAX);
			for (int i = 0; i < 10; i++)
				printf("%x ", ((uint16_t *) track)[i]);
			printf("\n");
			if (track == dmk_tmp_track)
				printf("TMP track\n");
			if (track == dmk_track)
				printf("common track\n");
			if (track == dmk_merged_track)
				printf("merged track\n");
#endif
			return -1;
		}

		return s1 - s0;
	}

	return tracklen - (s0 - (track + DMK_TKHDR_SIZE));
}


static int
dmk_get_sector_num(uint8_t *secdata)
{
	/* Single density repeats every byte in DMK format.  If we see
	 * a repeat of the sector ID then we know the sector number is
	 * at twice the offset.
	 */

	return secdata[secdata[1] == 0xfe ? 6 : 3];
}


static int
copy_preamble(uint8_t **dst, uint8_t *track)
{
	uint8_t *pre_end = dmk_get_phys_sector(track, 0);

	if (!pre_end || pre_end <= track + DMK_TKHDR_SIZE)
		return 0;

	int pre_len = pre_end - (track + DMK_TKHDR_SIZE);

	memcpy(*dst, track + DMK_TKHDR_SIZE, pre_len);
	*dst += pre_len;

	return 1;
}


/*
 * Go over the track we have read and replace any bad sectors with sectors
 * from any previous read attempts.
 *
 * Takes a very simple-minded approach and cannot cope with a situation
 * where sectors appear to be missing because of damage to the IDAM or DAM
 * headers.
 */

void
merge_sectors(struct dmk_track *trk_merged,
	      struct dmk_track_stats *trk_merged_stats,
	      struct dmk_track *trk_working,
	      struct dmk_track_stats *trk_working_stats)
{
	uint8_t *dmk_merged_track = trk_merged->track;
	uint8_t *dmk_track        = trk_working->track;

	int tracklen = trk_working->track_len - DMK_TKHDR_SIZE;

	uint16_t *idam_p = trk_working->idam_offset;
	uint16_t *merged_idam_p = trk_merged->idam_offset;
	uint8_t *dmk_sec;
	int overflow = 0;
	int best_errcount;
	int best_repair;

	enum Pick { Merged, Current, Tmp } best;

	/* As a special case, use the track as-is if it read without error. */
	if (trk_working_stats->errcount == 0) {
		*trk_merged = *trk_working;
		*trk_merged_stats = *trk_working_stats;
		trk_merged_stats->reused_sectors = 0;
		return;
	}

	struct dmk_track tmp_track;
	uint8_t *dmk_tmp_track = tmp_track.track;

	memset(dmk_tmp_track, 0, DMK_TKHDR_SIZE);
	uint8_t *tmp_data_p  = dmk_tmp_track + DMK_TKHDR_SIZE;
	uint16_t *tmp_idam_p = tmp_track.idam_offset;

	struct dmk_track_stats tmp_stat;
	tmp_stat = *trk_working_stats;
	tmp_stat.reused_sectors = 0;

	for (int cur = 0;
	     (dmk_sec = dmk_get_phys_sector(dmk_track, cur));
	     cur++) {
		int replaced = 0;
		/* Bad sector?  See if we can find a replacement. */
		if (idam_p[cur] & DMK_EXTRA_FLAG) {
			int secnum = dmk_get_sector_num(dmk_sec), prev;
			uint8_t *prev_sec;
			for (prev = 0;
			     (prev_sec =
			      dmk_get_phys_sector(dmk_merged_track, prev));
			     prev++) {
				int seclen =
				    dmk_get_phys_sector_len(dmk_merged_track,
						    prev,
						    trk_merged->track_len);
				if (dmk_get_sector_num(prev_sec) != secnum)
					continue;

				/* Ignore previous sector if it had an error. */
				if (merged_idam_p[prev] & DMK_EXTRA_FLAG)
					continue;

				/* The very first sector needs the pre-amble
				 * copied over, too.  We only understand this
				 * if the first sector is replacing the first
				 * sector.  If not, then we skip because best
				 * not create bogus data. */
				if (cur == 0) {
					if (prev != 0)
						continue;

					if (!copy_preamble
					    (&tmp_data_p, dmk_merged_track))
						continue;
				}
				// Don't overflow the merged track.
				if (seclen <= 0
				    || tmp_data_p + seclen >
				    dmk_tmp_track + DMK_TRACKLEN_MAX)
					continue;

				msg(MSG_ERRORS, "[reuse %02x] ", secnum);

				*tmp_idam_p++ =
				    (merged_idam_p[prev] & ~DMK_IDAMP_BITS) |
				    ((tmp_data_p -
				      dmk_tmp_track) & DMK_IDAMP_BITS);

				memcpy(tmp_data_p, prev_sec, seclen);
				tmp_data_p += seclen;
				replaced = 1;
				tmp_stat.reused_sectors++;
				tmp_stat.enc_sec[cur] =
				    trk_merged_stats->enc_sec[cur];
				tmp_stat.enc_count[trk_merged_stats->enc_sec[cur]]++;
				/* There should be an error for every bad
				 * sector, but just to be careful. */
				if (tmp_stat.errcount > 0)
					tmp_stat.errcount--;
				break;
			}
		}

		if (!replaced) {
			/* Copy the sector we have whether it be a good or
			 * bad read. */
			int seclen =
			    dmk_get_phys_sector_len(dmk_track, cur, tracklen);

			/* Need to copy preamble if we are the first sector. */
			if (cur == 0 && !copy_preamble(&tmp_data_p, dmk_track))
				overflow = 1;
			else if (seclen < 0
				 || tmp_data_p + seclen >
				 dmk_tmp_track + DMK_TRACKLEN_MAX)
				overflow = 1;
			else {
				*tmp_idam_p++ =
				    (idam_p[cur] & ~DMK_IDAMP_BITS) |
				    ((tmp_data_p -
				      dmk_tmp_track) & DMK_IDAMP_BITS);
				memcpy(tmp_data_p, dmk_sec, seclen);
				tmp_data_p += seclen;
			}
		}
	}

	/* dmk_tmp_track has tmp_stat.errcount errors
	 * (or is unusable if overflow is set).
	 * dmk_merged_track has trk_merged_stats->errcount errors.
	 * dmk_track has errcount errors. */

	/* We want to keep the best as determined by the lowest error
	 * count and that will become our merged track. */

	best = Current;
	best_errcount = trk_working_stats->errcount;
	best_repair = 0;

	/* overflow means that the candidate merged track tmp is not viable. */
	if (!overflow && tmp_stat.errcount < best_errcount) {
		best = Tmp;
		best_errcount = tmp_stat.errcount;
		best_repair = tmp_stat.reused_sectors;
	}

	/* If we have a previous merged track, it may still be the best.
	 * Especially if it has fewer repairs. */
	if (trk_merged->track_len > 0) {
		if (trk_merged_stats->errcount < best_errcount ||
		    (trk_merged_stats->errcount == best_errcount &&
		     trk_merged_stats->reused_sectors < best_repair)) {
			best = Merged;
			best_errcount = trk_merged_stats->errcount;
			best_repair = trk_merged_stats->reused_sectors;
		}
	}

	//msg(MSG_ERRORS, "(%d,%d,%d) ", errcount, tmp_stat.errcount,
	//    trk_merged_stats->errcount);

	switch (best) {
	default:
	case Current:
		msg(MSG_ERRORS, "[using current] ");
		*trk_merged = *trk_working;
		*trk_merged_stats = *trk_working_stats;
		trk_merged_stats->reused_sectors = 0;
		break;

	case Tmp:
		msg(MSG_ERRORS, "[using merged] ");
		*trk_merged = tmp_track;
		*trk_merged_stats = tmp_stat;
		break;

	case Merged:
		msg(MSG_ERRORS, "[using previous] ");
		break;
	}

	trk_merged_stats->errcount = best_errcount;
}
