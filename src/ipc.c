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

static char *timetostr(time_t t, char *buf, size_t len)
{
	int sec, min, hour, day;
	static char tmp[20];

	if (!buf) {
		buf = tmp;
		len = sizeof(tmp);
	}

	day  = t / 86400;
	t    = t % 86400;
	hour = t / 3600;
	t    = t % 3600;
	min  = t / 60;
	t    = t % 60;
	sec  = t;

	if (day)
		snprintf(buf, len, "%dd%dh%dm%ds", day, hour, min, sec);
	else
		snprintf(buf, len, "%dh%dm%ds", hour, min, sec);

	return buf;
}

static char *get_dr_prio(pim_nbr_entry_t *n)
{
	static char prio[5];

	if (n->dr_prio_present)
		snprintf(prio, sizeof(prio), "%4d", n->dr_prio);
	else
		snprintf(prio, sizeof(prio), "   N");

	return prio;
}

static void show_neighbor(FILE *fp, struct uvif *uv, pim_nbr_entry_t *n)
{
	time_t now, uptime;
	char tmp[20], buf[42];

	now = time(NULL);
	uptime = now - n->uptime;
	snprintf(buf, sizeof(buf), "%s/%s",
		 timetostr(uptime, tmp, sizeof(tmp)),
		 timetostr(n->timer, NULL, 0));

	fprintf(fp, "%-16s  %-15s  %4s  %-28s\n",
		uv->uv_name,
		inet_fmt(n->address, s1, sizeof(s1)),
		get_dr_prio(n),
		buf);
}

/* PIM Neighbor Table */
static void show_neighbors(FILE *fp)
{
	pim_nbr_entry_t *n;
	struct uvif *uv;
	vifi_t vifi;
	int first = 1;

	for (vifi = 0; vifi < numvifs; vifi++) {
		uv = &uvifs[vifi];

		for (n = uv->uv_pim_neighbors; n; n = n->next) {
			if (first) {
				fprintf(fp, "Interface         Address          Prio  Uptime/Expires               =\n");
				first = 0;
			}
			show_neighbor(fp, uv, n);
		}
	}
}

static void show_interface(FILE *fp, struct uvif *uv)
{
	pim_nbr_entry_t *n;
	uint32_t addr = 0;
	size_t num  = 0;
	char tmp[5], *pri;

	if (uv->uv_flags & VIFF_DR) {
		addr = uv->uv_lcl_addr;
		snprintf(tmp, sizeof(tmp), "%d", uv->uv_dr_prio);
		pri  = tmp;
	} else if (uv->uv_pim_neighbors) {
		addr = uv->uv_pim_neighbors->address;
		pri  = get_dr_prio(uv->uv_pim_neighbors);
	}

	for (n = uv->uv_pim_neighbors; n; n = n->next)
		num++;

	fprintf(fp, "%-16s  %-4s   %-15s  %3zu  %5d  %4s  %-15s\n",
		uv->uv_name,
		uv->uv_flags & VIFF_DOWN ? "DOWN" : "UP",
		inet_fmt(uv->uv_lcl_addr, s1, sizeof(s1)),
		num, pim_timer_hello_interval,
		pri, inet_fmt(addr, s1, sizeof(s2)));
}

/* PIM Interface Table */
static void show_interfaces(FILE *fp)
{
	vifi_t vifi;

	if (numvifs)
		fprintf(fp, "Interface         State  Address          Nbr  Hello  Prio  DR Address =\n");

	for (vifi = 0; vifi < numvifs; vifi++)
		show_interface(fp, &uvifs[vifi]);
}

/* PIM RP Set Table */
static void show_rp(FILE *fp)
{
	grp_mask_t *grp;

	if (grp_mask_list)
		fprintf(fp, "Group Address       RP Address       Type     Prio  Holdtime =\n");

	for (grp = grp_mask_list; grp; grp = grp->next) {
		struct rp_grp_entry *rp_grp = grp->grp_rp_next;

		while (rp_grp) {
			uint16_t ht = rp_grp->holdtime;
			char htstr[10];
			char type[10];

			if (rp_grp == grp->grp_rp_next)
				fprintf(fp, "%-18s  ", netname(grp->group_addr, grp->group_mask));
			else
				fprintf(fp, "%-18s  ", "");

			if (ht == (uint16_t)0xffffff) {
				snprintf(type, sizeof(type), "Static");
				snprintf(htstr, sizeof(htstr), "N/A");
			} else {
				snprintf(type, sizeof(type), "Dynamic");
				snprintf(htstr, sizeof(htstr), "%d", ht);
			}

			fprintf(fp, "%-15s  %-7s  %4d  %8s\n",
				inet_fmt(rp_grp->rp->rpentry->address, s1, sizeof(s1)),
				type, rp_grp->priority, htstr);

			rp_grp = rp_grp->grp_rp_next;
		}
	}
}

static void dump_route(FILE *fp, mrtentry_t *r, int detail)
{
	vifi_t vifi;
	char oifs[MAXVIFS+1];
	char joined_oifs[MAXVIFS+1];
	char pruned_oifs[MAXVIFS+1];
	char leaves_oifs[MAXVIFS+1];
	char asserted_oifs[MAXVIFS+1];
	char incoming_iif[MAXVIFS+1];

	for (vifi = 0; vifi < numvifs; vifi++) {
		oifs[vifi] =
			PIMD_VIFM_ISSET(vifi, r->oifs) ? 'o' : '.';
		joined_oifs[vifi] =
			PIMD_VIFM_ISSET(vifi, r->joined_oifs) ? 'j' : '.';
		pruned_oifs[vifi] =
			PIMD_VIFM_ISSET(vifi, r->pruned_oifs) ? 'p' : '.';
		leaves_oifs[vifi] =
			PIMD_VIFM_ISSET(vifi, r->leaves) ? 'l' : '.';
		asserted_oifs[vifi] =
			PIMD_VIFM_ISSET(vifi, r->asserted_oifs) ? 'a' : '.';
		incoming_iif[vifi] = '.';
	}
	oifs[vifi]		= 0x0;	/* End of string */
	joined_oifs[vifi]	= 0x0;
	pruned_oifs[vifi]	= 0x0;
	leaves_oifs[vifi]	= 0x0;
	asserted_oifs[vifi] = 0x0;
	incoming_iif[vifi]	= 0x0;
	incoming_iif[r->incoming] = 'I';

	/* TODO: don't need some of the flags */
	if (r->flags & MRTF_SPT)	  fprintf(fp, " SPT");
	if (r->flags & MRTF_WC)	          fprintf(fp, " WC");
	if (r->flags & MRTF_RP)	          fprintf(fp, " RP");
	if (r->flags & MRTF_REGISTER)     fprintf(fp, " REG");
	if (r->flags & MRTF_IIF_REGISTER) fprintf(fp, " IIF_REG");
	if (r->flags & MRTF_NULL_OIF)     fprintf(fp, " NULL_OIF");
	if (r->flags & MRTF_KERNEL_CACHE) fprintf(fp, " CACHE");
	if (r->flags & MRTF_ASSERTED)     fprintf(fp, " ASSERTED");
	if (r->flags & MRTF_REG_SUPP)     fprintf(fp, " REG_SUPP");
	if (r->flags & MRTF_SG)	          fprintf(fp, " SG");
	if (r->flags & MRTF_PMBR)	  fprintf(fp, " PMBR");
	fprintf(fp, "\n");

	if (!detail)
		return;

	fprintf(fp, "Joined   oifs: %-20s\n", joined_oifs);
	fprintf(fp, "Pruned   oifs: %-20s\n", pruned_oifs);
	fprintf(fp, "Leaves   oifs: %-20s\n", leaves_oifs);
	fprintf(fp, "Asserted oifs: %-20s\n", asserted_oifs);
	fprintf(fp, "Outgoing oifs: %-20s\n", oifs);
	fprintf(fp, "Incoming     : %-20s\n", incoming_iif);

	fprintf(fp, "\nTIMERS:  Entry    JP    RS  Assert VIFS:");
	for (vifi = 0; vifi < numvifs; vifi++)
		fprintf(fp, "  %d", vifi);
	fprintf(fp, "\n         %5d  %4d  %4d  %6d      ",
		r->timer, r->jp_timer, r->rs_timer, r->assert_timer);
	for (vifi = 0; vifi < numvifs; vifi++)
		fprintf(fp, " %2d", r->vif_timers[vifi]);
	fprintf(fp, "\n");
}

/* PIM Multicast Routing Table */
static void show_pim_mrt(FILE *fp)
{
	grpentry_t *g;
	mrtentry_t *r;
	u_int number_of_cache_mirrors = 0;
	u_int number_of_groups = 0;
	cand_rp_t *rp;
	kernel_cache_t *kc;

	fprintf(fp, "Source           Group            RP Address       Flags =\n");

	/* TODO: remove the dummy 0.0.0.0 group (first in the chain) */
	for (g = grplist->next; g; g = g->next) {
		number_of_groups++;

		r = g->grp_route;
		if (r) {
			if (r->flags & MRTF_KERNEL_CACHE) {
				for (kc = r->kernel_cache; kc; kc = kc->next)
					number_of_cache_mirrors++;
			}

			fprintf(fp, "%-15s  %-15s  %-15s ",
				"ANY",
				inet_fmt(g->group, s1, sizeof(s1)),
				IN_PIM_SSM_RANGE(g->group)
				? "SSM"
				: (g->active_rp_grp
				   ? inet_fmt(g->rpaddr, s2, sizeof(s2))
				   : "NULL"));

			dump_route(fp, r, 0);
		}

		for (r = g->mrtlink; r; r = r->grpnext) {
			if (r->flags & MRTF_KERNEL_CACHE)
				number_of_cache_mirrors++;

			fprintf(fp, "%-15s  %-15s  %-15s ",
				inet_fmt(r->source->address, s1, sizeof(s1)),
				inet_fmt(g->group, s2, sizeof(s2)),
				IN_PIM_SSM_RANGE(g->group)
				? "SSM"
				: (g->active_rp_grp
				   ? inet_fmt(g->rpaddr, s2, sizeof(s2))
				   : "NULL"));

			dump_route(fp, r, 0);
		}
	}

	for (rp = cand_rp_list; rp; rp = rp->next) {
		r = rp->rpentry->mrtlink;
		if (r) {
			if (r->flags & MRTF_KERNEL_CACHE) {
				for (kc = r->kernel_cache; kc; kc = kc->next)
					number_of_cache_mirrors++;
			}

			fprintf(fp, "%-15s  %-15s  %-15s ",
				inet_fmt(r->source->address, s1, sizeof(s1)),
				"ANY",
				"");

			dump_route(fp, r, 0);
		}
	}

	fprintf(fp, "\nNumber of Groups        : %u\n", number_of_groups);
	fprintf(fp, "Number of Cache MIRRORs : %u\n", number_of_cache_mirrors);
}

static void show_status(FILE *fp)
{
	dump_vifs(fp);
	dump_ssm(fp);
	dump_pim_mrt(fp);
	dump_rp_set(fp);
}

static void ipc_generic(int sd, char *fn, void (*cb)(FILE *))
{
	struct ipc msg;
	FILE *fp;

	fp = fopen(fn, "w");
	if (!fp) {
		logit(LOG_WARNING, errno, "Cannot open %s for writing");
		msg.cmd = IPC_ERR_CMD;
		goto fail;
	}

	cb(fp);
	fclose(fp);

	msg.cmd = IPC_OK_CMD;
	strlcpy(msg.buf, fn, sizeof(msg.buf));
fail:
	if (write(sd, &msg, sizeof(msg)) == -1)
		logit(LOG_WARNING, errno, "Failed sending IPC reply");
}

static void ipc_handle(int sd)
{
	socklen_t socklen = 0;
	struct ipc msg;
	ssize_t len;
	char fn[256];
	int client;

	client = accept(sd, NULL, &socklen);
	if (client < 0)
		return;

	len = read(client, &msg, sizeof(msg));
	if (len < 0) {
		logit(LOG_WARNING, errno, "Failed reading IPC command");
		return;
	}

	snprintf(fn, sizeof(fn), _PATH_PIMD_DUMP, ident);
	switch (msg.cmd) {
	case IPC_IFACE_CMD:
		ipc_generic(client, fn, show_interfaces);
		break;

	case IPC_NEIGH_CMD:
		ipc_generic(client, fn, show_neighbors);
		break;

	case IPC_ROUTE_CMD:
		ipc_generic(client, fn, show_pim_mrt);
		break;

	case IPC_RP_CMD:
		ipc_generic(client, fn, show_rp);
		break;

	case IPC_STAT_CMD:
		ipc_generic(client, fn, show_status);
		break;

	default:
		logit(LOG_WARNING, 0, "Invalid IPC command '0x%02x'", msg.cmd);
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
