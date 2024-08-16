#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "greaseweazle.h"
#include "gw.h"
#include "gwx.h"

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

	nanoseconds_t clock_ns = 1000000000.0 / gw_info.sample_freq;
	nanoseconds_t period_ns;

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


int
main(int argc, char **argv)
{
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

	test_get_set_params(gwfd);

	test_seek(gwfd, 0);

	//test_read_track_flux(gwfd, 0);

	test_get_rpm(gwfd, 0);

	return 0;
}
