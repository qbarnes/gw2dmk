#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dmk.h"
#include "dmkx.h"
#include "gwdecode.h"
#include "gwmedia.h"

#include "simmedia.h"
#include "simdmk.h"


struct sim_dmk {
	struct sim_media	m;
	struct dmk_file		*dmkf;
};


static struct sim_media *
dmk_load(const char *path)
{
	FILE	*fp = fopen(path, "rb");

	if (!fp)
		return NULL;

	struct sim_dmk	*dm = calloc(1, sizeof(*dm));

	if (!dm) {
		fclose(fp);
		return NULL;
	}

	dm->dmkf = calloc(1, sizeof(*dm->dmkf));

	if (!dm->dmkf || fp2dmk(fp, dm->dmkf) != 0)
		goto err;

	fclose(fp);

	struct dmk_header *h = &dm->dmkf->header;

	if (h->ntracks == 0 || h->ntracks > DMK_MAX_TRACKS ||
	    h->tracklen <= DMK_TKHDR_SIZE ||
	    h->tracklen > DMKRD_TRACKLEN_MAX) {
		free(dm->dmkf);
		free(dm);
		return NULL;
	}

	dm->m.ops   = &sim_dmk_ops;
	dm->m.path  = strdup(path);
	dm->m.wp    = (h->writeprot != 0) || (access(path, W_OK) != 0);
	dm->m.dirty = false;

	return &dm->m;

err:
	fclose(fp);
	free(dm->dmkf);
	free(dm);

	return NULL;
}


static int
dmk_save(struct sim_media *media)
{
	struct sim_dmk	*dm = (struct sim_dmk *)media;
	FILE		*fp = fopen(media->path, "wb");

	if (!fp)
		return -1;

	int	ret = dmk2fp(dm->dmkf, fp);

	if (fclose(fp) != 0)
		ret = -1;

	if (ret == 0)
		media->dirty = false;

	return ret;
}


static void
dmk_unload(struct sim_media *media)
{
	struct sim_dmk	*dm = (struct sim_dmk *)media;

	free(dm->dmkf);
	free(media->path);
	free(dm);
}


struct pulse_vec {
	uint32_t	*p;
	size_t		cnt;
	size_t		len;
};


static int
collect_pulse(uint32_t pulse, void *data)
{
	struct pulse_vec *pv = (struct pulse_vec *)data;

	if (pulse == 0)
		return 0;

	if (pv->cnt == pv->len) {
		size_t	nlen = pv->len ? pv->len * 2 : 65536;
		uint32_t *np = realloc(pv->p, nlen * sizeof(*np));

		if (!np)
			return -1;

		pv->p   = np;
		pv->len = nlen;
	}

	pv->p[pv->cnt++] = pulse;

	return 0;
}


/*
 * Derive the flux timing multiplier for this media in a drive
 * rotating at "rpm".
 *
 * The DMK track length gives the media's nominal bytes per
 * revolution; with the drive's rotation rate that yields the data
 * rate, and dmk2gw's encoding multiplier scales inversely with data
 * rate (mult == 1 tick/us at 500 kbps).  Single-density images store
 * each FM byte once at half the byte rate, hence the doubling.
 */

static double
dmk_rate_bps(const struct dmk_header *h, int rpm)
{
	int	data_len  = h->tracklen - DMK_TKHDR_SIZE;
	int	equiv_bpr = data_len *
			    ((h->options & DMK_SDEN_OPT) ? 2 : 1);

	return equiv_bpr * 8.0 * rpm / 60.0;
}


static double
dmk_mult(const struct dmk_header *h, uint32_t freq, int rpm)
{
	return (freq / 1e6) * (500000.0 / dmk_rate_bps(h, rpm));
}


static int
dmk_track_pulses(struct sim_media *media, int track, int side,
		 uint32_t freq, int rpm, uint32_t **pulses, size_t *cnt)
{
	struct sim_dmk		*dm = (struct sim_dmk *)media;
	struct dmk_header	*h  = &dm->dmkf->header;
	int	sides = 2 - !!(h->options & DMK_SSIDE_OPT);

	if (track < 0 || track >= h->ntracks || side < 0 || side >= sides)
		return 1;

	struct dmk_track	*dmkt = &dm->dmkf->track[track][side];

	int	extra_bytes = 0;

	if (h->quirks & DMK_QUIRK_EXTRA_CRC)
		extra_bytes = 6;
	else if (h->quirks & DMK_QUIRK_EXTRA)
		extra_bytes = 6;

	struct extra_track_info	eti = {
		.track	     = track,
		.track_len   = h->tracklen,
		.side	     = side,
		.max_sides   = 2,
		.fmtimes     = 2 - !!(h->options & DMK_SDEN_OPT),
		.iam_pos     = -1,
		.rx02	     = !!(h->options & DMK_RX02_OPT),
		.extra_bytes = extra_bytes,
		.fill	     = 0,
		.quirks	     = h->quirks,
		.precomp     = 0.0
	};

	struct encode_bit	ebs;

	encode_bit_init(&ebs, freq, dmk_mult(h, freq, rpm));
	ebs.precomp	= 0.0;
	ebs.extra_bytes	= extra_bytes;

	struct pulse_vec	pv = { NULL, 0, 0 };

	struct dmk_encode_s	des = {
		.encode_pulse = collect_pulse,
		.pulse_data   = &pv
	};

	dmk2pulses(dmkt, &eti, &ebs, &des);

	if (pv.cnt == 0) {
		free(pv.p);
		return 1;
	}

	*pulses = pv.p;
	*cnt	= pv.cnt;

	return 0;
}


/*
 * Decode one written revolution of flux pulses back into a DMK
 * track, reusing gw2dmk's flux decoder state machine.  The stream
 * was cued at the index hole, so decoding starts at the track start
 * and no rotation to the hole is needed.
 */

static int
dmk_track_from_pulses(struct sim_media *media, int track, int side,
		      uint32_t freq, int rpm, const uint32_t *pulses,
		      size_t cnt)
{
	struct sim_dmk		*dm = (struct sim_dmk *)media;
	struct dmk_header	*h  = &dm->dmkf->header;
	int	sides = 2 - !!(h->options & DMK_SSIDE_OPT);

	if (track < 0 || track >= DMK_MAX_TRACKS || side < 0 ||
	    side >= sides)
		return -1;

	struct decode_ctx {
		struct flux2dmk_sm	f2d;
		struct dmk_track	merged;
		struct dmk_disk_stats	dds;
		struct dmk_track_stats	dts;
	};

	struct decode_ctx	*ctx = calloc(1, sizeof(*ctx));

	if (!ctx)
		return -1;

	dmk_disk_stats_init(&ctx->dds);
	dmk_track_stats_init(&ctx->dts);

	fdecoder_init(&ctx->f2d.fdec, freq);

	ctx->f2d.fdec.usr_encoding   = MIXED;
	ctx->f2d.fdec.first_encoding = MIXED;
	ctx->f2d.fdec.cur_encoding   = MIXED;
	ctx->f2d.fdec.maxsecsize     = 3;
	ctx->f2d.fdec.use_hole	     = 0;
	ctx->f2d.fdec.quirk	     = h->quirks;
	ctx->f2d.fdec.cyl_prev_seen  = track;

	dmk_track_sm_init(&ctx->f2d.dtsm, &ctx->dds, h, &ctx->merged,
			  &ctx->dts);

	struct gw_media_encoding	gme;

	media_encoding_init(&gme, freq,
			    2.0 * 500000.0 / dmk_rate_bps(h, rpm));
	gme.postcomp = 0.5;

	uint64_t	total = 0;

	gwflux_decode_index(0, &ctx->f2d);

	for (size_t i = 0; i < cnt; ++i) {
		total += pulses[i];

		if (gwflux_decode_pulse(pulses[i], &gme, &ctx->f2d))
			break;
	}

	gwflux_decode_index(total, &ctx->f2d);
	gw_decode_flush(&ctx->f2d);

	if (track >= h->ntracks)
		h->ntracks = track + 1;

	dm->dmkf->track[track][side] = ctx->f2d.dtsm.trk_working;
	media->dirty = true;

	free(ctx);

	return 0;
}


static int
dmk_tracks(struct sim_media *media)
{
	struct sim_dmk	*dm = (struct sim_dmk *)media;

	return dm->dmkf->header.ntracks;
}


static void
dmk_describe(struct sim_media *media, char *buf, size_t buflen)
{
	struct sim_dmk		*dm = (struct sim_dmk *)media;
	struct dmk_header	*h  = &dm->dmkf->header;

	snprintf(buf, buflen, "DMK %s: %d tracks, %d side%s, "
		 "tracklen %d%s%s",
		 media->path, h->ntracks,
		 2 - !!(h->options & DMK_SSIDE_OPT),
		 (h->options & DMK_SSIDE_OPT) ? "" : "s",
		 h->tracklen - DMK_TKHDR_SIZE,
		 (h->options & DMK_SDEN_OPT) ? ", single-density" : "",
		 media->wp ? ", write-protected" : "");
}


const struct sim_media_ops sim_dmk_ops = {
	.name		   = "dmk",
	.load		   = dmk_load,
	.save		   = dmk_save,
	.unload		   = dmk_unload,
	.track_pulses	   = dmk_track_pulses,
	.track_from_pulses = dmk_track_from_pulses,
	.tracks		   = dmk_tracks,
	.describe	   = dmk_describe,
};
