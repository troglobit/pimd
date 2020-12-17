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
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_TERMIOS_H
# include <termios.h>
#endif
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "queue.h"
#define MAXARGS	32

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

#ifndef tempfile
extern FILE *tempfile(void);
#endif

struct cmd {
	char *argv[MAXARGS];
	int   argc;

	char *opt;
	char *desc;

	TAILQ_ENTRY(cmd) link;
};

static int plain = 0;
static int debug = 0;
static int heading = 1;

static int cmdind;
static TAILQ_HEAD(head, cmd) cmds = TAILQ_HEAD_INITIALIZER(cmds);

static char *ident = NULL;


static void add_cmd(char *cmd, char *opt, char *desc)
{
	struct cmd *c;
	char *token;

	c = calloc(1, sizeof(struct cmd));
	if (!c)
		err(1, "Cannot get memory for arg node");

	while ((token = strsep(&cmd, " "))) {
		if (strlen(token) < 1)
			continue;

		c->argv[c->argc++] = token;
	}

	c->opt = opt;
	c->desc = desc;

	TAILQ_INSERT_TAIL(&cmds, c, link);
}

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
		return -1;

	close(sd);
	return 0;
}

static int get_width(void)
{
	int ret = 79;
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
	fprintf(stderr, "\e7\e[r\e[999;999H\e[6n");

	if (poll(&fd, 1, 300) > 0) {
		int row, col;

		if (scanf("\e[%d;%dR", &row, &col) == 2)
			ret = col;
	}

	fprintf(stderr, "\e8");
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

static void print(char *line, int indent)
{
	int len, head = 0;

	chomp(line);

	/* Table headings, or repeat headers, end with a '=' */
	len = (int)strlen(line) - 1;
	if (len > 0) {
		if (line[len] == '_') {
			if (!plain)
				fprintf(stdout, "\e[4m%*s\e[0m\n", get_width(), "");
			else
				fprintf(stdout, "%*s\n", 79, "_");
		}
		if (line[len] == '=') {
			if (!heading)
				return;

			line[len] = 0;
			head = 1;
			if (!plain)
				len = get_width() - len;
			else
				len = len < 79 ? 79 : len;
		}
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
		fprintf(stdout, "%*s%s\n", indent, "", line);
		while (len--)
			fputc('=', stdout);
		fputs("\n", stdout);
	}
}

static int get(char *cmd, FILE *fp)
{
	struct pollfd pfd;
	FILE *lfp = NULL;
	int indent = 0;
	char buf[768];
	ssize_t len;
	int sd;

	if (debug)
		warn("Sending cmd %s", cmd);

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

	if (!fp) {
		lfp = tempfile();
		if (!lfp) {
			warn("Failed opening tempfile");
			close(sd);
			return 3;
		}

		fp = lfp;
	}

	pfd.fd = sd;
	pfd.events = POLLIN | POLLHUP;
	while (poll(&pfd, 1, 2000) > 0) {
		if (pfd.events & POLLIN) {
			memset(buf, 0, sizeof(buf));
			len = read(sd, buf, sizeof(buf) - 1);
			if (len == -1)
				break;

			buf[len] = 0;
			fwrite(buf, len, 1, fp);
		}
		if (pfd.revents & POLLHUP)
			break;
	}
	close(sd);

	rewind(fp);
	if (!lfp)
		return 0;

	while (fgets(buf, sizeof(buf), fp))
		print(buf, indent);

	fclose(lfp);

	return 0;
}

static int ipc_fetch(void)
{
	char buf[768];
	char *cmd;
	FILE *fp;

	if (!TAILQ_EMPTY(&cmds))
		return 0;

	if (ipc_ping())
		return 1;

	fp = tempfile();
	if (!fp)
		err(4, "Failed opening tempfile");

	if (get("help", fp)) {
		fclose(fp);
		err(5, "Failed fetching commands");
	}

	while ((cmd = fgets(buf, sizeof(buf), fp))) {
		char *opt, *desc;

		if (!chomp(cmd))
			continue;

		cmd = strdup(cmd);
		if (!cmd)
			err(1, "Failed strdup(cmd)");

		desc = strchr(cmd, '\t');
		if (desc)
			*desc++ = 0;

		opt = strchr(cmd, '[');
		if (opt)
			*opt++ = 0;

		add_cmd(cmd, opt, desc);
	}

	fclose(fp);

	return 0;
}

static int string_match(const char *a, const char *b)
{
   size_t min = MIN(strlen(a), strlen(b));

   return !strncasecmp(a, b, min);
}

struct cmd *match(int argc, char *argv[])
{
	struct cmd *c;

	TAILQ_FOREACH(c, &cmds, link) {
		for (int i = 0, j = 0; i < argc && j < c->argc; i++, j++) {
			if (!string_match(argv[i], c->argv[j]))
				break;

			cmdind = i + 1;
			if (i + 1 == c->argc)
				return c;
		}
	}

	return NULL;
}

char *compose(struct cmd *c, char *buf, size_t len)
{
	memset(buf, 0, len);

	for (int i = 0; c && i < c->argc; i++) {
		if (i > 0)
			strlcat(buf, " ", len);
		strlcat(buf, c->argv[i], len);
	}

	return buf;
}

static int usage(int rc)
{
	struct cmd *c;
	char buf[120];

	printf("Usage: pimctl [OPTIONS] [COMMAND]\n"
	       "\n"
	       "Options:\n"
	       "  -i, --ident=NAME           Connect to named pimd instance\n"
	       "  -p, --plain                Use plain table headings, no ctrl chars\n"
	       "  -t, --no-heading           Skip table headings\n"
	       "  -h, --help                 This help text\n"
	       "\n");

	if (ipc_fetch()) {
		if (errno == EACCES)
			printf("Not enough permissions to query pimd for commands.\n");
		else
			printf("No pimd running, no commands available.\n");

		return rc;
	}

	printf("Commands:\n");
	TAILQ_FOREACH(c, &cmds, link) {
		compose(c, buf, sizeof(buf));
		if (c->opt) {
			strlcat(buf, " [", sizeof(buf));
			strlcat(buf, c->opt, sizeof(buf));
		}

		printf("  %-25s  %s\n", buf, c->desc);
	}

	return rc;
}

static int version(void)
{
	printf("pimctl version %s (%s)\n", PACKAGE_VERSION, PACKAGE_NAME);
	return 0;
}

static int cmd(int argc, char *argv[])
{
	struct cmd *c, *tmp;
	char buf[768];
	char *cmd;

	if (ipc_fetch()) {
		if (errno == EACCES)
			printf("Not enough permissions to send commands to pimd.\n");
		else
			printf("No pimd running, no commands available.\n");

		return 1;
	}

	cmd = compose(match(argc, argv), buf, sizeof(buf));
	while (cmdind < argc) {
		strlcat(buf, " ", sizeof(buf));
		strlcat(buf, argv[cmdind++], sizeof(buf));
	}

	if (strlen(cmd) < 1) {
		warnx("Invalid command.");
		return 1;
	}

	if (!strcmp(cmd, "help"))
		return usage(0);

	return get(cmd, NULL);
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
		return get("show", NULL);

	return cmd(argc - optind, &argv[optind]);
}

/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */
