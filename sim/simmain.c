/*
 * gwsim: Greaseweazle device simulator.
 *
 * Emulates a Greaseweazle (F7 or V4.1) with attached floppy drives
 * whose diskettes are backed by DMK image files.  The host tools
 * (gw2dmk, dmk2gw, gwhist) connect unmodified through a
 * pseudo-terminal: run gwsim, then point the tools at the printed
 * device path with their -G option.
 */

#define _GNU_SOURCE

#include <errno.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "simclock.h"
#include "simctl.h"
#include "simdrive.h"
#include "simfdadap.h"
#include "simgw.h"
#include "simmedia.h"
#include "simproto.h"
#include "simpty.h"

#define MAX_CTL_CLIENTS	4
#define CTL_LINE_MAX	512

static volatile sig_atomic_t	got_signal = 0;


static void
sig_handler(int sig)
{
	got_signal = sig;
}


static void
usage(FILE *fp, const char *prog)
{
	fprintf(fp,
		"Usage: %s [options]\n"
		"  -m, --model MODEL     Greaseweazle model to emulate "
		"[f7]\n"
		"  -f, --fast            no seek/rotational delays\n"
		"  -p, --pty-link PATH   symlink to the pty device "
		"[./gwsim-pty]\n"
		"  -s, --socket PATH     control socket, "
		"\"none\" disables [./gwsim.sock]\n"
		"  -D, --drive N:TYPE[,tracks=T][,rpm=R][,sides=S]"
		"[,fdadap][,wp]\n"
		"                        attach a drive at unit N "
		"[0:525dd]\n"
		"  -i, --insert N:FILE   insert diskette image at "
		"startup\n"
		"  -l, --list            list drive types and models\n"
		"  -h, --help            this help\n"
		"\n"
		"Control commands (stdin or socket): insert, eject, "
		"wp, status, quit.\n",
		prog);
}


static int
parse_drive_spec(struct sim_gw *gw, char *spec)
{
	char	*colon = strchr(spec, ':');

	if (!colon || colon == spec) {
		fprintf(stderr, "gwsim: bad drive spec '%s'\n", spec);
		return -1;
	}

	*colon = '\0';

	char	*end;
	long	unit = strtol(spec, &end, 10);

	if (*end || unit < 0 || unit >= SIM_MAX_UNITS) {
		fprintf(stderr, "gwsim: bad unit '%s'\n", spec);
		return -1;
	}

	if (gw->unit[unit]) {
		fprintf(stderr, "gwsim: unit %ld already has a drive\n",
			unit);
		return -1;
	}

	char	*save = NULL;
	char	*type = strtok_r(colon + 1, ",", &save);

	const struct sim_drive_type *dt =
			type ? sim_drive_type_find(type) : NULL;

	if (!dt) {
		fprintf(stderr, "gwsim: unknown drive type '%s'\n",
			type ? type : "");
		return -1;
	}

	struct sim_drive	*drv = sim_drive_new(dt);

	if (!drv)
		return -1;

	for (char *opt = strtok_r(NULL, ",", &save); opt;
	     opt = strtok_r(NULL, ",", &save)) {
		if (!strncmp(opt, "tracks=", 7)) {
			int	t = atoi(opt + 7);

			if (t < 1 || t > 88) {
				fprintf(stderr, "gwsim: bad tracks "
					"'%s'\n", opt);
				goto err;
			}

			drv->tracks = t;
		} else if (!strncmp(opt, "rpm=", 4)) {
			int	r = atoi(opt + 4);

			if (r != dt->rpm && r != dt->rpm_alt) {
				fprintf(stderr, "gwsim: %s does not "
					"support %d RPM\n", dt->name, r);
				goto err;
			}

			drv->rpm = r;
		} else if (!strncmp(opt, "sides=", 6)) {
			int	s = atoi(opt + 6);

			if (s < 1 || s > 2) {
				fprintf(stderr, "gwsim: bad sides "
					"'%s'\n", opt);
				goto err;
			}

			drv->sides = s;
		} else if (!strcmp(opt, "fdadap")) {
			drv->fdadap = true;
		} else if (!strcmp(opt, "wp")) {
			drv->wp_force = true;
		} else {
			fprintf(stderr, "gwsim: unknown drive option "
				"'%s'\n", opt);
			goto err;
		}
	}

	char	errbuf[128];

	if (!sim_fdadap_attach_ok(drv, errbuf, sizeof(errbuf))) {
		fprintf(stderr, "gwsim: unit %ld: %s\n", unit, errbuf);
		goto err;
	}

	gw->unit[unit] = drv;

	return 0;

err:
	sim_drive_free(drv);

	return -1;
}


static int
ctl_socket_open(const char *path)
{
	int	fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);

	if (fd == -1)
		return -1;

	struct sockaddr_un	sa = { .sun_family = AF_UNIX };

	if (strlen(path) >= sizeof(sa.sun_path)) {
		close(fd);
		errno = ENAMETOOLONG;
		return -1;
	}

	strcpy(sa.sun_path, path);
	unlink(path);

	if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) == -1 ||
	    listen(fd, 4) == -1) {
		close(fd);
		return -1;
	}

	return fd;
}


struct ctl_client {
	int	fd;
	char	line[CTL_LINE_MAX];
	size_t	cnt;
};


/*
 * Split buffered input into lines and run each.  Returns 1 if a
 * command requested shutdown.
 */

static int
ctl_feed(struct sim_gw *gw, struct ctl_client *cc, int out)
{
	char	*nl;

	while ((nl = memchr(cc->line, '\n', cc->cnt))) {
		*nl = '\0';

		size_t	linelen = nl - cc->line + 1;

		if (sim_ctl_command(gw, cc->line, out))
			return 1;

		memmove(cc->line, nl + 1, cc->cnt - linelen);
		cc->cnt -= linelen;
	}

	if (cc->cnt == sizeof(cc->line)) {
		dprintf(out, "error: line too long\n");
		cc->cnt = 0;
	}

	return 0;
}


int
main(int argc, char **argv)
{
	const char	*pty_link  = "./gwsim-pty";
	const char	*sock_path = "./gwsim.sock";
	const char	*model	   = "f7";

	static struct sim_gw	gw;

	struct {
		char	*spec;
	} inserts[SIM_MAX_UNITS * 2];
	int	insert_cnt = 0;

	static const struct option opts[] = {
		{ "model",	required_argument, NULL, 'm' },
		{ "fast",	no_argument,	   NULL, 'f' },
		{ "pty-link",	required_argument, NULL, 'p' },
		{ "socket",	required_argument, NULL, 's' },
		{ "drive",	required_argument, NULL, 'D' },
		{ "insert",	required_argument, NULL, 'i' },
		{ "list",	no_argument,	   NULL, 'l' },
		{ "help",	no_argument,	   NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};

	int	c;
	bool	have_drive = false;

	while ((c = getopt_long(argc, argv, "m:fp:s:D:i:lh", opts,
				NULL)) != -1) {
		switch (c) {
		case 'm':
			model = optarg;
			break;

		case 'f':
			sim_fast = true;
			break;

		case 'p':
			pty_link = optarg;
			break;

		case 's':
			sock_path = optarg;
			break;

		case 'D':
			if (parse_drive_spec(&gw, optarg) == -1)
				return 1;

			have_drive = true;
			break;

		case 'i':
			if (insert_cnt >= (int)(sizeof(inserts) /
						sizeof(inserts[0]))) {
				fprintf(stderr, "gwsim: too many "
					"--insert\n");
				return 1;
			}

			inserts[insert_cnt++].spec = optarg;
			break;

		case 'l':
			printf("Greaseweazle models:\n");
			sim_gw_model_list();
			printf("Drive types:\n");
			sim_drive_type_list();
			return 0;

		case 'h':
			usage(stdout, argv[0]);
			return 0;

		default:
			usage(stderr, argv[0]);
			return 1;
		}
	}

	if (optind < argc) {
		usage(stderr, argv[0]);
		return 1;
	}

	gw.model = sim_gw_model_find(model);

	if (!gw.model) {
		fprintf(stderr, "gwsim: unknown model '%s'\n", model);
		return 1;
	}

	if (!have_drive) {
		char	spec[] = "0:525dd";

		if (parse_drive_spec(&gw, spec) == -1)
			return 1;
	}

	sim_gw_reset(&gw);

	for (int i = 0; i < insert_cnt; ++i) {
		char	*spec  = inserts[i].spec;
		char	*colon = strchr(spec, ':');

		if (!colon || colon[1] == '\0' ||
		    colon - spec != 1 || spec[0] < '0' || spec[0] > '2') {
			fprintf(stderr, "gwsim: bad insert spec '%s'\n",
				spec);
			return 1;
		}

		int			unit = spec[0] - '0';
		struct sim_drive	*drv = gw.unit[unit];

		if (!drv) {
			fprintf(stderr, "gwsim: no drive at unit %d\n",
				unit);
			return 1;
		}

		drv->media = sim_media_load(colon + 1);

		if (!drv->media) {
			fprintf(stderr, "gwsim: cannot load '%s'\n",
				colon + 1);
			return 1;
		}
	}

	struct sim_pty	pty;

	if (sim_pty_open(&pty, pty_link) == -1) {
		fprintf(stderr, "gwsim: cannot create pty: %s\n",
			strerror(errno));
		return 1;
	}

	int	lfd = -1;

	if (strcmp(sock_path, "none") != 0) {
		lfd = ctl_socket_open(sock_path);

		if (lfd == -1) {
			fprintf(stderr, "gwsim: cannot create socket "
				"'%s': %s\n", sock_path, strerror(errno));
			sim_pty_close(&pty);
			return 1;
		}
	}

	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);
	signal(SIGPIPE, SIG_IGN);

	printf("gwsim: emulating Greaseweazle %s (%s mode)\n",
	       gw.model->name, sim_fast ? "fast" : "timed");
	printf("gwsim: device: %s (-> %s)\n", pty_link, pty.slave_path);

	if (lfd != -1)
		printf("gwsim: control socket: %s\n", sock_path);

	printf("gwsim: example: gw2dmk -G %s out.dmk\n", pty_link);
	fflush(stdout);

	struct sim_proto	proto;

	sim_proto_init(&proto, &gw, &pty);

	struct ctl_client	stdin_cc   = { .fd = 0 };
	bool			stdin_open = true;
	struct ctl_client	clients[MAX_CTL_CLIENTS];

	for (int i = 0; i < MAX_CTL_CLIENTS; ++i)
		clients[i].fd = -1;

	bool	quit = false;

	while (!quit && !got_signal) {
		struct pollfd	pfds[3 + MAX_CTL_CLIENTS];
		int		npfd = 0;

		pfds[npfd++] = (struct pollfd){ pty.mfd, POLLIN, 0 };
		pfds[npfd++] = (struct pollfd){ stdin_open ? 0 : -1,
						POLLIN, 0 };

		int	lidx = -1;

		if (lfd != -1) {
			lidx = npfd;
			pfds[npfd++] = (struct pollfd){ lfd, POLLIN, 0 };
		}

		int	cidx[MAX_CTL_CLIENTS];

		for (int i = 0; i < MAX_CTL_CLIENTS; ++i) {
			cidx[i] = -1;

			if (clients[i].fd != -1) {
				cidx[i] = npfd;
				pfds[npfd++] = (struct pollfd){
					clients[i].fd, POLLIN, 0
				};
			}
		}

		if (poll(pfds, npfd, -1) == -1) {
			if (errno == EINTR)
				continue;

			break;
		}

		if (pfds[0].revents & (POLLIN | POLLHUP)) {
			uint8_t		buf[8192];
			ssize_t		rd = read(pty.mfd, buf,
						  sizeof(buf));

			if (rd > 0) {
				sim_proto_input(&proto, buf, rd);
			} else if (rd == -1 && errno != EAGAIN &&
				   errno != EIO) {
				fprintf(stderr, "gwsim: pty read: %s\n",
					strerror(errno));
				break;
			}
		}

		if (pfds[1].revents & (POLLIN | POLLHUP)) {
			ssize_t	rd = read(0,
				stdin_cc.line + stdin_cc.cnt,
				sizeof(stdin_cc.line) - stdin_cc.cnt);

			if (rd > 0) {
				stdin_cc.cnt += rd;
				quit = ctl_feed(&gw, &stdin_cc, 1);
			} else if (rd == 0) {
				/* EOF: keep serving pty/socket. */
				stdin_open = false;
			}
		}

		if (lidx != -1 && (pfds[lidx].revents & POLLIN)) {
			int	cfd = accept(lfd, NULL, NULL);

			if (cfd != -1) {
				int	i;

				for (i = 0; i < MAX_CTL_CLIENTS; ++i) {
					if (clients[i].fd == -1)
						break;
				}

				if (i == MAX_CTL_CLIENTS) {
					dprintf(cfd, "error: too many "
						"clients\n");
					close(cfd);
				} else {
					clients[i].fd  = cfd;
					clients[i].cnt = 0;
				}
			}
		}

		for (int i = 0; i < MAX_CTL_CLIENTS && !quit; ++i) {
			if (cidx[i] == -1 ||
			    !(pfds[cidx[i]].revents & (POLLIN | POLLHUP)))
				continue;

			struct ctl_client	*cc = &clients[i];
			ssize_t	rd = read(cc->fd, cc->line + cc->cnt,
					  sizeof(cc->line) - cc->cnt);

			if (rd <= 0) {
				close(cc->fd);
				cc->fd = -1;
				continue;
			}

			cc->cnt += rd;
			quit = ctl_feed(&gw, cc, cc->fd);
		}
	}

	/* Flush any dirty media back to their files. */
	for (int u = 0; u < SIM_MAX_UNITS; ++u) {
		if (gw.unit[u]) {
			sim_drive_free(gw.unit[u]);
			gw.unit[u] = NULL;
		}
	}

	for (int i = 0; i < MAX_CTL_CLIENTS; ++i) {
		if (clients[i].fd != -1)
			close(clients[i].fd);
	}

	if (lfd != -1) {
		close(lfd);
		unlink(sock_path);
	}

	sim_pty_close(&pty);
	free(proto.wbuf);

	printf("gwsim: exiting\n");

	return 0;
}
