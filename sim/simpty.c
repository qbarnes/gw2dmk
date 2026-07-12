#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "simpty.h"


int
sim_pty_open(struct sim_pty *pty, const char *link_path)
{
	pty->mfd = posix_openpt(O_RDWR | O_NOCTTY);

	if (pty->mfd == -1)
		return -1;

	if (grantpt(pty->mfd) == -1 || unlockpt(pty->mfd) == -1)
		goto err_mfd;

	if (ptsname_r(pty->mfd, pty->slave_path,
		      sizeof(pty->slave_path)) != 0)
		goto err_mfd;

	/*
	 * Keepalive open of the slave so the master never sees EIO
	 * when a host tool closes its fd.  Also used to put the pty
	 * into raw mode before any host connects, so the line
	 * discipline can't mangle protocol bytes.
	 */

	pty->kfd = open(pty->slave_path, O_RDWR | O_NOCTTY);

	if (pty->kfd == -1)
		goto err_mfd;

	struct termios	t;

	if (tcgetattr(pty->kfd, &t) == 0) {
		cfmakeraw(&t);
		tcsetattr(pty->kfd, TCSANOW, &t);
	}

	int	fl = fcntl(pty->mfd, F_GETFL);

	if (fl == -1 || fcntl(pty->mfd, F_SETFL, fl | O_NONBLOCK) == -1)
		goto err_kfd;

	pty->link_path = NULL;

	if (link_path) {
		unlink(link_path);

		if (symlink(pty->slave_path, link_path) == -1)
			goto err_kfd;

		pty->link_path = strdup(link_path);
	}

	return 0;

err_kfd:
	close(pty->kfd);
	pty->kfd = -1;
err_mfd:
	close(pty->mfd);
	pty->mfd = -1;

	return -1;
}


void
sim_pty_close(struct sim_pty *pty)
{
	if (pty->link_path) {
		unlink(pty->link_path);
		free(pty->link_path);
		pty->link_path = NULL;
	}

	if (pty->kfd != -1) {
		close(pty->kfd);
		pty->kfd = -1;
	}

	if (pty->mfd != -1) {
		close(pty->mfd);
		pty->mfd = -1;
	}
}


/*
 * Write the full buffer to the pty master, waiting for the host to
 * drain it as needed.  Flux streams far exceed the pty buffer size,
 * so partial writes are the norm.
 *
 * Return 0 on success, -1 on error.
 */

int
sim_pty_write_all(struct sim_pty *pty, const uint8_t *buf, size_t cnt)
{
	while (cnt > 0) {
		ssize_t	wr = write(pty->mfd, buf, cnt);

		if (wr == -1) {
			if (errno == EAGAIN || errno == EINTR) {
				struct pollfd pfd = {
					.fd = pty->mfd,
					.events = POLLOUT
				};

				poll(&pfd, 1, -1);
				continue;
			}

			return -1;
		}

		buf += wr;
		cnt -= wr;
	}

	return 0;
}
