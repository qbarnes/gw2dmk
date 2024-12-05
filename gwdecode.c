#include "gwdecode.h"

#define	WRITE_SPLICE	32


const char *
encoding_name(int encoding)
{
	static const char *enc_name[] = {
		"autodetect",
		"FM",
		"MFM",
		"RX02"
	};

	return (encoding >= 0 && encoding < COUNT_OF(enc_name)) ?
		enc_name[encoding] : "unknown";
}


void
fdecoder_init(struct fdecoder *fdec,
	      uint32_t sample_freq)
{
	*fdec = (struct fdecoder){
		.sample_freq = sample_freq,

		.accum = 0,
		.taccum = 0,
		.bit_cnt = 0,
		.premark = 0,
		.quirk = 0,
		.reverse_sides = 0,

		.usr_encoding = MIXED,
		.first_encoding = MIXED,
		.cur_encoding = MIXED,

		.mark_after = -1,
		.sizecode = 0,
		.maxsecsize = 3,

		.awaiting_iam = false,
		.awaiting_dam = false,

		.write_splice = 0,

		.backward_am = 0,
		.flippy = 0,

		.use_hole = true,
		.quirk = 0x00,

		.curcyl = 0,
		.cyl_seen = -1,
		.cyl_prev_seen = -1,

		.crc = 0,

		.ibyte = -1,
		.dbyte = -1,
		.ebyte = -1,

		.index_edge = 0,
		.revs_seen = 0,
		.total_ticks = 0,
		.index = { ~0, ~0 }
	};
}


void
dmk_track_sm_init(struct dmk_track_sm *dtsm,
		  struct dmk_disk_stats *dds,
		  struct dmk_header *dmkh,
		  struct dmk_track *trk_merged,
		  struct dmk_track_stats *trk_merged_stats)
{
	*dtsm = (struct dmk_track_sm){
		.dds              = dds,
		.header           = dmkh,
		.trk_merged       = trk_merged,
		.trk_merged_stats = trk_merged_stats,

		.idam_p           = dtsm->trk_working.idam_offset,
		.track_data_p     = dtsm->trk_working.track + DMK_TKHDR_SIZE,
		.track_hole_p     = NULL,

		.dmk_ignored      = 0,
		.dmk_full         = 0,

		/* Should be from user args upon return. */
		.dmk_iam_pos      = -1,
		.dmk_ignore       = 0,
		.accum_sectors    = 1
	};
}


bool
dmk_idam_list_empty(struct dmk_track_sm *dtsm)
{
	return dtsm->idam_p == dtsm->trk_working.idam_offset;
}


/* True if we are ignoring data while waiting for an iam or for the
 * first idam */

int
dmk_awaiting_track_start(struct flux2dmk_sm *f2dsm)
{
	struct fdecoder *fdec	  = &f2dsm->fdec;
	struct dmk_track_sm *dtsm = &f2dsm->dtsm;

	return (dtsm->dmk_iam_pos == -1) ?
		!fdec->use_hole && dmk_idam_list_empty(dtsm) :
		fdec->awaiting_iam;
}


int
dmk_in_range(struct flux2dmk_sm *f2dsm)
{
	struct fdecoder *fdec	  = &f2dsm->fdec;
	struct dmk_track_sm *dtsm = &f2dsm->dtsm;

	if (dtsm->dmk_full) return 0;

	if (dtsm->dmk_ignored < dtsm->dmk_ignore) {
		dtsm->dmk_ignored++;
		return 0;
	}

	/* Stop at leading edge of last index unless in sector data. */
	if (fdec->use_hole &&
	    fdec->index_edge >= 3 &&
	    fdec->dbyte == -1 &&
	    fdec->ebyte == -1) {
		msg(MSG_HEX, "[index edge %d] ", fdec->index_edge);
		dtsm->dmk_full = 1;
		return 0;
	}

	return 1;
}


/*
 * Write out a byte to the DMK.
 * If DMK is single density, write out a duplicate byte.
 */

void
dmk_data(struct flux2dmk_sm *f2dsm, unsigned char byte, int encoding)
{
	struct fdecoder *fdec     = &f2dsm->fdec;
	struct dmk_track_sm *dtsm = &f2dsm->dtsm;

	if (!dmk_in_range(f2dsm)) return;

	if (fdec->awaiting_dam && byte >= 0xf8 && byte <= 0xfd) {
		/* Kludge: DMK doesn't tag DAMs, so noise after the ID bytes
		 * but before the DAM must not have the data bit pattern of
		 * a DAM */
		byte = 0xf0;
	}

	if (dtsm->track_data_p - dtsm->trk_working.track <=
	    dtsm->header->tracklen - 2) {
		*dtsm->track_data_p++ = byte;
		if (encoding == FM &&
		    !(dtsm->header->options & DMK_SDEN_OPT)) {
			*dtsm->track_data_p++ = byte;
		}
	}

	dtsm->trk_working.track_len =
				dtsm->track_data_p - dtsm->trk_working.track;

#if 0
	// XXX With reallocing tracks, do we need this, or just max check?
	if (dtsm->track_data_p - dtsm->trk_working.track > dtsm->header->tracklen - 2) {
printf("dtsm->track_data_p = %p, dtsm->trk_working.track = %p, dtsm->header->tracklen = %d\n", dtsm->track_data_p, dtsm->trk_working.track, dtsm->header->tracklen);
exit(0);
	
		/* No room for more bytes after this one */
		msg(MSG_HEX, "[DMK track buffer full] ");
		dtsm->dmk_full = 1;
	}
#endif
}


void
dmk_idam(struct flux2dmk_sm *f2dsm, unsigned char byte, int encoding)
{
	struct fdecoder *fdec     = &f2dsm->fdec;
	struct dmk_track_sm *dtsm = &f2dsm->dtsm;

	if (!dmk_in_range(f2dsm)) return;

	if (!fdec->awaiting_iam && dmk_awaiting_track_start(f2dsm)) {
		/* In this mode, we position the first IDAM a nominal distance
		 * from the start of the track, to make sure that (1) the whole
		 * track will fit and (2) if dmk2gw is used to write the image
		 * back to a real disk, the first IDAM won't be too close to
		 * the index hole.  */
#define GAP1PLUS 48
		int bytesread = dtsm->track_data_p -
				(dtsm->trk_working.track + DMK_TKHDR_SIZE);

		if (bytesread < GAP1PLUS) {
			/* Not enough bytes read yet.  Move read bytes
			 * forward and add fill. */
			memmove(dtsm->trk_working.track + DMK_TKHDR_SIZE +
				GAP1PLUS - bytesread,
				dtsm->trk_working.track + DMK_TKHDR_SIZE,
				bytesread);
			memset(dtsm->trk_working.track + DMK_TKHDR_SIZE,
				(encoding == MFM) ? 0x4e : 0xff,
				GAP1PLUS - bytesread);
		} else {
			/* Too many bytes read.  Move last GAP1PLUS back
			 * and throw rest away. */
			memmove(dtsm->trk_working.track + DMK_TKHDR_SIZE,
				dtsm->trk_working.track + DMK_TKHDR_SIZE +
				bytesread - GAP1PLUS, GAP1PLUS);
		}

		dtsm->track_data_p = dtsm->trk_working.track +
					DMK_TKHDR_SIZE + GAP1PLUS;
	}

	fdec->awaiting_dam = 0;
	dtsm->valid_id = 0;
	unsigned short idamp = dtsm->track_data_p - dtsm->trk_working.track;
	if (encoding == MFM)
		idamp |= DMK_DDEN_FLAG;

	if (dtsm->track_data_p <
	    dtsm->trk_working.track + dtsm->header->tracklen) {
		if ((uint8_t *)dtsm->idam_p >=
		    dtsm->trk_working.track + DMK_TKHDR_SIZE) {
			msg(MSG_ERRORS, "[too many IDAMs on track] ");
			dtsm->trk_working_stats.errcount++;
		} else {
			if (dtsm->accum_sectors) {
				int	enc_idx = dtsm->idam_p -
					(uint16_t *)dtsm->trk_working.track;

				dtsm->trk_working_stats.enc_sec[enc_idx] =
					encoding;
			}

			*dtsm->idam_p++ = idamp;
			fdec->ibyte = 0;
			dmk_data(f2dsm, byte, encoding);
		}
	}
}


void
dmk_iam(struct flux2dmk_sm *f2dsm, unsigned char byte, int encoding)
{
	struct fdecoder *fdec	  = &f2dsm->fdec;
	struct dmk_track_sm *dtsm = &f2dsm->dtsm;

	if (!dmk_in_range(f2dsm)) return;

	if (dtsm->dmk_iam_pos >= 0) {
		/* If the user told us where to position the IAM...*/
		int bytesread = dtsm->track_data_p -
				(dtsm->trk_working.track + DMK_TKHDR_SIZE);

		if (fdec->awaiting_iam || dmk_idam_list_empty(dtsm)) {

			/* First IAM.  (Or a subsequent IAM with no IDAMs in
			 * between -- in the latter case, we assume the
			 * previous IAM(s) were garbage.)  Position the IAM
			 * as instructed.  This can result in data loss if
			 * an IAM appears somewhere in the middle of the
			 * track, unless the read was for twice the track
			 * length as the use_hole=0 (--nohole) flag sets it.
			 */

			int iam_pos = dtsm->dmk_iam_pos;
	 
			if (encoding == FM &&
			    !(dtsm->header->options & DMK_SDEN_OPT)) {
				iam_pos *= 2;
			}

			if (bytesread < iam_pos) {
				/* Not enough bytes read yet.  Move read
				 * bytes forward and add fill. */
				memmove(dtsm->trk_working.track +
					DMK_TKHDR_SIZE + iam_pos - bytesread,
					dtsm->trk_working.track +
					DMK_TKHDR_SIZE,
					bytesread);
				memset(dtsm->trk_working.track + DMK_TKHDR_SIZE,
				       (encoding == MFM) ? 0x4e : 0xff,
				       iam_pos - bytesread);
			} else {
				/* Too many bytes read.  Move last iam_pos
				 * back and throw rest away. */
				memmove(dtsm->trk_working.track + DMK_TKHDR_SIZE,
					dtsm->trk_working.track + DMK_TKHDR_SIZE +
					bytesread - iam_pos, iam_pos);
			}

			dtsm->track_data_p = dtsm->trk_working.track + DMK_TKHDR_SIZE +
						iam_pos;
			fdec->awaiting_iam = 0;
		} else {
			/* IAM that follows another IAM and one or more IDAMs.
			 * If we're >95% of the way around the track, assume
			 * it's actually the first one again and stop here.
			 * XXX This heuristic might be useful even when the
			 * user isn't having us position by IAM. */

			if (bytesread >
			    (dtsm->header->tracklen - DMK_TKHDR_SIZE)
			    * 95 / 100) {
				msg(MSG_IDS, "[stopping before second IAM] ");
				dtsm->dmk_full = 1;
				return;
			}
		}
	}

	fdec->awaiting_dam = 0;
	dtsm->valid_id = 0;
	dmk_data(f2dsm, byte, encoding);
}


void
check_missing_dam(struct flux2dmk_sm *f2dsm)
{
	struct fdecoder *fdec	  = &f2dsm->fdec;
	struct dmk_track_sm *dtsm = &f2dsm->dtsm;

	if (fdec->awaiting_dam)
		msg(MSG_ERRORS, "[missing DAM] ");
	else if (fdec->dbyte > 0)
		msg(MSG_ERRORS, "[incomplete sector data] ");
	else
		return;

	fdec->awaiting_dam = 0;
	dtsm->valid_id = 0;
	fdec->dbyte = fdec->ibyte = fdec->ebyte = -1;
	dtsm->trk_working_stats.errcount++;

	if (dtsm->accum_sectors)
		dtsm->idam_p[-1] |= DMK_EXTRA_FLAG;
}


int
dmk_check_wraparound(struct flux2dmk_sm *f2dsm)
{
	struct fdecoder *fdec	  = &f2dsm->fdec;
	struct dmk_track_sm *dtsm = &f2dsm->dtsm;

	/* Once we've read 95% of the track, if we see a sector ID that's
	 * identical to the first one we saw on the track, conclude that we
	 * wrapped around and are seeing the first one again, and
	 * retroactively ignore it.
	 */

	if (dtsm->track_data_p - dtsm->trk_working.track - DMK_TKHDR_SIZE <
	    (DMKRD_TRACKLEN_MIN - DMK_TKHDR_SIZE) * 95 / 100)
		return 0;

	uint16_t first_idamp = *dtsm->trk_working.idam_offset;
	uint16_t last_idamp  = *(dtsm->idam_p - 1);

	if (first_idamp == last_idamp) return 0;

	if ((first_idamp & DMK_DDEN_FLAG) != (last_idamp & DMK_DDEN_FLAG))
		return 0;

	int cmplen = ((first_idamp & DMK_DDEN_FLAG) ||
			(dtsm->header->options & DMK_SDEN_OPT)) ? 5 : 10;

	if (memcmp(&dtsm->trk_working.track[first_idamp & DMK_IDAMP_BITS],
		   &dtsm->trk_working.track[last_idamp & DMK_IDAMP_BITS],
		   cmplen) == 0) {

		msg(MSG_ERRORS, "[wraparound] ");
		*--dtsm->idam_p = 0;
		fdec->awaiting_dam = 0;
		fdec->ibyte = -1;
		dtsm->dmk_full = 1;
		return 1;
	}

	return 0;
}


int
mfm_valid_clock(uint64_t accum)
{
	/* Check for valid clock bits */
	unsigned int xclock = ~((accum >> 1) | (accum << 1)) & 0xaaaa;
	unsigned int clock = accum & 0xaaaa;

	if (xclock != clock) {
		//msg(MSG_ERRORS, "[clock exp %04x got %04x]", xclock, clock);
		return 0;
	}

	return 1;
}


int
change_encoding(struct fdecoder *fdec, int new_encoding)
{
	if (fdec->cur_encoding != new_encoding) {
		msg(MSG_ERRORS, "[%s->%s] ",
			encoding_name(fdec->cur_encoding),
			encoding_name(new_encoding));

		fdec->cur_encoding = new_encoding;
	}

	return fdec->cur_encoding;
}


/*
 * Main routine of the FM/MFM/RX02 decoder.  The input is a stream of
 * alternating clock/data bits, passed in one by one.  See decoder.txt
 * for documentation on how the decoder works.
 */

void
gwflux_decode_bit(struct flux2dmk_sm *f2dsm, int bit)
{
	struct fdecoder *fdec	  = &f2dsm->fdec;
	struct dmk_track_sm *dtsm = &f2dsm->dtsm;

	if (dtsm->dmk_full)
		return;

	fdec->accum  = (fdec->accum << 1) + bit;
	fdec->taccum = (fdec->taccum << 1) + bit;

	++fdec->bit_cnt;

	if (fdec->mark_after >= 0)  fdec->mark_after--;
	if (fdec->write_splice > 0) fdec->write_splice--;

	if (fdec->usr_encoding != MFM &&
	    fdec->bit_cnt >= 36 &&
	    !fdec->write_splice &&
	    (fdec->cur_encoding != MFM ||
	     (fdec->ibyte == -1 &&
	      fdec->dbyte == -1 &&
	      fdec->ebyte == -1 &&
	      fdec->mark_after == -1))) {

		switch (fdec->accum & 0xfffffffffULL) {
		case 0x8aa222a88ULL:  /* 0xfc / 0xc7: Quirky index address mark */
			if ((fdec->quirk & QUIRK_IAM) == 0) break;
			/* fall through */
		case 0x8aa2a2a88ULL:  /* 0xfc / 0xd7: Index address mark */
		case 0x8aa222aa8ULL:  /* 0xfe / 0xc7: ID address mark */
		case 0x8aa222888ULL:  /* 0xf8 / 0xc7: Standard deleted DAM */
		case 0x8aa22288aULL:  /* 0xf9 / 0xc7: RX02 deleted DAM / WD1771 user DAM */
		case 0x8aa2228a8ULL:  /* 0xfa / 0xc7: WD1771 user DAM */
		case 0x8aa2228aaULL:  /* 0xfb / 0xc7: Standard DAM */
		case 0x8aa222a8aULL:  /* 0xfd / 0xc7: RX02 DAM */
			change_encoding(fdec, FM);

			if (fdec->bit_cnt >= 48 && fdec->bit_cnt < 64) {
				msg(MSG_HEX, "(+%d)", 64 - fdec->bit_cnt);
				/* byte-align by repeating some bits */
				fdec->bit_cnt = 64;
			} else if (fdec->bit_cnt > 32 && fdec->bit_cnt < 48) {
				msg(MSG_HEX, "(-%d)", fdec->bit_cnt - 32);
				/* byte-align by dropping some bits */
				fdec->bit_cnt = 32;
			}

			fdec->mark_after = 32;
			fdec->premark = 0;  /* doesn't apply to FM marks */
			break;

		case 0xa222a8888ULL:  /* Backward 0xf8-0xfd DAM */
			/* avoid firing on quirky IAM */
			if (fdec->mark_after > 0) break;

			/* discourage firing on noise or splices */
			if (dtsm->trk_working_stats.good_sectors > 0) break;

			change_encoding(fdec, FM);
			fdec->backward_am++;
			msg(MSG_ERRORS, "[backward AM] ");
			break;
		}
	}

	/*
	 * For MFM premarks, we look at 16 data bits (two copies of the
	 * premark), which ends up being 32 bits of accum (2x for clocks).
	 */
	if (fdec->usr_encoding != FM && fdec->usr_encoding != RX02 &&
	    fdec->bit_cnt >= 32 && !fdec->write_splice) {

		switch (fdec->accum & 0xffffffff) {
		case 0x52245224:
			/* Pre-index mark, 0xc2c2 with missing clock between
			 * bits 3 & 4 (using 0-origin big-endian counting!).
			 * Would be 0x52a452a4 without missing clock. */

			change_encoding(fdec, MFM);
			fdec->premark = 0xc2;

			if (fdec->bit_cnt < 64 && fdec->bit_cnt > 48) {
				msg(MSG_HEX, "(+%d)", 64 - fdec->bit_cnt);
				/* byte-align by repeating some bits */
				fdec->bit_cnt = 64;
			}

			fdec->mark_after = fdec->bit_cnt;
			break;

		case 0x448944a9:
			/* Quirky pre-address mark, 0xa1a1 with missing clock
			 * in only the first 0xa1. */

			if ((fdec->quirk & QUIRK_PREMARK) == 0) break;
			/* fall through */

		case 0x44894489:
			/* Pre-address mark, 0xa1a1 with missing clock between
			 * bits 4 & 5 (using 0-origin big-endian counting!).
			 * Would be 0x44a944a9 without missing clock.  Reading
			 * a pre-address mark backward also matches this
			 * pattern, but the following byte is then 0x80. */

			change_encoding(fdec, MFM);
			fdec->premark = 0xa1;

			if (fdec->bit_cnt < 64 && fdec->bit_cnt > 48) {
				msg(MSG_HEX, "(+%d)", 64 - fdec->bit_cnt);
				/* byte-align by repeating some bits */
				fdec->bit_cnt = 64;
			}

			fdec->mark_after = fdec->bit_cnt;
			break;

		case 0x55555555:
			if ((fdec->quirk & QUIRK_EXTRA) == 0 &&
			    fdec->cur_encoding == MFM &&
			    fdec->mark_after < 0 &&
			    fdec->ibyte == -1 &&
			    fdec->dbyte == -1 &&
			    fdec->ebyte == -1 &&
			    !(fdec->bit_cnt & 1)) {
				/* ff ff in gap.  This should probably be
				 * 00 00, so drop 1/2 bit to bit-align.  This
				 * heuristic is harmful if the disk format has
				 * meaningful extra bytes in a gap following
				 * the data CRC and prior to the write splice,
				 * so suppress it if QUIRK_EXTRA is set.
				 * We'll still bit-align when the premark
				 * shows up, and if dmk2gw is used to write
				 * the disk back later, it will force the
				 * bytes preceding the premark to be 00 then.
				 * The DMK file just won't look as nice. */

				msg(MSG_HEX, "(-1)");
				fdec->bit_cnt--;
			}
			break;

		case 0x92549254:
			if ((fdec->quirk & QUIRK_EXTRA) == 0 &&
			    fdec->mark_after < 0 &&
			    fdec->ibyte == -1 &&
			    fdec->dbyte == -1 &&
			    fdec->ebyte == -1) {
				/* 4e 4e in gap.  This should probably be
				 * byte-aligned, so do so by dropping bits.
				 * This heuristic needs to be suppressed
				 * by QUIRK_EXTRA too, as the extra bytes
				 * could theoretically contain valid data
				 * that looks like 4e 4e when read with
				 * wrong alignment. */

				change_encoding(fdec, MFM);

				if (fdec->bit_cnt > 48 && fdec->bit_cnt < 64) {
					msg(MSG_HEX, "(-%d)",
							fdec->bit_cnt - 48);
					fdec->bit_cnt = 48;
				}
			}
			break;
		}
	}

	/* Undo RX02 DEC-modified MFM transform (in taccum) */
#define WINDOW 4
#if WINDOW == 4
	if (fdec->bit_cnt >= WINDOW &&
	    (fdec->bit_cnt & 1) == 0 &&
	    (fdec->accum & 0xfULL) == 0x8ULL) {
		fdec->taccum = (fdec->taccum & ~0xfULL) | 0x5ULL;
	}
#else /* WINDOW == 12 */
	if (fdec->bit_cnt >= WINDOW &&
	    (fdec->bit_cnt & 1) == 0 &&
	    (fdec->accum & 0x7ffULL) == 0x222ULL) {
		fdec->taccum = (fdec->taccum & ~0x7ffULL) | 0x154ULL;
	}
#endif

	if (fdec->bit_cnt < 64) return;

	/* XXX Seems to be a natural break here. Split? */

	uint8_t	val = 0;

	switch (fdec->cur_encoding) {
	case FM:
	case MIXED:
		/* Heuristic to detect being off by some number of bits */
		if (fdec->mark_after != 0 &&
		    ((fdec->accum >> 32) & 0xddddddddULL) != 0x88888888ULL) {
		    	int	i;

			for (i = 1; i <= 3; i++) {
				if (((fdec->accum >> (32 - i))
				    & 0xddddddddULL) == 0x88888888ULL) {
					/* Ignore oldest i bits */
					fdec->bit_cnt -= i;
					msg(MSG_HEX, "(-%d)", i);
					if (fdec->bit_cnt < 64) return;
					break;
				}
			}

			if (i > 3) {
				/* Note bad clock pattern.  This doesn't mean
				 * the next data byte to be output actually
				 * has a bad clock pattern.  It just means
				 * that we see a bad clock pattern in the top
				 * half of accum, and we don't have a complete
				 * predetected mark there that we're just
				 * about to output (that is, mark_after != 0).
				 * The bad clock pattern may get fixed by a
				 * bit drop or repeat heuristic before we
				 * output the next data byte. */

				msg(MSG_HEX, "?");
			}
		}

		for (int i = 0; i < 8; i++) {
			val |= (fdec->accum & (1ULL << (4*i + 1 + 32))) >>
								(3*i + 1 + 32);
		}
		fdec->bit_cnt = 32;
		break;

	case MFM:
		for (int i = 0; i < 8; i++) {
			val |= (fdec->accum & (1ULL << (2*i + 48))) >>
								(i + 48);
		}
		fdec->bit_cnt = 48;
		break;

	case RX02:
		for (int i = 0; i < 8; i++) {
			val |= (fdec->taccum & (1ULL << (2*i + 48))) >>
								(i + 48);
		}
		fdec->bit_cnt = 48;
		break;

	default:
		msg(MSG_SUMMARY, "Internal error: bad current encoding (%d)\n",
			fdec->cur_encoding);
		return;
	}

	/* XXX Seems to be a natural break here.  Split? */

	if (fdec->mark_after == 0) {
		fdec->mark_after = -1;

		switch (val) {
		case 0xfc:
			/* Index address mark */
			if (fdec->cur_encoding == MFM && fdec->premark != 0xc2)
				break;

			check_missing_dam(f2dsm);
			msg(MSG_IDS, "\n#fc ");
			dmk_iam(f2dsm, val, fdec->cur_encoding);
			fdec->ibyte = -1;
			fdec->dbyte = -1;
			fdec->ebyte = -1;
			return;

		case 0xfe:
			/* ID address mark */
			if (fdec->cur_encoding == MFM && fdec->premark != 0xa1)
				break;

			if (fdec->awaiting_iam)
				break;

			check_missing_dam(f2dsm);
			msg(MSG_IDS, "\n#fe ");
			dmk_idam(f2dsm, val, fdec->cur_encoding);

			/* For normal MFM, premark a1a1a1 is included in the
			 * ID CRC.  With QUIRK_ID_CRC, it is omitted. */
			fdec->crc = calc_crc1((fdec->cur_encoding == MFM &&
					(fdec->quirk & QUIRK_ID_CRC) == 0) ?
						0xcdb4 : 0xffff, val);
			fdec->dbyte = -1;
			fdec->ebyte = -1;
			return;

		case 0xf8: /* Standard deleted data address mark */
		case 0xf9: /* WD1771 user or RX02 deleted data address mark */
		case 0xfa: /* WD1771 user data address mark */
		case 0xfb: /* Standard data address mark */
		case 0xfd: /* RX02 data address mark */
			if (dmk_awaiting_track_start(f2dsm) ||
			    !dmk_in_range(f2dsm))
				break;

			if (fdec->cur_encoding == MFM && fdec->premark != 0xa1)
				break;

			if (!fdec->awaiting_dam) {
				msg(MSG_ERRORS, "[unexpected DAM] ");
				dtsm->trk_working_stats.errcount++;
				break;
			}

			fdec->awaiting_dam = 0;
			msg(MSG_HEX, "\n");
			msg(MSG_IDS, "#%2x ", val);
			dmk_data(f2dsm, val, fdec->cur_encoding);
			if ((fdec->usr_encoding == MIXED ||
			     fdec->usr_encoding == RX02) &&
			    (val == 0xfd || (val == 0xf9 &&
			    (dtsm->dds->enc_count_total[RX02] +
			    dtsm->trk_working_stats.enc_count[RX02] > 0 ||
				    fdec->usr_encoding == RX02)))) {
				change_encoding(fdec, RX02);
			}

			/* For MFM, premark a1a1a1 is included in the data CRC.
			 * With QUIRK_DATA_CRC, it is omitted. */
			fdec->crc = calc_crc1((fdec->cur_encoding == MFM &&
					(fdec->quirk & QUIRK_DATA_CRC) == 0) ?
						0xcdb4 : 0xffff, val);
			fdec->ibyte = -1;
			fdec->dbyte = secsize(fdec->sizecode,
					      fdec->cur_encoding,
					      fdec->maxsecsize,
					      fdec->quirk) + 2;
			fdec->ebyte = -1;
			return;

		case 0x80: /* MFM DAM or IDAM premark read backward */
			if (fdec->cur_encoding != MFM || fdec->premark != 0xc2)
				break;

			fdec->backward_am++;
			msg(MSG_ERRORS, "[backward AM] ");
			break;

		default:
			/* Premark with no mark */
			msg(MSG_ERRORS, "[dangling premark] ");
			dmk_data(f2dsm, val, fdec->cur_encoding);
			/* probably wraparound or write splice, so don't
			 * inc dtsm->trk_working_stats.errcount */
			break;
		}
	}

	switch (fdec->ibyte) {
	default:
		break;

	case 0:
		msg(MSG_IDS, "cyl=");
		fdec->curcyl = val;
		break;

	case 1:
		msg(MSG_IDS, "side=");
		break;

	case 2:
		msg(MSG_IDS, "sec=");
		break;

	case 3:
		msg(MSG_IDS, "size=");
		fdec->sizecode = val;
		break;

	case 4:
		msg(MSG_HEX, "crc=");
		break;

	case 6:
		if (fdec->crc == 0) {
			msg(MSG_IDS, "[good ID CRC] ");
			dtsm->valid_id = 1;
		} else {
			msg(MSG_ERRORS, "[bad ID CRC] ");
			dtsm->trk_working_stats.errcount++;
			if (dtsm->accum_sectors)
				dtsm->idam_p[-1] |= DMK_EXTRA_FLAG;
			fdec->ibyte = -1;
		}

		msg(MSG_HEX, "\n");
		fdec->awaiting_dam = 1;
		dmk_check_wraparound(f2dsm);
		break;

	case 18:
		/* Done with post-ID gap */
		fdec->ibyte = -1;
		break;
	}

	if (fdec->ibyte == 2) {
		msg(MSG_ERRORS, "%02x ", val);
	} else if (fdec->ibyte >= 0 && fdec->ibyte <= 3) {
		msg(MSG_IDS, "%02x ", val);
	} else {
		msg(MSG_SAMPLES, "<");
		msg(MSG_HEX, "%02x", val);
		msg(MSG_SAMPLES, ">");
		msg(MSG_HEX, " ");
		//msg(MSG_RAW, "%c", val);
	}

	dmk_data(f2dsm, val, fdec->cur_encoding);

	if (fdec->ibyte >= 0) fdec->ibyte++;
	if (fdec->dbyte > 0) fdec->dbyte--;
	if (fdec->ebyte > 0) fdec->ebyte--;
	fdec->crc = calc_crc1(fdec->crc, val);

	if (fdec->dbyte == 0) {
		if (fdec->crc == 0) {
			msg(MSG_IDS, "[good data CRC] ");
			if (dtsm->valid_id) {
				if (dtsm->trk_working_stats.good_sectors == 0)
					fdec->first_encoding =
							fdec->cur_encoding;

				dtsm->trk_working_stats.good_sectors++;
				dtsm->trk_working_stats.enc_count[fdec->cur_encoding]++;
				fdec->cyl_seen = fdec->curcyl;
			}
		} else {
			msg(MSG_ERRORS, "[bad data CRC] ");
			dtsm->trk_working_stats.errcount++;

			if (dtsm->accum_sectors) {
				/* Don't count both header and data CRC errors
				 * for a sector.  Because otherwise dropping a
				 * single error for a replacement sector will
				 * not show it fully corrected.  Need to track
				 * errors/sector. */
				if (dtsm->idam_p[-1] & DMK_EXTRA_FLAG)
					dtsm->trk_working_stats.errcount--;

				dtsm->idam_p[-1] |= DMK_EXTRA_FLAG;
			}
		}

		msg(MSG_HEX, "\n");
		fdec->dbyte = -1;
		dtsm->valid_id = 0;
		fdec->write_splice = WRITE_SPLICE;

		if (fdec->cur_encoding == RX02) {
			change_encoding(fdec, FM);
		}

		if (fdec->quirk & QUIRK_EXTRA_CRC) {
			fdec->ebyte = 6;
			fdec->crc = 0xffff;
		}
	}

	if (fdec->ebyte == 0) {
		if (fdec->crc == 0) {
			msg(MSG_IDS, "[good extra CRC] ");
		} else {
			msg(MSG_ERRORS, "[bad extra CRC] ");
			dtsm->trk_working_stats.errcount++;
			if (dtsm->accum_sectors) {
				if (dtsm->idam_p[-1] & DMK_EXTRA_FLAG)
					dtsm->trk_working_stats.errcount--;
				dtsm->idam_p[-1] |= DMK_EXTRA_FLAG;
			}
		}

		msg(MSG_HEX, "\n");
		fdec->ebyte = -1;
		fdec->write_splice = WRITE_SPLICE;
	}

	/* Predetect bad MFM clock pattern.  Can't detect at decode time
	 * because we need to look at 17 bits. */
	if (fdec->cur_encoding == MFM && fdec->bit_cnt == 48 &&
			!mfm_valid_clock(fdec->accum >> 32)) {
		if (mfm_valid_clock(fdec->accum >> 31)) {
			msg(MSG_HEX, "(-1)");
			fdec->bit_cnt--;
		} else {
			msg(MSG_HEX, "?");
		}
	}

	return;
}


/*
 * Clean up and re-encode a data/clock pulse window.  Pass the pulse
 * train to gwflux_decode_bit for further decoding.  Ad hoc method
 * using two fixed thresholds modified by a postcomp factor.
 *
 * Return 0 to continue processing stream, or 1 when dmk_full to stop
 * further processing.
 */

int
gwflux_decode_pulse(uint32_t pulse,
		    struct gw_media_encoding *gme,
		    struct flux2dmk_sm  *f2dsm)
{
	struct fdecoder		*fdec = &f2dsm->fdec;
	struct dmk_track_sm	*dtsm = &f2dsm->dtsm;

	// XXX For now, block decoding stream until hole seen.
	// Change when we can abort the stream in progress without waiting
	// for a full rotation and move on.
	if (fdec->use_hole && !dtsm->track_hole_p)
		return 0;

	int	len;

	msg(MSG_SAMPLES, "%d", pulse);

	if (fdec->usr_encoding == FM) {
		if (pulse + gme->thresh_adj <= gme->fmthresh) {
			/* Short: output 10 */
			len = 2;
		} else {
			/* Long: output 1000 */
			len = 4;
		}
	} else {
		if ((fdec->quirk & QUIRK_MFM_CLOCK) &&
			pulse + gme->thresh_adj <= gme->mfmthresh0) {
			/* Tiny: output 1 */
			len = 1;
		} else if (pulse + gme->thresh_adj <= gme->mfmthresh1) {
			/* Short: output 10 */
			len = 2;
		} else if (pulse + gme->thresh_adj <= gme->mfmthresh2) {
			/* Medium: output 100 */
			len = 3;
		} else {
			/* Long: output 1000 */
			len = 4;
		}

	}

	gme->thresh_adj = (pulse - (len/2.0 * gme->mfmshort * 2.0))
			  * gme->postcomp;

	msg(MSG_SAMPLES, "%c ", "-tsml"[len]);

	gwflux_decode_bit(f2dsm, 1);
	while (--len) gwflux_decode_bit(f2dsm, 0);

	return dtsm->dmk_full ? 1 : 0;
}


/*
 * Push out any valid bits left in accum at end of track.
 */

void
gw_decode_flush(struct flux2dmk_sm *f2dsm)
{
	const int accum_sz_bits =
		MEMBER_SIZE(struct fdecoder, accum) * CHAR_BIT;

	for (int i = 0; i < accum_sz_bits; ++i)
		gwflux_decode_bit(f2dsm, !(i & 1) );
}


int
gwflux_decode_index(uint32_t imark, struct flux2dmk_sm *f2dsm)
{
	struct fdecoder *fdec	  = &f2dsm->fdec;
	struct dmk_track_sm *dtsm = &f2dsm->dtsm;

	fdec->index[0] = fdec->index[1];
	fdec->index[1] = imark;

	/* First index hole encountered. */
	if (f2dsm->fdec.index[0] == ~0) {
		uint8_t	**track_hole_pp = &dtsm->track_hole_p;

		if (*track_hole_pp == NULL)
			*track_hole_pp = f2dsm->dtsm.track_data_p;

		if (dtsm->dmk_ignore < 0) {
			int	i = dtsm->dmk_ignore;
			while (i++) *dtsm->track_data_p++ = 0xff;
		}
	} else {
		fdec->total_ticks += fdec->index[1] - fdec->index[0];
		++fdec->revs_seen;
	}

	/* Unlike CW, GW only lets us see the rising edge. */
	++fdec->index_edge;

	msg(MSG_HEX, "{");

	return 0;
}


void
gw_post_process_track(struct flux2dmk_sm *f2dsm)
{
	struct fdecoder *fdec     = &f2dsm->fdec;
	struct dmk_track_sm *dtsm = &f2dsm->dtsm;

	check_missing_dam(f2dsm);

	if (fdec->ibyte != -1) {
		/* Ignore incomplete sector IDs; assume they are wraparound */
		msg(MSG_IDS, "[wraparound] ");
		*--dtsm->idam_p = 0;
	}

	if (fdec->dbyte != -1) {
		dtsm->trk_merged_stats->errcount++;
		msg(MSG_ERRORS, "[incomplete sector data] ");
	}

        if (fdec->ebyte != -1) {
		dtsm->trk_merged_stats->errcount++;
		msg(MSG_ERRORS, "[incomplete extra data] ");
        }

	msg(MSG_IDS, "\n");
}
