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
 *  $Id: route.c,v 1.39 2003/02/12 21:56:55 pavlin Exp $
 */

#include "defs.h"

#define MRT_IS_LASTHOP(mrt) PIMD_VIFM_LASTHOP_ROUTER(mrt->leaves, mrt->oifs)
#define MRT_IS_RP(mrt)      mrt->incoming == PIMREG_VIF

/* Marian Stagarescu : 07/31/01:
 *
 * Administrative scoped multicast filtering im PIMD.  This allows an
 * interface to be configured as an administrative boundary for the
 * specified scoped address.  Packets belonging to the scoped address
 * will not be forwarded.
 *
 * Please note that in order to minimize the search for the matching
 * groups the implementation is limited to:
 *
 * Packets are stopped from being forwarded by installing a NULL
 * outgoing interface; the user space (pimd) is not up-call-ed any more
 * for these packets which are dropped by kernel (nil oif) except for
 * when we de-install the route are re-create it (timer 3 minute).  uses
 * the VIF acl that was installed via config scoped statements.
 *
 * this is not an all-purpose packet filtering mechanism.  we tried here
 * to achieve the filtering with minimal processing (inspect (g) when we
 * are about to install a route for it).
 */
/*
 * Contributed by Marian Stagarescu <marian@bile.cidera.com>
 * adapted from mrouted: check for scoped multicast addresses
 * install null oif if matched
 */
#define APPLY_SCOPE(g, mp) {			\
	vifi_t i;				\
	for (i = 0; i < numvifs; i++)		\
	    if (scoped_addr(i, g))              \
		PIMD_VIFM_CLRALL((mp)->oifs);	\
    }

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
 * Forward declarations
 */
static void   process_cache_miss  (struct igmpmsg *igmpctl);
static void   process_wrong_iif   (struct igmpmsg *igmpctl);
static void   process_whole_pkt   (char *buf);
static void   check_spt_threshold (mrtentry_t *mrt);

/*
 * Init some timers
 */
void init_route(void)
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
    srcentry_save.cand_rp    = NULL;

    rpentry_save.prev       = NULL;
    rpentry_save.next       = NULL;
    rpentry_save.address    = INADDR_ANY_N;
    rpentry_save.mrtlink    = NULL;
    rpentry_save.incoming   = NO_VIF;
    rpentry_save.upstream   = NULL;
    rpentry_save.metric     = ~0;
    rpentry_save.preference = ~0;
    rpentry_save.cand_rp    = NULL;
}

/* from mrouted. Contributed by Marian Stagarescu <marian@bile.cidera.com>*/
static int scoped_addr(vifi_t vifi, uint32_t addr)
{
    struct vif_acl *acl;

    for (acl = uvifs[vifi].uv_acl; acl; acl = acl->acl_next) {
	if ((addr & acl->acl_mask) == acl->acl_addr)
	    return 1;
    }

    return 0;
}

/* Return the iif for given address */
vifi_t get_iif(uint32_t address)
{
    struct rpfctl rpfc;

    k_req_incoming(address, &rpfc);
    if (rpfc.iif == NO_VIF || rpfc.rpfneighbor.s_addr == INADDR_ANY_N)
	return NO_VIF;

    return rpfc.iif;
}

/* Return the PIM neighbor toward a source */
/* If route not found or if a local source or if a directly connected source,
 * but is not PIM router, or if the first hop router is not a PIM router,
 * then return NULL.
 */
pim_nbr_entry_t *find_pim_nbr(uint32_t source)
{
    struct rpfctl rpfc;
    pim_nbr_entry_t *nbr;
    uint32_t addr;

    if (local_address(source) != NO_VIF)
	return NULL;

    k_req_incoming(source, &rpfc);
    if (rpfc.iif == NO_VIF || rpfc.rpfneighbor.s_addr == INADDR_ANY_N)
	return NULL;

    /* Figure out the nexthop neighbor by checking the reverse path */
    addr = rpfc.rpfneighbor.s_addr;
    for (nbr = uvifs[rpfc.iif].uv_pim_neighbors; nbr; nbr = nbr->next)
	if (nbr->address == addr)
	    return nbr;

    return NULL;
}


/* TODO: check again the exact setup if the source is local or directly
 * connected!!!
 */
/* TODO: XXX: change the metric and preference for all (S,G) entries per
 * source or RP?
 */
/* TODO - If possible, this would be the place to correct set the
 * source's preference and metric to that obtained from the kernel
 * and/or unicast routing protocol.  For now, set it to the configured
 * default for local pref/metric.
 */

/*
 * Set the iif, upstream router, preference and metric for the route
 * toward the source. Return TRUE is the route was found, othewise FALSE.
 * If type==PIM_IIF_SOURCE and if the source is directly connected
 * then the "upstream" is set to NULL. If srcentry==PIM_IIF_RP, then
 * "upstream" in case of directly connected "source" will be that "source"
 * (if it is also PIM router).,
 */
int set_incoming(srcentry_t *src, int type)
{
    struct rpfctl rpfc;
    uint32_t src_addr = src->address;
    uint32_t nbr_addr;
    struct uvif *vif;
    pim_nbr_entry_t *nbr;

    /* Preference will be 0 if directly connected */
    src->metric = 0;
    src->preference = 0;

    /* The source is a local address */
    src->incoming = local_address(src_addr);
    if (src->incoming != NO_VIF) {
	/* iif of (*,G) at RP has to be register_if */
	if (type == PIM_IIF_RP)
	    src->incoming = PIMREG_VIF;

	/* TODO: set the upstream to myself? */
	src->upstream = NULL;
	return TRUE;
    }

    src->incoming = find_vif_direct(src_addr);
    if (src->incoming != NO_VIF) {
	/* The source is directly connected. Check whether we are
	 * looking for real source or RP */
	if (type == PIM_IIF_SOURCE) {
	    src->upstream = NULL;
	    return TRUE;
	}

	/* PIM_IIF_RP */
	nbr_addr = src_addr;
    } else {
	/* TODO: probably need to check the case if the iif is disabled */
	/* Use the lastest resource: the kernel unicast routing table */
	k_req_incoming(src_addr, &rpfc);
	if (rpfc.iif == NO_VIF || rpfc.rpfneighbor.s_addr == INADDR_ANY_N) {
	    /* couldn't find a route */
	    if (!IN_LINK_LOCAL_RANGE(src_addr)) {
		IF_DEBUG(DEBUG_RPF)
		    logit(LOG_DEBUG, 0, "NO ROUTE found for %s", inet_fmt(src_addr, s1, sizeof(s1)));
	    }
	    return FALSE;
	}

	src->incoming = rpfc.iif;
	nbr_addr      = rpfc.rpfneighbor.s_addr;

	/* set the preference for sources that aren't directly connected. */
	vif = &uvifs[src->incoming];
	src->preference = vif->uv_local_pref;
	src->metric     = vif->uv_local_metric;
    }

    /* The upstream router must be a (PIM router) neighbor, otherwise we
     * are in big trouble ;-) */
    vif = &uvifs[src->incoming];
    for (nbr = vif->uv_pim_neighbors; nbr; nbr = nbr->next) {
	if (ntohl(nbr_addr) < ntohl(nbr->address))
	    continue;

	if (nbr_addr == nbr->address) {
	    /* The upstream router is found in the list of neighbors.
	     * We are safe! */
	    src->upstream = nbr;
	    IF_DEBUG(DEBUG_RPF)
		logit(LOG_DEBUG, 0, "For src %s, iif is %s, next hop router is %s",
		      inet_fmt(src_addr, s1, sizeof(s1)), vif->uv_name,
		      inet_fmt(nbr_addr, s2, sizeof(s2)));

	    return TRUE;
	}

	break;
    }

    /* TODO: control the number of messages! */
    logit(LOG_INFO, 0, "For src %s, iif is %s, next hop router is %s: NOT A PIM ROUTER",
	  inet_fmt(src_addr, s1, sizeof(s1)), vif->uv_name,
	  inet_fmt(nbr_addr, s2, sizeof(s2)));
    src->upstream = NULL;

    return FALSE;
}


/*
 * TODO: XXX: currently `source` is not used. Will be used with IGMPv3 where
 * we have source-specific Join/Prune.
 */
void add_leaf(vifi_t vifi, uint32_t source, uint32_t group)
{
    mrtentry_t *mrt;
    mrtentry_t *srcs;
    uint8_t old_oifs[MAXVIFS];
    uint8_t new_oifs[MAXVIFS];
    uint8_t new_leaves[MAXVIFS];
    uint16_t flags;

    /* Don't create routing entries for the LAN scoped addresses */
    if (ntohl(group) <= INADDR_MAX_LOCAL_GROUP) { /* group <= 224.0.0.255? */
	IF_DEBUG(DEBUG_IGMP)
	    logit(LOG_DEBUG, 0, "Not creating routing entry for LAN scoped group %s",
		  inet_fmt(group, s1, sizeof(s1)));
	return;
    }

    /*
     * XXX: only if I am a DR, the IGMP Join should result in creating
     * a PIM MRT state.
     * XXX: Each router must know if it has local members, i.e., whether
     * it is a last-hop router as well. This info is needed so it will
     * know whether is allowed to initiate a SPT switch by sending
     * a PIM (S,G) Join to the high datarate source.
     * However, if a non-DR last-hop router has not received
     * a PIM Join, it should not create a PIM state, otherwise later
     * this state may incorrectly trigger PIM joins.
     * There is a design flow in pimd, so without making major changes
     * the best we can do is that the non-DR last-hop router will
     * record the local members only after it receives PIM Join from the DR
     * (i.e.  after the second or third IGMP Join by the local member).
     * The downside is that a last-hop router may delay the initiation
     * of the SPT switch. Sigh...
     */
     /* Initialize flags */
    flags = MRTF_RP | MRTF_WC;
    if (IN_PIM_SSM_RANGE(group)) {
	mrt = find_route(source, group, MRTF_SG, CREATE);
	flags = MRTF_SG;
    }
    else if (uvifs[vifi].uv_flags & VIFF_DR)
	mrt = find_route(INADDR_ANY_N, group, MRTF_WC, CREATE);
    else
	mrt = find_route(INADDR_ANY_N, group, MRTF_WC, DONT_CREATE);

    if (!mrt)
	return;

    IF_DEBUG(DEBUG_MRT)
	logit(LOG_DEBUG, 0, "Adding oif %s for group %s", uvifs[vifi].uv_name,
	      inet_fmt(group, s1, sizeof(s1)));

    if (PIMD_VIFM_ISSET(vifi, mrt->leaves))
	return;     /* Already a leaf */

    calc_oifs(mrt, old_oifs);
    PIMD_VIFM_COPY(mrt->leaves, new_leaves);
    PIMD_VIFM_SET(vifi, new_leaves);    /* Add the leaf */
    change_interfaces(mrt,
		      mrt->incoming,
		      mrt->joined_oifs,
		      mrt->pruned_oifs,
		      new_leaves,
		      mrt->asserted_oifs, 0);
    calc_oifs(mrt, new_oifs);

    /* Only if I am the DR for that subnet, eventually initiate a Join */
    if (!(uvifs[vifi].uv_flags & VIFF_DR))
	return;

    if ((mrt->flags & MRTF_NEW) || (PIMD_VIFM_ISEMPTY(old_oifs) && (!PIMD_VIFM_ISEMPTY(new_oifs)))) {
	/* A new created entry or the oifs have changed
	 * from NULL to non-NULL. */
	mrt->flags &= ~MRTF_NEW;
	if (mrt->upstream) {
	    send_pim_join(mrt->upstream, mrt, flags, PIM_JOIN_PRUNE_HOLDTIME);
	    SET_TIMER(mrt->jp_timer, PIM_JOIN_PRUNE_PERIOD);
	}
	else  {
	    FIRE_TIMER(mrt->jp_timer); /* Timeout the Join/Prune timer */
	    logit(LOG_DEBUG, 0, "Upstream router not available.");
	}
    }

    /* Check all (S,G) entries and set the inherited "leaf" flag.
     * TODO: XXX: This won't work for IGMPv3, because there we don't know
     * whether the (S,G) leaf oif was inherited from the (*,G) entry or
     * was created by source specific IGMP join.
     */
    for (srcs = mrt->group->mrtlink; srcs; srcs = srcs->grpnext) {
	PIMD_VIFM_COPY(srcs->leaves, new_leaves);
	PIMD_VIFM_SET(vifi, new_leaves);
	change_interfaces(srcs,
			  srcs->incoming,
			  srcs->joined_oifs,
			  srcs->pruned_oifs,
			  new_leaves,
			  srcs->asserted_oifs, 0);
	/* In the case of SG entry we can create MFC directy without waiting for cache miss. */
	if (flags & MRTF_SG) {
	    add_kernel_cache(srcs, srcs->source->address, srcs->group->group, MFC_MOVE_FORCE);
	    k_chg_mfc(igmp_socket, srcs->source->address, srcs->group->group, 
		      srcs->incoming, srcs->oifs, srcs->source->address);
	}
    }
}


/*
 * TODO: XXX: currently `source` is not used. To be used with IGMPv3 where
 * we have source-specific joins/prunes.
 */
void delete_leaf(vifi_t vifi, uint32_t source, uint32_t group)
{
    mrtentry_t *mrt;
    mrtentry_t *srcs;
    uint8_t new_oifs[MAXVIFS];
    uint8_t old_oifs[MAXVIFS];
    uint8_t new_leaves[MAXVIFS];

    if (IN_PIM_SSM_RANGE(group))
	mrt = find_route(source, group, MRTF_SG, DONT_CREATE);
    else
	mrt = find_route(INADDR_ANY_N, group, MRTF_WC, DONT_CREATE);

    if (!mrt)
	return;

    if (!PIMD_VIFM_ISSET(vifi, mrt->leaves))
	return;      /* This interface wasn't leaf */

    IF_DEBUG(DEBUG_MRT)
	logit(LOG_DEBUG, 0, "Deleting vif %d for group %s", vifi, inet_fmt(group, s1, sizeof(s1)));

    calc_oifs(mrt, old_oifs);

    /* For SSM, source must match */
    if (!IN_PIM_SSM_RANGE(group) || (mrt->source->address==source)) {
	PIMD_VIFM_COPY(mrt->leaves, new_leaves);
	PIMD_VIFM_CLR(vifi, new_leaves);
	change_interfaces(mrt,
			  mrt->incoming,
			  mrt->joined_oifs,
			  mrt->pruned_oifs,
			  new_leaves,
			  mrt->asserted_oifs, 0);
    }
    calc_oifs(mrt, new_oifs);

    if ((!PIMD_VIFM_ISEMPTY(old_oifs)) && PIMD_VIFM_ISEMPTY(new_oifs)) {
	/* The result oifs have changed from non-NULL to NULL */
	FIRE_TIMER(mrt->jp_timer); /* Timeout the Join/Prune timer */

	/* TODO: explicitly call the function below?
	send_pim_join_prune(mrt->upstream->vifi,
			    mrt->upstream,
			    PIM_JOIN_PRUNE_HOLDTIME);
	*/
    }

    /* Check all (S,G) entries and clear the inherited "leaf" flag.
     * TODO: XXX: This won't work for IGMPv3, because there we don't know
     * whether the (S,G) leaf oif was inherited from the (*,G) entry or
     * was created by source specific IGMP join.
     */
    for (srcs = mrt->group->mrtlink; srcs; srcs = srcs->grpnext) {
	PIMD_VIFM_COPY(srcs->leaves, new_leaves);
	PIMD_VIFM_CLR(vifi, new_leaves);
	change_interfaces(srcs,
			  srcs->incoming,
			  srcs->joined_oifs,
			  srcs->pruned_oifs,
			  new_leaves,
			  srcs->asserted_oifs, 0);
    }
}


void calc_oifs(mrtentry_t *mrt, uint8_t *oifs_ptr)
{
    uint8_t oifs[MAXVIFS];
    mrtentry_t *grp;
    mrtentry_t *mrp;

    /*
     * oifs =
     * (((copied_outgoing + my_join) - my_prune) + my_leaves)
     *              - my_asserted_oifs - incoming_interface,
     * i.e. `leaves` have higher priority than `prunes`, but lower priority
     * than `asserted`. The incoming interface is always deleted from the oifs
     */

    if (!mrt) {
	PIMD_VIFM_CLRALL(oifs_ptr);
	return;
    }

    PIMD_VIFM_CLRALL(oifs);
    if (!(mrt->flags & MRTF_PMBR)) {
	/* Either (*,G) or (S,G). Merge with the oifs from the (*,*,RP) */
	mrp = mrt->group->active_rp_grp->rp->rpentry->mrtlink;
	if (mrp) {
	    PIMD_VIFM_MERGE(oifs, mrp->joined_oifs, oifs);
	    PIMD_VIFM_CLR_MASK(oifs, mrp->pruned_oifs);
	    PIMD_VIFM_MERGE(oifs, mrp->leaves, oifs);
	    PIMD_VIFM_CLR_MASK(oifs, mrp->asserted_oifs);
	}
    }
    if (mrt->flags & MRTF_SG) {
	/* (S,G) entry. Merge with the oifs from (*,G) */
	grp = mrt->group->grp_route;
	if (grp) {
	    PIMD_VIFM_MERGE(oifs, grp->joined_oifs, oifs);
	    PIMD_VIFM_CLR_MASK(oifs, grp->pruned_oifs);
	    PIMD_VIFM_MERGE(oifs, grp->leaves, oifs);
	    PIMD_VIFM_CLR_MASK(oifs, grp->asserted_oifs);
	}
    }

    /* Calculate my own stuff */
    PIMD_VIFM_MERGE(oifs, mrt->joined_oifs, oifs);
    PIMD_VIFM_CLR_MASK(oifs, mrt->pruned_oifs);
    PIMD_VIFM_MERGE(oifs, mrt->leaves, oifs);
    PIMD_VIFM_CLR_MASK(oifs, mrt->asserted_oifs);

    PIMD_VIFM_COPY(oifs, oifs_ptr);
}

/*
 * Set the iif, join/prune/leaves/asserted interfaces. Calculate and
 * set the oifs.
 * Return 1 if oifs change from NULL to not-NULL.
 * Return -1 if oifs change from non-NULL to NULL
 *  else return 0
 * If the iif change or if the oifs change from NULL to non-NULL
 * or vice-versa, then schedule that mrtentry join/prune timer to
 * timeout immediately.
 */
int change_interfaces(mrtentry_t *mrt,
		      vifi_t new_iif,
		      uint8_t *new_joined_oifs_,
		      uint8_t *new_pruned_oifs,
		      uint8_t *new_leaves_,
		      uint8_t *new_asserted_oifs,
		      uint16_t flags)
{
    uint8_t new_joined_oifs[MAXVIFS];  /* The oifs for that particular mrtentry */
    uint8_t old_joined_oifs[MAXVIFS] __attribute__ ((unused));
    uint8_t old_pruned_oifs[MAXVIFS] __attribute__ ((unused));
    uint8_t old_leaves[MAXVIFS] __attribute__ ((unused));
    uint8_t new_leaves[MAXVIFS];
    uint8_t old_asserted_oifs[MAXVIFS] __attribute__ ((unused));
    uint8_t new_real_oifs[MAXVIFS];    /* The result oifs */
    uint8_t old_real_oifs[MAXVIFS];
    vifi_t      old_iif;
    rpentry_t   *rp;
    cand_rp_t   *cand_rp;
    kernel_cache_t *kc;
    rp_grp_entry_t *rp_grp;
    grpentry_t     *grp;
    mrtentry_t     *srcs;
    mrtentry_t     *mwc;
    mrtentry_t     *mrp;
    int delete_mrt_flag;
    int result;
    int fire_timer_flag;

    if (!mrt)
	return 0;

    /* When iif changes, discover new upstream pim nbr */
    if (new_iif != mrt->incoming && mrt->source && mrt->source->address)
        mrt->upstream = find_pim_nbr(mrt->source->address);

    PIMD_VIFM_COPY(new_joined_oifs_, new_joined_oifs);
    PIMD_VIFM_COPY(new_leaves_, new_leaves);

    old_iif = mrt->incoming;
    PIMD_VIFM_COPY(mrt->joined_oifs, old_joined_oifs);
    PIMD_VIFM_COPY(mrt->leaves, old_leaves);
    PIMD_VIFM_COPY(mrt->pruned_oifs, old_pruned_oifs);
    PIMD_VIFM_COPY(mrt->asserted_oifs, old_asserted_oifs);

    PIMD_VIFM_COPY(mrt->oifs, old_real_oifs);

    mrt->incoming = new_iif;
    PIMD_VIFM_COPY(new_joined_oifs, mrt->joined_oifs);
    PIMD_VIFM_COPY(new_pruned_oifs, mrt->pruned_oifs);
    PIMD_VIFM_COPY(new_leaves, mrt->leaves);
    PIMD_VIFM_COPY(new_asserted_oifs, mrt->asserted_oifs);
    calc_oifs(mrt, new_real_oifs);

    if (PIMD_VIFM_ISEMPTY(old_real_oifs)) {
	if (PIMD_VIFM_ISEMPTY(new_real_oifs))
	    result = 0;
	else
	    result = 1;
    } else {
	if (PIMD_VIFM_ISEMPTY(new_real_oifs))
	    result = -1;
	else
	    result = 0;
    }

    if ((PIMD_VIFM_SAME(new_real_oifs, old_real_oifs))
	&& (new_iif == old_iif)
	&& !(flags & MFC_UPDATE_FORCE))
	return 0;		/* Nothing to change */

    if ((result != 0) || (new_iif != old_iif) || (flags & MFC_UPDATE_FORCE)) {
	FIRE_TIMER(mrt->jp_timer);
    }
    PIMD_VIFM_COPY(new_real_oifs, mrt->oifs);

    if (mrt->flags & MRTF_PMBR) {
	/* (*,*,RP) entry */
	rp = mrt->source;
	if (!rp)
	    return 0;		/* Shouldn't happen */

	rp->incoming = new_iif;
	cand_rp = rp->cand_rp;

	if (PIMD_VIFM_ISEMPTY(new_real_oifs)) {
	    delete_mrt_flag = TRUE;
	} else {
	    delete_mrt_flag = FALSE;
#ifdef RSRR
	    rsrr_cache_send(mrt, RSRR_NOTIFICATION_OK);
#endif /* RSRR */
	}

	if (mrt->flags & MRTF_KERNEL_CACHE) {
	    /* Update the kernel MFC entries */
	    if (delete_mrt_flag == TRUE) {
		/*
		 * XXX: no need to send RSRR message. Will do it when
		 * deleting the mrtentry.
		 */
		delete_mrtentry_all_kernel_cache(mrt);
	    } else {
		/* here mrt->source->address is the RP address */
		for (kc = mrt->kernel_cache; kc; kc = kc->next)
		    k_chg_mfc(igmp_socket, kc->source,
			      kc->group, new_iif,
			      new_real_oifs, mrt->source->address);
	    }
	}

	/*
	 * Update all (*,G) entries associated with this RP.
	 * The particular (*,G) outgoing are not changed, but the change
	 * in the (*,*,RP) oifs may have affect the real oifs.
	 */
	fire_timer_flag = FALSE;
	for (rp_grp = cand_rp->rp_grp_next; rp_grp; rp_grp = rp_grp->rp_grp_next) {
	    for (grp = rp_grp->grplink; grp; grp = grp->rpnext) {
		if (grp->grp_route) {
		    if (change_interfaces(grp->grp_route, new_iif,
					  grp->grp_route->joined_oifs,
					  grp->grp_route->pruned_oifs,
					  grp->grp_route->leaves,
					  grp->grp_route->asserted_oifs,
					  flags))
			fire_timer_flag = TRUE;
		} else {
		    /* Change all (S,G) entries if no (*,G) */
		    for (srcs = grp->mrtlink; srcs; srcs = srcs->grpnext) {
			if (srcs->flags & MRTF_RP) {
			    if (change_interfaces(srcs, new_iif,
						  srcs->joined_oifs,
						  srcs->pruned_oifs,
						  srcs->leaves,
						  srcs->asserted_oifs,
						  flags))
				fire_timer_flag = TRUE;
			} else {
			    if (change_interfaces(srcs,
						  srcs->incoming,
						  srcs->joined_oifs,
						  srcs->pruned_oifs,
						  srcs->leaves,
						  srcs->asserted_oifs,
						  flags))
				fire_timer_flag = TRUE;
			}
		    }
		}
	    }
	}
	if (fire_timer_flag == TRUE)
	    FIRE_TIMER(mrt->jp_timer);
	if (delete_mrt_flag == TRUE) {
	    /* TODO: XXX: trigger a Prune message? Don't delete now, it will
	     * be automatically timed out. If want to delete now, don't
	     * reference to it anymore!
	    delete_mrtentry(mrt);
	    */
	}

	return result;   /* (*,*,RP) */
    }

    if (mrt->flags & MRTF_WC) {
	/* (*,G) entry */
	if (PIMD_VIFM_ISEMPTY(new_real_oifs)) {
	    delete_mrt_flag = TRUE;
	} else {
	    delete_mrt_flag = FALSE;
#ifdef RSRR
	    rsrr_cache_send(mrt, RSRR_NOTIFICATION_OK);
#endif /* RSRR */
	}

	if (mrt->flags & MRTF_KERNEL_CACHE) {
	    if (delete_mrt_flag == TRUE) {
		delete_mrtentry_all_kernel_cache(mrt);
	    } else {
		for (kc = mrt->kernel_cache; kc; kc = kc->next)
		    k_chg_mfc(igmp_socket, kc->source,
			      kc->group, new_iif,
			      new_real_oifs, mrt->group->rpaddr);
	    }
	}

	/* Update all (S,G) entries for this group.
	 * For the (S,G)RPbit entries the iif is the iif toward the RP;
	 * The particular (S,G) oifs are not changed, but the change in the
	 * (*,G) oifs may affect the real oifs.
	 */
	fire_timer_flag = FALSE;
	for (srcs = mrt->group->mrtlink; srcs; srcs = srcs->grpnext) {
	    if (srcs->flags & MRTF_RP) {
		if (change_interfaces(srcs, new_iif,
				      srcs->joined_oifs,
				      srcs->pruned_oifs,
				      srcs->leaves,
				      srcs->asserted_oifs, flags))
		    fire_timer_flag = TRUE;
	    } else {
		if (change_interfaces(srcs, srcs->incoming,
				      srcs->joined_oifs,
				      srcs->pruned_oifs,
				      srcs->leaves,
				      srcs->asserted_oifs, flags))
		    fire_timer_flag = TRUE;
	    }
	}

	if (fire_timer_flag == TRUE)
	    FIRE_TIMER(mrt->jp_timer);

	if (delete_mrt_flag == TRUE) {
	    /* TODO: XXX: the oifs are NULL. Send a Prune message? */
	}

	return result;		/* (*,G) */
    }

    /* (S,G) entry */
    if (mrt->flags & MRTF_SG) {
	mrp = mrt->group->active_rp_grp->rp->rpentry->mrtlink;
	mwc = mrt->group->grp_route;

#ifdef KERNEL_MFC_WC_G
	mrtentry_t *tmp;

	/* Check whether (*,*,RP) or (*,G) have different (iif,oifs) from
	 * the (S,G). If "yes", then forbid creating (*,G) MFC. */
	for (tmp = mrp; 1; tmp = mwc) {
	    while (1) {
		uint8_t oifs[MAXVIFS];

		if (!tmp)
		    break;

		if (tmp->flags & MRTF_MFC_CLONE_SG)
		    break;

		if (tmp->incoming != mrt->incoming) {
		    delete_single_kernel_cache_addr(tmp, INADDR_ANY_N, mrt->group->group);
		    tmp->flags |= MRTF_MFC_CLONE_SG;
		    break;
		}

		calc_oifs(tmp, oifs);
		if (!(PIMD_VIFM_SAME(new_real_oifs, oifs)))
		    tmp->flags |= MRTF_MFC_CLONE_SG;

		break;
	    }

	    if (tmp == mwc)
		break;
	}
#endif /* KERNEL_MFC_WC_G */

	if (PIMD_VIFM_ISEMPTY(new_real_oifs)) {
	    delete_mrt_flag = TRUE;
	} else {
	    delete_mrt_flag = FALSE;
#ifdef RSRR
	    rsrr_cache_send(mrt, RSRR_NOTIFICATION_OK);
#endif
	}

	if (mrt->flags & MRTF_KERNEL_CACHE) {
	    if (delete_mrt_flag == TRUE)
		delete_mrtentry_all_kernel_cache(mrt);
	    else
		k_chg_mfc(igmp_socket, mrt->source->address,
			  mrt->group->group, new_iif, new_real_oifs,
			  mrt->group->rpaddr);
	}

	if (old_iif != new_iif) {
	    if (mrt->source && new_iif == mrt->source->incoming) {
		/* For example, if this was (S,G)RPbit with iif toward the RP,
		 * and now switch to the Shortest Path.
		 * The setup of MRTF_SPT flag must be
		 * done by the external calling function (triggered only
		 * by receiving of a data from the source.)
		 */
		mrt->flags &= ~MRTF_RP;
		/* TODO: XXX: delete? Check again where will be the best
		 * place to set it.
		mrt->flags |= MRTF_SPT;
		*/
	    }

	    if ((mwc && mwc->incoming == new_iif) ||
		(mrp && mrp->incoming == new_iif)) {
		/* If the new iif points toward the RP, reset the SPT flag.
		 * (PIM-SM-spec-10.ps pp. 11, 2.10, last sentence of first
		 * paragraph. */

		/* TODO: XXX: check again! */
		mrt->flags &= ~MRTF_SPT;
		mrt->flags |= MRTF_RP;
	    }
	}

	/* TODO: XXX: if this is (S,G)RPbit entry and the oifs==(*,G)oifs,
	 * then delete the (S,G) entry?? The same if we have (*,*,RP) ? */
	if (delete_mrt_flag == TRUE) {
	    /* TODO: XXX: the oifs are NULL. Send a Prune message ? */
	}

	/* TODO: XXX: have the feeling something is missing.... */
	return result;		/* (S,G) */
    }

    return result;
}


/* TODO: implement it. Required to allow changing of the physical interfaces
 * configuration without need to restart pimd.
 */
int delete_vif_from_mrt(vifi_t vifi __attribute__((unused)))
{
    return TRUE;
}


void process_kernel_call(void)
{
    struct igmpmsg *igmpctl = (struct igmpmsg *)igmp_recv_buf;

    switch (igmpctl->im_msgtype) {
	case IGMPMSG_NOCACHE:
	    process_cache_miss(igmpctl);
	    break;

	case IGMPMSG_WRONGVIF:
	    process_wrong_iif(igmpctl);
	    break;

	case IGMPMSG_WHOLEPKT:
	    process_whole_pkt(igmp_recv_buf);
	    break;

	default:
	    IF_DEBUG(DEBUG_KERN)
		logit(LOG_DEBUG, 0, "Unknown IGMP message type from kernel: %d", igmpctl->im_msgtype);
	    break;
    }
}


/*
 * TODO: when cache miss, check the iif, because probably ASSERTS
 * shoult take place
 */
static void process_cache_miss(struct igmpmsg *igmpctl)
{
    uint32_t source, mfc_source;
    uint32_t group;
    uint32_t rp_addr;
    vifi_t iif;
    mrtentry_t *mrt;
    mrtentry_t *mrp;

    /* When there is a cache miss, we check only the header of the packet
     * (and only it should be sent up by the kernel. */

    group  = igmpctl->im_dst.s_addr;
    source = mfc_source = igmpctl->im_src.s_addr;
    iif    = igmpctl->im_vif;

    IF_DEBUG(DEBUG_MRT)
	logit(LOG_DEBUG, 0, "Cache miss, src %s, dst %s, iif %s",
	      inet_fmt(source, s1, sizeof(s1)), inet_fmt(group, s2, sizeof(s2)), uvifs[iif].uv_name);

    /* TODO: XXX: check whether the kernel generates cache miss for the LAN scoped addresses */
    if (ntohl(group) <= INADDR_MAX_LOCAL_GROUP)
	return; /* Don't create routing entries for the LAN scoped addresses */

    /* TODO: check if correct in case the source is one of my addresses */
    /* If I am the DR for this source, create (S,G) and add the register_vif
     * to the oifs. */

    if ((uvifs[iif].uv_flags & VIFF_DR) && (find_vif_direct_local(source) == iif)) {
	mrt = find_route(source, group, MRTF_SG, CREATE);
	if (!mrt)
	    return;

	mrt->flags &= ~MRTF_NEW;
	/* set PIMREG_VIF as outgoing interface ONLY if I am not the RP */
	if (mrt->group->rpaddr != my_cand_rp_address)
	    PIMD_VIFM_SET(PIMREG_VIF, mrt->joined_oifs);
	change_interfaces(mrt,
			  mrt->incoming,
			  mrt->joined_oifs,
			  mrt->pruned_oifs,
			  mrt->leaves,
			  mrt->asserted_oifs, 0);
    } else {
	mrt = find_route(source, group, MRTF_SG | MRTF_WC | MRTF_PMBR, DONT_CREATE);
	if (!mrt)
	    return;

	if (IN_PIM_SSM_RANGE(group))
	    switch_shortest_path(source, group);
	else
	    check_spt_threshold(mrt);
    }

    /* TODO: if there are too many cache miss for the same (S,G),
     * install negative cache entry in the kernel (oif==NULL) to prevent
     * too many upcalls. */

    if (mrt->incoming == iif) {
	if (!PIMD_VIFM_ISEMPTY(mrt->oifs)) {
	    if (mrt->flags & MRTF_SG) {
		/* TODO: check that the RPbit is not set? */
		/* TODO: XXX: TIMER implem. dependency! */
		if (mrt->timer < PIM_DATA_TIMEOUT)
		    SET_TIMER(mrt->timer, PIM_DATA_TIMEOUT);

		if (!(mrt->flags & MRTF_SPT)) {
		    mrp = mrt->group->grp_route;
		    if (!mrp)
			mrp = mrt->group->active_rp_grp->rp->rpentry->mrtlink;

		    if (mrp) {
			/* Check if the (S,G) iif is different from
			 * the (*,G) or (*,*,RP) iif */
			if ((mrt->incoming != mrp->incoming) ||
			    (mrt->upstream != mrp->upstream)) {
			    mrt->flags |= MRTF_SPT;
			    mrt->flags &= ~MRTF_RP;
			}
		    }
		}
	    }

	    if (mrt->flags & MRTF_PMBR)
		rp_addr = mrt->source->address;
	    else
		rp_addr = mrt->group->rpaddr;

	    mfc_source = source;
#ifdef KERNEL_MFC_WC_G
	    if (mrt->flags & (MRTF_WC | MRTF_PMBR))
		if (!(mrt->flags & MRTF_MFC_CLONE_SG))
		    mfc_source = INADDR_ANY_N;
#endif /* KERNEL_MFC_WC_G */

	    add_kernel_cache(mrt, mfc_source, group, MFC_MOVE_FORCE);

	    APPLY_SCOPE(group, mrt);
	    k_chg_mfc(igmp_socket, mfc_source, group, iif, mrt->oifs, rp_addr);

	    /* No need for RSRR message, because nothing has changed. */
	}

	return;			/* iif match */
    }

    /* The iif doesn't match */
    if (mrt->flags & MRTF_SG) {
	/* Arrived on wrong interface */
	if (mrt->flags & MRTF_SPT)
	    return;

	mrp = mrt->group->grp_route;
	if (!mrp)
	    mrp = mrt->group->active_rp_grp->rp->rpentry->mrtlink;

	if (mrp) {
	    /* Forward on (*,G) or (*,*,RP) */
	    if (mrp->incoming == iif) {
#ifdef KERNEL_MFC_WC_G
		if (!(mrp->flags & MRTF_MFC_CLONE_SG))
		    mfc_source = INADDR_ANY_N;
#endif /* KERNEL_MFC_WC_G */

		add_kernel_cache(mrp, mfc_source, group, 0);

		/* marian: not sure if we reach here with our scoped traffic? */
		APPLY_SCOPE(group, mrt);
		k_chg_mfc(igmp_socket, mfc_source, group, iif, mrp->oifs, mrt->group->rpaddr);
#ifdef RSRR
		rsrr_cache_send(mrp, RSRR_NOTIFICATION_OK);
#endif /* RSRR */
	    }
	}
    }
}


/*
 * A multicast packet has been received on wrong iif by the kernel.
 * Check for a matching entry. If there is (S,G) with reset SPTbit and
 * the packet was received on the iif toward the source, this completes
 * the switch to the shortest path and triggers (S,G) prune toward the RP
 * (unless I am the RP).
 * Otherwise, if the packet's iif is in the oiflist of the routing entry,
 * trigger an Assert.
 */
static void process_wrong_iif(struct igmpmsg *igmpctl)
{
    uint32_t source;
    uint32_t group;
    vifi_t  iif;
    mrtentry_t *mrt;

    group  = igmpctl->im_dst.s_addr;
    source = igmpctl->im_src.s_addr;
    iif    = igmpctl->im_vif;

    IF_DEBUG(DEBUG_MRT)
	logit(LOG_DEBUG, 0, "Wrong iif: src %s, dst %s, iif %s",
	      inet_fmt(source, s1, sizeof(s1)), inet_fmt(group, s2, sizeof(s2)), uvifs[iif].uv_name);

    /* Don't create routing entries for the LAN scoped addresses */
    if (ntohl(group) <= INADDR_MAX_LOCAL_GROUP)
	return;

    /* Ignore if it comes on register vif. register vif is neither SPT iif,
     * neither is used to send asserts out.
     */
    if (uvifs[iif].uv_flags & VIFF_REGISTER)
	return;

    mrt = find_route(source, group, MRTF_SG | MRTF_WC | MRTF_PMBR, DONT_CREATE);
    if (!mrt)
	return;

    /*
     * TODO: check again!
     */
    if (mrt->flags & MRTF_SG) {
	if (!(mrt->flags & MRTF_SPT)) {
	    if (mrt->source->incoming == iif) {
		/* Switch to the Shortest Path */
		mrt->flags |= MRTF_SPT;
		mrt->flags &= ~MRTF_RP;
		add_kernel_cache(mrt, source, group, MFC_MOVE_FORCE);
		k_chg_mfc(igmp_socket, source, group, iif,
			  mrt->oifs, mrt->group->rpaddr);
		FIRE_TIMER(mrt->jp_timer);
#ifdef RSRR
		rsrr_cache_send(mrt, RSRR_NOTIFICATION_OK);
#endif /* RSRR */

		return;
	    }
	}
    }

    /* Trigger an Assert */
    if (PIMD_VIFM_ISSET(iif, mrt->oifs))
	send_pim_assert(source, group, iif, mrt);
}

/*
 * Receives whole packets from the register vif entries
 * in the kernel, and calls the send_pim_register procedure to
 * encapsulate the packets and unicasts them to the RP.
 */
static void process_whole_pkt(char *buf)
{
    send_pim_register((char *)(buf + sizeof(struct igmpmsg)));
}

mrtentry_t *switch_shortest_path(uint32_t source, uint32_t group)
{
    mrtentry_t *mrt;

    IF_DEBUG(DEBUG_MRT)
	logit(LOG_DEBUG, 0, "Switch shortest path (SPT): src %s, group %s",
	      inet_fmt(source, s1, sizeof(s1)), inet_fmt(group, s2, sizeof(s2)));

    /* TODO: XXX: prepare and send immediately the (S,G) join? */
    mrt = find_route(source, group, MRTF_SG, CREATE);
    if (mrt) {
	if (mrt->flags & MRTF_NEW) {
	    mrt->flags &= ~MRTF_NEW;
	} else if (mrt->flags & MRTF_RP || IN_PIM_SSM_RANGE(group)) {
	    /* (S,G)RPbit with iif toward RP. Reset to (S,G) with iif
	     * toward S. Delete the kernel cache (if any), because
	     * change_interfaces() will reset it with iif toward S
	     * and no data will arrive from RP before the switch
	     * really occurs.
             * For SSM, (S,G)RPbit entry does not exist but switch to
             * SPT must be allowed right away.
	     */
	    mrt->flags &= ~MRTF_RP;
	    mrt->incoming = mrt->source->incoming;
	    mrt->upstream = mrt->source->upstream;
	    delete_mrtentry_all_kernel_cache(mrt);
	    change_interfaces(mrt,
			      mrt->incoming,
			      mrt->joined_oifs,
			      mrt->pruned_oifs,
			      mrt->leaves,
			      mrt->asserted_oifs, 0);
	}

	SET_TIMER(mrt->timer, PIM_DATA_TIMEOUT);
	FIRE_TIMER(mrt->jp_timer);
    }

    return mrt;
}

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
    kernel_cache_t *kc, *kc_next;

    /* XXX: TODO: When we add group-list support to spt-threshold we need
     * to move this infinity check to inside the for-loop ... obviously. */
    if (spt_threshold.mode == SPT_INF)
	return;

    for (kc = mrt->kernel_cache; kc; kc = kc_next) {
	uint32_t prev_bytecnt, prev_pktcnt;
	int rc;

	kc_next = kc->next;

	prev_bytecnt = kc->sg_count.bytecnt;
	prev_pktcnt  = kc->sg_count.pktcnt;

	rc = k_get_sg_cnt(udp_socket, kc->source, kc->group, &kc->sg_count);
	if (rc || prev_bytecnt == kc->sg_count.bytecnt) {
	    /*
	     * Either (for whatever reason) there is no such routing
	     * entry, or that particular (S,G) was idle.
	     *
	     * Note: This code path used to delete the routing entry
	     *       from the kernel.  This caused issues on unicast
	     *       routing changes, GitHub issue #79.  Now we let
	     *       it linger and be removed on IGMPMSG_WRONGVIF.
	     */
	    continue;
	}

	IF_DEBUG(DEBUG_MRT)
	    logit(LOG_DEBUG, 0, "Checking SPT threshold for (%s,%s) pkt cnt now %d vs %d",
		  inet_fmt(kc->source, s1, sizeof(s1)), inet_fmt(kc->group, s2, sizeof(s2)),
		  kc->sg_count.pktcnt, prev_pktcnt);

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
    int update_src_iif;
    uint8_t new_pruned_oifs[MAXVIFS];
    int assert_timer_expired = 0;
    uint8_t rate_flag;

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
	int update_rp_iif;

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
		if (PIMD_VIFM_ISSET(vifi, mrt_rp->joined_oifs)) {
		    IF_TIMEOUT(mrt_rp->vif_timers[vifi]) {
			PIMD_VIFM_CLR(vifi, mrt_rp->joined_oifs);
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
	    if (rate_flag == TRUE)
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
	    if ((TIMEOUT(mrt_rp->timer)) && (PIMD_VIFM_ISEMPTY(mrt_rp->leaves)))
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
			if (PIMD_VIFM_ISSET(vifi, mrt_grp->joined_oifs)) {
			    IF_TIMEOUT(mrt_grp->vif_timers[vifi]) {
				PIMD_VIFM_CLR(vifi, mrt_grp->joined_oifs);
				change_flag = TRUE;
			    }
			}

			if (assert_timer_expired) {
			    PIMD_VIFM_CLR(vifi, mrt_grp->asserted_oifs);
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
		    if (rate_flag == TRUE)
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
		    if ((TIMEOUT(mrt_grp->timer)) && (PIMD_VIFM_ISEMPTY(mrt_grp->leaves)))
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
			if (PIMD_VIFM_ISSET(vifi, mrt_srcs->joined_oifs)) {
			    /* TODO: checking for reg_num_vif is slow! */
			    if (vifi != PIMREG_VIF) {
				IF_TIMEOUT(mrt_srcs->vif_timers[vifi]) {
				    PIMD_VIFM_CLR(vifi, mrt_srcs->joined_oifs);
				    change_flag = TRUE;
				}
			    }
			}

			if (assert_timer_expired) {
			    PIMD_VIFM_CLR(vifi, mrt_srcs->asserted_oifs);
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
			    if ((srcentry_save.incoming != mrt_srcs->source->incoming) ||
				(srcentry_save.upstream != mrt_srcs->source->upstream)) {
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
					  mrt_srcs->asserted_oifs, MFC_UPDATE_FORCE);

		    if (rate_flag == TRUE)
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
			    PIMD_VIFM_COPY(mrt_srcs->pruned_oifs, new_pruned_oifs);
			    PIMD_VIFM_CLR(PIMREG_VIF, new_pruned_oifs);
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
			if (PIMD_VIFM_ISEMPTY(mrt_srcs->leaves)) {
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
			    if (PIMD_VIFM_LASTHOP_ROUTER(mrt_srcs->group->grp_route->leaves, mrt_srcs->leaves)) {
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
}

/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "cc-mode"
 * End:
 */
