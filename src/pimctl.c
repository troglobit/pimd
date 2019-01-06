/*
 * Copyright (c) 2018 Joachim Nilsson <troglobit@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "defs.h"
#include <getopt.h>
#include <poll.h>
#ifdef HAVE_TERMIOS_H
# include <termios.h>
#endif

struct cmd {
	char        *cmd;
	struct cmd  *ctx;
	int        (*cb)(char *arg);
	int         op;
};

static int plain = 0;
static int detail = 0;
static int heading = 1;
static int verbose = 0;
static int interactive = 1;

char *ident = "pimd";


static int do_connect(char *ident)
{
	struct sockaddr_un sun;
	int sd;

#ifdef HAVE_SOCKADDR_UN_SUN_LEN
	sun.sun_len = 0;	/* <- correct length is set by the OS */
#endif
	sun.sun_family = AF_UNIX,
	snprintf(sun.sun_path, sizeof(sun.sun_path), _PATH_PIMD_SOCK, ident);
	sd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (-1 == sd)
		goto error;

	if (connect(sd, (struct sockaddr*)&sun, sizeof(sun)) == -1) {
		close(sd);
		goto error;
	}

	return sd;
error:
	perror("Failed connecting to pimd");
	return -1;
}

static struct ipc *do_cmd(uint8_t cmd, int detail, char *buf, size_t len)
{
	static struct ipc msg = { 0 };
	struct pollfd pfd;
	int sd;

	if (buf && len >= sizeof(msg.buf)) {
		errno = EINVAL;
		return NULL;
	}

	sd = do_connect(ident);
	if (-1 == sd)
		return NULL;

	msg.cmd = cmd;
	msg.detail = detail;
	if (buf)
		memcpy(msg.buf, buf, len);

	if (write(sd, &msg, sizeof(msg)) == -1)
		goto fail;

	pfd.fd = sd;
	pfd.events = POLLIN;
	if (poll(&pfd, 1, 2000) <= 0)
		goto fail;

	if (read(sd, &msg, sizeof(msg)) == -1)
		goto fail;

	msg.buf[sizeof(msg.buf) - 1] = 0;
	close(sd);

	return &msg;
fail:
	close(sd);
	return NULL;
}

static int do_set(int cmd, char *arg)
{
	struct ipc *msg;

	if (!arg)
		arg = "";

	msg = do_cmd(cmd, 0, arg, strlen(arg));
	if (!msg)
		return 1;

	if (strlen(msg->buf) > 1)
		puts(msg->buf);

	return 0;
}

static int set_debug(char *arg)
{
	return do_set(IPC_DEBUG_CMD, arg);
}

static int set_loglevel(char *arg)
{
	return do_set(IPC_LOGLEVEL_CMD, arg);
}

#define ESC "\033"
static int get_width(void)
{
	int ret = 74;
#ifdef HAVE_TERMIOS_H
	char buf[42];
	struct termios tc, saved;
	struct pollfd fd = { STDIN_FILENO, POLLIN, 0 };

	memset(buf, 0, sizeof(buf));
	tcgetattr(STDERR_FILENO, &tc);
	saved = tc;
	tc.c_cflag |= (CLOCAL | CREAD);
	tc.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
	tcsetattr(STDERR_FILENO, TCSANOW, &tc);
	fprintf(stderr, ESC "7" ESC "[r" ESC "[999;999H" ESC "[6n");

	if (poll(&fd, 1, 300) > 0) {
		int row, col;

		if (scanf(ESC "[%d;%dR", &row, &col) == 2)
			ret = col;
	}

	fprintf(stderr, ESC "8");
	tcsetattr(STDERR_FILENO, TCSANOW, &saved);
#endif
	return ret;
}

static char *chomp(char *str)
{
	char *p;

	if (!str || strlen(str) < 1) {
		errno = EINVAL;
		return NULL;
	}

	p = str + strlen(str) - 1;
        while (*p == '\n')
		*p-- = 0;

	return str;
}

static int show_generic(int cmd, int detail)
{
	struct ipc *msg;
	FILE *fp;
	char line[512];

	msg = do_cmd(cmd, detail, NULL, 0);
	if (!msg)
		return -1;

	fp = fopen(msg->buf, "r");
	if (!fp)
		return 1;

	while (fgets(line, sizeof(line), fp)) {
		int len, head = 0;

		chomp(line);

		/* Table headings, or repeat headers, end with a '=' */
		len = (int)strlen(line) - 1;
		if (len > 0 && line[len] == '=') {
			if (!heading)
				continue;
			line[len] = 0;
			head = 1;
			if (!plain)
				len = get_width() - len;
		}

		if (head && !plain)
			fprintf(stdout, "\e[7m%s%*s\e[0m\n", line, len < 0 ? 0 : len, "");
		else
			puts(line);

		if (head && plain) {
			while (len--)
				fputc('=', stdout);
			fputs("\n", stdout);
		}
	}

	return fclose(fp) || remove(msg->buf);
}

static int string_match(const char *a, const char *b)
{
   size_t min = MIN(strlen(a), strlen(b));

   return !strncasecmp(a, b, min);
}

static int usage(int rc)
{
	fprintf(stderr,
		"Usage: pimctl [OPTIONS] [COMMAND]\n"
		"\n"
		"Options:\n"
		"  -b, --batch               Batch mode, no screen size probing\n"
		"  -d, --detail              Detailed output, where applicable\n"
		"  -i, --ident=NAME          Connect to named pimd instance\n"
		"  -p, --plain               Use plain table headings, no ctrl chars\n"
		"  -t, --no-heading          Skip table headings\n"
		"  -v, --verbose             Verbose output\n"
		"  -h, --help                This help text\n"
		"\n"
		"Commands:\n"
		"  debug [? | none | SYS]    Debug subystem(s), separate multiple with comma\n"
		"  help                      This help text\n"
		"  kill                      Kill running daemon, like SIGTERM\n"
		"  log [? | none | LEVEL]    Set pimd log level: none, err, notice*, info, debug\n"
		"  restart                   Restart pimd and reload .conf file, like SIGHUP\n"
		"  version                   Show pimd version\n"
		"  show status               Show pimd status, default\n"
		"  show igmp groups          Show IGMP group memberships\n"
		"  show igmp interface       Show IGMP interface status\n"
		"  show pim interface        Show PIM interface table\n"
		"  show pim neighbor         Show PIM neighbor table\n"
		"  show pim routes           Show PIM routing table\n"
		"  show pim rp               Show PIM Rendezvous-Point (RP) set\n"
		"  show pim crp              Show PIM Candidate Rendezvous-Point (CRP) from BSR\n"
		"  show pim compat           Show PIM status, compat mode, previously `pimd -r`\n"
		);

	return rc;
}

static int help(char *arg)
{
	(void)arg;
	return usage(0);
}

static int version(char *arg)
{
	(void)arg;
	printf("v%s\n", PACKAGE_VERSION);

	return 0;
}

static int cmd_parse(int argc, char *argv[], struct cmd *command)
{
	int i;

	for (i = 0; argc > 0 && command[i].cmd; i++) {
		if (!string_match(command[i].cmd, argv[0]))
			continue;

		if (command[i].ctx)
			return cmd_parse(argc - 1, &argv[1], command[i].ctx);

		if (command[i].cb) {
			char arg[80] = "";
			int j;

			for (j = 1; j < argc; j++) {
				if (j > 1)
					strlcat(arg, " ", sizeof(arg));
				strlcat(arg, argv[j], sizeof(arg));
			}

			return command[i].cb(arg);
		}

		return show_generic(command[i].op, detail);
	}

	return usage(1);
}

int main(int argc, char *argv[])
{
	struct option long_options[] = {
		{ "batch",      0, NULL, 'b' },
		{ "detail",     0, NULL, 'd' },
		{ "ident",      1, NULL, 'i' },
		{ "no-heading", 0, NULL, 't' },
		{ "plain",      0, NULL, 'p' },
		{ "help",       0, NULL, 'h' },
		{ "debug",      0, NULL, 'd' },
		{ "verbose",    0, NULL, 'v' },
		{ NULL, 0, NULL, 0 }
	};
	struct cmd igmp[] = {
		{ "groups",    NULL, NULL, IPC_SHOW_IGMP_GROUPS_CMD },
		{ "interface", NULL, NULL, IPC_SHOW_IGMP_IFACE_CMD  },
		{ "iface",     NULL, NULL, IPC_SHOW_IGMP_IFACE_CMD  }, /* ALIAS */
		{ NULL }
	};
	struct cmd pim[] = {
		{ "interface", NULL, NULL, IPC_SHOW_PIM_IFACE_CMD },
		{ "iface",     NULL, NULL, IPC_SHOW_PIM_IFACE_CMD }, /* ALIAS */
		{ "neighbor",  NULL, NULL, IPC_SHOW_PIM_NEIGH_CMD },
		{ "routes",    NULL, NULL, IPC_SHOW_PIM_ROUTE_CMD },
		{ "rp",        NULL, NULL, IPC_SHOW_PIM_RP_CMD    },
		{ "crp",       NULL, NULL, IPC_SHOW_PIM_CRP_CMD   },
		{ "compat",    NULL, NULL, IPC_SHOW_PIM_DUMP_CMD  },
		{ NULL }
	};
	struct cmd show[] = {
		{ "igmp",      igmp, NULL, 0                    },
		{ "pim",       pim,  NULL, 0                    },
		{ "status",    NULL, NULL, IPC_SHOW_STATUS_CMD  },
		{ NULL }
	};
	struct cmd command[] = {
		{ "debug",     NULL, set_debug,    0                   },
		{ "help",      NULL, help,         0                   },
		{ "kill",      NULL, NULL,         IPC_KILL_CMD        },
		{ "log",       NULL, set_loglevel, 0                   },
		{ "status",    NULL, NULL,         IPC_SHOW_STATUS_CMD },
		{ "restart",   NULL, NULL,         IPC_RESTART_CMD     },
		{ "version",   NULL, version,      0                   },
		{ "show",      show, NULL,         0                   },
		{ NULL }
	};
	int c;

	while ((c = getopt_long(argc, argv, "bdh?i:ptv", long_options, NULL)) != EOF) {
		switch(c) {
		case 'b':
			interactive = 0;
			break;

		case 'd':
			detail = 1;
			break;

		case 'h':
		case '?':
			return usage(0);

		case 'i':	/* --ident=NAME */
			ident = optarg;
			break;

		case 'p':
			plain = 1;
			break;

		case 't':
			heading = 0;
			break;

		case 'v':
			verbose = 1;
			break;
		}
	}

	if (optind >= argc)
		return show_generic(IPC_SHOW_STATUS_CMD, detail);

	return cmd_parse(argc - optind, &argv[optind], command);
}

/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */
