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

struct command {
	char  *cmd;
	int  (*cb)(char *arg);
	int    op;
};

static int heading = 1;
static int verbose = 0;
static int interactive = 1;

char *ident = "pimd";


static int do_connect(char *ident)
{
	struct sockaddr_un sun;
	char path[256];
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

static struct ipc *do_cmd(uint8_t cmd, int detail)
{
	static struct ipc msg;
	struct pollfd pfd;
	int sd;

	sd = do_connect(ident);
	if (-1 == sd)
		return NULL;

	msg.cmd = cmd;
	msg.detail = detail;
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

	msg = do_cmd(cmd, detail);
	if (!msg)
		return -1;

	fp = fopen(msg->buf, "r");
	if (!fp)
		return 1;

	while (fgets(line, sizeof(line), fp)) {
		int len, head = 0;

		chomp(line);

		/* Table headings, or repeat headers, end with a '=' */
		len = (int)strlen(line);
		if (line[len - 1] == '=') {
			if (!heading)
				continue;
			line[len - 1] = 0;
			head = 1;
			len = get_width() - len;
		}

		if (head)
			fprintf(stdout, "\e[7m%s%*s\e[0m\n", line, len < 0 ? 0 : len, "");
		else
			puts(line);
	}

	return fclose(fp);
}

static int string_match(const char *a, const char *b)
{
   size_t min = MIN(strlen(a), strlen(b));

   return !strncasecmp(a, b, min);
}

static int usage(int rc)
{
	fprintf(stderr,
		"Usage: %s [OPTIONS] [COMMAND]\n"
		"\n"
		"Options:\n"
		"  -b, --batch               Batch mode, no screen size probing\n"
		"  -d, --detail              Detailed output, where applicable\n"
		"  -I, --ident=NAME          Connect to named pimd instance\n"
		"  -t, --no-heading          Skip table headings\n"
		"  -v, --verbose             Verbose output\n"
		"  -h, --help                This help text\n"
		"\n"
		"Commands:\n"
		"  interface                 Show PIM interface table\n"
		"  neighbor                  Show PIM neighbor table\n"
		"  routes                    Show PIM routing table\n"
		"  rp                        Show PIM Rendezvous-Point (RP) set\n"
		"  status                    Show PIM status, default\n",
		"pimctl");
	return 0;
}

int main(int argc, char *argv[])
{
	struct option long_options[] = {
		{ "batch",      0, NULL, 'b' },
		{ "detail",     0, NULL, 'd' },
		{ "ident",      1, NULL, 'I' },
		{ "no-heading", 0, NULL, 't' },
		{ "help",       0, NULL, 'h' },
		{ "debug",      0, NULL, 'd' },
		{ "verbose",    0, NULL, 'v' },
		{ NULL, 0, NULL, 0 }
	};
	struct command command[] = {
		{ "interface", NULL, IPC_IFACE_CMD },
		{ "neighbor",  NULL, IPC_NEIGH_CMD },
		{ "routes",    NULL, IPC_ROUTE_CMD },
		{ "rp",        NULL, IPC_RP_CMD    },
		{ "status",    NULL, IPC_STAT_CMD  },
		{ NULL, NULL }
	};
	int detail = 0;
	int c;

	while ((c = getopt_long(argc, argv, "bdh?I:tv", long_options, NULL)) != EOF) {
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

		case 'I':	/* --ident=NAME */
			ident = optarg;
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
		return show_generic(IPC_STAT_CMD, detail);

	for (c = 0; command[c].cmd; c++) {
		if (!string_match(command[c].cmd, argv[optind]))
			continue;

		if (command[c].cb)
			return command[c].cb(NULL);

		return show_generic(command[c].op, detail);
	}

	return usage(1);
}

/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */
