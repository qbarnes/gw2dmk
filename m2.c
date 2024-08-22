#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "greaseweazle.h"
#include "gw.h"
#include "gwx.h"
#include "msg.h"
#include "msg_levels.h"

static const char *usb_speed_str[] = {
	"Unknown",
	"Low (1.5 Mb/s)",
	"Full (12M b/s)",
	"High (480 Mb/s)",
	"Super (5 Gb/s)",
	"Super+ (10 Gb/s)"
};


const char *
usb_speed(uint8_t speed)
{
	if (speed >=0 && speed < 6) {
		return usb_speed_str[speed];
	} else {
		return "Invalid speed ID";
	}
}

int
show_gw_info(const struct gw_info *gw_info)
{
	printf("fw_major->fw_minor = %d->%d\n", gw_info->fw_major,
					      gw_info->fw_minor);
	printf("is_main_firmware = %d\n", gw_info->is_main_firmware);
	printf("max_cmd = %d\n", gw_info->max_cmd);
	printf("sample_freq = %u\n", gw_info->sample_freq);
	printf("hw_model.submodel = %d.%d\n", gw_info->hw_model,
					      gw_info->hw_submodel);
	printf("usb_speed = %d (%s)\n", gw_info->usb_speed,
				   usb_speed(gw_info->usb_speed));

	return 0;
}


int
show_gw_delay(const struct gw_delay *gw_delay)
{
	printf("select delay = %d\n", gw_delay->select_delay);
	printf("step delay = %d\n", gw_delay->step_delay);
	printf("seek settle delay = %d\n", gw_delay->seek_settle);
	printf("motor delay = %d\n", gw_delay->motor_delay);
	printf("auto off = %d\n", gw_delay->auto_off);

	return 0;
}


int
test_get_info(gw_devt gwfd)
{
	struct gw_info	gw_info;

	gw_get_info(gwfd, &gw_info);

	show_gw_info(&gw_info);

	return 0;
}


int
test_get_set_params(gw_devt gwfd)
{
	struct gw_delay gw_delay;

	gw_get_params(gwfd, &gw_delay);

	printf("\nOriginal delay values:\n");
	show_gw_delay(&gw_delay);

	gw_delay.seek_settle += 1;
	gw_set_params(gwfd, &gw_delay);

	printf("\nModified delay values with seek settle +=1:\n");
	gw_get_params(gwfd, &gw_delay);

	show_gw_delay(&gw_delay);

	return 0;
}


int
test_seek(gw_devt gwfd, int drive)
{
	gw_setdrive(gwfd, drive, 0);

	gw_seek(gwfd, 0);
	gw_seek(gwfd, 30);
	sleep(1);
	gw_seek(gwfd, 0);

	gw_unsetdrive(gwfd, drive);

	return 0;
}


int
test_get_bandwidth(gw_devt gwfd)
{
	double	min_bw, max_bw;

	int cmd_ret = gw_get_bandwidth(gwfd, &min_bw, &max_bw);

	if (cmd_ret != ACK_OKAY)
		return cmd_ret;

	printf("Bandwidth(min,max) = (%f,%f)\n", min_bw, max_bw);

	return 0;
}


int
test_read_track_flux(gw_devt gwfd, int drive)
{
	gw_setdrive(gwfd, drive, 0);

	uint8_t	*fbuf = NULL;
	ssize_t gwr = gw_read_stream(gwfd, 3, 0, &fbuf);

	gw_unsetdrive(gwfd, drive);

	free(fbuf);

	if (gwr >= 0)
		printf("flux bytes read = %lld\n", (long long int)gwr);
	else
		fprintf(stderr, "flux read error\n");

	return 0;
}


int
test_get_rpm(gw_devt gwfd, int drive)
{
	printf("Testing RPM.\n");

	struct gw_info	gw_info;

	int cmd_ret = gw_get_info(gwfd, &gw_info);

	if (cmd_ret != ACK_OKAY) {
		// error handling
		return -1;
	}

	nsec_type clock_ns = 1000000000.0 / gw_info.sample_freq;
	nsec_type period_ns;

	gw_setdrive(gwfd, drive, 0);

	cmd_ret = gw_get_period_ns(gwfd, drive, clock_ns, &period_ns);

	gw_unsetdrive(gwfd, drive);

	if (cmd_ret == -1) {
		printf("Getting RPM failed.\n");
		return -1;
	}

	printf("Rate: %.3f rpm ; Period: %.3f ms\n",
		60000000000 / period_ns,
		period_ns / 1000000);

	return 0;
}


#if 0
static int
imark_fn(uint32_t imark, void *data)
{
	return 0;
}
#endif


struct dpdata_s {
	uint32_t	*debuf;
	size_t		debuf_len;
	size_t		debuf_cnt;
	uint32_t	prev_space;
};


#if 0
static int
dspace_fn(uint32_t space, void *data)
{
	struct dpdata_s *dps = (struct dpdata_s *)data;

	dps->prev_space += space;

	return 0;
}
#endif


#if 0
static int
dpulse_fn(uint32_t pulse, void *data)
{
	struct dpdata_s *dps = (struct dpdata_s *)data;

	if (dps->debuf_cnt >= dps->debuf_len)
		return -1;

	dps->debuf[dps->debuf_cnt++] = pulse;

	return 0;
}


struct epdata_s {
	uint8_t		*enbuf;
	size_t		enbuf_len;
	size_t		enbuf_cnt;
};

static int
epulse_fn(uint8_t *sbuf, int sbuf_cnt, void *data)
{
	struct epdata_s *eps = (struct epdata_s *)data;

	if (eps->enbuf_cnt + sbuf_cnt >= eps->enbuf_len)
		return -1;

	memcpy(&eps->enbuf[eps->enbuf_cnt], sbuf, sbuf_cnt);
	eps->enbuf_cnt += sbuf_cnt;
	return 0;
}
#endif


#if 0
void
test_encode_stream(gw_devt gwfd)
{
	struct gw_info	gw_info;

	int gi_ret = gw_get_info(gwfd, &gw_info);

	if (gi_ret != ACK_OKAY) {
		fprintf(stderr, "gw_get_info failed: %s (%d).\n",
			gw_cmd_ack(gi_ret), gi_ret);
		exit(1);
	}

	//int fd = open("ra.out", O_RDONLY);
	int fd = open("dat.out", O_RDONLY);
	if (fd == -1) {
		perror("Error opening ra.out");
		exit(1);
	}

	/* Get the size of the file using fstat */
	struct stat sb;
	if (fstat(fd, &sb) == -1) {
		perror("Error getting file size");
		close(fd);
		exit(1);
	}

	/* File size must be non-zero */
	if (sb.st_size == 0) {
		fprintf(stderr, "Error: File is empty.\n");
		close(fd);
		exit(1);
	}

	uint8_t *fbuf = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (fbuf == MAP_FAILED) {
		perror("Error mapping file");
		close(fd);
		exit(1);
	}
	close(fd);

	printf("\nBinary file is %lld in length\n", (long long)sb.st_size);

	/*
	 * Decode 8-bit encoded stream into 32-bit pulse stream
	 */

	uint32_t *pbuf = malloc(2000000);
	struct dpdata_s pds = {
			       .debuf = pbuf,
			       .debuf_len = 2000000,
			       .debuf_cnt = 0,
			       .prev_space = 0
			      };

	struct gw_decode_stream_s gwds = {
					  .ds_ticks = 0,
					  .ds_last_pulse = 0,
					  .ds_status = -1,
					  .decoded_imark = NULL,
					  .imark_data = NULL,
					  .decoded_space = NULL,
					  .space_data = NULL,
					  .decoded_pulse = dpulse_fn,
					  .pulse_data = &pds
					 };

	/* decode into pbuf */
	ssize_t gd_ret = gw_decode_stream(fbuf, sb.st_size, &gwds);

	if (gd_ret == -1) {
		fprintf(stderr, "gw_decode_stream() failed.\n");
		exit(1);
	}

	printf("gw_decode_stream() return = %lld\n", (long long)gd_ret);

	if (gd_ret != sb.st_size) {
		fprintf(stderr, "Did not process all bytes (%d remaining)\n",
			(int)(sb.st_size - gd_ret));
	}

	printf("pds.debuf_cnt = %lld\n", (long long)pds.debuf_cnt);

	FILE *fp = fopen("decoded_stream.txt", "w");
	unsigned int big = 0, small = ~0;

	for (int i = 0; i < pds.debuf_cnt; ++i) {
		fprintf(fp, "%7d: 0x%04x\n", i, pbuf[i]);
		if (pbuf[i] > big) big = pbuf[i];
		if (pbuf[i] < small) small = pbuf[i];
	}

	fprintf(fp, "smallest = 0x%04x, biggest = 0x%04x\n",
		small, big);

	fclose(fp);

	/*
	 * re-encode pulse stream back into 8-bit encoded stream
	 */

	uint8_t *tbuf = malloc(2000000);
	struct epdata_s pes = { tbuf, 2000000, 0 };
	struct gw_encode_stream_s gwes = {
		.encoded_pulse = epulse_fn,
		.pulse_data    = &pes
	};

	ssize_t ge_ret = gw_encode_stream(pbuf, pds.debuf_cnt,
					  gw_info.sample_freq, &gwes);

	if (ge_ret == -1) {
		fprintf(stderr, "gw_encode_stream() failed.\n");
		exit(1);
	}

	printf("\ngw_encode_stream() return = %lld\n", (long long)ge_ret);
	printf("pes.enbuf_cnt = %lld\n", (long long)pes.enbuf_cnt);

	if (sb.st_size != pes.enbuf_cnt) {
		fprintf(stderr, "File sizes differ: %lld %lld\n",
			(long long)sb.st_size, (long long)pes.enbuf_cnt);
	}

	int ts = 0, te = ge_ret;
	int f, t, fend = sb.st_size;
	for (f = 0, t = 0; f < fend; ++f, ++t) {
		/* Helps skip dummy encoding value. */
		if (fbuf[f] == 0) {
			fend = f;
			break;
		}

		if (fbuf[f] != tbuf[t]) {
			if (fbuf[f] == 0xff && fbuf[f+1] == FLUXOP_INDEX) {
				printf("Skipping index @ %d.\n", f);
				f += 6;
				if (ts == 0) {
					ts = t;
				} else if (te == ge_ret) {
					te = t;
				}
			} else {
				printf("fbuf[%d] = 0x%02x, tbuf[%d] = 0x%02x\n",
					f, fbuf[f], t, tbuf[t]);
				break;
			}
		}
	}

	printf("Bytes matching = %d/%d\n", f, fend);

	FILE *rfp = fopen("redecoded_stream.out", "w");
	size_t fwr = fwrite(&tbuf[ts], te - ts, 1, rfp);
	/* Grab the dummy load and '\0' byte at end of recode. */
	fwr += fwrite(&tbuf[ge_ret-8], 8, 1, rfp);
	if (fwr != 2) {
		fprintf(stderr, "Redecoded write failed.\n");
		exit(1);
	}
	fclose(rfp);
}


void
test_write(gw_devt gwfd, int drive)
{
	//const char rawfile[] = "redecoded_stream.out";
	//const char rawfile[] = "gwraw.out";
	//const char rawfile[] = "gwr1.out";
	const char rawfile[] = "dat.out";

	FILE *fp = fopen(rawfile, "rb");

	struct stat sb;
	if (fstat(fileno(fp), &sb) == -1) {
		perror("Error getting file size");
		fclose(fp);
		exit(1);
	}

	uint8_t *buf = malloc(sb.st_size);
	if (!buf) {
		perror("Error getting memory");
		fclose(fp);
		exit(1);
	}

	size_t fret = fread(buf, sb.st_size, 1, fp);

	if (fret != 1) {
		printf("fret = %d\n", (int)fret);
		fprintf(stderr, "Error reading %s: %s",
			rawfile, strerror(errno));
		fclose(fp);
		free(buf);
		exit(1);
	}

	gw_setdrive(gwfd, drive, 0);

	gw_seek(gwfd, 0);
	gw_head(gwfd, 0);

#if 1
	// XXX -- NOT needed!  Or maybe it is sometimes?
	// Maybe to ensure the disk is up to speed?
	gw_read_flux(gwfd, 1, 0);

	ssize_t rret;
	uint8_t rbuf[1024];
	do {
		rret = gw_read(gwfd, rbuf, sizeof(rbuf));

		if (rret < 1)
			break;

	} while (rbuf[rret-1] != 0);

	int gfsr = gw_get_flux_status(gwfd);

	if (gfsr != ACK_OKAY) {
		fprintf(stderr, "gw_get_flux_status failed.\n");
		exit(1);
	}
#endif

	printf("sb.st_size = %lld\n", (long long)sb.st_size);
	size_t fs;
#if 0
	fs = sb.st_size;
#else
	for (fs = 0; buf[fs]; ++fs);
	printf("fs = %d 0x%02x\n", (int)fs, buf[fs]);
	fs += 1;
#endif
	ssize_t gwwsr = gw_write_stream(gwfd, buf, fs, true, true, 5);
	printf("gw_write_stream() returned %d\n", (int)gwwsr);

	gw_unsetdrive(gwfd, drive);
}
#endif


int
main(int argc, char **argv)
{
#if 0
	uint32_t ticks = gw_read_28((const uint8_t[]){0x21, 0x17, 0x01, 0x01});
	printf ("ticks{0x21, 0x17, 0x01, 0x01} = %u\n", (unsigned int)ticks);
#endif

	const char *devlogfile = "m2.gwlog";
	if (devlogfile) {
		FILE *dlfp = fopen(devlogfile, "w");

		if (!dlfp) {
			msg_fatal("Failed to open device log file '%s': %s\n",
				  devlogfile, strerror(errno));
		}

		if (!gw_set_logfp(dlfp)) {
			msg_fatal("Failed to set device log file '%s': %s\n",
				  devlogfile, strerror(errno));
		}
	}

#if linux
	gw_devt gwfd = gw_open("/dev/ttyACM0");
#elif defined(WIN64) || defined(WIN32)
	gw_devt gwfd = gw_open("COM3");
#endif

	if (gwfd == GW_DEVT_INVALID) {
		fprintf(stderr, "Failed to open GW device.\n");
		return EXIT_FAILURE;
	}

	gw_init(gwfd);

	// Get us back into a saner state if crashed on last run.
	gw_reset(gwfd);

	test_get_info(gwfd);

	gw_set_bus_type(gwfd, BUS_IBMPC);

#if 0
	test_get_set_params(gwfd);

	test_seek(gwfd, 0);

	//test_read_track_flux(gwfd, 0);

	test_get_rpm(gwfd, 0);
#endif

	//test_encode_stream(gwfd);

	//test_write(gwfd, 0);

	return 0;
}
