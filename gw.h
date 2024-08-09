#ifndef GW_H
#define GW_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#if defined(WIN64) || defined(WIN32)
#include <winsock2.h>
  #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  /* These can be considered no-ops. */
  #define htole16(x) ((uint16_t)(x))
  #define htole32(x) ((uint32_t)(x))
  #define le16toh(x) ((uint16_t)(x))
  #define le32toh(x) ((uint32_t)(x))
  #else
  #error "Need macros for BE MSW."
  #endif
#else
#include <endian.h>
#endif

#if defined(WIN64) || defined(WIN32)
#include <windows.h>
#include <tchar.h>
#else
#include <signal.h>
#include <sys/ioctl.h>
#include <termios.h>
  #if linux
  #include <linux/usb/ch9.h>
  #endif
#endif

#include "misc.h"
#include "greaseweazle.h"


#define GW_MAX_TRACKS	88

#if defined(WIN64) || defined(WIN32)
#define GW_DEVT_INVALID	INVALID_HANDLE_VALUE
typedef HANDLE		gw_devt;
#else
#define GW_DEVT_INVALID	(-1)
typedef int		gw_devt;
#endif


struct gw_cmd {
	uint8_t		*cmd;
	size_t		cmd_cnt;
	uint8_t		cmd_ret[2];
	uint8_t		*rbuf;
	size_t		rbuf_cnt;
	size_t		rbuf_cnt_ret;
};


extern const char *gw_cmd_name(uint8_t cmd);

extern const char *gw_cmd_ack(uint8_t response);

extern FILE *gw_set_logfp(FILE *fp);

extern FILE *gw_get_logfp(void);

extern gw_devt gw_open(const char *gw_devname);

extern int gw_close(gw_devt gwfd);

extern int gw_init(gw_devt gwfd);

extern ssize_t gw_read(gw_devt gwfd, uint8_t *rbuf, size_t rbuf_cnt);

extern int gw_write(gw_devt gwfd, const uint8_t *wbuf, size_t wbuf_cnt);

extern int gw_do_command(gw_devt gwfd, struct gw_cmd *gw_cmd);

extern int gw_get_info(gw_devt gwfd, struct gw_info *gw_info);

extern int gw_get_info_bw_stats(gw_devt gwfd, struct gw_bw_stats *gw_bw_stats);

extern int gw_seek(gw_devt gwfd, int cyl);

extern int gw_head(gw_devt gwfd, int head);

extern int gw_set_params(gw_devt gwfd, const struct gw_delay *gw_delay);

extern int gw_get_params(gw_devt gwfd, struct gw_delay *gw_delay);

extern int gw_motor(gw_devt gwfd, int drive, int motor);

extern ssize_t gw_read_flux(gw_devt gwfd, int revs, int ticks);

extern int gw_read_flux_status(gw_devt gwfd);

extern int gw_select(gw_devt gwfd, int drive);

extern int gw_deselect(gw_devt gwfd);

extern int gw_set_bus_type(gw_devt gwfd, int bus_type);

extern int gw_set_pin(gw_devt gwfd, int pin, int level);

extern int gw_reset(gw_devt gwfd);

#ifdef __cplusplus
}
#endif

#endif
