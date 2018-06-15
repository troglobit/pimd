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

static struct sockaddr_un sun;
static int ipc_socket = -1;

static void ipc_neighbors(int sd, char *fn)
{
	struct ipc msg;
	FILE *fp;

	fp = fopen(fn, "w");
	if (!fp) {
		logit(LOG_WARNING, errno, "Cannot open %s for writing");
		msg.cmd = IPC_ERR_CMD;
		goto fail;
	}

	dump_vifs(fp);
	fclose(fp);

	msg.cmd = IPC_OK_CMD;
	strlcpy(msg.buf, fn, sizeof(msg.buf));
fail:
	if (write(sd, &msg, sizeof(msg)) == -1)
		logit(LOG_ERR, errno, "Failed sending IPC reply");
}

static void ipc_handle(int sd)
{
	socklen_t socklen = 0;
	ssize_t len;
	char fn[256];
	char cmd;
	int client;

	client = accept(sd, NULL, &socklen);
	if (client < 0)
		return;

	len = read(client, &cmd, sizeof(cmd));
	if (len < 0) {
		logit(LOG_WARNING, errno, "Failed reading IPC command");
		return;
	}

	snprintf(fn, sizeof(fn), _PATH_PIMD_DUMP, ident);
	switch (cmd) {
	case 1:
		ipc_neighbors(client, fn);
		break;

	default:
		logit(LOG_WARNING, 0, "Invalid IPC command '0x%02x'", cmd);
		break;
	}

	close(client);
}


void ipc_init(void)
{
	socklen_t len;
	int sd;

	sd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sd < 0)
		logit(LOG_ERR, errno, "Failed creating IPC socket");

	if (register_input_handler(sd, ipc_handle) < 0)
		logit(LOG_ERR, 0, "Failed registering IPC handler");

#ifdef HAVE_SOCKADDR_UN_SUN_LEN
	sun.sun_len = 0;	/* <- correct length is set by the OS */
#endif
	sun.sun_family = AF_UNIX;
	snprintf(sun.sun_path, sizeof(sun.sun_path), _PATH_PIMD_SOCK, ident);

	unlink(sun.sun_path);
	logit(LOG_DEBUG, 0, "Binding IPC socket to %s", sun.sun_path);

	len = offsetof(struct sockaddr_un, sun_path) + strlen(sun.sun_path);
	if (bind(sd, (struct sockaddr *)&sun, len) < 0 || listen(sd, 1)) {
		logit(LOG_WARNING, errno, "Failed binding IPC socket, client disabled");
		close(sd);
		return;
	}

	ipc_socket = sd;
}

void ipc_exit(void)
{
	if (ipc_socket > -1)
		close(ipc_socket);

	unlink(sun.sun_path);
	ipc_socket = -1;
}

/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */
