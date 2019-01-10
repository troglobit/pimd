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
static int detail = 0;

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

	if (uv->uv_flags & VIFF_DR) {
		memset(tmp, 0, sizeof(tmp));
	} else {
		if (uv->uv_pim_neighbors == n)
			snprintf(tmp, sizeof(tmp), "DR");
	}

	fprintf(fp, "%-16s  %-15s  %4s  %-4s  %-28s\n",
		uv->uv_name,
		inet_fmt(n->address, s1, sizeof(s1)),
		get_dr_prio(n), tmp, buf);
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
				fprintf(fp, "Interface         Address          Prio  Mode  Uptime/Expires               =\n");
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
	char *pri = "N/A";
	char tmp[5];

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
		pri, inet_fmt(addr, s2, sizeof(s2)));
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

/* PIM Cand-RP Table */
static void show_crp(FILE *fp)
{
	struct cand_rp *rp;

	if (cand_rp_list)
		fprintf(fp, "RP Address       Group Address       Prio  Holdtime  Expires =\n");

	for (rp = cand_rp_list; rp; rp = rp->next) {
		struct rp_grp_entry *rp_grp = rp->rp_grp_next;
		struct grp_mask *grp = rp_grp->group;
		rpentry_t *entry = rp->rpentry;
		char buf[10];

		if (entry->adv_holdtime == (uint16_t)0xffffff)
			snprintf(buf, sizeof(buf), "%8s", "Static");
		else
			snprintf(buf, sizeof(buf), "%8d", entry->adv_holdtime);

		fprintf(fp, "%-15s  %-18s  %4d  %s  %s\n",
			inet_fmt(entry->address, s1, sizeof(s1)),
			netname(grp->group_addr, grp->group_mask),
			rp_grp->priority, buf,
			timetostr(rp_grp->holdtime, NULL, 0));
	}

	fprintf(fp, "\nCurrent BSR address: %s\n", inet_fmt(curr_bsr_address, s1, sizeof(s1)));
}

static void dump_route(FILE *fp, mrtentry_t *r)
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

	fprintf(fp, "\nTIMERS       :  Entry    JP    RS  Assert  VIFS:");
	for (vifi = 0; vifi < numvifs; vifi++)
		fprintf(fp, "  %d", vifi);
	fprintf(fp, "\n                %5d  %4d  %4d  %6d       ",
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

			if (detail)
				fprintf(fp, "\nSource           Group            RP Address       Flags =\n");
			fprintf(fp, "%-15s  %-15s  %-15s ",
				"ANY",
				inet_fmt(g->group, s1, sizeof(s1)),
				IN_PIM_SSM_RANGE(g->group)
				? "SSM"
				: (g->active_rp_grp
				   ? inet_fmt(g->rpaddr, s2, sizeof(s2))
				   : "NULL"));

			dump_route(fp, r);
		}

		for (r = g->mrtlink; r; r = r->grpnext) {
			if (r->flags & MRTF_KERNEL_CACHE)
				number_of_cache_mirrors++;

			if (detail)
				fprintf(fp, "\nSource           Group            RP Address       Flags =\n");
			fprintf(fp, "%-15s  %-15s  %-15s ",
				inet_fmt(r->source->address, s1, sizeof(s1)),
				inet_fmt(g->group, s2, sizeof(s2)),
				IN_PIM_SSM_RANGE(g->group)
				? "SSM"
				: (g->active_rp_grp
				   ? inet_fmt(g->rpaddr, s3, sizeof(s3))
				   : "NULL"));

			dump_route(fp, r);
		}
	}

	for (rp = cand_rp_list; rp; rp = rp->next) {
		r = rp->rpentry->mrtlink;
		if (r) {
			if (r->flags & MRTF_KERNEL_CACHE) {
				for (kc = r->kernel_cache; kc; kc = kc->next)
					number_of_cache_mirrors++;
			}

			if (detail)
				fprintf(fp, "\nSource           Group            RP Address       Flags =\n");
			fprintf(fp, "%-15s  %-15s  %-15s ",
				inet_fmt(r->source->address, s1, sizeof(s1)),
				"ANY",
				"");

			dump_route(fp, r);
		}
	}

	fprintf(fp, "\nNumber of Groups        : %u\n", number_of_groups);
	fprintf(fp, "Number of Cache MIRRORs : %u\n", number_of_cache_mirrors);
}

#define ENABLED(v) (v ? "Enabled" : "Disabled")

static void show_status(FILE *fp)
{
	char buf[10];
	int len;

	snprintf(buf, sizeof(buf), "%d", curr_bsr_priority);
	MASK_TO_MASKLEN(curr_bsr_hash_mask, len);

	fprintf(fp, "Elected BSR\n");
	fprintf(fp, "    Address          : %s\n", inet_fmt(curr_bsr_address, s1, sizeof(s1)));
	fprintf(fp, "    Expiry Time      : %s\n", !pim_bootstrap_timer ? "N/A" : timetostr(pim_bootstrap_timer, NULL, 0));
	fprintf(fp, "    Priority         : %s\n", !curr_bsr_priority ? "N/A" : buf);
	fprintf(fp, "    Hash Mask Length : %d\n", len);

	snprintf(buf, sizeof(buf), "%d", my_bsr_priority);
	MASK_TO_MASKLEN(my_bsr_hash_mask, len);

	fprintf(fp, "Candidate BSR\n");
	fprintf(fp, "    State            : %s\n", ENABLED(cand_bsr_flag));
	fprintf(fp, "    Address          : %s\n", inet_fmt(my_bsr_address, s1, sizeof(s1)));
	fprintf(fp, "    Priority         : %s\n", !my_bsr_priority ? "N/A" : buf);

	fprintf(fp, "Candidate RP\n");
	fprintf(fp, "    State            : %s\n", ENABLED(cand_rp_flag));
	fprintf(fp, "    Address          : %s\n", inet_fmt(my_cand_rp_address, s1, sizeof(s1)));
	fprintf(fp, "    Priority         : %d\n", my_cand_rp_priority);
	fprintf(fp, "    Holdtime         : %d sec\n", my_cand_rp_holdtime);

	fprintf(fp, "Join/Prune Interval  : %d sec\n", PIM_JOIN_PRUNE_PERIOD);
	fprintf(fp, "Hello Interval       : %d sec\n", pim_timer_hello_interval);
	fprintf(fp, "Hello Holdtime       : %d sec\n", pim_timer_hello_holdtime);
	fprintf(fp, "IGMP query interval  : %d sec\n", igmp_query_interval);
	fprintf(fp, "IGMP querier timeout : %d sec\n", igmp_querier_timeout);
	fprintf(fp, "SPT Threshold        : %s\n", spt_threshold.mode == SPT_INF ? "Disabled" : "Enabled");
}

static void show_igmp_groups(FILE *fp)
{
	struct listaddr *group, *source;
	struct uvif *uv;
	vifi_t vifi;

	fprintf(fp, "Interface         Group            Source           Last Reported    Timeout=\n");
	for (vifi = 0, uv = uvifs; vifi < numvifs; vifi++, uv++) {
		for (group = uv->uv_groups; group; group = group->al_next) {
			char pre[40], post[40];

			snprintf(pre, sizeof(pre), "%-16s  %-15s  ",
				 uv->uv_name, inet_fmt(group->al_addr, s1, sizeof(s1)));

			snprintf(post, sizeof(post), "%-15s  %7u",
				 inet_fmt(group->al_reporter, s1, sizeof(s1)),
				 group->al_timer);

			if (!group->al_sources) {
				fprintf(fp, "%s%-15s  %s\n", pre, "ANY", post);
				continue;
			}

			for (source = group->al_sources; source; source = source->al_next)
				fprintf(fp, "%s%-15s  %s\n",
					pre, inet_fmt(source->al_addr, s1, sizeof(s1)), post);
		}
	}
}

static const char *ifstate(struct uvif *uv)
{
	if (uv->uv_flags & VIFF_DOWN)
		return "Down";

	if (uv->uv_flags & VIFF_DISABLED)
		return "Disabled";

	return "Up";
}

static void show_igmp_iface(FILE *fp)
{
	struct listaddr *group;
	struct uvif *uv;
	vifi_t vifi;

	fprintf(fp, "Interface         State     Querier          Timeout Version  Groups=\n");

	for (vifi = 0, uv = uvifs; vifi < numvifs; vifi++, uv++) {
		size_t num = 0;
		char timeout[10];
		int version;

		if (!uv->uv_querier) {
			strlcpy(s1, "Local", sizeof(s1));
			snprintf(timeout, sizeof(timeout), "None");
		} else {
			inet_fmt(uv->uv_querier->al_addr, s1, sizeof(s1));
			snprintf(timeout, sizeof(timeout), "%u", igmp_querier_timeout - uv->uv_querier->al_timer);
		}

		for (group = uv->uv_groups; group; group = group->al_next)
			num++;

		if (uv->uv_flags & VIFF_IGMPV1)
			version = 1;
		else if (uv->uv_flags & VIFF_IGMPV2)
			version = 2;
		else
			version = 3;

		fprintf(fp, "%-16s  %-8s  %-15s  %7s %7d  %6zd\n", uv->uv_name,
			ifstate(uv), s1, timeout, version, num);
	}
}

static void show_dump(FILE *fp)
{
	dump_vifs(fp);
	dump_ssm(fp);
	dump_pim_mrt(fp);
	dump_rp_set(fp);
}

static int do_debug(void *arg)
{
	struct ipc *msg = (struct ipc *)arg;

	if (!strcmp(msg->buf, "?"))
		return debug_list(DEBUG_ALL, msg->buf, sizeof(msg->buf));

	if (strlen(msg->buf)) {
		int rc = debug_parse(msg->buf);

		if ((int)DEBUG_PARSE_FAIL == rc)
			return 1;

		/* Activate debugging of new subsystems */
		debug = rc;
	}

	/* Return list of activated subsystems */
	if (debug)
		debug_list(debug, msg->buf, sizeof(msg->buf));
	else
		snprintf(msg->buf, sizeof(msg->buf), "none");

	return 0;
}

static int do_loglevel(void *arg)
{
	struct ipc *msg = (struct ipc *)arg;
	int rc;

	if (!strcmp(msg->buf, "?"))
		return log_list(msg->buf, sizeof(msg->buf));

	if (!strlen(msg->buf)) {
		strlcpy(msg->buf, log_lvl2str(loglevel), sizeof(msg->buf));
		return 0;
	}

	rc = log_str2lvl(msg->buf);
	if (-1 == rc)
		return 1;

	logit(LOG_NOTICE, 0, "Setting new log level %s", log_lvl2str(rc));
	loglevel = rc;

	return 0;
}

static void ipc_show(int sd, void (*cb)(FILE *))
{
	struct ipc msg = { 0 };
	FILE *fp;
	char fn[256];

	snprintf(fn, sizeof(fn), _PATH_PIMD_DUMP, ident);

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

static void ipc_generic(int sd, struct ipc *msg, int (*cb)(void *), void *arg)
{
	if (cb(arg))
		msg->cmd = IPC_ERR_CMD;
	else
		msg->cmd = IPC_OK_CMD;

	if (write(sd, msg, sizeof(*msg)) == -1)
		logit(LOG_WARNING, errno, "Failed sending IPC reply");
}

static void ipc_handle(int sd)
{
	socklen_t socklen = 0;
	struct ipc msg;
	ssize_t len;
	int client;

	client = accept(sd, NULL, &socklen);
	if (client < 0)
		return;

	len = read(client, &msg, sizeof(msg));
	if (len < 0) {
		logit(LOG_WARNING, errno, "Failed reading IPC command");
		close(client);
		return;
	}

	/* Set requested detail level */
	detail = msg.detail;

	switch (msg.cmd) {
	case IPC_DEBUG_CMD:
		ipc_generic(client, &msg, do_debug, &msg);
		break;

	case IPC_LOGLEVEL_CMD:
		ipc_generic(client, &msg, do_loglevel, &msg);
		break;

	case IPC_KILL_CMD:
		ipc_generic(client, &msg, daemon_kill, NULL);
		break;

	case IPC_RESTART_CMD:
		ipc_generic(client, &msg, daemon_restart, NULL);
		break;

	case IPC_SHOW_IGMP_GROUPS_CMD:
		ipc_show(client, show_igmp_groups);
		break;

	case IPC_SHOW_IGMP_IFACE_CMD:
		ipc_show(client, show_igmp_iface);
		break;

	case IPC_SHOW_PIM_IFACE_CMD:
		ipc_show(client, show_interfaces);
		break;

	case IPC_SHOW_PIM_NEIGH_CMD:
		ipc_show(client, show_neighbors);
		break;

	case IPC_SHOW_PIM_ROUTE_CMD:
		ipc_show(client, show_pim_mrt);
		break;

	case IPC_SHOW_PIM_RP_CMD:
		ipc_show(client, show_rp);
		break;

	case IPC_SHOW_PIM_CRP_CMD:
		ipc_show(client, show_crp);
		break;

	case IPC_SHOW_STATUS_CMD:
		ipc_show(client, show_status);
		break;

	case IPC_SHOW_PIM_DUMP_CMD:
		ipc_show(client, show_dump);
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
	if (sd < 0) {
		logit(LOG_ERR, errno, "Failed creating IPC socket");
		return;
	}

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
