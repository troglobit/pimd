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

struct command {
	char  *cmd;
	int  (*cb)(char *arg);
};

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

static struct ipc *do_cmd(uint8_t cmd)
{
	static struct ipc msg;
	struct pollfd pfd;
	int sd;

	sd = do_connect(ident);
	if (-1 == sd)
		return NULL;

	msg.cmd = cmd;
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

static int show_generic(int cmd)
{
	struct ipc *msg;
	char show[512];

	msg = do_cmd(cmd);
	if (!msg)
		return -1;

	snprintf(show, sizeof(show), "cat %s", msg->buf);
	return system(show);
}

static int show_interface(char *arg)
{
	return show_generic(IPC_IFACE_CMD);
}

static int show_neighbor(char *arg)
{
	return show_generic(IPC_NEIGH_CMD);
}

static int show_status(char *arg)
{
	return show_generic(IPC_STAT_CMD);
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
		"  -v, --verbose             Verbose output\n"
		"  -h, --help                This help text\n"
		"\n"
		"Commands:\n"
		"  interface                 Show PIM interface table\n"
		"  neighbor                  Show PIM neighbor table\n"
		"  status                    Show PIM status, default\n",
		"pimctl");
	return 0;
}

int main(int argc, char *argv[])
{
	struct option long_options[] = {
		{"batch",   0, NULL, 'b'},
		{"help",    0, NULL, 'h'},
		{"debug",   0, NULL, 'd'},
		{"verbose", 0, NULL, 'v'},
		{NULL, 0, NULL, 0}
	};
	struct command command[] = {
		{ "interface", show_interface },
		{ "neighbor",  show_neighbor },
		{ "status",    show_status },
		{ NULL, NULL }
	};
	int c;

	while ((c = getopt_long(argc, argv, "bh?v", long_options, NULL)) != EOF) {
		switch(c) {
		case 'b':
			interactive = 0;
			break;

		case 'h':
		case '?':
			return usage(0);

		case 'v':
			verbose = 1;
			break;
		}
	}

	if (optind >= argc)
		return show_status(NULL);

	for (c = 0; command[c].cmd; c++) {
		if (!string_match(command[c].cmd, argv[optind]))
			continue;

		return command[c].cb(NULL);
	}

	return usage(1);
}

/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */
