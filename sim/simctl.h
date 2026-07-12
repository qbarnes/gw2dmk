#ifndef SIMCTL_H
#define SIMCTL_H

#include "simgw.h"

/*
 * Control-plane commands, shared by the simulator's stdin and its
 * UNIX-domain control socket:
 *
 *   insert <unit> <file>     insert a diskette image
 *   eject <unit>             remove the diskette (flushes writes)
 *   wp <unit> on|off         force/release write protect
 *   status                   show drives and media
 *   help                     list commands
 *   quit                     shut the simulator down
 *
 * <unit> is 0-2, or a/b for the IBM PC bus naming.
 */

/*
 * Execute one command line, writing any response to fd "out".
 * Returns 1 if the simulator should exit, else 0.
 */
extern int sim_ctl_command(struct sim_gw *gw, char *line, int out);

#endif
