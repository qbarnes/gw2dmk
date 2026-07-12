#ifndef SIMPROTO_H
#define SIMPROTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "simgw.h"
#include "simpty.h"

/*
 * Greaseweazle serial protocol engine.
 *
 * Commands arrive as [opcode, total_length, args...]; each is
 * answered with [opcode, ack] plus an optional payload.  After
 * CMD_WRITE_FLUX the engine switches to consuming a flux stream
 * until its 0x00 terminator.
 */

enum sim_proto_state {
	PROTO_CMD,
	PROTO_WRSTREAM
};

struct sim_proto {
	struct sim_gw		*gw;
	struct sim_pty		*pty;

	enum sim_proto_state	state;
	uint8_t			cbuf[64];
	size_t			cbuf_cnt;

	/* CMD_WRITE_FLUX stream capture */
	uint8_t			*wbuf;
	size_t			wbuf_cnt;
	size_t			wbuf_len;
	bool			wr_cue;
	bool			wr_term;
	bool			wr_ok;	/* writable target selected */
};

extern void sim_proto_init(struct sim_proto *sp, struct sim_gw *gw,
			   struct sim_pty *pty);

extern void sim_proto_reset(struct sim_proto *sp);

/* Feed host bytes into the engine.  Returns 0, or -1 on I/O error. */
extern int sim_proto_input(struct sim_proto *sp, const uint8_t *buf,
			   size_t cnt);

#endif
