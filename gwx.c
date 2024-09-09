#include "gwx.h"


/*
 * Extended low-level routines to interface with the Greaseweazle.
 *
 * These routines should return status only to the caller and not
 * give any textual warnings or errors.
 */

int
gw_setdrive(gw_devt gwfd, int drive, int densel)
{
	// XXX Check function return values.
	gw_select(gwfd, drive);
	gw_set_pin(gwfd, 2, densel);
	gw_motor(gwfd, drive, 1);

	return 0;
}


int
gw_unsetdrive(gw_devt gwfd, int drive)
{
	// XXX Check function return values.
	gw_motor(gwfd, drive, 0);
	gw_deselect(gwfd);

	return 0;
}


// Get min/max bandwidth for previous source/sink command. Mbps (float).
int
gw_get_bandwidth(gw_devt gwfd, double *min_bw, double *max_bw)
{
	struct gw_bw_stats bw_stats;

	int cmd_ret = gw_get_info_bw_stats(gwfd, &bw_stats);

	if (cmd_ret == ACK_OKAY) {
		*min_bw = (8 * bw_stats.min_bw.bytes) / bw_stats.min_bw.usecs;
		*max_bw = (8 * bw_stats.max_bw.bytes) / bw_stats.max_bw.usecs;
	}

	return cmd_ret;
}


/*
 * Stream bytes from GW.
 *
 * On success, returns number of bytes read.
 * On failure, returns either the negative value of the GW error code
 * or -99 if an internal error occurred.
 *
 * Data returned via fbuf must be free()d when done.
 */

ssize_t
gw_read_stream(gw_devt gwfd, int revs, int ticks, uint8_t **fbuf)
{
	int cmd_ret = gw_read_flux(gwfd, revs, ticks);

	if (cmd_ret != ACK_OKAY)
		return cmd_ret < 0 ? -99 : -cmd_ret;

	ssize_t fbuf_cnt = 0;

	do {
		uint8_t	rbuf[1];
		// Not sure what the returned number means.
		ssize_t gwr = gw_read(gwfd, rbuf, sizeof(rbuf));

		if (gwr == -1) {
			fbuf_cnt = -1;
			goto flux_status;
		}

		int	nrd;

#if defined(WIN64) || defined(WIN32)
		DWORD	errors;
		COMSTAT	comStat;

		if (!ClearCommError(gwfd, &errors, &comStat)) {
			fbuf_cnt = -1;
			goto flux_status;
		}

		nrd = comStat.cbInQue;
#else
		if (ioctl(gwfd, FIONREAD, &nrd) == -1) {
			fbuf_cnt = -1;
			goto flux_status;
		}
#endif

		// Does there need to be a breakout after so much time?
		if (nrd == 0) continue;

		// +1 to make room for rbuf[0] read above and added below.
		uint8_t *fbuf_new = realloc(*fbuf, fbuf_cnt + 1 + nrd);

		if (!fbuf_new) {
			fbuf_cnt = -1;
			// Goto here for CMD_GET_FLUX_STATUS cleanup?
			goto flux_status;
		}

		fbuf_new[fbuf_cnt++] = rbuf[0];

		gwr = gw_read(gwfd, fbuf_new + fbuf_cnt, nrd);

		if (gwr == -1) {
			fbuf_cnt = -1;
			goto flux_status;
		}

		*fbuf     = fbuf_new;
		fbuf_cnt += nrd;

		/* 0 byte in flux at end of last read means we're done. */
	} while (fbuf_cnt == 0 || (*fbuf)[fbuf_cnt-1] != 0);

flux_status:
	cmd_ret = gw_get_flux_status(gwfd);

	if (cmd_ret != ACK_OKAY)
		return cmd_ret < 0 ? -99 : -cmd_ret;

	return fbuf_cnt;
}


static int
decode_stub(uint32_t ticks, void *data)
{
	return 0;
}


/*
 * Decode the byte stream from the Greaseweazle.  Use the callbacks
 * from gwds to process the index holes and data pulses.
 *
 * Returns the number of bytes consumed in the stream, or -1 on error.
 * If the returned value was less than fbuf_cnt, the full buffer wasn't
 * processed due to a multibyte sequence that couldn't be fully decoded.
 * Call again with existing undecoded bytes plus additional following
 * bytes.
 */

ssize_t
gw_decode_stream(const uint8_t *fbuf,
		 size_t fbuf_cnt,
		 struct gw_decode_stream_s *gwds)
{
	uint32_t	gw_ticks = gwds->ds_ticks;
	const uint8_t	*f = fbuf, *ff = fbuf;
	const uint8_t	*const fend = fbuf + fbuf_cnt;
	int		(*f_imark)(uint32_t ticks, void *data);
	int		(*f_space)(uint32_t ticks, void *data);
	int		(*f_pulse)(uint32_t ticks, void *data);

	f_imark = gwds->decoded_imark ? gwds->decoded_imark : decode_stub;
	f_space = gwds->decoded_space ? gwds->decoded_space : decode_stub;
	f_pulse = gwds->decoded_pulse ? gwds->decoded_pulse : decode_stub;

	while (f < fend) {
		uint8_t c = *f++;

		if (c == 0) {
			ff = f;
			goto done;
		} else if (c == 255) {
			if ((f + 4) < fend) {
				uint8_t  fop = *f++;
				uint32_t v   = gw_read_28(f);
				f += 4;

				switch (fop) {
				case FLUXOP_INDEX:
					ff = f;
					gwds->ds_status = (*f_imark)(
							gw_ticks + v,
							gwds->imark_data);
					break;

				case FLUXOP_SPACE:
					gw_ticks += v;
					ff = f;
					gwds->ds_status = (*f_space)(v,
							gwds->space_data);
					break;

				default:
					return -1;
				}
			} else {
				goto done;
			}
		} else if (c < 250) {
			gw_ticks += c;
			ff = f;
			gwds->ds_status = (*f_pulse)(gw_ticks -
						     gwds->ds_last_pulse,
						     gwds->pulse_data);
			gwds->ds_last_pulse = gw_ticks;
		} else if (f < fend) {
			gw_ticks += 250 + (c - 250) * 255 + *f++ - 1;
			ff = f;
			gwds->ds_status = (*f_pulse)(gw_ticks -
						     gwds->ds_last_pulse,
						     gwds->pulse_data);
			gwds->ds_last_pulse = gw_ticks;
		} else {
			goto done;
		}

		if (gwds->ds_status)
			break;
	}

done:
	gwds->ds_ticks = gw_ticks;
	return ff - fbuf;
}


/*
 * Read from index hole to index hole for measuring the disk's
 * rotational period in nanoseconds.
 */

int
gw_get_period_ns(gw_devt gwfd, int drive, nsec_type clock_ns,
			nsec_type *period_ns)
{
	uint8_t	*fbuf = NULL;

	gw_motor(gwfd, drive, 1);

	ssize_t bytes_read = gw_read_stream(gwfd, 1, 0, &fbuf);

	if (bytes_read < 0) {
		// error handling
		return -1;
	}

	gw_motor(gwfd, drive, 0);

	uint32_t gw_ticks = 0;
	uint32_t index[2] = { ~0, ~0 };

	for (uint8_t *f = fbuf, c = *f++; c; c = *f++) {
		if (c == 255) {
			if ((f + 5 - fbuf) < bytes_read) {
				uint8_t  fop = *f++;
				uint32_t v   = gw_read_28(f);
				f += 4;

				switch (fop) {
				case FLUXOP_INDEX:
					index[COUNT_OF(index)-2] =
						index[COUNT_OF(index)-1];
					index[COUNT_OF(index)-1] = gw_ticks + v;
					break;

				case FLUXOP_SPACE:
					gw_ticks += v;
					break;

				default:
					// error handling
					return -1;
				}
			} else {
				// error handling
				return -1;
			}
		} else if (c < 250) {
			gw_ticks += c;
		} else if ((f + 1 - fbuf) < bytes_read) {
			gw_ticks += 250 + (c - 250) * 255 + *f++ - 1;
		} else {
			// error handling
			return -1;
		}
	}

	if (index[1] == ~0) {
		// error handling
		return -1;
	}

	*period_ns = (index[1] - index[0]) * clock_ns;

	return 0;
}


ssize_t
gw_write_stream(gw_devt gwfd,
		const uint8_t *enbuf,
		size_t enbuf_cnt,
		bool cue_at_index,
		bool terminate_at_index,
		int retries)
{
	int	retry_cnt = 0;

retry:;
	int cmd_ret = gw_write_flux(gwfd, cue_at_index, terminate_at_index);

	if (cmd_ret != ACK_OKAY)
		return cmd_ret < 0 ? -99 : -cmd_ret;

	ssize_t wr_cnt_total = 0;

	do {
		int wr_cnt = gw_write(gwfd, enbuf, enbuf_cnt);

		if (wr_cnt == -1) {
			wr_cnt_total = -1;
			goto flux_status;
		} else if (wr_cnt == 0) {
			break;
		}

		enbuf_cnt    -= wr_cnt;
		wr_cnt_total += wr_cnt;

	} while (enbuf_cnt > 0);

flux_status:
	/* Synchronize with GW */
	gw_read(gwfd, (uint8_t[1]){}, 1);

	cmd_ret = gw_get_flux_status(gwfd);

	if (cmd_ret == ACK_FLUX_UNDERFLOW && retry_cnt++ < retries)
		goto retry;
	else if (cmd_ret != ACK_OKAY)
		return cmd_ret < 0 ? -99 : -cmd_ret;

	return wr_cnt_total;
}


int
encode_ticks(uint32_t ticks,
	     uint32_t nfa_thresh,
	     uint32_t nfa_period,
	     uint8_t sbuf[GWCODE_MAX])
{
	int	sbuf_cnt = 0;

	if (ticks < 250) {
		sbuf[sbuf_cnt++] = ticks;
	} else if (ticks > nfa_thresh) {
		sbuf[sbuf_cnt++] = 255;
		sbuf[sbuf_cnt++] = FLUXOP_SPACE;
		gw_write_28(ticks, &sbuf[sbuf_cnt]);
		sbuf_cnt += 4;
		sbuf[sbuf_cnt++] = FLUXOP_ASTABLE;
		gw_write_28(nfa_period, &sbuf[sbuf_cnt]);
		sbuf_cnt += 4;
	} else {
		int high = (ticks - 250) / 255;
		if (high < 5) {
			sbuf[sbuf_cnt++] = 250 + high;
			sbuf[sbuf_cnt++] = 1 + (ticks - 250) % 255;
		} else {
			sbuf[sbuf_cnt++] = 255;
			sbuf[sbuf_cnt++] = FLUXOP_SPACE;
			gw_write_28(ticks - 249, &sbuf[sbuf_cnt]);
			sbuf_cnt += 4;
			sbuf[sbuf_cnt++] = 249;
		}
	}

	return sbuf_cnt;
}
