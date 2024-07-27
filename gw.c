#include "gw.h"


/*
 * Low-level routines to interface with the Greaseweazle.
 *
 * These routines should return status only to the caller and not
 * give any textual warnings or errors.
 */


const char *
gw_cmd_name(uint8_t cmd) 
{
	static const char *const cmd_names[] = {
		"Get Info",
		"Update",
		"Seek",
		"Head",
		"Set Params",
		"Get Params",
		"Motor",
		"Read Flux",
		"Write Flux",
		"Get Flux Status",
		"Get Index Times",
		"Switch FW Mode",
		"Select",
		"Deselect",
		"Set Bus Type",
		"Set Pin",
		"Reset",
		"Erase Flux",
		"Source Bytes",
		"Sink Bytes",
		"Get Pin",
		"Test Mode",
		"No Click Step"
	};

	return (cmd < COUNT_OF(cmd_names)) ? cmd_names[cmd] : "Unknown";
}


const char *
gw_cmd_ack(uint8_t response) 
{
	static const char *const cmd_acks[] = {
		"Okay",
		"Bad Command",
		"No Index",
		"Track 0 Not Found",
		"Flux Overflow",
		"Flux Underflow",
		"Disk is Write Protected",
		"No Drive Unit Selected",
		"No Bus Type (eg. Shugart, IBM/PC) Specified",
		"Invalid Unit Number",
		"Invalid Pin",
		"Invalid Cylinder",
		"Out of SRAM",
		"Out of Flash"
	};

	return (response < COUNT_OF(cmd_acks)) ?
			cmd_acks[response] : "Unknown";
}


static FILE *logfp = NULL;


FILE *
gw_get_logfp()
{
	return logfp;
}


FILE *
gw_set_logfp(FILE *fp)
{
	if (logfp && (fclose(logfp) == EOF))
		return NULL;

	logfp = fp;

	return logfp;
}


static void
db_dump(const uint8_t *buf, size_t buf_cnt, int writing)
{
	for (int i = 0; i < buf_cnt; ++i) {
		static const char *arr_prefix[] = { "<- ", "-> " };
		const char *p = (i % 16) ? " " :
				(i == 0) ? arr_prefix[writing] : "   ";

		if (logfp)
			fprintf(logfp, "%s0x%02x", p, buf[i]);

		/* new-line at 16 items or reached last item. */
		if (!((i+1) % 16) || (i == (buf_cnt-1))) {
			if (logfp)
				fprintf(logfp, "\n");
		}
	}
}


static void
rd_db_dump(const uint8_t *buf, size_t buf_cnt)
{
	db_dump(buf, buf_cnt, 0);
}


static void
wr_db_dump(const uint8_t *buf, size_t buf_cnt)
{
	db_dump(buf, buf_cnt, 1);
}


#if defined(WIN64) || defined(WIN32)
static int
getlasterror2errno(DWORD error)
{
	switch(error) {
	case ERROR_SUCCESS:		return 0;
	case ERROR_FILE_NOT_FOUND:	return ENOENT;
	case ERROR_PATH_NOT_FOUND:	return ENOENT;
	case ERROR_ACCESS_DENIED:	return EACCES;
	case ERROR_INVALID_HANDLE:	return EBADF;
	case ERROR_NOT_ENOUGH_MEMORY:	return ENOMEM;
	case ERROR_INVALID_DATA: 	return EINVAL;
	case ERROR_OUTOFMEMORY: 	return ENOMEM;
	case ERROR_SHARING_VIOLATION: 	return ETXTBSY;
	case ERROR_INVALID_PARAMETER: 	return EINVAL;
	case ERROR_OPEN_FAILED: 	return ENOENT;
	case ERROR_ALREADY_EXISTS: 	return EEXIST;
	case ERROR_DIRECTORY: 		return ENOTDIR;
	case ERROR_OPERATION_ABORTED: 	return EINTR;
	case ERROR_IO_INCOMPLETE: 	return EAGAIN;
	case ERROR_IO_PENDING:		return EAGAIN;
	default:			return EINVAL;
	}
}
#endif


/*
 * Open the Greaseweazle serial device.
 *
 * Returns file descriptor on success, or GW_DEVT_INVALID on failure.
 * "errno" returns a possible reason for failure.
 */

gw_devt
gw_open(const char *gw_devname)
{
#if defined(WIN64) || defined(WIN32)

	HANDLE cfh = CreateFile(_T(gw_devname), GENERIC_READ | GENERIC_WRITE,
				0, NULL, OPEN_EXISTING, 0, NULL);

	if (cfh == INVALID_HANDLE_VALUE)
		errno = getlasterror2errno(GetLastError());

	return cfh;

#else

	return open(gw_devname, O_RDWR|O_CLOEXEC|O_NOCTTY);

#endif
}


/*
 * Close the Greaseweazle serial device.
 *
 * Returns 0 success or -1 on failure.
 * "errno" returns a possible reason for failure.
 */

int
gw_close(gw_devt gwfd)
{
#if defined(WIN64) || defined(WIN32)

	BOOL cret = CloseHandle(gwfd);
	if (cret) {
		return 0;
	} else {
		errno = getlasterror2errno(GetLastError());
		return -1;
	}

#else

	return close(gwfd);

#endif
}


/*
 * Initialize the GW device.
 *
 * 0: Operation succeeded.
 * Non-zero: Error value with possible error in "errno".
 */

int
gw_init(gw_devt gwfd)
{
	int		err = 0;

#if defined(WIN64) || defined(WIN32)

	DCB	dcbSerialParams = { 0 };

	dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

	if (!GetCommState(gwfd, &dcbSerialParams)) {
		err = -1;
		goto err;
	}

	dcbSerialParams.BaudRate = CBR_9600;
	dcbSerialParams.ByteSize = 8;
	dcbSerialParams.StopBits = ONESTOPBIT;
	dcbSerialParams.Parity   = NOPARITY;

	if(!SetCommState(gwfd, &dcbSerialParams)) {
		err = -2;
		goto err;
	}

	COMMTIMEOUTS	timeouts = { 0 };

	timeouts.ReadIntervalTimeout         = MAXDWORD - 1;
	timeouts.ReadTotalTimeoutMultiplier  = 0;
	timeouts.ReadTotalTimeoutConstant    = 0;
	timeouts.WriteTotalTimeoutMultiplier = 0;
	timeouts.WriteTotalTimeoutConstant   = 0;

	if(!SetCommTimeouts(gwfd, &timeouts)) {
		err = -3;
		goto err;
	}

err:
	if (err)
		errno = getlasterror2errno(GetLastError());

#else
	int		flag;
	struct termios	t;

	tcgetattr(gwfd, &t);

	t.c_iflag = 0;
	t.c_oflag = 0;
	t.c_cflag = CREAD;
	t.c_lflag = 0;
	t.c_cc[VMIN] = 1;
	t.c_cc[VTIME] = 0;

	cfsetspeed(&t, 9600);
	tcsetattr(gwfd, TCSANOW, &t);

	/* Reset the device by setting DTR and RTS. */

	flag = TIOCM_DTR;
	if (ioctl(gwfd, TIOCMBIS, &flag) == -1) {
		err = -1;
		goto err;
	}

	flag = TIOCM_RTS;
	if (ioctl(gwfd, TIOCMBIS, &flag) == -1) {
		err = -2;
		goto err;
	}

	/* Flush any pending I/O from the Greaseweazle. */
	tcsetattr(gwfd, TCSAFLUSH, &t);
err:

#endif

	return err;
}


/*
 * Read from the GW.
 *
 * Return the number of bytes read up to "rbuf_cnt" bytes.
 * If an error occurs, then return -1.
 *
 * "errno" returns a possible reason for failure.
 */

ssize_t
gw_read(gw_devt gwfd, uint8_t *rbuf, size_t rbuf_cnt)
{
#if defined(WIN64) || defined(WIN32)

	DWORD rd_cnt = 0;
	if (ReadFile(gwfd, rbuf, rbuf_cnt, &rd_cnt, NULL) == FALSE) {
		errno = getlasterror2errno(GetLastError());
		rd_cnt = -1;
	}

#else
	/*
	 * Since we can't successfully restart I/O to a device,
	 * around the read() syscall, hold off delivery of SIGINTs.
	 */

	sigset_t	new_set, old_set;

	sigemptyset(&new_set);
	sigaddset(&new_set, SIGINT);

	if (sigprocmask(SIG_BLOCK, &new_set, &old_set) < 0)
		return -1;

	ssize_t rd_cnt = read(gwfd, rbuf, rbuf_cnt);

	if (sigprocmask(SIG_SETMASK, &old_set, NULL) < 0)
		return -1;

#endif

	if (rd_cnt != -1) {
		int eno = errno;
		rd_db_dump(rbuf, rd_cnt);
		errno = eno;
	}

	return rd_cnt;
}


/*
 * Write to the GW.
 *
 * Return the number of bytes written up to "wbuf_cnt" bytes.
 * If an error occurred, return -1.
 *
 * "errno" may be checked for a cause of error.
 */

int
gw_write(gw_devt gwfd, const uint8_t *wbuf, size_t wbuf_cnt)
{
	wr_db_dump(wbuf, wbuf_cnt);

#if defined(WIN64) || defined(WIN32)

	DWORD	dwBytesWritten;
	ssize_t	wr_cnt;

	if (WriteFile(gwfd, wbuf, wbuf_cnt, &dwBytesWritten, NULL) == FALSE) {
		errno = getlasterror2errno(GetLastError());
		wr_cnt = -1;
	} else {
		wr_cnt = dwBytesWritten;
	}

#else
	/*
	 * Since we can't successfully restart I/O to a device,
	 * around the write() syscall, hold off delivery of SIGINTs.
	 */

	sigset_t	new_set, old_set;

	sigemptyset(&new_set);
	sigaddset(&new_set, SIGINT);

	if (sigprocmask(SIG_BLOCK, &new_set, &old_set) < 0)
		return -1;

	ssize_t wr_cnt = write(gwfd, wbuf, wbuf_cnt);

	if (sigprocmask(SIG_SETMASK, &old_set, NULL) < 0)
		return -1;
#endif

	return wr_cnt;
}


/*
 * Run a GW command.
 *
 * Return the status byte from the command, or a negative value if
 * an error occurred:
 *    -1   I/O failure writing command
 *    -2   I/O failure reading command
 *    -3   Unexpected command returned, check "cmd_ret[0]"
 */

int
gw_do_command(gw_devt gwfd, struct gw_cmd *gw_cmd)
{
	ssize_t wr_cnt = gw_write(gwfd, gw_cmd->cmd, gw_cmd->cmd_cnt);

	if (wr_cnt == -1 || wr_cnt != gw_cmd->cmd_cnt)
		return -1;

	ssize_t rd_cnt = gw_read(gwfd, gw_cmd->cmd_ret,
		sizeof(gw_cmd->cmd_ret));

	if (rd_cnt == -1 || rd_cnt != 2) {

		return -2;

	} else if (gw_cmd->cmd_ret[0] != gw_cmd->cmd[0]) {

		return -3;

	} else if (gw_cmd->cmd_ret[1] == ACK_OKAY && gw_cmd->rbuf_cnt) {

		gw_cmd->rbuf_cnt_ret =
			gw_read(gwfd, gw_cmd->rbuf, gw_cmd->rbuf_cnt);

	}

	return gw_cmd->cmd_ret[1];
}


int
gw_get_info(gw_devt gwfd, struct gw_info *gw_info)
{
	int	ret;
	uint8_t	rbuf[32];

	struct gw_cmd gw_cmd = {
		      (uint8_t[]){CMD_GET_INFO, 3, GETINFO_FIRMWARE}, 3,
		      { 0, 0 }, rbuf, sizeof(rbuf), 0
	};

	ret = gw_do_command(gwfd, &gw_cmd);

	gw_info->fw_major	  = rbuf[0];
	gw_info->fw_minor	  = rbuf[1];
	gw_info->is_main_firmware = rbuf[2];
	gw_info->max_cmd	  = rbuf[3];
	gw_info->sample_freq	  = le32toh(*(uint32_t *)&rbuf[4]);
	gw_info->hw_model	  = rbuf[8];
	gw_info->hw_submodel	  = rbuf[9];
	gw_info->usb_speed	  = rbuf[10];

	return ret;
}


int
gw_get_info_bw_stats(gw_devt gwfd, struct gw_bw_stats *gw_bw_stats)
{
	uint8_t	rbuf[32];

	struct gw_cmd gw_cmd = {
		      (uint8_t[]){CMD_GET_INFO, 3, GETINFO_BW_STATS}, 3,
		      { 0, 0 }, rbuf, sizeof(rbuf), 0
	};

	int ret = gw_do_command(gwfd, &gw_cmd);

	gw_bw_stats->min_bw.bytes = le32toh(*(uint32_t *)&rbuf[0]);
	gw_bw_stats->min_bw.usecs = le32toh(*(uint32_t *)&rbuf[4]);
	gw_bw_stats->max_bw.bytes = le32toh(*(uint32_t *)&rbuf[8]);
	gw_bw_stats->max_bw.usecs = le32toh(*(uint32_t *)&rbuf[16]);

	return ret;
}


int
gw_seek(gw_devt gwfd, int cyl)
{
	return gw_do_command(gwfd,
		&(struct gw_cmd){(uint8_t[]){CMD_SEEK, 3, cyl}, 3});
}


int
gw_head(gw_devt gwfd, int head)
{
	return gw_do_command(gwfd,
		&(struct gw_cmd){(uint8_t[]){CMD_HEAD, 3, head}, 3});
}


int
gw_set_params(gw_devt gwfd, const struct gw_delay *gw_delay)
{
	union {
		uint8_t		cbuf[10];
		uint16_t	cbuf16[5];
	} u = {
		.cbuf16 = {
			htole16(gw_delay->select_delay),
			htole16(gw_delay->step_delay),
			htole16(gw_delay->seek_settle),
			htole16(gw_delay->motor_delay),
			htole16(gw_delay->auto_off)
		}
	};

	struct gw_cmd gw_cmd = {
		(uint8_t[3+sizeof(u.cbuf)])
		{CMD_SET_PARAMS, 3+sizeof(u.cbuf), 0},
		3+sizeof(u.cbuf), { 0, 0 }, 0, 0, 0
	};

	memcpy(&gw_cmd.cmd[3], u.cbuf, sizeof(u.cbuf));

	return gw_do_command(gwfd, &gw_cmd);
}


int
gw_get_params(gw_devt gwfd, struct gw_delay *gw_delay)
{
	int	ret;
	union {
		uint8_t		rbuf[10];
		uint16_t	rbuf16[5];
	} u;

	struct gw_cmd gw_cmd = {
		(uint8_t[])
		{CMD_GET_PARAMS, 4, PARAMS_DELAYS, sizeof(u.rbuf)}, 4,
		{ 0, 0 }, u.rbuf, sizeof(u.rbuf), 0
	};

	ret = gw_do_command(gwfd, &gw_cmd);

	gw_delay->select_delay = le16toh(u.rbuf16[0]);
	gw_delay->step_delay   = le16toh(u.rbuf16[1]);
	gw_delay->seek_settle  = le16toh(u.rbuf16[2]);
	gw_delay->motor_delay  = le16toh(u.rbuf16[3]);
	gw_delay->auto_off     = le16toh(u.rbuf16[4]);

	return ret;
}


int
gw_motor(gw_devt gwfd, int drive, int motor)
{
	return gw_do_command(gwfd,
		&(struct gw_cmd){(uint8_t[]){CMD_MOTOR, 4, drive, motor}, 4});
}


ssize_t
gw_read_flux(gw_devt gwfd, int revs, int ticks)
{
	uint16_t crevs  = htole16(revs ? revs+1 : 0);
	uint32_t cticks = htole32(ticks);

	int cmd_ret = gw_do_command(gwfd,
					&(struct gw_cmd){(uint8_t[])
					{CMD_READ_FLUX, 8,
					cticks & 0x8,
					(cticks >> 8) & 0xff,
					(cticks >> 16) & 0xff,
					(cticks >> 24) & 0xff,
					crevs & 0xff,
					(crevs >> 8) & 0xff},
					8, { 0, 0 }, 0, 0, 0});

	if (cmd_ret != ACK_OKAY) {
		// error handling
	}

	return cmd_ret;
}


#if 0
gw_write_flux(int gwfd, )
#endif


int
gw_read_flux_status(gw_devt gwfd)
{
	int cmd_ret = gw_do_command(gwfd,
		&(struct gw_cmd){(uint8_t[]){CMD_GET_FLUX_STATUS, 2}, 2});

	if (cmd_ret != ACK_OKAY) {
		// error handling
		return -1;
	}

	return cmd_ret;
}


#if 0
gw_get_flux_status()
gw_switch_fw_mode()
#endif


int
gw_select(gw_devt gwfd, int drive)
{
	return gw_do_command(gwfd,
		&(struct gw_cmd){(uint8_t[]){CMD_SELECT, 3, drive}, 3});
}


int
gw_deselect(gw_devt gwfd)
{
	return gw_do_command(gwfd,
		&(struct gw_cmd){(uint8_t[]){CMD_DESELECT, 2}, 2});
}


/*
 * bus_type:
 *    Invalid  = BUS_NONE (0)
 *    IBM PC   = BUS_IBMPC (1)
 *    Shugart  = BUS_SHUGART (2)
 *    Apple II = BUS_APPLE2 (3)
 */

int
gw_set_bus_type(gw_devt gwfd, int bus_type)
{
	return gw_do_command(gwfd,
		&(struct gw_cmd){(uint8_t[]){CMD_SET_BUS_TYPE, 3, bus_type}, 3});
}


int
gw_set_pin(gw_devt gwfd, int pin, int level)
{
	return gw_do_command(gwfd,
		&(struct gw_cmd){(uint8_t[]){CMD_SET_PIN, 4, pin, level}, 4});
}


int
gw_reset(gw_devt gwfd)
{
	return gw_do_command(gwfd,
		&(struct gw_cmd){(uint8_t[]){CMD_RESET, 2}, 2});
}


#if 0
gw_erase_flux()
gw_source_bytes()
gw_sink_bytes()
#endif
