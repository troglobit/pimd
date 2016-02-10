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
 *  $Id: timer.c,v 1.31 2001/09/10 20:31:37 pavlin Exp $
 */


#include "defs.h"

/*
 * Global variables
 */

/* To account for header overhead, we apx 1 byte/s = 10 bits/s (bps)
 * Note, in the new spt_threshold setting the rate is in kbps as well! */
spt_threshold_t spt_threshold = {
    .mode     = SPT_THRESHOLD_DEFAULT_MODE,
    .bytes    = SPT_THRESHOLD_DEFAULT_RATE * SPT_THRESHOLD_DEFAULT_INTERVAL / 10 * 1000,
    .packets  = SPT_THRESHOLD_DEFAULT_PACKETS,
    .interval = SPT_THRESHOLD_DEFAULT_INTERVAL,
};

/*
 * Local variables
 */
uint16_t unicast_routing_interval = UCAST_ROUTING_CHECK_INTERVAL;
uint16_t unicast_routing_timer;   /* Used to check periodically for any
				   * change in the unicast routing. */
uint8_t ucast_flag;

uint16_t pim_spt_threshold_timer; /* Used for periodic check of spt-threshold
				   * for the RP or the lasthop router. */
uint8_t rate_flag;

/*
 * TODO: XXX: the timers below are not used. Instead, the data rate timer is used.
 */
uint16_t kernel_cache_timer;      /* Used to timeout the kernel cache
				   * entries for idle sources */
uint16_t kernel_cache_interval;

/* to request and compare any route changes */
srcentry_t srcentry_save;
rpentry_t  rpentry_save;

/*
 * Init some timers
 */
void init_timers(void)
{
    SET_TIMER(unicast_routing_timer, unicast_routing_interval);
    SET_TIMER(pim_spt_threshold_timer, spt_threshold.interval);

    /* Initialize the srcentry and rpentry used to save the old routes
     * during unicast routing change discovery process. */
    srcentry_save.prev       = NULL;
    srcentry_save.next       = NULL;
    srcentry_save.address    = INADDR_ANY_N;
    srcentry_save.mrtlink    = NULL;
    srcentry_save.incoming   = NO_VIF;
    srcentry_save.upstream   = NULL;
    srcentry_save.metric     = ~0;
    srcentry_save.preference = ~0;
    RESET_TIMER(srcentry_save.timer);
    srcentry_save.cand_rp    = NULL;

    rpentry_save.prev       = NULL;
    rpentry_save.next       = NULL;
    rpentry_save.address    = INADDR_ANY_N;
    rpentry_save.mrtlink    = NULL;
    rpentry_save.incoming   = NO_VIF;
    rpentry_save.upstream   = NULL;
    rpentry_save.metric     = ~0;
    rpentry_save.preference = ~0;
    RESET_TIMER(rpentry_save.timer);
    rpentry_save.cand_rp    = NULL;
}

/*
 * On every timer interrupt, advance (i.e. decrease) the timer for each
 * neighbor and group entry for each vif.
 */
void age_vifs(void)
{
    vifi_t           vifi;
    struct uvif     *v;
    pim_nbr_entry_t *next, *curr;

    /* XXX: TODO: currently, sending to qe* interface which is DOWN
     * doesn't return error (ENETDOWN) on my Solaris machine,
     * so have to check periodically the
     * interfaces status. If this is fixed, just remove the defs around
     * the "if (vifs_down)" line.
     */

#if (!((defined SunOS) && (SunOS >= 50)))
    if (vifs_down)
#endif /* Solaris */
	check_vif_state();

    /* Age many things */
    for (vifi = 0, v = uvifs; vifi < numvifs; ++vifi, ++v) {
	if (v->uv_flags & (VIFF_DISABLED | VIFF_DOWN | VIFF_REGISTER))
	    continue;

	/* Timeout neighbors */
	for (curr = v->uv_pim_neighbors; curr; curr = next) {
	    next = curr->next;

	    /* Never timeout neighbors with holdtime = 0xffff.
	     * This may be used with ISDN lines to avoid keeping the
	     * link up with periodic Hello messages.
	     */
	    /* TODO: XXX: TIMER implem. dependency! */
	    if (PIM_HELLO_HOLDTIME_FOREVER == curr->timer)
		continue;
	    IF_NOT_TIMEOUT(curr->timer)
		continue;

	    logit(LOG_INFO, 0, "Delete PIM neighbor %s on %s (holdtime timeout)",
		  inet_fmt(curr->address, s2, sizeof(s2)), v->uv_name);

	    delete_pim_nbr(curr);
	}

	/* PIM_HELLO periodic */
	IF_TIMEOUT(v->uv_hello_timer)
	    send_pim_hello(v, pim_timer_hello_holdtime);

#ifdef TOBE_DELETED
	/* PIM_JOIN_PRUNE periodic */
	/* TODO: XXX: TIMER implem. dependency! */
	if (v->uv_jp_timer <= TIMER_INTERVAL)
	    /* TODO: need to scan the whole routing table,
	     * because different entries have different Join/Prune timer.
	     * Probably don't need the Join/Prune timer per vif.
	     */
	    send_pim_join_prune(vifi, NULL, PIM_JOIN_PRUNE_HOLDTIME);
	else
	    /* TODO: XXX: TIMER implem. dependency! */
	    v->uv_jp_timer -= TIMER_INTERVAL;
#endif /* TOBE_DELETED */

	/* IGMP query periodic */
	IF_TIMEOUT(v->uv_gq_timer)
	    query_groups(v);

	if (v->uv_querier &&
	    (v->uv_querier->al_timer += TIMER_INTERVAL) > igmp_querier_timeout) {
	    /*
	     * The current querier has timed out.  We must become the
	     * querier.
	     */
	    IF_DEBUG(DEBUG_IGMP) {
		logit(LOG_DEBUG, 0, "IGMP Querier %s timed out.",
		      inet_fmt(v->uv_querier->al_addr, s1, sizeof(s1)));
	    }
	    free(v->uv_querier);
	    v->uv_querier = NULL;
	    v->uv_flags |= VIFF_QUERIER;
	    query_groups(v);
	}
    }

    IF_DEBUG(DEBUG_IF) {
	fputs("\n", stderr);
	dump_vifs(stderr);
    }
}

#define MRT_IS_LASTHOP(mrt) VIFM_LASTHOP_ROUTER(mrt->leaves, mrt->oifs)
#define MRT_IS_RP(mrt)      mrt->incoming == reg_vif_num

static void try_switch_to_spt(mrtentry_t *mrt, kernel_cache_t *kc)
{
    if (MRT_IS_LASTHOP(mrt) || MRT_IS_RP(mrt)) {
#ifdef KERNEL_MFC_WC_G
	if (kc->source == INADDR_ANY_N) {
	    delete_single_kernel_cache(mrt, kc);
	    mrt->flags |= MRTF_MFC_CLONE_SG;
	    return;
	}
#endif /* KERNEL_MFC_WC_G */

	switch_shortest_path(kc->source, kc->group);
    }
}

/*
 * Check the SPT threshold for a given (*,*,RP) or (*,G) entry
 *
 * XXX: the spec says to start monitoring first the total traffic for
 * all senders for particular (*,*,RP) or (*,G) and if the total traffic
 * exceeds some predefined threshold, then start monitoring the data
 * traffic for each particular sender for this group: (*,G) or
 * (*,*,RP). However, because the kernel cache/traffic info is of the
 * form (S,G), it is easier if we are simply collecting (S,G) traffic
 * all the time.
 *
 * For (*,*,RP) if the number of bytes received between the last check
 * and now exceeds some precalculated value (based on interchecking
 * period and datarate threshold AND if there are directly connected
 * members (i.e. we are their last hop(e) router), then create (S,G) and
 * start initiating (S,G) Join toward the source. The same applies for
 * (*,G).  The spec does not say that if the datarate goes below a given
 * threshold, then will switch back to the shared tree, hence after a
 * switch to the source-specific tree occurs, a source with low
 * datarate, but periodically sending will keep the (S,G) states.
 *
 * If a source with kernel cache entry has been idle after the last time
 * a check of the datarate for the whole routing table, then delete its
 * kernel cache entry.
 */
static void check_spt_threshold(mrtentry_t *mrt)
{
    int status;
    uint32_t prev_bytecnt, prev_pktcnt;
    kernel_cache_t *kc, *kc_next;

    /* XXX: TODO: When we add group-list support to spt-threshold we need
     * to move this infinity check to inside the for-loop ... obviously. */
    if (!rate_flag || spt_threshold.mode == SPT_INF)
	return;

    for (kc = mrt->kernel_cache; kc; kc = kc_next) {
	kc_next = kc->next;

	prev_bytecnt = kc->sg_count.bytecnt;
	prev_pktcnt  = kc->sg_count.pktcnt;

	status = k_get_sg_cnt(udp_socket, kc->source, kc->group, &kc->sg_count);
	if (status || prev_bytecnt == kc->sg_count.bytecnt) {
	    /* Either (for whatever reason) there is no such routing
	     * entry, or that particular (S,G) was idle.  Delete the
	     * routing entry from the kernel. */
	    delete_single_kernel_cache(mrt, kc);
	    continue;
	}

	// TODO: Why is this needed?
	try_switch_to_spt(mrt, kc);

	/* Check spt-threshold for forwarder and RP, should we switch to
	 * source specific tree (SPT).  Need to check only when we have
	 * (S,G)RPbit in the forwarder or the RP itself. */
	switch (spt_threshold.mode) {
	    case SPT_RATE:
		if (prev_bytecnt + spt_threshold.bytes < kc->sg_count.bytecnt)
		    try_switch_to_spt(mrt, kc);
		break;

	    case SPT_PACKETS:
		if (prev_pktcnt + spt_threshold.packets < kc->sg_count.pktcnt)
		    try_switch_to_spt(mrt, kc);
		break;

	    default:
		;		/* INF not handled here yet. */
	}

	/* XXX: currently the spec doesn't say to switch back to the
	 * shared tree if low datarate, but if needed to implement, the
	 * check must be done here. Don't forget to check whether I am a
	 * forwarder for that source. */
    }
}


/*
 * Scan the whole routing table and timeout a bunch of timers:
 *  - oifs timers
 *  - Join/Prune timer
 *  - routing entry
 *  - Assert timer
 *  - Register-Suppression timer
 *
 *  - If the global timer for checking the unicast routing has expired, perform
 *  also iif/upstream router change verification
 *  - If the global timer for checking the data rate has expired, check the
 *  number of bytes forwarded after the lastest timeout. If bigger than
 *  a given threshold, then switch to the shortest path.
 *  If `number_of_bytes == 0`, then delete the kernel cache entry.
 *
 * Only the entries which have the Join/Prune timer expired are sent.
 * In the special case when we have ~(S,G)RPbit Prune entry, we must
 * include any (*,G) or (*,*,RP) XXX: ???? what and why?
 *
 * Below is a table which summarizes the segmantic rules.
 *
 * On the left side is "if A must be included in the J/P message".
 * On the top is "shall/must include B?"
 * "Y" means "MUST include"
 * "SY" means "SHOULD include"
 * "N" means  "NO NEED to include"
 * (G is a group that matches to RP)
 *
 *              -----------||-----------||-----------
 *            ||  (*,*,RP) ||   (*,G)   ||   (S,G)   ||
 *            ||-----------||-----------||-----------||
 *            ||  J  |  P  ||  J  |  P  ||  J  |  P  ||
 * ==================================================||
 *          J || n/a | n/a ||  N  |  Y  ||  N  |  Y  ||
 * (*,*,RP) -----------------------------------------||
 *          P || n/a | n/a ||  SY |  N  ||  SY |  N  ||
 * ==================================================||
 *          J ||  N  |  N  || n/a | n/a ||  N  |  Y  ||
 *   (*,G)  -----------------------------------------||
 *          P ||  N  |  N  || n/a | n/a ||  SY |  N  ||
 * ==================================================||
 *          J ||  N  |  N  ||  N  |  N  || n/a | n/a ||
 *   (S,G)  -----------------------------------------||
 *          P ||  N  |  N  ||  N  |  N  || n/a | n/a ||
 * ==================================================
 *
 */
void age_routes(void)
{
    cand_rp_t  *cand_rp;
    grpentry_t *grp;
    grpentry_t *grp_next;
    mrtentry_t *mrt_grp;
    mrtentry_t *mrt_rp;
    mrtentry_t *mrt_wide;
    mrtentry_t *mrt_srcs;
    mrtentry_t *mrt_srcs_next;
    rp_grp_entry_t *rp_grp;
    struct uvif *v;
    vifi_t  vifi;
    pim_nbr_entry_t *nbr;
    int change_flag;
    int rp_action, grp_action, src_action = PIM_ACTION_NOTHING, src_action_rp = PIM_ACTION_NOTHING;
    int dont_calc_action;
    rpentry_t *rp;
    int update_rp_iif;
    int update_src_iif;
    vifbitmap_t new_pruned_oifs;
    int assert_timer_expired = 0;

    /*
     * Timing out of the global `unicast_routing_timer`
     * and `data_rate_timer`
     */
    IF_TIMEOUT(unicast_routing_timer) {
	ucast_flag = TRUE;
	SET_TIMER(unicast_routing_timer, unicast_routing_interval);
    }
    ELSE {
	ucast_flag = FALSE;
    }

    IF_TIMEOUT(pim_spt_threshold_timer) {
	rate_flag = TRUE;
	SET_TIMER(pim_spt_threshold_timer, spt_threshold.interval);
    }
    ELSE {
	rate_flag = FALSE;
    }

    /* Scan the (*,*,RP) entries */
    for (cand_rp = cand_rp_list; cand_rp; cand_rp = cand_rp->next) {
	rp = cand_rp->rpentry;

	/* Need to save only `incoming` and `upstream` to discover
	 * unicast route changes. `metric` and `preference` are not
	 * interesting for us.
	 */
	rpentry_save.incoming = rp->incoming;
	rpentry_save.upstream = rp->upstream;

	update_rp_iif = FALSE;
	if ((ucast_flag == TRUE) && (rp->address != my_cand_rp_address)) {
	    /* I am not the RP. If I was the RP, then the iif is
	     * register_vif and no need to reset it. */
	    if (set_incoming(rp, PIM_IIF_RP) != TRUE) {
		/* TODO: XXX: no route to that RP. Panic? There is a high
		 * probability the network is partitioning so immediately
		 * remapping to other RP is not a good idea. Better wait
		 * the Bootstrap mechanism to take care of it and provide
		 * me with correct Cand-RP-Set. */
	    }
	    else {
		if ((rpentry_save.upstream != rp->upstream) ||
		    (rpentry_save.incoming != rp->incoming)) {
		    /* Routing change has occur. Update all (*,G)
		     * and (S,G)RPbit iifs mapping to that RP */
		    update_rp_iif = TRUE;
		}
	    }
	}

	rp_action = PIM_ACTION_NOTHING;
	mrt_rp = cand_rp->rpentry->mrtlink;
	if (mrt_rp) {
	    /* outgoing interfaces timers */
	    change_flag = FALSE;
	    for (vifi = 0; vifi < numvifs; vifi++) {
		if (VIFM_ISSET(vifi, mrt_rp->joined_oifs)) {
		    IF_TIMEOUT(mrt_rp->vif_timers[vifi]) {
			VIFM_CLR(vifi, mrt_rp->joined_oifs);
			change_flag = TRUE;
		    }
		}
	    }
	    if ((change_flag == TRUE) || (update_rp_iif == TRUE)) {
		change_interfaces(mrt_rp,
				  rp->incoming,
				  mrt_rp->joined_oifs,
				  mrt_rp->pruned_oifs,
				  mrt_rp->leaves,
				  mrt_rp->asserted_oifs, 0);
		mrt_rp->upstream = rp->upstream;
	    }

	    /* Check the activity for this entry */
	    check_spt_threshold(mrt_rp);

	    /* Join/Prune timer */
	    IF_TIMEOUT(mrt_rp->jp_timer) {
		rp_action = join_or_prune(mrt_rp, mrt_rp->upstream);

		if (rp_action != PIM_ACTION_NOTHING)
		    add_jp_entry(mrt_rp->upstream,
				 PIM_JOIN_PRUNE_HOLDTIME,
				 htonl(CLASSD_PREFIX),
				 STAR_STAR_RP_MSKLEN,
				 mrt_rp->source->address,
				 SINGLE_SRC_MSKLEN,
				 MRTF_RP | MRTF_WC,
				 rp_action);

		SET_TIMER(mrt_rp->jp_timer, PIM_JOIN_PRUNE_PERIOD);
	    }

	    /* Assert timer */
	    if (mrt_rp->flags & MRTF_ASSERTED) {
		IF_TIMEOUT(mrt_rp->assert_timer) {
		    /* TODO: XXX: reset the upstream router now */
		    mrt_rp->flags &= ~MRTF_ASSERTED;
		}
	    }

	    /* Register-Suppression timer */
	    /* TODO: to reduce the kernel calls, if the timer is running,
	     * install a negative cache entry in the kernel?
	     */
	    /* TODO: can we have Register-Suppression timer for (*,*,RP)?
	     * Currently no...
	     */
	    IF_TIMEOUT(mrt_rp->rs_timer) {}

	    /* routing entry */
	    if ((TIMEOUT(mrt_rp->timer)) && (VIFM_ISEMPTY(mrt_rp->leaves)))
		delete_mrtentry(mrt_rp);
	} /* if (mrt_rp) */

	/* Just in case if that (*,*,RP) was deleted */
	mrt_rp = cand_rp->rpentry->mrtlink;

	/* Check the (*,G) and (S,G) entries */
	for (rp_grp = cand_rp->rp_grp_next; rp_grp; rp_grp = rp_grp->rp_grp_next) {
	    for (grp = rp_grp->grplink; grp; grp = grp_next) {
		grp_next   = grp->rpnext;
		grp_action = PIM_ACTION_NOTHING;
		mrt_grp    = grp->grp_route;
		mrt_srcs   = grp->mrtlink;

		if (mrt_grp) {
		    /* The (*,G) entry */
		    /* outgoing interfaces timers */
		    change_flag = FALSE;
		    assert_timer_expired = 0;
		    if (mrt_grp->flags & MRTF_ASSERTED)
			assert_timer_expired = TIMEOUT(mrt_grp->assert_timer);

		    for (vifi = 0; vifi < numvifs; vifi++) {
			if (VIFM_ISSET(vifi, mrt_grp->joined_oifs)) {
			    IF_TIMEOUT(mrt_grp->vif_timers[vifi]) {
				VIFM_CLR(vifi, mrt_grp->joined_oifs);
				change_flag = TRUE;
			    }
			}

			if (assert_timer_expired) {
			    VIFM_CLR(vifi, mrt_grp->asserted_oifs);
			    change_flag = TRUE;
			    mrt_grp->flags &= ~MRTF_ASSERTED;
			}
		    }

		    if ((change_flag == TRUE) || (update_rp_iif == TRUE)) {
			change_interfaces(mrt_grp,
					  rp->incoming,
					  mrt_grp->joined_oifs,
					  mrt_grp->pruned_oifs,
					  mrt_grp->leaves,
					  mrt_grp->asserted_oifs, 0);
			mrt_grp->upstream = rp->upstream;
		    }

		    /* Check the sources activity */
		    check_spt_threshold(mrt_grp);

		    dont_calc_action = FALSE;
		    if (rp_action != PIM_ACTION_NOTHING) {
			dont_calc_action = TRUE;

			grp_action = join_or_prune(mrt_grp, mrt_grp->upstream);
			if (((rp_action == PIM_ACTION_JOIN)  && (grp_action == PIM_ACTION_PRUNE)) ||
			    ((rp_action == PIM_ACTION_PRUNE) && (grp_action == PIM_ACTION_JOIN)))
			    FIRE_TIMER(mrt_grp->jp_timer);
		    }


		    /* Join/Prune timer */
		    IF_TIMEOUT(mrt_grp->jp_timer) {
			if (dont_calc_action != TRUE)
			    grp_action = join_or_prune(mrt_grp, mrt_grp->upstream);

			if (grp_action != PIM_ACTION_NOTHING)
			    add_jp_entry(mrt_grp->upstream,
					 PIM_JOIN_PRUNE_HOLDTIME,
					 mrt_grp->group->group,
					 SINGLE_GRP_MSKLEN,
					 cand_rp->rpentry->address,
					 SINGLE_SRC_MSKLEN,
					 MRTF_RP | MRTF_WC,
					 grp_action);
			SET_TIMER(mrt_grp->jp_timer, PIM_JOIN_PRUNE_PERIOD);
		    }

		    /* Register-Suppression timer */
		    /* TODO: to reduce the kernel calls, if the timer
		     * is running, install a negative cache entry in
		     * the kernel?
		     */
		    /* TODO: currently cannot have Register-Suppression
		     * timer for (*,G) entry, but keep this around.
		     */
		    IF_TIMEOUT(mrt_grp->rs_timer) {}

		    /* routing entry */
		    if ((TIMEOUT(mrt_grp->timer)) && (VIFM_ISEMPTY(mrt_grp->leaves)))
			delete_mrtentry(mrt_grp);
		} /* if (mrt_grp) */


		/* For all (S,G) for this group */
		/* XXX: mrt_srcs was set before */
		for (; mrt_srcs; mrt_srcs = mrt_srcs_next) {
		    /* routing entry */
		    mrt_srcs_next = mrt_srcs->grpnext;

		    /* outgoing interfaces timers */
		    change_flag = FALSE;
		    assert_timer_expired = 0;
		    if (mrt_srcs->flags & MRTF_ASSERTED)
			assert_timer_expired = TIMEOUT(mrt_srcs->assert_timer);

		    for (vifi = 0; vifi < numvifs; vifi++) {
			if (VIFM_ISSET(vifi, mrt_srcs->joined_oifs)) {
			    /* TODO: checking for reg_num_vif is slow! */
			    if (vifi != reg_vif_num) {
				IF_TIMEOUT(mrt_srcs->vif_timers[vifi]) {
				    VIFM_CLR(vifi, mrt_srcs->joined_oifs);
				    change_flag = TRUE;
				}
			    }
			}

			if (assert_timer_expired) {
			    VIFM_CLR(vifi, mrt_srcs->asserted_oifs);
			    change_flag = TRUE;
			    mrt_srcs->flags &= ~MRTF_ASSERTED;
			}
		    }

		    update_src_iif = FALSE;
		    if (ucast_flag == TRUE) {
			if (!(mrt_srcs->flags & MRTF_RP)) {
			    /* iif toward the source */
			    srcentry_save.incoming = mrt_srcs->source->incoming;
			    srcentry_save.upstream = mrt_srcs->source->upstream;
			    if (set_incoming(mrt_srcs->source, PIM_IIF_SOURCE) != TRUE) {
				/* XXX: not in the spec!
				 * Cannot find route toward that source.
				 * This is bad. Delete the entry.
				 */
				delete_mrtentry(mrt_srcs);
				continue;
			    }

			    /* iif info found */
			    if ((srcentry_save.incoming != mrt_srcs->incoming) ||
				(srcentry_save.upstream != mrt_srcs->upstream)) {
				/* Route change has occur */
				update_src_iif = TRUE;
				mrt_srcs->incoming = mrt_srcs->source->incoming;
				mrt_srcs->upstream = mrt_srcs->source->upstream;
			    }
			} else {
			    /* (S,G)RPBit with iif toward RP */
			    if ((rpentry_save.upstream != mrt_srcs->upstream) ||
				(rpentry_save.incoming != mrt_srcs->incoming)) {
				update_src_iif = TRUE; /* XXX: a hack */
				/* XXX: setup the iif now! */
				mrt_srcs->incoming = rp->incoming;
				mrt_srcs->upstream = rp->upstream;
			    }
			}
		    }

		    if ((change_flag == TRUE) || (update_src_iif == TRUE))
			/* Flush the changes */
			change_interfaces(mrt_srcs,
					  mrt_srcs->incoming,
					  mrt_srcs->joined_oifs,
					  mrt_srcs->pruned_oifs,
					  mrt_srcs->leaves,
					  mrt_srcs->asserted_oifs, 0);

		    check_spt_threshold(mrt_srcs);

		    mrt_wide = mrt_srcs->group->grp_route;
		    if (!mrt_wide)
			mrt_wide = mrt_rp;

		    dont_calc_action = FALSE;
		    if ((rp_action  != PIM_ACTION_NOTHING) ||
			(grp_action != PIM_ACTION_NOTHING)) {
			src_action_rp    = join_or_prune(mrt_srcs, rp->upstream);
			src_action       = src_action_rp;
			dont_calc_action = TRUE;

			if (src_action_rp == PIM_ACTION_JOIN) {
			    if ((grp_action == PIM_ACTION_PRUNE) ||
				(rp_action  == PIM_ACTION_PRUNE))
				FIRE_TIMER(mrt_srcs->jp_timer);
			} else if (src_action_rp == PIM_ACTION_PRUNE) {
			    if ((grp_action == PIM_ACTION_JOIN) ||
				(rp_action  == PIM_ACTION_JOIN))
				FIRE_TIMER(mrt_srcs->jp_timer);
			}
		    }

		    /* Join/Prune timer */
		    IF_TIMEOUT(mrt_srcs->jp_timer) {
			if ((dont_calc_action != TRUE) || (rp->upstream != mrt_srcs->upstream))
			    src_action = join_or_prune(mrt_srcs, mrt_srcs->upstream);

			if (src_action != PIM_ACTION_NOTHING)
			    add_jp_entry(mrt_srcs->upstream,
					 PIM_JOIN_PRUNE_HOLDTIME,
					 mrt_srcs->group->group,
					 SINGLE_GRP_MSKLEN,
					 mrt_srcs->source->address,
					 SINGLE_SRC_MSKLEN,
					 mrt_srcs->flags & MRTF_RP,
					 src_action);

			if (mrt_wide) {
			    /* Have both (S,G) and (*,G) (or (*,*,RP)).
			     * Check if need to send (S,G) PRUNE toward RP */
			    if (mrt_srcs->upstream != mrt_wide->upstream) {
				if (dont_calc_action != TRUE)
				    src_action_rp = join_or_prune(mrt_srcs, mrt_wide->upstream);

				/* XXX: TODO: do error check if
				 * src_action == PIM_ACTION_JOIN, which
				 * should be an error. */
				if (src_action_rp == PIM_ACTION_PRUNE)
				    add_jp_entry(mrt_wide->upstream,
						 PIM_JOIN_PRUNE_HOLDTIME,
						 mrt_srcs->group->group,
						 SINGLE_GRP_MSKLEN,
						 mrt_srcs->source->address,
						 SINGLE_SRC_MSKLEN,
						 MRTF_RP,
						 src_action_rp);
			    }
			}
			SET_TIMER(mrt_srcs->jp_timer, PIM_JOIN_PRUNE_PERIOD);
		    }

		    /* Register-Suppression timer */
		    /* TODO: to reduce the kernel calls, if the timer
		     * is running, install a negative cache entry in
		     * the kernel? */
		    IF_TIMER_SET(mrt_srcs->rs_timer) {
			IF_TIMEOUT(mrt_srcs->rs_timer) {
			    /* Start encapsulating the packets */
			    VIFM_COPY(mrt_srcs->pruned_oifs, new_pruned_oifs);
			    VIFM_CLR(reg_vif_num, new_pruned_oifs);
			    change_interfaces(mrt_srcs,
					      mrt_srcs->incoming,
					      mrt_srcs->joined_oifs,
					      new_pruned_oifs,
					      mrt_srcs->leaves,
					      mrt_srcs->asserted_oifs, 0);
			}
			ELSE {
			    /* The register suppression timer is running. Check
			     * whether it is time to send PIM_NULL_REGISTER.
			     */
			    /* TODO: XXX: TIMER implem. dependency! */
			    if (mrt_srcs->rs_timer <= PIM_REGISTER_PROBE_TIME)
				/* Time to send a PIM_NULL_REGISTER */
				/* XXX: a (bad) hack! This will be sending
				 * periodically NULL_REGISTERS between
				 * PIM_REGISTER_PROBE_TIME and 0. Well,
				 * because PROBE_TIME is 5 secs, it will
				 * happen only once, so it helps to avoid
				 * adding a flag to the routing entry whether
				 * a NULL_REGISTER was sent.
				 */
				send_pim_null_register(mrt_srcs);
			}
		    }

		    /* routing entry */
		    if (TIMEOUT(mrt_srcs->timer)) {
			if (VIFM_ISEMPTY(mrt_srcs->leaves)) {
			    delete_mrtentry(mrt_srcs);
			    continue;
			}
			/* XXX: if DR, Register suppressed,
			 * and leaf oif inherited from (*,G), the
			 * directly connected source is not active anymore,
			 * this (S,G) entry won't timeout. Check if the leaf
			 * oifs are inherited from (*,G); if true. delete the
			 * (S,G) entry.
			 */
			if (mrt_srcs->group->grp_route) {
			    if (!((mrt_srcs->group->grp_route->leaves & mrt_srcs->leaves) ^ mrt_srcs->leaves)) {
				delete_mrtentry(mrt_srcs);
				continue;
			    }
			}
		    }
		} /* End of (S,G) loop */
	    } /* End of (*,G) loop */
	}
    } /* For all cand RPs */

    /* TODO: check again! */
    for (vifi = 0, v = &uvifs[0]; vifi < numvifs; vifi++, v++) {
	/* Send all pending Join/Prune messages */
	for (nbr = v->uv_pim_neighbors; nbr; nbr = nbr->next)
	    pack_and_send_jp_message(nbr);
    }

    IF_DEBUG(DEBUG_PIM_MRT) {
	fputs("\n", stderr);
	dump_pim_mrt(stderr);
    }
}


/*
 * TODO: timeout the RP-group mapping entries during the scan of the
 * whole routing table?
 */
void age_misc(void)
{
    rp_grp_entry_t *rp;
    rp_grp_entry_t *rp_next;
    grp_mask_t     *grp;
    grp_mask_t     *grp_next;

    /* Timeout the Cand-RP-set entries */
    for (grp = grp_mask_list; grp; grp = grp_next) {
	/* If we timeout an entry, the grp entry might be removed */
	grp_next = grp->next;
	for (rp = grp->grp_rp_next; rp; rp = rp_next) {
	    rp_next = rp->grp_rp_next;

	    if (rp->holdtime < 60000) {
		IF_TIMEOUT(rp->holdtime) {
		    if (rp->group!=NULL) {
			logit(LOG_INFO, 0, "Delete RP group entry for group %s (holdtime timeout)",
			      inet_fmt(rp->group->group_addr, s2, sizeof(s2)));
		    }
		    delete_rp_grp_entry(&cand_rp_list, &grp_mask_list, rp);
		}
	    }
	}
    }

    /* Cand-RP-Adv timer */
    if (cand_rp_flag == TRUE) {
	IF_TIMEOUT(pim_cand_rp_adv_timer) {
	    send_pim_cand_rp_adv();
	    SET_TIMER(pim_cand_rp_adv_timer, my_cand_rp_adv_period);
	}
    }

    /* bootstrap-timer */
    IF_TIMEOUT(pim_bootstrap_timer) {
	if (cand_bsr_flag == FALSE) {
	    /* If I am not Cand-BSR, start accepting Bootstrap messages from anyone.
	     * XXX: Even if the BSR has timeout, the existing Cand-RP-Set is kept. */
	    SET_TIMER(pim_bootstrap_timer, PIM_BOOTSTRAP_TIMEOUT);
	    curr_bsr_fragment_tag = 0;
	    curr_bsr_priority     = 0;		  /* Lowest priority */
	    curr_bsr_address      = INADDR_ANY_N; /* Lowest priority */
	    MASKLEN_TO_MASK(RP_DEFAULT_IPV4_HASHMASKLEN, curr_bsr_hash_mask);
	} else {
	    /* I am Cand-BSR, so set the current BSR to me */
	    if (curr_bsr_address == my_bsr_address) {
		SET_TIMER(pim_bootstrap_timer, PIM_BOOTSTRAP_PERIOD);
		send_pim_bootstrap();
	    } else {
		/* Short delay before becoming the BSR and start sending
		 * of the Cand-RP set (to reduce the transient control
		 * overhead). */
		SET_TIMER(pim_bootstrap_timer, bootstrap_initial_delay());
		curr_bsr_fragment_tag = RANDOM();
		curr_bsr_priority     = my_bsr_priority;
		curr_bsr_address      = my_bsr_address;
		curr_bsr_hash_mask    = my_bsr_hash_mask;
	    }
	}
    }

    IF_DEBUG(DEBUG_PIM_BOOTSTRAP | DEBUG_PIM_CAND_RP)
	dump_rp_set(stderr);
    /* TODO: XXX: anything else to timeout */
}

/**
 * Local Variables:
 *  version-control: t
 *  indent-tabs-mode: t
 *  c-file-style: "ellemtel"
 *  c-basic-offset: 4
 * End:
 */
