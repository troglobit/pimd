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
#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#endif
#include <getopt.h>
#include <paths.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_TERMIOS_H
# include <termios.h>
#endif
#include <time.h>
#include <unistd.h>
#ifdef HAVE_SYS_IOCTL_H
# include <sys/ioctl.h>
#endif
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

static char *sock_file = NULL;
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
	if (-1 == sd) {
		warn("failed socket()");
		return -1;
	}

	/* Portable SOCK_NONBLOCK replacement, ignore any error. */
	(void)fcntl(sd, F_SETFD, fcntl(sd, F_GETFD) | O_NONBLOCK);

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

	if (!dir) {
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

	if ((ident && *ident == '/') || sock_file) {
		if (sock_file)
			compose_path(&sun, NULL, sock_file);
		else
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
	struct pollfd fd = { STDIN_FILENO, POLLIN, 0 };
	struct termios tc, saved;
	struct winsize ws;
	char buf[42];

	if (!ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws)) {
		return ws.ws_col;
	} else if (!isatty(STDOUT_FILENO)) {
		char *columns;

		/* we may be running under watch(1) */
		columns = getenv("COLUMNS");
		if (columns)
			return atoi(columns);

		return ret;
	}

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
        while (p >= str && *p == '\n')
		*p-- = 0;

	return str;
}

static void print(char *line, int indent)
{
	int type = 0;
	int i, len;

	chomp(line);

	/* Table headings, or repeat headers, end with a '=' */
	len = (int)strlen(line) - 1;
	if (len > 0) {
		if (line[len] == '_')
			type = 1;
		if (line[len] == '=')
			type = 2;

		if (type) {
			if (!heading)
				return;
			line[len] = 0;
		}
	}

	switch (type) {
	case 1:
		if (!plain) {
			fprintf(stdout, "\e[4m%*s\e[0m\n%s\n", get_width(), "", line);
			return;

		}

		len = len < 79 ? 79 : len;
		for (i = 0; i < len; i++)
			fputc('_', stdout);
		fprintf(stdout, "\n%*s%s\n", indent, "", line);
		break;

	case 2:
		if (!plain) {
			len = get_width() - len;
			fprintf(stdout, "\e[7m%s%*s\e[0m\n", line, len, "");
			return;
		}

		len = len < 79 ? 79 : len;
		for (i = 0; i < len; i++)
			fputc('=', stdout);
		fprintf(stdout, "\n%*s%s\n", indent, "", line);
		for (i = 0; i < len; i++)
			fputc('=', stdout);
		fputs("\n", stdout);
		break;

	default:
		puts(line);
		break;
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
	while (write(sd, buf, len) == -1) {
		if (errno == EAGAIN)
			continue;
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
			ssize_t blen = sizeof(buf) - 1;

			len = read(sd, buf, blen);
			if (len == -1) {
				if (errno == EAGAIN || errno == EINTR)
					continue;
				break;
			}

			buf[len] = 0;
			fwrite(buf, len, 1, fp);
			if (len == blen)
				continue;
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

	printf("Usage:\n"
	       "  pimctl [OPTIONS] [COMMAND]\n"
	       "\n"
	       "Options:\n"
	       "  -i, --ident=NAME           Connect to named pimd instance\n"
	       "  -m, --monitor              Run 'COMMAND' every two seconds, like watch(1)\n"
	       "  -p, --plain                Use plain table headings, no ctrl chars\n"
	       "  -t, --no-heading           Skip table headings\n"
	       "  -h, --help                 This help text\n"
	       "  -u, --ipc=FILE             Override UNIX domain socket file, default based on -i\n"
	       "  -v, --version              Show pimctl version\n"
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
		if (!c->desc || !*c->desc)
			continue;

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
		{ "help",       0, NULL, 'h' },
		{ "ident",      1, NULL, 'i' },
		{ "monitor",    0, NULL, 'm' },
		{ "no-heading", 0, NULL, 't' },
		{ "plain",      0, NULL, 'p' },
		{ "ipc",        1, NULL, 'u' },
		{ "version",    0, NULL, 'v' },
		{ NULL, 0, NULL, 0 }
	};
	int monitor = 0;
	int c, rc;

	while ((c = getopt_long(argc, argv, "dh?i:mptu:v", long_options, NULL)) != EOF) {
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

		case 'm':
			monitor = 1;
			break;

		case 'p':
			plain = 1;
			break;

		case 't':
			heading = 0;
			break;

		case 'u':
			sock_file = optarg;
			break;

		case 'v':
			return version();
		}
	}

	do {
		if (monitor) {
			time_t now;

			fputs("\033[2J\033[1;1H", stderr); /* clear */
			now = time(NULL);
			fputs(ctime(&now), stderr);
		}

		if (optind >= argc)
			rc = get("show", NULL);
		else
			rc = cmd(argc - optind, &argv[optind]);

		if (monitor)
			rc = sleep(2);
	} while (rc == 0 && monitor);

	return rc;
}

/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */
