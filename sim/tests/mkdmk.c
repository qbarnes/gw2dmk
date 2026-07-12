/*
 * mkdmk: test helper for gwsim.
 *
 * Generate a formatted DMK image with predictable sector payloads,
 * or compare the sector payloads of two DMK images.
 *
 *   mkdmk [-t tracks] [-s sides] [-n sectors] [-l tracklen] out.dmk
 *   mkdmk -c a.dmk b.dmk
 *
 * Generated tracks are IBM-style MFM: for each sector, 12x00,
 * 3xA1, FE, C H R N, CRC, gap2, 12x00, 3xA1, FB, 256 data bytes,
 * CRC, gap3.  Payload bytes derive from (track, side, sector) so
 * mismatches are unambiguous.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dmk.h"
#include "crc.h"

#define SECSIZE	256


static uint8_t
payload_byte(int t, int s, int r, int i)
{
	return (uint8_t)(t * 7 + s * 131 + r * 43 + i);
}


static uint16_t
crc_bytes(uint16_t crc, const uint8_t *p, int cnt)
{
	while (cnt--)
		crc = calc_crc1(crc, *p++);

	return crc;
}


static void
gen_track(struct dmk_track *trk, uint16_t tracklen, int t, int s,
	  int nsec)
{
	int	datalen = tracklen - DMK_TKHDR_SIZE;
	uint8_t	*d	= trk->data;
	int	p	= 0;
	int	idam	= 0;

	memset(trk, 0, sizeof(*trk));
	memset(d, 0x4e, datalen);

	trk->track_len = tracklen;

	p = 32;						/* gap 4a */

	for (int r = 1; r <= nsec && idam < DMK_MAX_SECTORS; ++r) {
		uint16_t	crc;

		memset(&d[p], 0x00, 12);		/* sync */
		p += 12;

		int	am = p;

		d[p++] = 0xa1;
		d[p++] = 0xa1;
		d[p++] = 0xa1;

		trk->idam_offset[idam++] = (DMK_TKHDR_SIZE + p) |
					   DMK_DDEN_FLAG;

		d[p++] = 0xfe;				/* IDAM */
		d[p++] = t;				/* C */
		d[p++] = s;				/* H */
		d[p++] = r;				/* R */
		d[p++] = 1;				/* N: 256 bytes */

		crc = crc_bytes(0xffff, &d[am], p - am);
		d[p++] = crc >> 8;
		d[p++] = crc & 0xff;

		memset(&d[p], 0x4e, 22);		/* gap 2 */
		p += 22;

		memset(&d[p], 0x00, 12);		/* sync */
		p += 12;

		am = p;

		d[p++] = 0xa1;
		d[p++] = 0xa1;
		d[p++] = 0xa1;
		d[p++] = 0xfb;				/* DAM */

		for (int i = 0; i < SECSIZE; ++i)
			d[p++] = payload_byte(t, s, r, i);

		crc = crc_bytes(0xffff, &d[am], p - am);
		d[p++] = crc >> 8;
		d[p++] = crc & 0xff;

		memset(&d[p], 0x4e, 24);		/* gap 3 */
		p += 24;

		if (p > datalen - 8) {
			fprintf(stderr, "mkdmk: too many sectors "
				"for track length\n");
			exit(1);
		}
	}
}


static int
generate(const char *path, int tracks, int sides, int nsec,
	 int tracklen)
{
	struct dmk_file	*dmkf = calloc(1, sizeof(*dmkf));

	if (!dmkf) {
		perror("mkdmk");
		return 1;
	}

	dmk_header_init(&dmkf->header, tracks, tracklen);

	if (sides == 1)
		dmkf->header.options |= DMK_SSIDE_OPT;

	for (int t = 0; t < tracks; ++t) {
		for (int s = 0; s < sides; ++s)
			gen_track(&dmkf->track[t][s], tracklen, t, s,
				  nsec);
	}

	FILE	*fp = fopen(path, "wb");

	if (!fp) {
		perror(path);
		free(dmkf);
		return 1;
	}

	dmk2fp(dmkf, fp);
	fclose(fp);
	free(dmkf);

	return 0;
}


/*
 * Extract the payload of the sector with ID (t, s, r) by walking
 * the track's IDAM pointers.  Returns 0 on success.
 */

static int
find_sector(const struct dmk_file *dmkf, int t, int s, int r,
	    uint8_t *payload)
{
	const struct dmk_track	*trk = &dmkf->track[t][s];
	int	datalen = dmkf->header.tracklen - DMK_TKHDR_SIZE;

	for (int i = 0; i < DMK_MAX_SECTORS && trk->idam_offset[i];
	     ++i) {
		int	off = (trk->idam_offset[i] & DMK_IDAMP_BITS) -
			      DMK_TKHDR_SIZE;

		if (off < 0 || off + 7 > datalen)
			continue;

		const uint8_t	*id = &trk->data[off];

		if (id[0] != 0xfe || id[1] != t || id[2] != s ||
		    id[3] != r)
			continue;

		/* Find the DAM within a plausible distance. */
		for (int p = off + 7; p < off + 60 &&
		     p + 1 + SECSIZE <= datalen; ++p) {
			if (trk->data[p] == 0xfb ||
			    trk->data[p] == 0xf8) {
				memcpy(payload, &trk->data[p + 1],
				       SECSIZE);
				return 0;
			}
		}
	}

	return -1;
}


static struct dmk_file *
load_dmk(const char *path)
{
	FILE	*fp = fopen(path, "rb");

	if (!fp) {
		perror(path);
		return NULL;
	}

	struct dmk_file	*dmkf = calloc(1, sizeof(*dmkf));

	if (dmkf && fp2dmk(fp, dmkf) != 0) {
		fprintf(stderr, "mkdmk: bad DMK file '%s'\n", path);
		free(dmkf);
		dmkf = NULL;
	}

	fclose(fp);

	return dmkf;
}


static int
compare(const char *patha, const char *pathb, int nsec)
{
	struct dmk_file	*a = load_dmk(patha);
	struct dmk_file	*b = load_dmk(pathb);

	if (!a || !b)
		return 1;

	int	tracks = a->header.ntracks < b->header.ntracks ?
			 a->header.ntracks : b->header.ntracks;
	int	sides_a = 2 - !!(a->header.options & DMK_SSIDE_OPT);
	int	sides_b = 2 - !!(b->header.options & DMK_SSIDE_OPT);
	int	sides = sides_a < sides_b ? sides_a : sides_b;

	if (tracks != a->header.ntracks ||
	    tracks != b->header.ntracks)
		fprintf(stderr, "mkdmk: note: comparing %d common "
			"tracks (%d vs %d)\n", tracks,
			a->header.ntracks, b->header.ntracks);

	long	ok = 0, bad = 0, missing = 0;

	for (int t = 0; t < tracks; ++t) {
		for (int s = 0; s < sides; ++s) {
			for (int r = 1; r <= nsec; ++r) {
				uint8_t	pa[SECSIZE], pb[SECSIZE];
				int	fa = find_sector(a, t, s, r, pa);
				int	fb = find_sector(b, t, s, r, pb);

				if (fa || fb) {
					fprintf(stderr, "mkdmk: sector "
						"%d/%d/%d missing in "
						"%s%s%s\n", t, s, r,
						fa ? patha : "",
						(fa && fb) ? " and " : "",
						fb ? pathb : "");
					++missing;
				} else if (memcmp(pa, pb, SECSIZE)) {
					fprintf(stderr, "mkdmk: sector "
						"%d/%d/%d differs\n",
						t, s, r);
					++bad;
				} else {
					++ok;
				}
			}
		}
	}

	printf("mkdmk: %ld sectors match, %ld differ, %ld missing\n",
	       ok, bad, missing);

	free(a);
	free(b);

	return (bad || missing || !ok) ? 1 : 0;
}


int
main(int argc, char **argv)
{
	int	tracks	 = 40;
	int	sides	 = 2;
	int	nsec	 = 16;
	int	tracklen = DMKI_TRACKLEN_5;
	bool	cmp	 = false;
	int	c;

	while ((c = getopt(argc, argv, "ct:s:n:l:")) != -1) {
		switch (c) {
		case 'c':
			cmp = true;
			break;
		case 't':
			tracks = atoi(optarg);
			break;
		case 's':
			sides = atoi(optarg);
			break;
		case 'n':
			nsec = atoi(optarg);
			break;
		case 'l':
			tracklen = strtol(optarg, NULL, 0);
			break;
		default:
			return 1;
		}
	}

	if (cmp) {
		if (argc - optind != 2) {
			fprintf(stderr, "usage: mkdmk -c a.dmk b.dmk\n");
			return 1;
		}

		return compare(argv[optind], argv[optind + 1], nsec);
	}

	if (argc - optind != 1) {
		fprintf(stderr, "usage: mkdmk [-t tracks] [-s sides] "
			"[-n sectors] [-l tracklen] out.dmk\n");
		return 1;
	}

	if (tracks < 1 || tracks > DMK_MAX_TRACKS || sides < 1 ||
	    sides > 2 || nsec < 1 ||
	    tracklen <= DMK_TKHDR_SIZE || tracklen > DMKRD_TRACKLEN_MAX) {
		fprintf(stderr, "mkdmk: bad parameters\n");
		return 1;
	}

	return generate(argv[optind], tracks, sides, nsec, tracklen);
}
