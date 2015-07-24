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


/* Marian Stagarescu : 07/31/01:
 *
 * Administrative scoped multicast filtering im PIMD.  This allows an
 * interface to be configured as an administrative boundary for the
 * specified scoped address.  Packets belonging to the scoped address will
 * not be forwarded.
 *
 * Please note the in order to minimize the search for the matching groups
 * the implementation is limited to:
 *
 * Packets are stopped from being forwarded by installing a NULL outgoing
 * interface; the user space (pimd) is not up-call-ed any more for
 * these packets which are dropped by kernel (nil oif) except for
 * when we de-install the route are re-create it (timer 3 minute).
 * uses the VIF acl that was installed via config scoped statements.
 *
 * this is not an all-purpose packet filtering mechanism.
 * we tried here to achieve the filtering with minimal processing
 * (inspect (g) when we are about to install a route for it).
 *
 * to use it edit pimd.conf and compile with -DSCOPED_ACL
 */

static void   process_cache_miss  (struct igmpmsg *igmpctl);
static void   process_wrong_iif   (struct igmpmsg *igmpctl);
static void   process_whole_pkt   (char *buf);

#ifdef SCOPED_ACL
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

/* Contributed by Marian Stagarescu <marian@bile.cidera.com>
 * adapted from mrouted: check for scoped multicast addresses
 * install null oif if matched
 */
#define APPLY_SCOPE(g, mp) {			\
	vifi_t i;				\
	for (i = 0; i < numvifs; i++)		\
	    if (scoped_addr(i, g))              \
		(mp)->oifs = 0;			\
    }
#endif  /* SCOPED_ACL */

/* Return the iif for given address */
vifi_t get_iif(uint32_t address)
{
    struct rpfctl rpfc;

    k_req_incoming(address, &rpfc);
    if (rpfc.rpfneighbor.s_addr == INADDR_ANY_N)
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

    if ((rpfc.rpfneighbor.s_addr == INADDR_ANY_N) || (rpfc.iif == NO_VIF))
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
	    src->incoming = reg_vif_num;

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
	if ((rpfc.iif == NO_VIF) || rpfc.rpfneighbor.s_addr == INADDR_ANY_N) {
	    /* couldn't find a route */
	    if (!IN_LINK_LOCAL_RANGE(src_addr)) {
		IF_DEBUG(DEBUG_PIM_MRT | DEBUG_RPF)
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
		logit(LOG_DEBUG, 0, "For src %s, iif is %d, next hop router is %s",
		      inet_fmt(src_addr, s1, sizeof(s1)), src->incoming,
		      inet_fmt(nbr_addr, s2, sizeof(s2)));

	    return TRUE;
	}

	break;
    }

    /* TODO: control the number of messages! */
    logit(LOG_INFO, 0, "For src %s, iif is %d, next hop router is %s: NOT A PIM ROUTER",
	  inet_fmt(src_addr, s1, sizeof(s1)), src->incoming,
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
    vifbitmap_t old_oifs;
    vifbitmap_t new_oifs;
    vifbitmap_t new_leaves;

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
    if (IN_PIM_SSM_RANGE(group))
	mrt = find_route(source, group, MRTF_SG, CREATE);
    else if (uvifs[vifi].uv_flags & VIFF_DR)
	mrt = find_route(INADDR_ANY_N, group, MRTF_WC, CREATE);
    else
	mrt = find_route(INADDR_ANY_N, group, MRTF_WC, DONT_CREATE);

    if (!mrt)
	return;

    IF_DEBUG(DEBUG_MRT)
	logit(LOG_DEBUG, 0, "Adding vif %d for group %s", vifi, inet_fmt(group, s1, sizeof(s1)));

    if (VIFM_ISSET(vifi, mrt->leaves))
	return;     /* Already a leaf */

    calc_oifs(mrt, &old_oifs);
    VIFM_COPY(mrt->leaves, new_leaves);
    VIFM_SET(vifi, new_leaves);    /* Add the leaf */
    change_interfaces(mrt,
		      mrt->incoming,
		      mrt->joined_oifs,
		      mrt->pruned_oifs,
		      new_leaves,
		      mrt->asserted_oifs, 0);
    calc_oifs(mrt, &new_oifs);

    /* Only if I am the DR for that subnet, eventually initiate a Join */
    if (!(uvifs[vifi].uv_flags & VIFF_DR))
	return;

    if ((mrt->flags & MRTF_NEW) || (VIFM_ISEMPTY(old_oifs) && (!VIFM_ISEMPTY(new_oifs)))) {
	/* A new created entry or the oifs have changed
	 * from NULL to non-NULL. */
	mrt->flags &= ~MRTF_NEW;
	FIRE_TIMER(mrt->jp_timer); /* Timeout the Join/Prune timer */

	/* TODO: explicitly call the function below?
	send_pim_join_prune(mrt->upstream->vifi,
			    mrt->upstream,
			    PIM_JOIN_PRUNE_HOLDTIME);
	*/
    }

    /* Check all (S,G) entries and set the inherited "leaf" flag.
     * TODO: XXX: This won't work for IGMPv3, because there we don't know
     * whether the (S,G) leaf oif was inherited from the (*,G) entry or
     * was created by source specific IGMP join.
     */
    for (srcs = mrt->group->mrtlink; srcs; srcs = srcs->grpnext) {
	VIFM_COPY(srcs->leaves, new_leaves);
	VIFM_SET(vifi, new_leaves);
	change_interfaces(srcs,
			  srcs->incoming,
			  srcs->joined_oifs,
			  srcs->pruned_oifs,
			  new_leaves,
			  srcs->asserted_oifs, 0);
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
    vifbitmap_t new_oifs;
    vifbitmap_t old_oifs;
    vifbitmap_t new_leaves;

    if (IN_PIM_SSM_RANGE(group))
	mrt = find_route(source, group, MRTF_SG, DONT_CREATE);
    else
	mrt = find_route(INADDR_ANY_N, group, MRTF_WC, DONT_CREATE);

    if (!mrt)
	return;

    if (!VIFM_ISSET(vifi, mrt->leaves))
	return;      /* This interface wasn't leaf */

    IF_DEBUG(DEBUG_MRT)
	logit(LOG_DEBUG, 0, "Deleting vif %d for group %s", vifi, inet_fmt(group, s1, sizeof(s1)));

    calc_oifs(mrt, &old_oifs);

    /* For SSM, source must match */
    if (!IN_PIM_SSM_RANGE(group) || (mrt->source->address==source)) {
	VIFM_COPY(mrt->leaves, new_leaves);
	VIFM_CLR(vifi, new_leaves);
	change_interfaces(mrt,
			  mrt->incoming,
			  mrt->joined_oifs,
			  mrt->pruned_oifs,
			  new_leaves,
			  mrt->asserted_oifs, 0);
    }
    calc_oifs(mrt, &new_oifs);

    if ((!VIFM_ISEMPTY(old_oifs)) && VIFM_ISEMPTY(new_oifs)) {
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
	VIFM_COPY(srcs->leaves, new_leaves);
	VIFM_CLR(vifi, new_leaves);
	change_interfaces(srcs,
			  srcs->incoming,
			  srcs->joined_oifs,
			  srcs->pruned_oifs,
			  new_leaves,
			  srcs->asserted_oifs, 0);
    }
}


void calc_oifs(mrtentry_t *mrt, vifbitmap_t *oifs_ptr)
{
    vifbitmap_t oifs;
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
	VIFM_CLRALL(*oifs_ptr);
	return;
    }

    VIFM_CLRALL(oifs);
    if (!(mrt->flags & MRTF_PMBR)) {
	/* Either (*,G) or (S,G). Merge with the oifs from the (*,*,RP) */
	mrp = mrt->group->active_rp_grp->rp->rpentry->mrtlink;
	if (mrp) {
	    VIFM_MERGE(oifs, mrp->joined_oifs, oifs);
	    VIFM_CLR_MASK(oifs, mrp->pruned_oifs);
	    VIFM_MERGE(oifs, mrp->leaves, oifs);
	    VIFM_CLR_MASK(oifs, mrp->asserted_oifs);
	}
    }
    if (mrt->flags & MRTF_SG) {
	/* (S,G) entry. Merge with the oifs from (*,G) */
	grp = mrt->group->grp_route;
	if (grp) {
	    VIFM_MERGE(oifs, grp->joined_oifs, oifs);
	    VIFM_CLR_MASK(oifs, grp->pruned_oifs);
	    VIFM_MERGE(oifs, grp->leaves, oifs);
	    VIFM_CLR_MASK(oifs, grp->asserted_oifs);
	}
    }

    /* Calculate my own stuff */
    VIFM_MERGE(oifs, mrt->joined_oifs, oifs);
    VIFM_CLR_MASK(oifs, mrt->pruned_oifs);
    VIFM_MERGE(oifs, mrt->leaves, oifs);
    VIFM_CLR_MASK(oifs, mrt->asserted_oifs);

    VIFM_COPY(oifs, *oifs_ptr);
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
		      vifbitmap_t new_joined_oifs_,
		      vifbitmap_t new_pruned_oifs,
		      vifbitmap_t new_leaves_,
		      vifbitmap_t new_asserted_oifs,
		      uint16_t flags)
{
    vifbitmap_t new_joined_oifs;  /* The oifs for that particular mrtentry */
    vifbitmap_t old_joined_oifs __attribute__ ((unused));
    vifbitmap_t old_pruned_oifs __attribute__ ((unused));
    vifbitmap_t old_leaves __attribute__ ((unused));
    vifbitmap_t new_leaves;
    vifbitmap_t old_asserted_oifs __attribute__ ((unused));
    vifbitmap_t new_real_oifs;    /* The result oifs */
    vifbitmap_t old_real_oifs;
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

    VIFM_COPY(new_joined_oifs_, new_joined_oifs);
    VIFM_COPY(new_leaves_, new_leaves);

    old_iif = mrt->incoming;
    VIFM_COPY(mrt->joined_oifs, old_joined_oifs);
    VIFM_COPY(mrt->leaves, old_leaves);
    VIFM_COPY(mrt->pruned_oifs, old_pruned_oifs);
    VIFM_COPY(mrt->asserted_oifs, old_asserted_oifs);

    VIFM_COPY(mrt->oifs, old_real_oifs);

    mrt->incoming = new_iif;
    VIFM_COPY(new_joined_oifs, mrt->joined_oifs);
    VIFM_COPY(new_pruned_oifs, mrt->pruned_oifs);
    VIFM_COPY(new_leaves, mrt->leaves);
    VIFM_COPY(new_asserted_oifs, mrt->asserted_oifs);
    calc_oifs(mrt, &new_real_oifs);

    if (VIFM_ISEMPTY(old_real_oifs)) {
	if (VIFM_ISEMPTY(new_real_oifs))
	    result = 0;
	else
	    result = 1;
    } else {
	if (VIFM_ISEMPTY(new_real_oifs))
	    result = -1;
	else
	    result = 0;
    }

    if ((VIFM_SAME(new_real_oifs, old_real_oifs))
	&& (new_iif == old_iif)
	&& !(flags & MFC_UPDATE_FORCE))
	return 0;		/* Nothing to change */

    if ((result != 0) || (new_iif != old_iif) || (flags & MFC_UPDATE_FORCE)) {
	FIRE_TIMER(mrt->jp_timer);
    }
    VIFM_COPY(new_real_oifs, mrt->oifs);

    if (mrt->flags & MRTF_PMBR) {
	/* (*,*,RP) entry */
	rp = mrt->source;
	if (!rp)
	    return 0;		/* Shouldn't happen */

	rp->incoming = new_iif;
	cand_rp = rp->cand_rp;

	if (VIFM_ISEMPTY(new_real_oifs)) {
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
		/* XXX: no need to send RSRR message. Will do it when
		 * delete the mrtentry.
		 */
		for (kc = mrt->kernel_cache; kc; kc = kc->next)
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
	if (VIFM_ISEMPTY(new_real_oifs)) {
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
		vifbitmap_t oifs;

		if (!tmp)
		    break;

		if (tmp->flags & MRTF_MFC_CLONE_SG)
		    break;

		if (tmp->incoming != mrt->incoming) {
		    delete_single_kernel_cache_addr(tmp, INADDR_ANY_N, mrt->group->group);
		    tmp->flags |= MRTF_MFC_CLONE_SG;
		    break;
		}

		calc_oifs(tmp, &oifs);
		if (!(VIFM_SAME(new_real_oifs, oifs)))
		    tmp->flags |= MRTF_MFC_CLONE_SG;

		break;
	    }

	    if (tmp == mwc)
		break;
	}
#endif /* KERNEL_MFC_WC_G */

	if (VIFM_ISEMPTY(new_real_oifs)) {
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
	    if (new_iif == mrt->source->incoming) {
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
	logit(LOG_DEBUG, 0, "Cache miss, src %s, dst %s, iif %d",
	      inet_fmt(source, s1, sizeof(s1)), inet_fmt(group, s2, sizeof(s2)), iif);

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
	/* set reg_vif_num as outgoing interface ONLY if I am not the RP */
	if (mrt->group->rpaddr != my_cand_rp_address)
	    VIFM_SET(reg_vif_num, mrt->joined_oifs);
	change_interfaces(mrt,
			  mrt->incoming,
			  mrt->joined_oifs,
			  mrt->pruned_oifs,
			  mrt->leaves,
			  mrt->asserted_oifs, 0);
    } else {
	mrt = find_route(source, group, MRTF_SG | MRTF_WC | MRTF_PMBR, DONT_CREATE);
	switch_shortest_path(source, group);
	if (!mrt)
	    return;
    }

    /* TODO: if there are too many cache miss for the same (S,G),
     * install negative cache entry in the kernel (oif==NULL) to prevent
     * too many upcalls. */

    if (mrt->incoming == iif) {
	if (!VIFM_ISEMPTY(mrt->oifs)) {
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

#ifdef SCOPED_ACL
	    APPLY_SCOPE(group, mrt);
#endif
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

#ifdef SCOPED_ACL
		/* marian: not sure if we reach here with our scoped traffic? */
		APPLY_SCOPE(group, mrt);
#endif
		k_chg_mfc(igmp_socket, mfc_source, group, iif,
			  mrp->oifs, mrt->group->rpaddr);
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
	logit(LOG_DEBUG, 0, "Wrong iif: src %s, dst %s, iif %d",
	      inet_fmt(source, s1, sizeof(s1)), inet_fmt(group, s2, sizeof(s2)), iif);

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
    if (VIFM_ISSET(iif, mrt->oifs))
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

/**
 * Local Variables:
 *  version-control: t
 *  indent-tabs-mode: t
 *  c-file-style: "ellemtel"
 *  c-basic-offset: 4
 * End:
 */
