/*
 * Copyright (c) 2018-2020  Joachim Wiberg <troglobit@gmail.com>
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

#include "config.h"

#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <paths.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#ifdef HAVE_TERMIOS_H
# include <termios.h>
#endif
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

/*
 * workaround for SunOS/Solaris/Illumos which defines this in
 * addition to the more logical __sun and __svr4__ macros.
 */
#ifdef sun
#undef sun
#endif

#ifndef MIN
#define MIN(a,b)	(((a) <= (b))? (a) : (b))
#endif

#ifndef strlcat
extern size_t strlcat(char *, const char *, size_t);
#endif

#ifndef strlcat
extern size_t strlcpy(char *, const char *, size_t);
#endif

struct cmd {
	char        *cmd;
	struct cmd  *ctx;
	int        (*cb)(char *arg);
	int         op;
};

static int plain = 0;
static int debug = 0;
static int heading = 1;

char *ident = NULL;

static void dedup(char *arr[])
{
	for (int i = 1; arr[i]; i++) {
		if (!strcmp(arr[i - 1], arr[i])) {
			arr[i - 1] = arr[i];
			for (int j = i; arr[j]; j++)
				arr[j] = arr[j + 1];
		}
	}
}

static int try_connect(struct sockaddr_un *sun)
{
	int sd;

	sd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (-1 == sd)
		return -1;

#ifdef HAVE_SOCKADDR_UN_SUN_LEN
	sun->sun_len = 0; /* <- correct length is set by the OS */
#endif
	sun->sun_family = AF_UNIX;

	if (connect(sd, (struct sockaddr*)sun, sizeof(*sun)) == -1) {
		close(sd);
		if (errno == ENOENT) {
			if (debug)
				warnx("no pimd at %s", sun->sun_path);
			return -1;
		}

		if (debug)
			warn("failed connecting to %s", sun->sun_path);
		return -1;
	}

	return sd;
}

static void compose_path(struct sockaddr_un *sun, char *dir, char *nm)
{
	int n;

	if (*nm == '/') {
		strlcpy(sun->sun_path, nm, sizeof(sun->sun_path));
		return;
	}

	n = snprintf(sun->sun_path, sizeof(sun->sun_path), "%s", dir);
	if (sun->sun_path[n - 1] != '/')
		strlcat(sun->sun_path, "/", sizeof(sun->sun_path));
	strlcat(sun->sun_path, nm, sizeof(sun->sun_path));
	strlcat(sun->sun_path, ".sock", sizeof(sun->sun_path));
}

/*
 * if ident is given, that's the name we're going for
 * if ident starts with a /, we skip all dirs as well
 */
static int ipc_connect(void)
{
	struct sockaddr_un sun;
	char *dirs[] = {
		RUNSTATEDIR "/",
		_PATH_VARRUN,
		NULL
	};
	char *names[] = {
		PACKAGE_NAME,	/* this daemon */
		"pimd",		/* PIM SM/SSM    */
		"pimdd",	/* PIM DM        */
		"pim6sd",	/* PIM SM (IPv6) */
		NULL
	};
	int sd;

	if (ident && *ident == '/') {
		compose_path(&sun, NULL, ident);
		sd = try_connect(&sun);
		if (sd == -1 && errno == ENOENT) {
			/* Check if user forgot .sock suffix */
			strlcat(sun.sun_path, ".sock", sizeof(sun.sun_path));
			sd = try_connect(&sun);
		}

		return sd;
	}

	/* Remove duplicates */
	dedup(dirs);
	dedup(names);

	for (size_t i = 0; dirs[i]; i++) {
		if (ident) {
			compose_path(&sun, dirs[i], ident);
			sd = try_connect(&sun);
			if (sd == -1) {
				if (errno == EACCES)
					return -1;
				continue;
			}

			return sd;
		}

		for (size_t j = 0; names[j]; j++) {
			compose_path(&sun, dirs[i], names[j]);
			sd = try_connect(&sun);
			if (sd == -1) {
				if (errno == EACCES)
					return -1;
				continue;
			}
			
			return sd;
		}
	}

	return -1;
}

static int ipc_ping(void)
{
	int sd;

	sd = ipc_connect();
	if (sd == -1)
		return 0;

	close(sd);
	return 1;
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

static void print(char *line)
{
	int len, head = 0;

	chomp(line);

	/* Table headings, or repeat headers, end with a '=' */
	len = (int)strlen(line) - 1;
	if (len > 0 && line[len] == '=') {
		if (!heading)
			return;
		line[len] = 0;
		head = 1;
		if (!plain)
			len = get_width() - len;
		else
			len = len < 79 ? 79 : len;
	}
	if (len < 0)
		len = 0;

	if (!head) {
		puts(line);
		return;
	}

	if (!plain) {
		fprintf(stdout, "\e[7m%s%*s\e[0m\n", line, len, "");
	} else {
		fprintf(stdout, "%s\n", line);
		while (len--)
			fputc('=', stdout);
		fputs("\n", stdout);
	}
}

static int get(char *cmd)
{
	struct pollfd pfd;
	char buf[768];
	ssize_t len;
	FILE *fp;
	int sd;

	sd = ipc_connect();
	if (-1 == sd) {
		if (errno == ENOENT)
			errx(1, "no pimd running.");
		else if (errno == EACCES)
			errx(1, "not enough permissions.");
		err(1, "failed connecting to pimd");

		return 1; /* we never get here, make gcc happy */
	}

	len = snprintf(buf, sizeof(buf), "%s", chomp(cmd));
	if (write(sd, buf, len) == -1) {
		close(sd);
		return 2;
	}

	fp = tmpfile();
	if (!fp) {
		warn("Failed opening tempfile");
		close(sd);
		return 3;
	}

	pfd.fd = sd;
	pfd.events = POLLIN | POLLHUP;
	while (poll(&pfd, 1, 2000) > 0) {
		if (pfd.events & POLLIN) {
			len = read(sd, buf, sizeof(buf));
			if (len == -1)
				break;

			fwrite(buf, len, 1, fp);
		}
		if (pfd.revents & POLLHUP)
			break;
	}
	close(sd);

	rewind(fp);
	while (fgets(buf, sizeof(buf), fp))
		print(buf);
	fclose(fp);

	return 0;
}

static int string_match(const char *a, const char *b)
{
   size_t min = MIN(strlen(a), strlen(b));

   return !strncasecmp(a, b, min);
}

static int usage(int rc)
{
	printf("Usage: pimctl [OPTIONS] [COMMAND]\n"
	       "\n"
	       "Options:\n"
	       "  -i, --ident=NAME           Connect to named pimd instance\n"
	       "  -p, --plain                Use plain table headings, no ctrl chars\n"
	       "  -t, --no-heading           Skip table headings\n"
	       "  -h, --help                 This help text\n"
	       "\n");

	if (ipc_ping()) {
		printf("Commands:\n");
		get("help");
	} else {
		if (errno == EACCES)
			printf("Not enough permissions to query pimd for commands.\n");
		else
			printf("No pimd running, no commands available.\n");
	}
	printf("\n");

	return rc;
}

static int version(void)
{
	printf("pimctl version %s (%s)\n", PACKAGE_VERSION, PACKAGE_NAME);
	return 0;
}

static int cmd(int argc, char *argv[])
{
	char line[120] = { 0 };

	if (!strcmp(argv[0], "help"))
		return usage(0);

	for (int i; i < argc; i++) {
		if (i != 0)
			strlcat(line, " ", sizeof(line));
		strlcat(line, argv[i], sizeof(line));
	}

	return get(line);
}

int main(int argc, char *argv[])
{
	struct option long_options[] = {
		{ "debug",      0, NULL, 'd' },
		{ "ident",      1, NULL, 'i' },
		{ "no-heading", 0, NULL, 't' },
		{ "plain",      0, NULL, 'p' },
		{ "help",       0, NULL, 'h' },
		{ "version",    0, NULL, 'v' },
		{ NULL, 0, NULL, 0 }
	};
	int c;

	while ((c = getopt_long(argc, argv, "dh?i:ptv", long_options, NULL)) != EOF) {
		switch(c) {
		case 'd':
			debug = 1;
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
			return version();
		}
	}

	if (optind >= argc)
		return get("show");

	return cmd(argc - optind, &argv[optind]);
}

/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */
