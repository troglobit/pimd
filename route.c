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

u_int32		default_source_metric     = UCAST_DEFAULT_SOURCE_METRIC;
u_int32		default_source_preference = UCAST_DEFAULT_SOURCE_PREFERENCE;

#ifdef SCOPED_ACL
/* from mrouted. Contributed by Marian Stagarescu <marian@bile.cidera.com>*/
static int scoped_addr(vifi_t vifi, u_int32 addr)
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
#define APPLY_SCOPE(g,mp) {			\
        vifi_t i;				\
        for (i = 0; i < numvifs; i++)		\
            if (scoped_addr(i, g))              \
                (mp)->oifs = NULL;		\
    }
#endif  /* SCOPED_ACL */

/* Return the iif for given address */
vifi_t get_iif(u_int32 address)
{
    struct rpfctl rpfc;

    k_req_incoming(address, &rpfc);
    if (rpfc.rpfneighbor.s_addr == INADDR_ANY_N)
        return (NO_VIF);
    return (rpfc.iif);
}

/* Return the PIM neighbor toward a source */
/* If route not found or if a local source or if a directly connected source,
 * but is not PIM router, or if the first hop router is not a PIM router,
 * then return NULL.
 */
pim_nbr_entry_t *find_pim_nbr(u_int32 source)
{
    struct rpfctl rpfc;
    pim_nbr_entry_t *pim_nbr;
    u_int32 next_hop_router_addr;

    if (local_address(source) != NO_VIF)
        return NULL;
    k_req_incoming(source, &rpfc);

    if ((rpfc.rpfneighbor.s_addr == INADDR_ANY_N)
        || (rpfc.iif == NO_VIF))
        return NULL;

    next_hop_router_addr = rpfc.rpfneighbor.s_addr;
    for (pim_nbr = uvifs[rpfc.iif].uv_pim_neighbors;
         pim_nbr != NULL;
         pim_nbr = pim_nbr->next)
        if (pim_nbr->address == next_hop_router_addr)
            return(pim_nbr);

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
 * If srctype==PIM_IIF_SOURCE and if the source is directly connected
 * then the "upstream" is set to NULL. If srcentry==PIM_IIF_RP, then
 * "upstream" in case of directly connected "source" will be that "source"
 * (if it is also PIM router).,
 */
int set_incoming(srcentry_t *srcentry_ptr, int srctype)
{
    struct rpfctl rpfc;
    u_int32 source = srcentry_ptr->address;
    u_int32 neighbor_addr;
    struct uvif *v;
    pim_nbr_entry_t *n;

    /* Preference will be 0 if directly connected */
    srcentry_ptr->metric = 0;
    srcentry_ptr->preference = 0;

    /* The source is a local address */
    if ((srcentry_ptr->incoming = local_address(source)) != NO_VIF) {
	/* iif of (*,G) at RP has to be register_if */
	if (srctype == PIM_IIF_RP)
	    srcentry_ptr->incoming = reg_vif_num;

        /* TODO: set the upstream to myself? */
        srcentry_ptr->upstream = NULL;
        return TRUE;
    }

    if ((srcentry_ptr->incoming = find_vif_direct(source)) != NO_VIF) {
        /* The source is directly connected. Check whether we are
         * looking for real source or RP
         */
        if (srctype == PIM_IIF_SOURCE) {
            srcentry_ptr->upstream = NULL;
            return (TRUE);
        } else {
            /* PIM_IIF_RP */
            neighbor_addr = source;
        }
    } else {
        /* TODO: probably need to check the case if the iif is disabled */
        /* Use the lastest resource: the kernel unicast routing table */
        k_req_incoming(source, &rpfc);
        if ((rpfc.iif == NO_VIF) ||
            rpfc.rpfneighbor.s_addr == INADDR_ANY_N) {
            /* couldn't find a route */
            IF_DEBUG(DEBUG_PIM_MRT | DEBUG_RPF)
                logit(LOG_DEBUG, 0, "NO ROUTE found for %s",
                      inet_fmt(source, s1, sizeof(s1)));
            return FALSE;
        }
        srcentry_ptr->incoming = rpfc.iif;
        neighbor_addr = rpfc.rpfneighbor.s_addr;
        /* set the preference for sources that aren't directly connected. */
        v = &uvifs[srcentry_ptr->incoming];
        srcentry_ptr->preference = v->uv_local_pref;
        srcentry_ptr->metric = v->uv_local_metric;
    }

    /*
     * The upstream router must be a (PIM router) neighbor, otherwise we
     * are in big trouble ;-)
     */
    v = &uvifs[srcentry_ptr->incoming];
    for (n = v->uv_pim_neighbors; n != NULL; n = n->next) {
        if (ntohl(neighbor_addr) < ntohl(n->address))
            continue;

        if (neighbor_addr == n->address) {
            /*
             *The upstream router is found in the list of neighbors.
             * We are safe!
             */
            srcentry_ptr->upstream = n;
            IF_DEBUG(DEBUG_RPF)
                logit(LOG_DEBUG, 0,
                      "For src %s, iif is %d, next hop router is %s",
                      inet_fmt(source, s1, sizeof(s1)), srcentry_ptr->incoming,
                      inet_fmt(neighbor_addr, s2, sizeof(s2)));

            return TRUE;
        }

	break;
    }

    /* TODO: control the number of messages! */
    logit(LOG_INFO, 0,
          "For src %s, iif is %d, next hop router is %s: NOT A PIM ROUTER",
          inet_fmt(source, s1, sizeof(s1)), srcentry_ptr->incoming,
          inet_fmt(neighbor_addr, s2, sizeof(s2)));
    srcentry_ptr->upstream = NULL;

    return FALSE;
}


/*
 * TODO: XXX: currently `source` is not used. Will be used with IGMPv3 where
 * we have source-specific Join/Prune.
 */
void add_leaf(vifi_t vifi, u_int32 source __attribute__((unused)), u_int32 group)
{
    mrtentry_t *mrtentry_ptr;
    mrtentry_t *mrtentry_srcs;
    vifbitmap_t old_oifs;
    vifbitmap_t new_oifs;
    vifbitmap_t new_leaves;

    if (ntohl(group) <= INADDR_MAX_LOCAL_GROUP)
        return; /* Don't create routing entries for the LAN scoped addresses */

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
    if (uvifs[vifi].uv_flags & VIFF_DR)
        mrtentry_ptr = find_route(INADDR_ANY_N, group, MRTF_WC, CREATE);
    else
        mrtentry_ptr = find_route(INADDR_ANY_N, group, MRTF_WC, DONT_CREATE);

    if (mrtentry_ptr == NULL)
        return;

    IF_DEBUG(DEBUG_MRT)
        logit(LOG_DEBUG, 0, "Adding vif %d for group %s", vifi,
              inet_fmt(group, s1, sizeof(s1)));

    if (VIFM_ISSET(vifi, mrtentry_ptr->leaves))
        return;     /* Already a leaf */
    calc_oifs(mrtentry_ptr, &old_oifs);
    VIFM_COPY(mrtentry_ptr->leaves, new_leaves);
    VIFM_SET(vifi, new_leaves);    /* Add the leaf */
    change_interfaces(mrtentry_ptr,
                      mrtentry_ptr->incoming,
                      mrtentry_ptr->joined_oifs,
                      mrtentry_ptr->pruned_oifs,
                      new_leaves,
                      mrtentry_ptr->asserted_oifs, 0);
    calc_oifs(mrtentry_ptr, &new_oifs);

    /* Only if I am the DR for that subnet, eventually initiate a Join */
    if (!(uvifs[vifi].uv_flags & VIFF_DR))
        return;

    if ((mrtentry_ptr->flags & MRTF_NEW)
        || (VIFM_ISEMPTY(old_oifs) && (!VIFM_ISEMPTY(new_oifs)))) {
        /* A new created entry or the oifs have changed
         * from NULL to non-NULL.
         */
        mrtentry_ptr->flags &= ~MRTF_NEW;
        FIRE_TIMER(mrtentry_ptr->jp_timer); /* Timeout the Join/Prune timer */
        /* TODO: explicitly call the function below?
           send_pim_join_prune(mrtentry_ptr->upstream->vifi,
           mrtentry_ptr->upstream,
           PIM_JOIN_PRUNE_HOLDTIME);
        */
    }

    /* Check all (S,G) entries and set the inherited "leaf" flag.
     * TODO: XXX: This won't work for IGMPv3, because there we don't know
     * whether the (S,G) leaf oif was inherited from the (*,G) entry or
     * was created by source specific IGMP join.
     */
    for (mrtentry_srcs = mrtentry_ptr->group->mrtlink;
         mrtentry_srcs != (mrtentry_t *)NULL;
         mrtentry_srcs = mrtentry_srcs->grpnext) {
        VIFM_COPY(mrtentry_srcs->leaves, new_leaves);
        VIFM_SET(vifi, new_leaves);
        change_interfaces(mrtentry_srcs,
                          mrtentry_srcs->incoming,
                          mrtentry_srcs->joined_oifs,
                          mrtentry_srcs->pruned_oifs,
                          new_leaves,
                          mrtentry_srcs->asserted_oifs, 0);
    }
}


/*
 * TODO: XXX: currently `source` is not used. To be used with IGMPv3 where
 * we have source-specific joins/prunes.
 */
void delete_leaf(vifi_t vifi, u_int32 source __attribute__((unused)), u_int32 group)
{
    mrtentry_t *mrtentry_ptr;
    mrtentry_t *mrtentry_srcs;
    vifbitmap_t new_oifs;
    vifbitmap_t old_oifs;
    vifbitmap_t new_leaves;

    mrtentry_ptr = find_route(INADDR_ANY_N, group, MRTF_WC, DONT_CREATE);
    if (mrtentry_ptr == NULL)
        return;

    if (!VIFM_ISSET(vifi, mrtentry_ptr->leaves))
        return;      /* This interface wasn't leaf */

    calc_oifs(mrtentry_ptr, &old_oifs);
    VIFM_COPY(mrtentry_ptr->leaves, new_leaves);
    VIFM_CLR(vifi, new_leaves);
    change_interfaces(mrtentry_ptr,
                      mrtentry_ptr->incoming,
                      mrtentry_ptr->joined_oifs,
                      mrtentry_ptr->pruned_oifs,
                      new_leaves,
                      mrtentry_ptr->asserted_oifs, 0);
    calc_oifs(mrtentry_ptr, &new_oifs);
    if ((!VIFM_ISEMPTY(old_oifs)) && VIFM_ISEMPTY(new_oifs)) {
        /* The result oifs have changed from non-NULL to NULL */
        FIRE_TIMER(mrtentry_ptr->jp_timer);  /* Timeout the Join/Prune timer */
        /* TODO: explicitly call the function?
           send_pim_join_prune(mrtentry_ptr->upstream->vifi,
           mrtentry_ptr->upstream, PIM_JOIN_PRUNE_HOLDTIME);
        */
    }
    /* Check all (S,G) entries and clear the inherited "leaf" flag.
     * TODO: XXX: This won't work for IGMPv3, because there we don't know
     * whether the (S,G) leaf oif was inherited from the (*,G) entry or
     * was created by source specific IGMP join.
     */
    for (mrtentry_srcs = mrtentry_ptr->group->mrtlink;
         mrtentry_srcs != NULL;
         mrtentry_srcs = mrtentry_srcs->grpnext) {
        VIFM_COPY(mrtentry_srcs->leaves, new_leaves);
        VIFM_CLR(vifi, new_leaves);
        change_interfaces(mrtentry_srcs,
                          mrtentry_srcs->incoming,
                          mrtentry_srcs->joined_oifs,
                          mrtentry_srcs->pruned_oifs,
                          new_leaves,
                          mrtentry_srcs->asserted_oifs, 0);
    }

}


void calc_oifs(mrtentry_t *mrtentry_ptr, vifbitmap_t *oifs_ptr)
{
    vifbitmap_t oifs;
    mrtentry_t *grp_route;
    mrtentry_t *rp_route;

    /*
     * oifs =
     * (((copied_outgoing + my_join) - my_prune) + my_leaves)
     *              - my_asserted_oifs - incoming_interface,
     * i.e. `leaves` have higher priority than `prunes`, but lower priority
     * than `asserted`. The incoming interface is always deleted from the oifs
     */

    if (mrtentry_ptr == NULL) {
        VIFM_CLRALL(*oifs_ptr);
        return;
    }

    VIFM_CLRALL(oifs);
    if (!(mrtentry_ptr->flags & MRTF_PMBR)) {
        /* Either (*,G) or (S,G). Merge with the oifs from the (*,*,RP) */
        if ((rp_route = mrtentry_ptr->group->active_rp_grp->rp->rpentry->mrtlink) != NULL) {
            VIFM_MERGE(oifs, rp_route->joined_oifs, oifs);
            VIFM_CLR_MASK(oifs, rp_route->pruned_oifs);
            VIFM_MERGE(oifs, rp_route->leaves, oifs);
            VIFM_CLR_MASK(oifs, rp_route->asserted_oifs);
        }
    }
    if (mrtentry_ptr->flags & MRTF_SG) {
        /* (S,G) entry. Merge with the oifs from (*,G) */
        if ((grp_route = mrtentry_ptr->group->grp_route) != NULL) {
            VIFM_MERGE(oifs, grp_route->joined_oifs, oifs);
            VIFM_CLR_MASK(oifs, grp_route->pruned_oifs);
            VIFM_MERGE(oifs, grp_route->leaves, oifs);
            VIFM_CLR_MASK(oifs, grp_route->asserted_oifs);
        }
    }

    /* Calculate my own stuff */
    VIFM_MERGE(oifs, mrtentry_ptr->joined_oifs, oifs);
    VIFM_CLR_MASK(oifs, mrtentry_ptr->pruned_oifs);
    VIFM_MERGE(oifs, mrtentry_ptr->leaves, oifs);
    VIFM_CLR_MASK(oifs, mrtentry_ptr->asserted_oifs);

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
int change_interfaces(mrtentry_t *mrtentry_ptr,
                      vifi_t new_iif,
                      vifbitmap_t new_joined_oifs_,
                      vifbitmap_t new_pruned_oifs,
                      vifbitmap_t new_leaves_,
                      vifbitmap_t new_asserted_oifs,
                      u_int16 flags)
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
    rpentry_t   *rpentry_ptr;
    cand_rp_t   *cand_rp_ptr;
    kernel_cache_t *kernel_cache_ptr;
    rp_grp_entry_t *rp_grp_entry_ptr;
    grpentry_t     *grpentry_ptr;
    mrtentry_t     *mrtentry_srcs;
    mrtentry_t     *mrtentry_wc;
    mrtentry_t     *mrtentry_rp;
    int delete_mrtentry_flag;
    int return_value;
    int fire_timer_flag;

    if (mrtentry_ptr == NULL)
        return 0;

    VIFM_COPY(new_joined_oifs_, new_joined_oifs);
    VIFM_COPY(new_leaves_, new_leaves);

    old_iif = mrtentry_ptr->incoming;
    VIFM_COPY(mrtentry_ptr->joined_oifs, old_joined_oifs);
    VIFM_COPY(mrtentry_ptr->leaves, old_leaves);
    VIFM_COPY(mrtentry_ptr->pruned_oifs, old_pruned_oifs);
    VIFM_COPY(mrtentry_ptr->asserted_oifs, old_asserted_oifs);

    VIFM_COPY(mrtentry_ptr->oifs, old_real_oifs);

    mrtentry_ptr->incoming = new_iif;
    VIFM_COPY(new_joined_oifs, mrtentry_ptr->joined_oifs);
    VIFM_COPY(new_pruned_oifs, mrtentry_ptr->pruned_oifs);
    VIFM_COPY(new_leaves, mrtentry_ptr->leaves);
    VIFM_COPY(new_asserted_oifs, mrtentry_ptr->asserted_oifs);
    calc_oifs(mrtentry_ptr, &new_real_oifs);

    if (VIFM_ISEMPTY(old_real_oifs)) {
        if (VIFM_ISEMPTY(new_real_oifs))
            return_value = 0;
        else
            return_value = 1;
    } else {
        if (VIFM_ISEMPTY(new_real_oifs))
            return_value = -1;
        else
            return_value = 0;
    }

    if ((VIFM_SAME(new_real_oifs, old_real_oifs))
        && (new_iif == old_iif)
        && !(flags & MFC_UPDATE_FORCE))
        return 0;                  /* Nothing to change */

    if ((return_value != 0) || (new_iif != old_iif) || (flags & MFC_UPDATE_FORCE)) {
        FIRE_TIMER(mrtentry_ptr->jp_timer);
    }
    VIFM_COPY(new_real_oifs, mrtentry_ptr->oifs);

    if (mrtentry_ptr->flags & MRTF_PMBR) {
        /* (*,*,RP) entry */
        rpentry_ptr = mrtentry_ptr->source;
        if (rpentry_ptr == NULL)
            return 0;    /* Shouldn't happen */

        rpentry_ptr->incoming = new_iif;
        cand_rp_ptr = rpentry_ptr->cand_rp;

        if (VIFM_ISEMPTY(new_real_oifs)) {
            delete_mrtentry_flag = TRUE;
        } else {
            delete_mrtentry_flag = FALSE;
#ifdef RSRR
            rsrr_cache_send(mrtentry_ptr, RSRR_NOTIFICATION_OK);
#endif /* RSRR */
        }

        if (mrtentry_ptr->flags & MRTF_KERNEL_CACHE) {
            /* Update the kernel MFC entries */
            if (delete_mrtentry_flag == TRUE) {
                /* XXX: no need to send RSRR message. Will do it when
                 * delete the mrtentry.
                 */
                for (kernel_cache_ptr = mrtentry_ptr->kernel_cache;
                     kernel_cache_ptr != NULL;
                     kernel_cache_ptr = kernel_cache_ptr->next)
                    delete_mrtentry_all_kernel_cache(mrtentry_ptr);
	    } else {
                for (kernel_cache_ptr = mrtentry_ptr->kernel_cache;
                     kernel_cache_ptr != NULL;
                     kernel_cache_ptr = kernel_cache_ptr->next)
                    /* here mrtentry_ptr->source->address is the RP address */
                    k_chg_mfc(igmp_socket, kernel_cache_ptr->source,
                              kernel_cache_ptr->group, new_iif,
                              new_real_oifs, mrtentry_ptr->source->address);
            }
        }

        /*
         * Update all (*,G) entries associated with this RP.
         * The particular (*,G) outgoing are not changed, but the change
         * in the (*,*,RP) oifs may have affect the real oifs.
         */
        fire_timer_flag = FALSE;
        for (rp_grp_entry_ptr = cand_rp_ptr->rp_grp_next;
             rp_grp_entry_ptr != NULL;
             rp_grp_entry_ptr = rp_grp_entry_ptr->rp_grp_next) {
            for (grpentry_ptr = rp_grp_entry_ptr->grplink;
                 grpentry_ptr != NULL;
                 grpentry_ptr = grpentry_ptr->rpnext) {
                if (grpentry_ptr->grp_route != NULL) {
                    if (change_interfaces(grpentry_ptr->grp_route, new_iif,
                                          grpentry_ptr->grp_route->joined_oifs,
                                          grpentry_ptr->grp_route->pruned_oifs,
                                          grpentry_ptr->grp_route->leaves,
                                          grpentry_ptr->grp_route->asserted_oifs,
                                          flags))
                        fire_timer_flag = TRUE;
                } else {
                    /* Change all (S,G) entries if no (*,G) */
                    for (mrtentry_srcs = grpentry_ptr->mrtlink;
                         mrtentry_srcs != NULL;
                         mrtentry_srcs = mrtentry_srcs->grpnext) {
                        if (mrtentry_srcs->flags & MRTF_RP) {
                            if (change_interfaces(mrtentry_srcs, new_iif,
                                                  mrtentry_srcs->joined_oifs,
                                                  mrtentry_srcs->pruned_oifs,
                                                  mrtentry_srcs->leaves,
                                                  mrtentry_srcs->asserted_oifs,
                                                  flags))
                                fire_timer_flag = TRUE;
                        } else {
                            if (change_interfaces(mrtentry_srcs,
                                                  mrtentry_srcs->incoming,
                                                  mrtentry_srcs->joined_oifs,
                                                  mrtentry_srcs->pruned_oifs,
                                                  mrtentry_srcs->leaves,
                                                  mrtentry_srcs->asserted_oifs,
                                                  flags))
                                fire_timer_flag = TRUE;
                        }
                    }
                }
            }
        }
        if (fire_timer_flag == TRUE)
            FIRE_TIMER(mrtentry_ptr->jp_timer);
        if (delete_mrtentry_flag == TRUE) {
            /* TODO: XXX: trigger a Prune message? Don't delete now, it will
             * be automatically timed out. If want to delete now, don't
             * reference to it anymore!
             delete_mrtentry(mrtentry_ptr);
            */
        }

        return return_value;   /* (*,*,RP) */
    }

    if (mrtentry_ptr->flags & MRTF_WC) {
        /* (*,G) entry */
        if (VIFM_ISEMPTY(new_real_oifs)) {
            delete_mrtentry_flag = TRUE;
        } else {
            delete_mrtentry_flag = FALSE;
#ifdef RSRR
            rsrr_cache_send(mrtentry_ptr, RSRR_NOTIFICATION_OK);
#endif /* RSRR */
        }

        if (mrtentry_ptr->flags & MRTF_KERNEL_CACHE) {
            if (delete_mrtentry_flag == TRUE) {
                delete_mrtentry_all_kernel_cache(mrtentry_ptr);
            } else {
                for (kernel_cache_ptr = mrtentry_ptr->kernel_cache;
                     kernel_cache_ptr != NULL;
                     kernel_cache_ptr = kernel_cache_ptr->next)
                    k_chg_mfc(igmp_socket, kernel_cache_ptr->source,
                              kernel_cache_ptr->group, new_iif,
                              new_real_oifs, mrtentry_ptr->group->rpaddr);
            }
        }

        /* Update all (S,G) entries for this group.
         * For the (S,G)RPbit entries the iif is the iif toward the RP;
         * The particular (S,G) oifs are not changed, but the change in the
         * (*,G) oifs may affect the real oifs.
         */
        fire_timer_flag = FALSE;
        for (mrtentry_srcs = mrtentry_ptr->group->mrtlink; mrtentry_srcs; mrtentry_srcs = mrtentry_srcs->grpnext) {
            if (mrtentry_srcs->flags & MRTF_RP) {
                if (change_interfaces(mrtentry_srcs, new_iif,
                                      mrtentry_srcs->joined_oifs,
                                      mrtentry_srcs->pruned_oifs,
                                      mrtentry_srcs->leaves,
                                      mrtentry_srcs->asserted_oifs, flags))
                    fire_timer_flag = TRUE;
            } else {
                if (change_interfaces(mrtentry_srcs, mrtentry_srcs->incoming,
                                      mrtentry_srcs->joined_oifs,
                                      mrtentry_srcs->pruned_oifs,
                                      mrtentry_srcs->leaves,
                                      mrtentry_srcs->asserted_oifs, flags))
                    fire_timer_flag = TRUE;
            }
        }

        if (fire_timer_flag == TRUE)
            FIRE_TIMER(mrtentry_ptr->jp_timer);

        if (delete_mrtentry_flag == TRUE) {
            /* TODO: XXX: the oifs are NULL. Send a Prune message? */
        }

        return return_value;   /* (*,G) */
    }

    if (mrtentry_ptr->flags & MRTF_SG) {
        /* (S,G) entry */
#ifdef KERNEL_MFC_WC_G
        vifbitmap_t tmp_oifs;
        mrtentry_t *mrtentry_tmp;
#endif /* KERNEL_MFC_WC_G */

        mrtentry_rp = mrtentry_ptr->group->active_rp_grp->rp->rpentry->mrtlink;
        mrtentry_wc = mrtentry_ptr->group->grp_route;
#ifdef KERNEL_MFC_WC_G
        /* Check whether (*,*,RP) or (*,G) have different (iif,oifs) from
         * the (S,G). If "yes", then forbid creating (*,G) MFC.
         */
        for (mrtentry_tmp = mrtentry_rp; 1; mrtentry_tmp = mrtentry_wc) {
            while (1) {
                if (mrtentry_tmp == NULL)
                    break;

                if (mrtentry_tmp->flags & MRTF_MFC_CLONE_SG)
                    break;

                if (mrtentry_tmp->incoming != mrtentry_ptr->incoming) {
                    delete_single_kernel_cache_addr(mrtentry_tmp, INADDR_ANY_N,
                                                    mrtentry_ptr->group->group);
                    mrtentry_tmp->flags |= MRTF_MFC_CLONE_SG;
                    break;
                }

                calc_oifs(mrtentry_tmp, &tmp_oifs);
                if (!(VIFM_SAME(new_real_oifs, tmp_oifs)))
                    mrtentry_tmp->flags |= MRTF_MFC_CLONE_SG;

                break;
            }

            if (mrtentry_tmp == mrtentry_wc)
                break;
        }
#endif /* KERNEL_MFC_WC_G */

        if (VIFM_ISEMPTY(new_real_oifs)) {
            delete_mrtentry_flag = TRUE;
        } else {
            delete_mrtentry_flag = FALSE;
#ifdef RSRR
            rsrr_cache_send(mrtentry_ptr, RSRR_NOTIFICATION_OK);
#endif /* RSRR */
        }

        if (mrtentry_ptr->flags & MRTF_KERNEL_CACHE) {
            if (delete_mrtentry_flag == TRUE)
                delete_mrtentry_all_kernel_cache(mrtentry_ptr);
            else
                k_chg_mfc(igmp_socket, mrtentry_ptr->source->address,
                          mrtentry_ptr->group->group, new_iif, new_real_oifs,
                          mrtentry_ptr->group->rpaddr);
        }

        if (old_iif != new_iif) {
            if (new_iif == mrtentry_ptr->source->incoming) {
                /* For example, if this was (S,G)RPbit with iif toward the RP,
                 * and now switch to the Shortest Path.
                 * The setup of MRTF_SPT flag must be
                 * done by the external calling function (triggered only
                 * by receiving of a data from the source.)
                 */
                mrtentry_ptr->flags &= ~MRTF_RP;
                /* TODO: XXX: delete? Check again where will be the best
                 * place to set it.
                 mrtentry_ptr->flags |= MRTF_SPT;
                */
            }

            if (((mrtentry_wc != NULL) && (mrtentry_wc->incoming == new_iif))
                || ((mrtentry_rp != NULL) && (mrtentry_rp->incoming == new_iif))) {
                /* If the new iif points toward the RP, reset the SPT flag.
                 * (PIM-SM-spec-10.ps pp. 11, 2.10, last sentence of first
                 * paragraph.
                 */
                /* TODO: XXX: check again! */
                mrtentry_ptr->flags &= ~MRTF_SPT;
                mrtentry_ptr->flags |= MRTF_RP;
            }
        }

        /* TODO: XXX: if this is (S,G)RPbit entry and the oifs==(*,G)oifs,
         * then delete the (S,G) entry?? The same if we have (*,*,RP) ? */
        if (delete_mrtentry_flag == TRUE) {
            /* TODO: XXX: the oifs are NULL. Send a Prune message ? */
        }

        /* TODO: XXX: have the feeling something is missing.... */
        return return_value;  /* (S,G) */
    }

    return return_value;
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
    struct igmpmsg *igmpctl; /* igmpmsg control struct */

    igmpctl = (struct igmpmsg *) igmp_recv_buf;

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
                logit(LOG_DEBUG, 0, "Unknown kernel_call code");
            break;
    }
}


/*
 * TODO: when cache miss, check the iif, because probably ASSERTS
 * shoult take place
 */
static void process_cache_miss(struct igmpmsg *igmpctl)
{
    u_int32 source, mfc_source;
    u_int32 group;
    u_int32 rp_addr;
    vifi_t iif;
    mrtentry_t *mrtentry_ptr;
    mrtentry_t *mrtentry_rp;

    /*
     * When there is a cache miss, we check only the header of the packet
     * (and only it should be sent up by the kernel.
     */

    group  = igmpctl->im_dst.s_addr;
    source = mfc_source = igmpctl->im_src.s_addr;
    iif    = igmpctl->im_vif;

    IF_DEBUG(DEBUG_MFC) {
        logit(LOG_DEBUG, 0, "Cache miss, src %s, dst %s, iif %d",
              inet_fmt(source, s1, sizeof(s1)), inet_fmt(group, s2, sizeof(s2)), iif);
    }

    /* TODO: XXX: check whether the kernel generates cache miss for the LAN scoped addresses */
    if (ntohl(group) <= INADDR_MAX_LOCAL_GROUP)
        return; /* Don't create routing entries for the LAN scoped addresses */

    /* TODO: check if correct in case the source is one of my addresses */
    /* If I am the DR for this source, create (S,G) and add the register_vif
     * to the oifs.
     */
    if ((uvifs[iif].uv_flags & VIFF_DR) && (find_vif_direct_local(source) == iif)) {
        mrtentry_ptr = find_route(source, group, MRTF_SG, CREATE);
        if (mrtentry_ptr == NULL)
            return;

        mrtentry_ptr->flags &= ~MRTF_NEW;
        /* set reg_vif_num as outgoing interface ONLY if I am not the RP */
        if (mrtentry_ptr->group->rpaddr != my_cand_rp_address)
            VIFM_SET(reg_vif_num, mrtentry_ptr->joined_oifs);
        change_interfaces(mrtentry_ptr,
                          mrtentry_ptr->incoming,
                          mrtentry_ptr->joined_oifs,
                          mrtentry_ptr->pruned_oifs,
                          mrtentry_ptr->leaves,
                          mrtentry_ptr->asserted_oifs, 0);
    } else {
        mrtentry_ptr = find_route(source, group, MRTF_SG | MRTF_WC | MRTF_PMBR, DONT_CREATE);
        if (mrtentry_ptr == NULL)
            return;
    }

    /* TODO: if there are too many cache miss for the same (S,G), install
     * negative cache entry in the kernel (oif==NULL) to prevent too
     * many upcalls.
     */

    if (mrtentry_ptr->incoming == iif) {
        if (!VIFM_ISEMPTY(mrtentry_ptr->oifs)) {
            if (mrtentry_ptr->flags & MRTF_SG) {
                /* TODO: check that the RPbit is not set? */
                /* TODO: XXX: TIMER implem. dependency! */
                if (mrtentry_ptr->timer < PIM_DATA_TIMEOUT)
                    SET_TIMER(mrtentry_ptr->timer, PIM_DATA_TIMEOUT);
                if (!(mrtentry_ptr->flags & MRTF_SPT)) {
                    if ((mrtentry_rp = mrtentry_ptr->group->grp_route) == NULL)
                        mrtentry_rp = mrtentry_ptr->group->active_rp_grp->rp->rpentry->mrtlink;

                    if (mrtentry_rp != NULL) {
                        /* Check if the (S,G) iif is different from
                         * the (*,G) or (*,*,RP) iif
                         */
                        if ((mrtentry_ptr->incoming != mrtentry_rp->incoming)
                            || (mrtentry_ptr->upstream != mrtentry_rp->upstream)) {
                            mrtentry_ptr->flags |= MRTF_SPT;
                            mrtentry_ptr->flags &= ~MRTF_RP;
                        }
                    }
                }
            }

            if (mrtentry_ptr->flags & MRTF_PMBR)
                rp_addr = mrtentry_ptr->source->address;
            else
                rp_addr = mrtentry_ptr->group->rpaddr;

            mfc_source = source;
#ifdef KERNEL_MFC_WC_G
            if (mrtentry_ptr->flags & (MRTF_WC | MRTF_PMBR))
                if (!(mrtentry_ptr->flags & MRTF_MFC_CLONE_SG))
                    mfc_source = INADDR_ANY_N;
#endif /* KERNEL_MFC_WC_G */
            add_kernel_cache(mrtentry_ptr, mfc_source, group, MFC_MOVE_FORCE);
#ifdef SCOPED_ACL
            APPLY_SCOPE(group,mrtentry_ptr);
#endif
            k_chg_mfc(igmp_socket, mfc_source, group, iif, mrtentry_ptr->oifs,
                      rp_addr);
            /* TODO: XXX: No need for RSRR message, because nothing has
             * changed.
             */
        }

        return; /* iif match */
    }

    /* The iif doesn't match */
    if (mrtentry_ptr->flags & MRTF_SG) {
	/* Arrived on wrong interface */
        if (mrtentry_ptr->flags & MRTF_SPT)
            return;

        if ((mrtentry_rp = mrtentry_ptr->group->grp_route) == NULL)
            mrtentry_rp = mrtentry_ptr->group->active_rp_grp->rp->rpentry->mrtlink;

        if (mrtentry_rp != NULL) {
            if (mrtentry_rp->incoming == iif) {
                /* Forward on (*,G) or (*,*,RP) */
#ifdef KERNEL_MFC_WC_G
                if (!(mrtentry_rp->flags & MRTF_MFC_CLONE_SG))
                    mfc_source = INADDR_ANY_N;
#endif /* KERNEL_MFC_WC_G */
                add_kernel_cache(mrtentry_rp, mfc_source, group, 0);
/* marian: not sure if we are going to reach here for our scoped traffic */
#ifdef SCOPED_ACL
                APPLY_SCOPE(group,mrtentry_ptr);
#endif
                k_chg_mfc(igmp_socket, mfc_source, group, iif,
                          mrtentry_rp->oifs, mrtentry_ptr->group->rpaddr);
#ifdef RSRR
                rsrr_cache_send(mrtentry_rp, RSRR_NOTIFICATION_OK);
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
    u_int32 source;
    u_int32 group;
    vifi_t  iif;
    mrtentry_t *mrtentry_ptr;

    group  = igmpctl->im_dst.s_addr;
    source = igmpctl->im_src.s_addr;
    iif    = igmpctl->im_vif;

    /* Don't create routing entries for the LAN scoped addresses */
    if (ntohl(group) <= INADDR_MAX_LOCAL_GROUP)
        return;

    /* Ignore if it comes on register vif. register vif is neither SPT iif,
     * neither is used to send asserts out.
     */
    if (uvifs[iif].uv_flags & VIFF_REGISTER)
        return;

    mrtentry_ptr = find_route(source, group, MRTF_SG | MRTF_WC | MRTF_PMBR,
                              DONT_CREATE);
    if (mrtentry_ptr == NULL)
        return;

    /*
     * TODO: check again!
     */
    if (mrtentry_ptr->flags & MRTF_SG) {
        if (!(mrtentry_ptr->flags & MRTF_SPT)) {
            if (mrtentry_ptr->source->incoming == iif) {
                /* Switch to the Shortest Path */
                mrtentry_ptr->flags |= MRTF_SPT;
                mrtentry_ptr->flags &= ~MRTF_RP;
                add_kernel_cache(mrtentry_ptr, source, group, MFC_MOVE_FORCE);
                k_chg_mfc(igmp_socket, source, group, iif,
                          mrtentry_ptr->oifs, mrtentry_ptr->group->rpaddr);
                FIRE_TIMER(mrtentry_ptr->jp_timer);
#ifdef RSRR
                rsrr_cache_send(mrtentry_ptr, RSRR_NOTIFICATION_OK);
#endif /* RSRR */

                return;
            }
        }
    }

    /* Trigger an Assert */
    if (VIFM_ISSET(iif, mrtentry_ptr->oifs))
        send_pim_assert(source, group, iif, mrtentry_ptr);
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


mrtentry_t *switch_shortest_path(u_int32 source, u_int32 group)
{
    mrtentry_t *mrtentry_ptr;

    /* TODO: XXX: prepare and send immediately the (S,G) join? */
    if ((mrtentry_ptr = find_route(source, group, MRTF_SG, CREATE)) != NULL) {
        if (mrtentry_ptr->flags & MRTF_NEW) {
            mrtentry_ptr->flags &= ~MRTF_NEW;
        }
        else if (mrtentry_ptr->flags & MRTF_RP) {
            /* (S,G)RPbit with iif toward RP. Reset to (S,G) with iif
             * toward S. Delete the kernel cache (if any), because
             * change_interfaces() will reset it with iif toward S
             * and no data will arrive from RP before the switch
             * really occurs.
             */
            mrtentry_ptr->flags &= ~MRTF_RP;
            mrtentry_ptr->incoming = mrtentry_ptr->source->incoming;
            mrtentry_ptr->upstream = mrtentry_ptr->source->upstream;
            delete_mrtentry_all_kernel_cache(mrtentry_ptr);
            change_interfaces(mrtentry_ptr,
                              mrtentry_ptr->incoming,
                              mrtentry_ptr->joined_oifs,
                              mrtentry_ptr->pruned_oifs,
                              mrtentry_ptr->leaves,
                              mrtentry_ptr->asserted_oifs, 0);
        }

        SET_TIMER(mrtentry_ptr->timer, PIM_DATA_TIMEOUT);
        FIRE_TIMER(mrtentry_ptr->jp_timer);
    }

    return mrtentry_ptr;
}

/**
 * Local Variables:
 *  version-control: t
 *  indent-tabs-mode: t
 *  c-file-style: "ellemtel"
 *  c-basic-offset: 4
 * End:
 */
