/*
 * Copyright (c) 2018-2020  Joachim Nilsson <troglobit@gmail.com>
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
#ifndef PIMD_IPC_H_
#define PIMD_IPC_H_

/*
 * pimd <--> pimctl IPC
 */
#define IPC_OK_CMD                0
#define IPC_RESTART_CMD           1
#define IPC_SHOW_STATUS_CMD       2
#define IPC_DEBUG_CMD             3
#define IPC_LOGLEVEL_CMD          4
#define IPC_KILL_CMD              9
#define IPC_SHOW_IGMP_GROUPS_CMD  10
#define IPC_SHOW_IGMP_IFACE_CMD   11
#define IPC_SHOW_PIM_IFACE_CMD    20
#define IPC_SHOW_PIM_NEIGH_CMD    21
#define IPC_SHOW_PIM_ROUTE_CMD    22
#define IPC_SHOW_PIM_RP_CMD       23
#define IPC_SHOW_PIM_CRP_CMD      24
#define IPC_SHOW_PIM_DUMP_CMD     250
#define IPC_EOF_CMD               254
#define IPC_ERR_CMD               255

struct ipc {
    uint8_t cmd;
    uint8_t detail;

    char    buf[766];
    char    sentry;
};

extern void ipc_init(void);
extern void ipc_exit(void);

#endif /* PIMD_IPC_H_ */
