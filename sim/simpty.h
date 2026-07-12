#ifndef SIMPTY_H
#define SIMPTY_H

#include <stddef.h>
#include <stdint.h>

/*
 * Pseudo-terminal transport.
 *
 * The simulator owns the pty master.  The host tools open the slave
 * side (via a stable symlink) exactly as they would a real
 * /dev/ttyACM* Greaseweazle device.
 *
 * A "keepalive" fd on the slave side is held open by the simulator so
 * that the master never sees EIO/POLLHUP when a host tool closes the
 * device, allowing tools to be run against the simulator repeatedly.
 */

struct sim_pty {
	int	mfd;		/* pty master */
	int	kfd;		/* keepalive open of the slave */
	char	slave_path[64];
	char	*link_path;	/* stable symlink to slave_path */
};

extern int sim_pty_open(struct sim_pty *pty, const char *link_path);

extern void sim_pty_close(struct sim_pty *pty);

extern int sim_pty_write_all(struct sim_pty *pty, const uint8_t *buf,
			     size_t cnt);

#endif
