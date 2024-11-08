#include "dmkx.h"


/* Cleanup parameters */
#define MFM_GAP3Z 8 /*12 nominal, but that is too many.  3 is too few! */
#define FM_GAP3Z  4 /*6 nominal */

/* Byte encodings and letters used when logging them */
#define SKIP    0  /* -  padding byte in FM area of a DMK */
#define FM      1  /* F  FM with FF clock */
#define FM_IAM  2  /* I  FM with D7 clock (IAM) */
#define FM_AM   3  /* A  FM with C7 clock (IDAM or DAM) */
#define MFM     4  /* M  MFM with normal clocking algorithm */
#define MFM_IAM 5  /* J  MFM C2 with missing clock */
#define MFM_AM  6  /* B  MFM A1 with missing clock */
#define RX02    7  /* X  DEC-modified MFM as in RX02 */
#define encoding_letter "-FIAMJBX"

/* Or'ed into encoding at end of sector data */
#define SECTOR_END 0x80
#define enc(encoding) ((encoding) & ~SECTOR_END)
#define isend(encoding) (((encoding) & SECTOR_END) != 0)
#define ismfm(encoding) (enc(encoding) >= MFM && enc(encoding) <= MFM_AM)
#define ismark(encoding) (enc(encoding) == FM_IAM || \
                          enc(encoding) == FM_AM || \
                          enc(encoding) == MFM_IAM || \
                          enc(encoding) == MFM_AM)


void
encode_bit_init(struct encode_bit *ebs, uint32_t freq, double mult)
{
	*ebs = (struct encode_bit){
		.freq	  = freq,
		.mult     = mult,
		.precomp  = 140.0,
		.prev_err = 0.0,
		.prev_adj = 0.0,
		.adj	  = 0.0,
		.dither   = 0,
		.len	  = 0,
		.next_len = -1
	};
}


/*
 * Encode a bit.
 * Returns number of timing cycles to encode the bit.
 */

uint32_t
encode_bit(struct encode_bit *ebs, bool bit)
{
	uint32_t	iticks = 0;

	++ebs->next_len;

	if (!bit)
		return 0;
	
	if (ebs->len > 0) {
		double	abs_adj = ebs->precomp * (double)ebs->freq /
				  1000000000.0;

		if (ebs->len == 2 && ebs->next_len > 2) {
			ebs->adj = -abs_adj;
		} else if (ebs->len > 2 && ebs->next_len == 2) {
			ebs->adj = abs_adj;
		} else {
			ebs->adj = 0.0;
		}

		double	fticks = ebs->len * ebs->mult -
				 ebs->prev_adj +
				 ebs->adj -
				 ebs->prev_err;

		iticks = (int)(fticks + 0.5);
		ebs->prev_adj = ebs->adj;

		if (ebs->dither)
			ebs->prev_err = (double)iticks - fticks;

		msg(MSG_SAMPLES, "/%d:%d", ebs->len, iticks);
	}

	ebs->len = ebs->next_len;
	ebs->next_len = 0;

	return iticks;
}


void
encode_bit_flush(struct encode_bit *ebs)
{
	encode_bit(ebs, 1);
	encode_bit(ebs, 1);
	encode_bit_init(ebs, ebs->freq, ebs->mult);
}


struct rx02_bitpair {
	uint32_t	accum;
	int		bitcnt;
};


void
rx02_bitpair_init(struct rx02_bitpair *rx02bp)
{
	*rx02bp = (struct rx02_bitpair){
		.accum	= 0,
		.bitcnt = 0
	};
}


/*
 * RX02 encoding requires some buffering
 * to encode data sequence 011110 specially.
 */

void
rx02_bitpair(struct rx02_bitpair *rx02bp,
	     bool bit,
	     struct encode_bit *ebs)
{
	rx02bp->accum = (rx02bp->accum << 1) | bit;
	rx02bp->bitcnt++;

	if (rx02bp->bitcnt == 5) {
		if ((rx02bp->accum & 0x3f) == 0x1e) {
			/* Special RX02 encoding for data bit sequence
			 * (0)11110: not the normal MFM (x0)0101010100,
			 * but (x0)1000100010.  Parentheses in this
			 * notation mean that those bits precede this
			 * sequence but have already been encoded.  A
			 * virtual 0 bit precedes the first bit in
			 * each sector, so the case of a sector
			 * beginning with bits 11110 is handled here.
			 * However, we encode a 0xff leadout after the
			 * last CRC byte of each sector, so the case
			 * of ending with 01111 does not arise. */

			encode_bit(ebs, 1);
			encode_bit(ebs, 0);
			encode_bit(ebs, 0);
			encode_bit(ebs, 0);
			encode_bit(ebs, 1);
			encode_bit(ebs, 0);
			encode_bit(ebs, 0);
			encode_bit(ebs, 0);
			encode_bit(ebs, 1);
			encode_bit(ebs, 0);
			rx02bp->bitcnt = 0;
		} else {
			/* clock bit */
			encode_bit(ebs, (rx02bp->accum & (3 << 4)) == 0);
			/* data bit */
			encode_bit(ebs, (rx02bp->accum & (1 << 4)) != 0);
			--rx02bp->bitcnt;
		}
	}
}


void
rx02_bitpair_flush(struct rx02_bitpair *rx02bp,
		   struct encode_bit *ebs)
{
	while (rx02bp->bitcnt--) {
		/* clock bit */
		encode_bit(ebs, (rx02bp->accum & (3 << rx02bp->bitcnt)) == 0);
		/* data bit */
		encode_bit(ebs, (rx02bp->accum & (1 << rx02bp->bitcnt)) != 0);
	}
}


/*
 * Write MFM byte with missing clock, or normal MFM if missing_clock = -1.
 *
 * On entry, *prev_bit is the previous bit encoded; on exit, the
 * last bit encoded.
 */

static int
mfm_byte(uint8_t byte,
	 int missing_clock,
	 struct encode_bit *ebs,
	 bool *prev_bit,
	 struct dmk_encode_s *des)
{
//printf("mfm_byte = 0x%02x\n", byte);
	int	ret = 0;

	for (int i = 0; i < 8; ++i) {
		uint32_t	pulse;
		bool		bit = (byte & 0x80) != 0;

		pulse = encode_bit(ebs, *prev_bit == 0 && bit == 0
				   && i != missing_clock);
		ret = des->encode_pulse(pulse, des->pulse_data);
		if (ret) goto leave;
		pulse = encode_bit(ebs, bit);
		ret = des->encode_pulse(pulse, des->pulse_data);
		if (ret) goto leave;

		byte <<= 1;
		*prev_bit = bit;
	}

leave:
	return ret;
}


/*
 * Write FM byte with specified clock pattern.
 *
 * On exit, *prev_bit is 0, in case the next byte is MFM.
 */

static int
fm_byte(uint8_t byte,
	uint8_t clock_byte,
	struct encode_bit *ebs,
	bool *prev_bit,
	struct dmk_encode_s *des)
{
//printf("fm_byte = 0x%02x\n", byte);
	int	ret = 0;

	for (int i = 0; i < 8; ++i) {
		uint8_t		bit = (clock_byte & 0x80) != 0;
		uint32_t	pulse;

		clock_byte <<= 1;
		pulse = encode_bit(ebs, bit);
		ret = des->encode_pulse(pulse, des->pulse_data);
		if (ret) goto leave;
		pulse = encode_bit(ebs, 0);
		ret = des->encode_pulse(pulse, des->pulse_data);
		if (ret) goto leave;

		bit = (byte & 0x80) != 0;
		byte <<= 1;
		pulse = encode_bit(ebs, bit);
		ret = des->encode_pulse(pulse, des->pulse_data);
		if (ret) goto leave;
		pulse = encode_bit(ebs, 0);
		ret = des->encode_pulse(pulse, des->pulse_data);
		if (ret) goto leave;
	}

leave:
	*prev_bit = 0;

	return ret;
}


/*
 * Convert a DMK track to pulses with frequency timing in ebs.
 */

int
dmk2pulses(struct dmk_track *dmkt,
	   struct extra_track_info *eti,
	   struct encode_bit *ebs,
	   struct dmk_encode_s *des)
{
	/*
	 * Determine encoding for each byte and clean up.
	 */

	int	idampp = 0;
	int	idamp  = 0;
	int	dam_min = 0;
	int	dam_max = 0;
	int	got_iam = 0;
	bool	skip = false;
	int	rx02_data = 0;
	int	sector_data = 0;
	uint8_t	dmk_encoding[DMKRD_TRACKLEN_MAX - DMK_TKHDR_SIZE] = { 0 };

	struct rx02_bitpair	rx02bp;
	rx02_bitpair_init(&rx02bp);

	/*
	 * First IDAM pointer has some special uses; need to get it here.
	 */

	int	first_idamp;
	int	next_idamp = le16toh(dmkt->idam_offset[idampp++]);
	int	next_encoding;

	if (next_idamp == 0 || next_idamp == 0xffff) {
		next_encoding  = FM;
		next_idamp     = 0x7fff;
		first_idamp    = 0;
	} else {
		next_encoding  = (next_idamp & DMK_DDEN_FLAG) ? MFM : FM;
		next_idamp    &= DMK_IDAMP_BITS;
		first_idamp    = next_idamp;
	}

	/*
	 * Check if writing to side 1 (the second side) of
	 * a 1-sided drive.
	 */

	// XXX I think this test is wrong.  Should be >=.
	if (eti->side > eti->max_sides) {
		if (first_idamp == 0) {
			/* No problem; there is nothing to
			 * write here. */
		} else {
			msg_fatal("Drive is 1-sided but DMK file is "
				  "2-sided.\n");
		}
	}

	int encoding = next_encoding;

	/*
	 * Loop through data bytes.
	 */

	int	datap;

	for (datap = DMK_TKHDR_SIZE; datap < eti->track_len; ++datap) {
		if (datap >= next_idamp) {
			/* Read next IDAM pointer */
			idamp = next_idamp;
			encoding = next_encoding;
			next_idamp = le16toh(dmkt->idam_offset[idampp++]);

			if (next_idamp == 0 || next_idamp == 0xffff) {
				next_encoding  = encoding;
				next_idamp     = 0x7fff;
			} else {
				next_encoding  = (next_idamp & DMK_DDEN_FLAG) ?
						  MFM : FM;
				next_idamp    &= DMK_IDAMP_BITS;
			}

			/* Project where DAM will be */
			if (encoding == FM) {
				dam_min = idamp + 7 * eti->fmtimes;
				/* ref 1791 datasheet */
				dam_max = dam_min + 30 * eti->fmtimes;
			} else {
				dam_min = idamp + 7;
				dam_max = dam_min + 43; /* ref 1791 datasheet */
			}
		}

		/*
		 * Choose encoding.
		 */

		if (datap == idamp && dmkt->track[datap] == 0xfe) {
			/* ID address mark */
			skip = true;
			if (encoding == FM) {
				dmk_encoding[datap] = FM_AM;
				/* Cleanup: precede mark with some FM 00's */
				for (int i = datap - 1;
				     i >= DMK_TKHDR_SIZE &&
					i >= datap - FM_GAP3Z*eti->fmtimes;
					--i) {
					dmkt->track[i] = 0;
					dmk_encoding[i] =
						(eti->fmtimes == 2 && (i & 1)) ?
							SKIP : FM;
				}
			} else {
				dmk_encoding[datap] = encoding;
				/* Cleanup: precede mark with 3 MFM A1's with
				 * missing clocks, and some MFM 00's before
				 * that */
				int	i;
				for (i = datap - 1;
				     i >= DMK_TKHDR_SIZE && i >= datap-3; --i) {
					dmkt->track[i] = 0xa1;
					dmk_encoding[i] = MFM_AM;
				}

				for (; i >= DMK_TKHDR_SIZE &&
					i >= datap-3 - MFM_GAP3Z; --i) {
					dmkt->track[i] = 0x00;
					dmk_encoding[i] = MFM;
				}
			}
		} else if (datap >= dam_min && datap <= dam_max &&
			   ((dmkt->track[datap] >= 0xf8 &&
			     dmkt->track[datap] <= 0xfb) ||
			       dmkt->track[datap] == 0xfd)) {
			/* Data address mark */
			dam_max = 0; /* prevent detecting again inside data */
			skip = true;
			if (encoding == FM) {
				dmk_encoding[datap] = FM_AM;

				/* Cleanup: precede mark with some FM 00's */
				for (int i = datap - 1;
				     i >= DMK_TKHDR_SIZE &&
				       i >= datap - FM_GAP3Z * eti->fmtimes;
				       --i) {
					dmkt->track[i] = 0;
					dmk_encoding[i] =
						(eti->fmtimes == 2 && (i & 1)) ?
							SKIP : FM;
				}
			} else {
				dmk_encoding[datap] = encoding;

				/* Cleanup: precede mark with 3 MFM A1's with
				 * missing clocks, and some MFM 00's before
				 * that */
				int	i;
				for (i = datap - 1;
				     i >= DMK_TKHDR_SIZE && i >= datap-3; --i) {
					dmkt->track[i] = 0xA1;
					dmk_encoding[i] = MFM_AM;
				}

				for (; i >= DMK_TKHDR_SIZE &&
						i >= datap-3 - MFM_GAP3Z; --i) {
					dmkt->track[i] = 0x00;
					dmk_encoding[i] = MFM;
				}
			}

			/* Prepare to switch to RX02-modified MFM if needed */
			if (eti->rx02 && (dmkt->track[datap] == 0xf9 ||
			    dmkt->track[datap] == 0xfd)) {
				rx02_data = 2 + (256 << dmkt->track[idamp+4]);

				/* Follow CRC with one RX02-MFM FF */
				dmkt->track[eti->fmtimes +
					datap + rx02_data++] = 0xff;
			} else {
				/* Compute expected sector size, including CRC
				 * and any extra bytes after the CRC.  This
				 * matters only for warning if the entire
				 * sector didn't fit, so be liberal and use
				 * maxsize = 7 instead of burdening the user
				 * with yet another command line option.  */
				sector_data = secsize(dmkt->track[idamp+4],
						      encoding, 7,
						      eti->quirks)
						      + 2 + eti->extra_bytes;
			}

		} else if (datap >= DMK_TKHDR_SIZE && datap <= first_idamp
			   && !got_iam && dmkt->track[datap] == 0xfc &&
			   ((encoding == MFM && dmkt->track[datap-1] == 0xc2) ||
			    (encoding == FM &&
			     (dmkt->track[datap-eti->fmtimes] == 0x00 ||
			       dmkt->track[datap-eti->fmtimes] == 0xff)))) {
			/* Index address mark */
			got_iam = datap;
			skip = true;
			if (encoding == FM) {
				dmk_encoding[datap] = FM_IAM;
				/* Cleanup: precede mark with some FM 00's */
				for (int i = datap-1;
				     i >= DMK_TKHDR_SIZE &&
				       i >= datap - FM_GAP3Z * eti->fmtimes;
				       --i) {
					dmkt->track[i] = 0;
					dmk_encoding[i] =
						(eti->fmtimes == 2 && (i & 1)) ?
							SKIP : FM;
				}
			} else {
				dmk_encoding[datap] = encoding;
				/* Cleanup: precede mark with 3 MFM C2's with
				 * missing clocks, and some MFM 00's before
				 * that */
				int	i;
				for (i = datap - 1;
				     i >= DMK_TKHDR_SIZE && i >= datap - 3;
				     --i) {
					dmkt->track[i] = 0xC2;
					dmk_encoding[i] = MFM_IAM;
				}

				for (; i >= DMK_TKHDR_SIZE &&
				       i >= datap - 3 - MFM_GAP3Z; --i) {
					dmkt->track[i] = 0x00;
					dmk_encoding[i] = MFM;
				}
			}
		} else if (rx02_data > 0) {
			if (eti->fmtimes == 2 && skip) {
				/* Skip the duplicated DAM */
				dmk_encoding[datap] = SKIP;
				skip = false;
			} else {
				/* Encode an rx02-modified MFM byte */
				dmk_encoding[datap] = RX02;
				--rx02_data;
				if (rx02_data == 0)
					dmk_encoding[datap] |= SECTOR_END;
			}
		} else if (encoding == FM && eti->fmtimes == 2 && skip) {
			/* Skip bytes that are an odd distance from an
			 * address mark */
			dmk_encoding[datap] = SKIP;
			skip = !skip;

		} else {
			/* Normal case */
			dmk_encoding[datap] = encoding;
			skip = !skip;
			if (sector_data > 0) {
				--sector_data;
				if (sector_data == 0)
					dmk_encoding[datap] |= SECTOR_END;
			}
		}
	}

#if 0
	/* Encode into clock/data stream */
	catweasel_reset_pointer(&c);
#endif
#if 0
	if (testmode >= 0x100 && testmode <= 0x1ff) {
		/* Fill with constant value instead of actual data;
		 * for testing */
		for (int i = 0; i < 128 * 1024; ++i) {
			catweasel_put_byte(&c, testmode);
		}

		return;
	}
#endif
#if 0
	for (int i = 0; i < 7; i++) {
		/* XXX Is this needed/correct? */
		catweasel_put_byte(&c, 0);
	}
#endif

	// XXX Natural break here?

	bool	bit = 0;
	int	prev_encoding = SKIP;
	int	ignore = 0;
	encoding = dmk_encoding[DMK_TKHDR_SIZE];

	if (eti->iam_pos >= 0) {
		if (got_iam == 0) {
			msg_error("No index address mark on track "
				  "%d, side %d\n", eti->track, eti->side);
		} else {
			ignore = got_iam - DMK_TKHDR_SIZE - eti->iam_pos;
		}
	}

	for (datap = DMK_TKHDR_SIZE + ignore; datap < eti->track_len; ++datap) {
		int	byte;

		if (datap >= DMK_TKHDR_SIZE) {
			byte = dmkt->track[datap];
			encoding = dmk_encoding[datap];
		} else {
			byte = ismfm(encoding) ? 0x4e : 0xff;
		}

		if (encoding != SKIP) {
			msg(MSG_SAMPLES, "\n");

			if (enc(encoding) != enc(prev_encoding)) {
				if (ismark(encoding)
				    && prev_encoding != SKIP)
					msg(MSG_BYTES, "\n");

				msg(MSG_BYTES, "<%c>",
				       encoding_letter[enc(encoding)]);
				prev_encoding = encoding;
			}

			msg(MSG_BYTES, "%02x%s ", byte,
			       isend(encoding) ? "|" : "");
		}
		switch (enc(encoding)) {
		case SKIP:	/* padding byte in FM area of a DMK */
			break;

		case FM:	/* FM with FF clock */
			fm_byte(byte, 0xff, ebs, &bit, des);
			break;

		case FM_IAM:	/* FM with D7 clock (IAM) */
			fm_byte(byte, 0xd7, ebs, &bit, des);
			break;

		case FM_AM:	/* FM with C7 clock (IDAM or DAM) */
			fm_byte(byte, 0xc7, ebs, &bit, des);
			break;

		case MFM:	/* MFM with normal clocking algorithm */
			mfm_byte(byte, -1, ebs, &bit, des);
			break;

		case MFM_IAM:	/* MFM with missing clock 4 */
			mfm_byte(byte, 4, ebs, &bit, des);
			break;

		case MFM_AM:	/* MFM with missing clock 5 */
			mfm_byte(byte, 5, ebs, &bit, des);
			break;

		case RX02:	/* DEC-modified MFM as in RX02 */
			if (enc(dmk_encoding[datap - 1]) != RX02)
				rx02_bitpair_init(&rx02bp);

			for (int i = 0; i < 8; i++) {
				bit = (byte & 0x80) != 0;
				rx02_bitpair(&rx02bp, bit, ebs);
				byte <<= 1;
			}

			if (enc(dmk_encoding[datap + 1]) != RX02)
				rx02_bitpair_flush(&rx02bp, ebs);

			break;
		}

		if (isend(encoding)) {
			//if (catweasel_sector_end(&c) < 0)
			//	msg_fatal("Catweasel memory full\n");
		}
	}

	rx02_bitpair_flush(&rx02bp, ebs);

	/* In case the DMK buffer is shorter than the physical track,
	   fill the rest of the Catweasel's memory with a fill
	   pattern. */

	switch (eti->fill) {
	case 0:
		/* Fill with a standard gap byte in most recent encoding */
		if (ismfm(encoding)) {
			//for (;;) {
			//	if (mfm_byte(0x4e, -1, ebs, &bit, des) < 0)
			//		break;
			//}
		} else {
			//for (;;) {
			//	if (fm_byte(0xff, 0xff, ebs, &bit, des) < 0)
			//		break;
			//}
		}
		break;

	case 1:
		/* Erase remainder of track and write nothing. */
		/* Note: when reading back a track like this, my drives
		   appear to see garbage there, not a lack of transitions.
		   Maybe the drive just isn't happy not seeing a transition
		   for a long time and it ends up manufacturing them from
		   noise? */
		encode_bit_flush(ebs);
		//for (;;) {
		//	if (catweasel_put_byte(&c, 0x81) < 0)
		//		break;
		//}
		break;

	case 2:
		/* Fill with a pattern of very long transitions. */
		encode_bit_flush(ebs);
		//for (;;) {
		//	if (catweasel_put_byte(&c, 0) < 0)
		//		break;
		//}
		break;

	case 3:
		/* Stop writing, leaving whatever was there before. */
		encode_bit_flush(ebs);
		//catweasel_put_byte(&c, 0xff);
		break;

	default:
		switch (eti->fill >> 8) {
		case 1:
		default:
			/* Fill with a specified byte in FM */
			//for (;;) {
			//	if (fm_byte(eti->fill & 0xff, 0xff,
			//		    ebs, &bit, des) < 0)
			//		break;
			//}
			break;
		case 2:
			/* Fill with a specified byte in MFM */
			//for (;;) {
			//	if (mfm_byte(eti->fill & 0xff, -1,
			//	    ebs, &bit, des) < 0)
			//		break;
	 		//}
			break;
		}
	}

	return 0;  // XXX Fix
}
