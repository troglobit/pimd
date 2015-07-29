/*
 * Copyright (c) 1998-2001
 * University of Southern California/Information Sciences Institute.
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
/*
 *  $Id: igmp_proto.c,v 1.15 2001/09/10 20:31:36 pavlin Exp $
 */
/*
 * Part of this program has been derived from mrouted.
 * The mrouted program is covered by the license in the accompanying file
 * named "LICENSE.mrouted".
 *
 * The mrouted program is COPYRIGHT 1989 by The Board of Trustees of
 * Leland Stanford Junior University.
 *
 */

#include "defs.h"

typedef struct {
    vifi_t  vifi;
    struct listaddr *g;
    uint32_t source; /* Source for SSM */
    int q_time; /* IGMP Code */
    int q_len; /* Data length */
} cbk_t;


/*
 * Forward declarations.
 */
static void DelVif       (void *arg);
static int SetTimer      (vifi_t vifi, struct listaddr *g, uint32_t source);
static int SetVersionTimer      (vifi_t vifi, struct listaddr *g);
static int DeleteTimer   (int id);
static void send_query   (struct uvif *v, uint32_t group, int interval);
static void SendQuery    (void *arg);
static int SetQueryTimer (struct listaddr *g, vifi_t vifi, int to_expire, int q_time, int q_len);
static uint32_t igmp_group_membership_timeout(void);

/* The querier timeout depends on the configured query interval */
uint32_t igmp_query_interval  = IGMP_QUERY_INTERVAL;
uint32_t igmp_querier_timeout = IGMP_OTHER_QUERIER_PRESENT_INTERVAL;


/*
 * Send group membership queries on that interface if I am querier.
 */
void query_groups(struct uvif *v)
{
    int datalen = 4;
    int code = IGMP_MAX_HOST_REPORT_DELAY * IGMP_TIMER_SCALE;
    struct listaddr *g;

    v->uv_gq_timer = igmp_query_interval;

    if (v->uv_flags & VIFF_QUERIER) {
	/* IGMP version to use depends on the compatibility mode of the interface */
	if (v->uv_flags & VIFF_IGMPV2) {
	    /* RFC 3376: When in IGMPv2 mode, routers MUST send Periodic
	    Queries truncated at the Group Address field (i.e., 8 bytes long) */
	    datalen = 0;
	} else if (v->uv_flags & VIFF_IGMPV1) {
	    /* RFC 3376: When in IGMPv1 mode, routers MUST send Periodic Queries with a Max Response Time of 0 */
	    datalen = 0;
	    code = 0;
	}

	IF_DEBUG(DEBUG_IGMP)
	    logit(LOG_DEBUG, 0, "%s(): Sending IGMP v%s query on %s",
		  __func__, datalen == 4 ? "3" : "2", v->uv_name);
	send_igmp(igmp_send_buf, v->uv_lcl_addr, allhosts_group,
		  IGMP_MEMBERSHIP_QUERY,
		  code, 0, datalen);
    }

    /*
     * Decrement the old-hosts-present timer for each
     * active group on that vif.
     */
    for (g = v->uv_groups; g != NULL; g = g->al_next) {
	if (g->al_old > TIMER_INTERVAL)
	    g->al_old -= TIMER_INTERVAL;
	else
	    g->al_old = 0;
    }
}


/*
 * Process an incoming host membership query
 */
void accept_membership_query(uint32_t src, uint32_t dst __attribute__((unused)), uint32_t group, int tmo, int igmp_version)
{
    vifi_t vifi;
    struct uvif *v;

    /* Ignore my own membership query */
    if (local_address(src) != NO_VIF)
	return;

    /* Only v3 is allowed for SSM
     * TODO: Rate-limit messages?
     */
    if (igmp_version != 3 && IN_PIM_SSM_RANGE(group)) {
	logit(LOG_WARNING, 0, "SSM addresses are not allowed in v%d query.", igmp_version);
	return;
    }

    /* TODO: modify for DVMRP?? */
    if ((vifi = find_vif_direct(src)) == NO_VIF) {
	IF_DEBUG(DEBUG_IGMP)
	    logit(LOG_INFO, 0, "Ignoring group membership query from non-adjacent host %s",
		  inet_fmt(src, s1, sizeof(s1)));
	return;
    }

    v = &uvifs[vifi];

    /* Do not accept messages of higher version than current
     * compatibility mode as specified in RFC 3376 - 7.3.1
     */
    if (v->uv_querier) {
	if ((igmp_version == 3 && (v->uv_flags & VIFF_IGMPV2)) ||
	    (igmp_version == 2 && (v->uv_flags & VIFF_IGMPV1))) {
	    int i;

	    /*
	     * Exponentially back-off warning rate
	     */
	    i = ++v->uv_igmpv1_warn;
	    while (i && !(i & 1)) {
		i >>= 1;
		if (i == 1) {
		    logit(LOG_WARNING, 0, "Received IGMP v%d query from %s on vif %d,"
			  " but I am configured for IGMP v%d network compatibility mode",
			  igmp_version,
			  inet_fmt(src, s1, sizeof(s1)),
			  vifi,
			  v->uv_flags & VIFF_IGMPV1 ? 1 : 2);
		}
		return;
	    }
	}
    }

    if (!v->uv_querier || v->uv_querier->al_addr != src) {
	/*
	 * This might be:
	 * - A query from a new querier, with a lower source address
	 *   than the current querier (who might be me)
	 * - A query from a new router that just started up and doesn't
	 *   know who the querier is.
	 * - A query from the current querier
	 */
	if (ntohl(src) < (v->uv_querier
			  ? ntohl(v->uv_querier->al_addr)
			  : ntohl(v->uv_lcl_addr))) {
	    IF_DEBUG(DEBUG_IGMP) {
		logit(LOG_DEBUG, 0, "new querier %s (was %s) on vif %d",
		      inet_fmt(src, s1, sizeof(s1)),
		      v->uv_querier
		      ? inet_fmt(v->uv_querier->al_addr, s2, sizeof(s2))
		      : "me", vifi);
	    }

	    if (!v->uv_querier) {
		v->uv_querier = (struct listaddr *) calloc(1, sizeof(struct listaddr));
		if (!v->uv_querier) {
		    logit(LOG_ERR, 0, "Failed calloc() in accept_membership_query()");
		    return;
		}

		v->uv_querier->al_next = (struct listaddr *)NULL;
		v->uv_querier->al_timer = 0;
		v->uv_querier->al_genid = 0;
		v->uv_querier->al_mv = 0;
		v->uv_querier->al_old = 0;
		v->uv_querier->al_index = 0;
		v->uv_querier->al_timerid = 0;
		v->uv_querier->al_query = 0;
		v->uv_querier->al_flags = 0;

		v->uv_flags &= ~VIFF_QUERIER;
	    }
	    v->uv_querier->al_addr = src;
	    time(&v->uv_querier->al_ctime);
	}
    }

    /*
     * Reset the timer since we've received a query.
     */
    if (v->uv_querier && src == v->uv_querier->al_addr)
	v->uv_querier->al_timer = 0;

    /*
     * If this is a Group-Specific query which we did not source,
     * we must set our membership timer to [Last Member Query Count] *
     * the [Max Response Time] in the packet.
     */
    if (!(v->uv_flags & VIFF_IGMPV1) && group != 0 && src != v->uv_lcl_addr) {
	struct listaddr *g;

	IF_DEBUG(DEBUG_IGMP) {
	    logit(LOG_DEBUG, 0, "Group-specific membership query for %s from %s on vif %d, timer %d",
		  inet_fmt(group, s2, sizeof(s2)), inet_fmt(src, s1, sizeof(s1)), vifi, tmo);
	}

	for (g = v->uv_groups; g != NULL; g = g->al_next) {
	    if (group == g->al_addr && g->al_query == 0) {
		/* setup a timeout to remove the group membership */
		if (g->al_timerid)
		    g->al_timerid = DeleteTimer(g->al_timerid);

		g->al_timer = IGMP_LAST_MEMBER_QUERY_COUNT * tmo / IGMP_TIMER_SCALE;
		/* use al_query to record our presence in last-member state */
		g->al_query = -1;
		g->al_timerid = SetTimer(vifi, g, 0);
		IF_DEBUG(DEBUG_IGMP) {
		    logit(LOG_DEBUG, 0, "Timer for grp %s on vif %d set to %ld",
			  inet_fmt(group, s2, sizeof(s2)), vifi, g->al_timer);
		}
		break;
	    }
	}
    }
}


/*
 * Process an incoming group membership report.
 */
void accept_group_report(uint32_t igmp_src, uint32_t ssm_src, uint32_t group, int igmp_report_type)
{
    vifi_t vifi;
    struct uvif *v;
    struct listaddr *g;
    struct listaddr *s = NULL;

    if ((vifi = find_vif_direct_local(igmp_src)) == NO_VIF) {
	IF_DEBUG(DEBUG_IGMP) {
	    logit(LOG_INFO, 0, "Ignoring group membership report from non-adjacent host %s",
		  inet_fmt(igmp_src, s1, sizeof(s1)));
	}
	return;
    }

    inet_fmt(igmp_src, s1, sizeof(s1));
    inet_fmt(ssm_src, s2, sizeof(s2));
    inet_fmt(group, s3, sizeof(s3));
    IF_DEBUG(DEBUG_IGMP)
	logit(LOG_DEBUG, 0, "%s(): igmp_src %s ssm_src %s group %s report_type %i",
	      __func__, s1, s2, s3, igmp_report_type);

    v = &uvifs[vifi];

    /*
     * Look for the group in our group list; if found, reset its timer.
     */
    for (g = v->uv_groups; g != NULL; g = g->al_next) {
	if (group == g->al_addr) {
	    if (igmp_report_type == IGMP_V1_MEMBERSHIP_REPORT) {
		g->al_old = DVMRP_OLD_AGE_THRESHOLD;
		if (!IN_PIM_SSM_RANGE(group) && g->al_pv>1) {
		    IF_DEBUG(DEBUG_IGMP)
			logit(LOG_DEBUG, 0, "Change IGMP compatibility mode to v1 for group %s", s3);
		    g->al_pv = 1;
		}
	    } else if (!IN_PIM_SSM_RANGE(group) && igmp_report_type == IGMP_V2_MEMBERSHIP_REPORT) {
		IF_DEBUG(DEBUG_IGMP)
		    logit(LOG_DEBUG,0, "%s(): al_pv=%d", __func__, g->al_pv);
		if (g->al_pv > 2) {
		    IF_DEBUG(DEBUG_IGMP)
			logit(LOG_DEBUG, 0, "Change IGMP compatibility mode to v2 for group %s", s3);
		    g->al_pv = 2;
		}
	    }

	    g->al_reporter = igmp_src;

	    /** delete old timers, set a timer for expiration **/
	    g->al_timer = igmp_group_membership_timeout();
	    if (g->al_query)
		g->al_query = DeleteTimer(g->al_query);

	    if (g->al_timerid)
		g->al_timerid = DeleteTimer(g->al_timerid);

	    g->al_timerid = SetTimer(vifi, g, ssm_src);

	    /* Reset timer for switching version back every time an older version report is received */
	    if (!IN_PIM_SSM_RANGE(group) && g->al_pv<3 && (igmp_report_type == IGMP_V1_MEMBERSHIP_REPORT ||
		igmp_report_type == IGMP_V2_MEMBERSHIP_REPORT)) {
		if (g->al_versiontimer)
			g->al_versiontimer = DeleteTimer(g->al_versiontimer);

		g->al_versiontimer = SetVersionTimer(vifi, g);
	    }

	    /* Find source */
	    if (IN_PIM_SSM_RANGE(group)) {
		for (s = g->al_sources; s; s = s->al_next) {
		    IF_DEBUG(DEBUG_IGMP)
			logit(LOG_DEBUG, 0, "%s(): Seek source %s, curr=%s", __func__,
			      inet_fmt(ssm_src, s1, sizeof(s1)),
			      inet_fmt(s->al_addr, s2, sizeof(s2)));
		    if (ssm_src == s->al_addr) {
			IF_DEBUG(DEBUG_IGMP)
			    logit(LOG_DEBUG, 0, "%s(): Source found", __func__);
			break;
		    }
		}
		if (!s) {
		    /* Add new source */
		    s = (struct listaddr *)calloc(1, sizeof(struct listaddr));
		    if (!s) {
			logit(LOG_ERR, errno, "%s(): Ran out of memory", __func__);
			return;
		    }
		    s->al_addr = ssm_src;
		    s->al_next = g->al_sources;
		    g->al_sources = s;

		    IF_DEBUG(DEBUG_IGMP)
			logit(LOG_DEBUG, 0, "%s(): Source %s added to g:%p", __func__, s2, g);
		}
	    }

	    /* TODO: might need to add a check if I am the forwarder??? */
	    /* if (v->uv_flags & VIFF_DR) */
	    if (IN_PIM_SSM_RANGE(group)) {
		IF_DEBUG(DEBUG_IGMP)
		    logit(LOG_INFO, 0, "Add leaf (%s,%s)", s1, s3);
		add_leaf(vifi, ssm_src, group);
	    } else {
		add_leaf(vifi, INADDR_ANY_N, group);
	    }
	    break;
	}
    }

    /*
     * If not found, add it to the list and update kernel cache.
     */
    if (!g) {
	g = (struct listaddr *)calloc(1, sizeof(struct listaddr));
	if (!g) {
	    logit(LOG_ERR, errno, "%s(): Ran out of memory", __func__);
	    return;
	}

	g->al_addr = group;
	if (!IN_PIM_SSM_RANGE(group) && igmp_report_type == IGMP_V1_MEMBERSHIP_REPORT) {
	    g->al_old = DVMRP_OLD_AGE_THRESHOLD;
	    IF_DEBUG(DEBUG_IGMP)
		logit(LOG_DEBUG, 0, "Change IGMP compatibility mode to v1 for group %s", s3);
	    g->al_pv = 1;
	} else if (!IN_PIM_SSM_RANGE(group) && igmp_report_type == IGMP_V2_MEMBERSHIP_REPORT) {
	    IF_DEBUG(DEBUG_IGMP)
		logit(LOG_DEBUG, 0, "Change IGMP compatibility mode to v2 for group %s", s3);
	    g->al_pv = 2;
	} else {
	    g->al_pv = 3;
	}

	/* Add new source */
	if (IN_PIM_SSM_RANGE(group)) {
	    s = (struct listaddr *)calloc(1, sizeof(struct listaddr));
	    if (!s) {
		logit(LOG_ERR, errno, "%s(): Ran out of memory", __func__);
		return;
	    }
	    s->al_addr = ssm_src;
	    s->al_next = g->al_sources;
	    g->al_sources = s;
	    IF_DEBUG(DEBUG_IGMP)
		logit(LOG_DEBUG, 0, "%s(): Source %s added to new g:%p", __func__, s2, g);
	}

	/** set a timer for expiration **/
	g->al_query     = 0;
	g->al_timer     = IGMP_GROUP_MEMBERSHIP_INTERVAL;
	g->al_reporter  = igmp_src;
	g->al_timerid   = SetTimer(vifi, g, ssm_src);

	/* Set timer for swithing version back if an older version report is received */
	if (!IN_PIM_SSM_RANGE(group) && g->al_pv<3) {
	    g->al_versiontimer = SetVersionTimer(vifi, g);
	}

	g->al_next      = v->uv_groups;
	v->uv_groups    = g;
	time(&g->al_ctime);

	/* TODO: might need to add a check if I am the forwarder??? */
	/* if (v->uv_flags & VIFF_DR) */
	if (IN_PIM_SSM_RANGE(group)) {
	    IF_DEBUG(DEBUG_IGMP)
		logit(LOG_INFO, 0, "SSM group order from  %s (%s,%s)", s1, s2, s3);
	    add_leaf(vifi, ssm_src, group);
	} else {
	    IF_DEBUG(DEBUG_IGMP)
		logit(LOG_INFO, 0, "SM group order from  %s (*,%s)", s1, s3);
	    add_leaf(vifi, INADDR_ANY_N, group);
	}
    }
}


/* TODO: send PIM prune message if the last member? */
void accept_leave_message(uint32_t src, uint32_t dst __attribute__((unused)), uint32_t group)
{
    vifi_t vifi;
    struct uvif *v;
    struct listaddr *g;

    int datalen = 4;
    int code = IGMP_LAST_MEMBER_QUERY_INTERVAL * IGMP_TIMER_SCALE;

    /* TODO: modify for DVMRP ??? */
    if ((vifi = find_vif_direct_local(src)) == NO_VIF) {
	IF_DEBUG(DEBUG_IGMP)
	    logit(LOG_INFO, 0, "ignoring group leave report from non-adjacent host %s",
		  inet_fmt(src, s1, sizeof(s1)));
	return;
    }

    inet_fmt(src, s1, sizeof(s1));
    inet_fmt(dst, s2, sizeof(s2));
    inet_fmt(group, s3, sizeof(s3));
    IF_DEBUG(DEBUG_IGMP)
	logit(LOG_DEBUG, 0, "%s(): src %s dst %s group %s", __func__, s1, s2, s3);
    v = &uvifs[vifi];

#if 0
    /* XXX: a PIM-SM last-hop router needs to know when a local member
     * has left.
     */
    if (!(v->uv_flags & (VIFF_QUERIER | VIFF_DR))
	|| (v->uv_flags & VIFF_IGMPV1))
	return;
#endif

    /*
     * Look for the group in our group list in order to set up a short-timeout
     * query.
     */
    for (g = v->uv_groups; g; g = g->al_next) {
	if (group == g->al_addr) {
	    IF_DEBUG(DEBUG_IGMP)
		logit(LOG_DEBUG, 0, "accept_leave_message(): old=%d query=%d", g->al_old, g->al_query);

	    /* Ignore the leave message if there are old hosts present */
	    if (g->al_old)
		return;

	    /* still waiting for a reply to a query, ignore the leave */
	    if (g->al_query)
		return;

	    /* TODO: Remove the source. Ignore the leave if there
	       are still sources left
	    if (IN_PIM_SSM_RANGE(g->al_addr)) {
		for (s = g->al_sources; s != NULL; s = s->al_next) {
		    if (dst == s->al_addr) {
		    }
		}
	    }
	    */

	    /** delete old timer set a timer for expiration **/
	    if (g->al_timerid)
		g->al_timerid = DeleteTimer(g->al_timerid);

#if IGMP_LAST_MEMBER_QUERY_COUNT != 2
/*
  This code needs to be updated to keep a counter of the number
  of queries remaining.
*/
#endif

	    if (v->uv_flags & VIFF_QUERIER) {
		/* Use lowest IGMP version */
		if (v->uv_flags & VIFF_IGMPV2 || g->al_pv <= 2) {
		    datalen = 0;
		} else if (v->uv_flags & VIFF_IGMPV1 || g->al_pv == 1) {
		    datalen = 0;
		    code = 0;
		}

		IF_DEBUG(DEBUG_IGMP)
		    logit(LOG_DEBUG, 0, "%s(): Sending IGMP v%s query (al_pv=%d)",
			  __func__, datalen == 4 ? "3" : "2", g->al_pv);
		send_igmp(igmp_send_buf, v->uv_lcl_addr, g->al_addr,
			  IGMP_MEMBERSHIP_QUERY,
			  code,
			  g->al_addr, datalen);
	    }

	    g->al_timer = IGMP_LAST_MEMBER_QUERY_INTERVAL * (IGMP_LAST_MEMBER_QUERY_COUNT + 1);
	    g->al_query = SetQueryTimer(g, vifi,
					IGMP_LAST_MEMBER_QUERY_INTERVAL,
					code, datalen);
	    g->al_timerid = SetTimer(vifi, g, dst);
	    break;
	}
    }
}

/*
 * Time out old version compatibility mode
 */
static void SwitchVersion(void *arg)
{
    cbk_t *cbk = (cbk_t *)arg;

    if (cbk->g->al_pv < 3)
	cbk->g->al_pv += 1;

    logit(LOG_INFO, 0, "Switch IGMP compatibility mode back to v%d for group %s",
	  cbk->g->al_pv, inet_fmt(cbk->g->al_addr, s1, sizeof(s1)));
}

/*
 * Loop through and process all sources in a v3 record.
 *
 * Parameters:
 *     igmp_report_type   Report type of IGMP message
 *     igmp_src           Src address of IGMP message
 *     group              Multicast group
 *     sources            Pointer to the beginning of sources list in the IGMP message
 *     report_pastend     Pointer to the end of IGMP message
 *
 * Returns:
 *     1 if succeeded, 0 if failed
 */
int accept_sources(int igmp_report_type, uint32_t igmp_src, uint32_t group, uint8_t *sources,
    uint8_t *report_pastend, int rec_num_sources) {
    int j;
    uint8_t *src;
    char src_str[200];

    for (j = 0, src = sources; j < rec_num_sources; ++j, src += 4) {
        if ((src + 4) > report_pastend) {
	    IF_DEBUG(DEBUG_IGMP)
		logit(LOG_DEBUG, 0, "src +4 > report_pastend");
            return 0;
        }

        inet_ntop(AF_INET, src, src_str , sizeof(src_str));
	IF_DEBUG(DEBUG_IGMP)
	    logit(LOG_DEBUG, 0, "Add source (%s,%s)", src_str, inet_fmt(group, s1, sizeof(s1)));

        accept_group_report(igmp_src, ((struct in_addr*)src)->s_addr, group, igmp_report_type);

	IF_DEBUG(DEBUG_IGMP)
	    logit(LOG_DEBUG, 0, "Accepted, switch SPT (%s,%s)", src_str, inet_fmt(group, s1, sizeof(s1)));
        switch_shortest_path(((struct in_addr*)src)->s_addr, group);
    }

    return 1;
}

/*
 * Handle IGMP v3 membership reports (join/leave)
 */
void accept_membership_report(uint32_t src, uint32_t dst, struct igmpv3_report *report, ssize_t reportlen)
{
    struct igmpv3_grec *record;
    int num_groups, i;
    uint8_t *report_pastend = (uint8_t *)report + reportlen;

    num_groups = ntohs(report->ngrec);
    if (num_groups < 0) {
	logit(LOG_INFO, 0, "Invalid Membership Report from %s: num_groups = %d",
	      inet_fmt(src, s1, sizeof(s1)), num_groups);
	return;
    }

    IF_DEBUG(DEBUG_IGMP)
	logit(LOG_DEBUG, 0, "%s(): IGMP v3 report, %d bytes, from %s to %s with %d group records.",
	      __func__, reportlen, inet_fmt(src, s1, sizeof(s1)), inet_fmt(dst, s2, sizeof(s2)), num_groups);

    record = &report->grec[0];

    for (i = 0; i < num_groups; i++) {
	struct in_addr  rec_group;
	uint8_t        *sources;
	int             rec_type;
	int             rec_auxdatalen;
	int             rec_num_sources;
	int             j;
	char src_str[200];
	int record_size = 0;

	rec_num_sources = ntohs(record->grec_nsrcs);
	rec_auxdatalen = record->grec_auxwords;
	record_size = sizeof(struct igmpv3_grec) + sizeof(uint32_t) * rec_num_sources + rec_auxdatalen;
	if ((uint8_t *)record + record_size > report_pastend) {
	    logit(LOG_INFO, 0, "Invalid group report %p > %p",
		  (uint8_t *)record + record_size, report_pastend);
	    return;
	}

	rec_type = record->grec_type;
	rec_group.s_addr = (in_addr_t)record->grec_mca;
	sources = (u_int8_t *)record->grec_src;

	switch (rec_type) {
	    case IGMP_MODE_IS_EXCLUDE:
		/* RFC 4604: A router SHOULD ignore a group record of
		   type MODE_IS_EXCLUDE if it refers to an SSM destination address */
		if (!IN_PIM_SSM_RANGE(rec_group.s_addr)) {
		    if (rec_num_sources==0) {
			/* RFC 5790: EXCLUDE (*,G) join can be interpreted by the router
			   as a request to include all sources. */
			accept_group_report(src, 0 /*dst*/, rec_group.s_addr, report->type);
		    } else {
			/* RFC 5790: LW-IGMPv3 does not use EXCLUDE filter-mode with a non-null source address list.*/
			logit(LOG_INFO, 0, "Record type MODE_IS_EXCLUDE with non-null source list is currently unsupported.");
		    }
		}
		break;

	    case IGMP_CHANGE_TO_EXCLUDE_MODE:
		/* RFC 4604: A router SHOULD ignore a group record of
		   type CHANGE_TO_EXCLUDE_MODE if it refers to an SSM destination address */
		if (!IN_PIM_SSM_RANGE(rec_group.s_addr)) {
		    if (rec_num_sources==0) {
			/* RFC 5790: EXCLUDE (*,G) join can be interpreted by the router
			   as a request to include all sources. */
			accept_group_report(src, 0 /*dst*/, rec_group.s_addr, report->type);
		    } else {
			/* RFC 5790: LW-IGMPv3 does not use EXCLUDE filter-mode with a non-null source address list.*/
			logit(LOG_DEBUG, 0, "Record type MODE_TO_EXCLUDE with non-null source list is currently unsupported.");
		    }
		}
		break;

	    case IGMP_MODE_IS_INCLUDE:
		if (!accept_sources(report->type, src, rec_group.s_addr, sources, report_pastend, rec_num_sources)) {
		    IF_DEBUG(DEBUG_IGMP)
			logit(LOG_DEBUG, 0, "Accept sources failed.");
		    return;
		}
		break;

	    case IGMP_CHANGE_TO_INCLUDE_MODE:
		if (!accept_sources(report->type, src, rec_group.s_addr, sources, report_pastend, rec_num_sources)) {
		    IF_DEBUG(DEBUG_IGMP)
			logit(LOG_DEBUG, 0, "Accept sources failed.");
		    return;
		}
		break;

	    case IGMP_ALLOW_NEW_SOURCES:
		if (!accept_sources(report->type, src, rec_group.s_addr, sources, report_pastend, rec_num_sources)) {
		    logit(LOG_DEBUG, 0, "Accept sources failed.");
		    return;
		}
		break;

	    case IGMP_BLOCK_OLD_SOURCES:
		for (j = 0; j < rec_num_sources; j++) {
		    uint32_t *gsrc = (uint32_t *)&record->grec_src[j];

		    if ((uint8_t *)gsrc > report_pastend) {
			logit(LOG_INFO, 0, "Invalid group record");
			return;
		    }

		    inet_ntop(AF_INET, gsrc, src_str , sizeof(src_str));
		    IF_DEBUG(DEBUG_IGMP)
			logit(LOG_DEBUG, 0, "Remove source[%d] (%s,%s)", j, src_str, inet_ntoa(rec_group));
		    accept_leave_message(src, *gsrc, rec_group.s_addr);
		    IF_DEBUG(DEBUG_IGMP)
			logit(LOG_DEBUG, 0, "Accepted");
		}
		break;

	    default:
		//  RFC3376: Unrecognized Record Type values MUST be silently ignored.
		break;
	}

	record = (struct igmpv3_grec *)((uint8_t *)record + record_size);
    }
}

/*
 * Calculate group membership timeout
 */
static uint32_t igmp_group_membership_timeout(void)
{
    return IGMP_ROBUSTNESS_VARIABLE * igmp_query_interval + IGMP_QUERY_RESPONSE_INTERVAL;
}

/*
 * Time out record of a group membership on a vif
 */
static void DelVif(void *arg)
{
    cbk_t *cbk = (cbk_t *)arg;
    vifi_t vifi = cbk->vifi;
    struct uvif *v = &uvifs[vifi];
    struct listaddr *a, **anp, *g = cbk->g;
    struct listaddr *curr, *prev = NULL;

    if (IN_PIM_SSM_RANGE(g->al_addr)) {
	for (curr = g->al_sources; curr; prev = curr, curr = curr->al_next) {
	    inet_fmt(cbk->source, s1, sizeof(s1));
	    inet_fmt(curr->al_addr, s2, sizeof(s2));
	    IF_DEBUG(DEBUG_IGMP)
		logit(LOG_DEBUG, 0, "DelVif: Seek source %s, curr=%s (%p)", s1, s2, curr);

	    if (curr->al_addr == cbk->source) {
		if (!prev)
		    g->al_sources = curr->al_next; /* Remove from beginning */
		else
		    prev->al_next = curr->al_next;

		free(curr);
		break;
	    }
	}

	IF_DEBUG(DEBUG_IGMP)
	    logit(LOG_DEBUG, 0, "DelVif: %s sources left", g->al_sources ? "Still" : "No");
	if (g->al_sources) {
	    IF_DEBUG(DEBUG_IGMP)
		logit(LOG_DEBUG, 0, "DelVif: Not last source, g->al_sources --> %s",
		      inet_fmt(g->al_sources->al_addr, s1, sizeof(s1)));
	    delete_leaf(vifi, cbk->source, g->al_addr);
	    free(cbk);

	    return;    /* This was not last source for this interface */
	}
    }

    /*
     * Group has expired
     * delete all kernel cache entries with this group
     */
    if (g->al_query)
	DeleteTimer(g->al_query);

    if (g->al_versiontimer)
	DeleteTimer(g->al_versiontimer);

    if (IN_PIM_SSM_RANGE(g->al_addr)) {
	inet_fmt(g->al_addr, s1, sizeof(s1));
	inet_fmt(cbk->source, s2, sizeof(s2));
	IF_DEBUG(DEBUG_IGMP)
	    logit(LOG_DEBUG, 0, "SSM range, source specific delete");

	/* delete (S,G) entry */
	IF_DEBUG(DEBUG_IGMP)
	    logit(LOG_DEBUG, 0, "DelVif: vif:%d(%s), (S=%s,G=%s)", vifi, v->uv_name, s2, s1);
	delete_leaf(vifi, cbk->source, g->al_addr);
    } else {
	delete_leaf(vifi, INADDR_ANY_N, g->al_addr);
    }

    anp = &(v->uv_groups);
    while ((a = *anp)) {
	if (a == g) {
	    *anp = a->al_next;
	    free(a->al_sources);
	    free(a);
	} else {
	    anp = &a->al_next;
	}
    }

    free(cbk);
}

/*
 * Set a timer to switch version back on a vif.
 */
static int SetVersionTimer(vifi_t vifi, struct listaddr *g)
{
    cbk_t *cbk;

    cbk = (cbk_t *)calloc(1, sizeof(cbk_t));
    if (!cbk) {
	logit(LOG_ERR, 0, "Failed calloc() in SetVersionTimer()\n");
	return -1;
    }

    cbk->vifi = vifi;
    cbk->g = g;

    return timer_setTimer(IGMP_ROBUSTNESS_VARIABLE * igmp_query_interval + IGMP_QUERY_RESPONSE_INTERVAL,
			  SwitchVersion, cbk);
}

/*
 * Set a timer to delete the record of a group membership on a vif.
 */
static int SetTimer(vifi_t vifi, struct listaddr *g, uint32_t source)
{
    cbk_t *cbk;

    cbk = (cbk_t *) calloc(1, sizeof(cbk_t));
    if (!cbk) {
	logit(LOG_ERR, 0, "Failed calloc() in SetTimer()");
	return -1;
    }

    cbk->vifi = vifi;
    cbk->g = g;
    cbk->source = source;

    IF_DEBUG(DEBUG_IGMP)
	logit(LOG_DEBUG, 0, "Set delete timer for group: %s", inet_ntoa(*((struct in_addr *)&g->al_addr)));

    return timer_setTimer(g->al_timer, DelVif, cbk);
}


/*
 * Delete a timer that was set above.
 */
static int DeleteTimer(int id)
{
    timer_clearTimer(id);

    return 0;
}


/*
 * Send IGMP Query
 */
static void send_query(struct uvif *v, uint32_t group, int interval)
{
    if (v->uv_flags & VIFF_QUERIER) {
	send_igmp(igmp_send_buf, v->uv_lcl_addr, group,
		  IGMP_MEMBERSHIP_QUERY, interval, group != allhosts_group ? group : 0, 0);
    }
}

/*
 * Send a group-specific query.
 */
static void SendQuery(void *arg)
{
    cbk_t *cbk = (cbk_t *)arg;

    IF_DEBUG(DEBUG_IGMP)
	logit(LOG_DEBUG, 0, "SendQuery: Send IGMP v%s query", cbk->q_len == 4 ? "3" : "2");
    send_query(&uvifs[cbk->vifi], cbk->g->al_addr, cbk->q_time);
    cbk->g->al_query = 0;
    free(cbk);
}


/*
 * Set a timer to send a group-specific query.
 */
static int SetQueryTimer(struct listaddr *g, vifi_t vifi, int to_expire, int q_time, int q_len)
{
    cbk_t *cbk;

    cbk = (cbk_t *)calloc(1, sizeof(cbk_t));
    if (!cbk) {
	logit(LOG_ERR, 0, "Failed calloc() in SetQueryTimer()");
	return -1;
    }

    cbk->g = g;
    cbk->q_time = q_time;
    cbk->q_len = q_len;
    cbk->vifi = vifi;

    return timer_setTimer(to_expire, SendQuery, cbk);
}

/**
 * Local Variables:
 *  version-control: t
 *  indent-tabs-mode: t
 *  c-file-style: "ellemtel"
 *  c-basic-offset: 4
 * End:
 */
