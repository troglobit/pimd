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
 *  $Id: mrt.c,v 1.27 2001/09/10 20:31:36 pavlin Exp $
 */


#include "defs.h"


srcentry_t		*srclist;
grpentry_t		*grplist;

/*
 * Local functions definition
 */
static srcentry_t *create_srcentry   (uint32_t);
static int	   search_srclist    (uint32_t, srcentry_t **);
static int	   search_srcmrtlink (srcentry_t *, uint32_t, mrtentry_t **);
static void	   insert_srcmrtlink (mrtentry_t *, mrtentry_t *, srcentry_t *);

static grpentry_t *create_grpentry   (uint32_t);
static int	   search_grplist    (uint32_t, grpentry_t **);
static int	   search_grpmrtlink (grpentry_t *, uint32_t, mrtentry_t **);
static void	   insert_grpmrtlink (mrtentry_t *, mrtentry_t *, grpentry_t *);

static mrtentry_t *alloc_mrtentry    (srcentry_t *, grpentry_t *);
static mrtentry_t *create_mrtentry   (srcentry_t *, grpentry_t *, uint16_t);

static void	   move_kernel_cache (mrtentry_t *, uint16_t);


void init_pim_mrt(void)
{
    /* TODO: delete any existing routing table */

    /* Initialize the source list */
    /* The first entry has address 'INADDR_ANY' and is not used */
    /* The order is the smallest address first. */
    srclist		= (srcentry_t *)calloc(1, sizeof(srcentry_t));
    if (!srclist)
	logit(LOG_ERR, 0, "Ran out of memory in init_pim_mrt()");
    srclist->next       = NULL;
    srclist->prev       = NULL;
    srclist->address	= INADDR_ANY_N;
    srclist->mrtlink    = NULL;
    srclist->incoming   = NO_VIF;
    srclist->upstream   = NULL;
    srclist->metric     = 0;
    srclist->preference = 0;
    RESET_TIMER(srclist->timer);
    srclist->cand_rp    = NULL;

    /* Initialize the group list */
    /* The first entry has address 'INADDR_ANY' and is not used */
    /* The order is the smallest address first. */
    grplist		= (grpentry_t *)calloc(1, sizeof(grpentry_t));
    if (!grplist)
	logit(LOG_ERR, 0, "Ran out of memory in init_pim_mrt()");
    grplist->next       = NULL;
    grplist->prev       = NULL;
    grplist->rpnext     = NULL;
    grplist->rpprev     = NULL;
    grplist->group	= INADDR_ANY_N;
    grplist->rpaddr     = INADDR_ANY_N;
    grplist->mrtlink    = NULL;
    grplist->active_rp_grp = NULL;
    grplist->grp_route   = NULL;
}


grpentry_t *find_group(uint32_t group)
{
    grpentry_t *grp;

    if (!IN_MULTICAST(ntohl(group)))
	return NULL;

    if (search_grplist(group, &grp) == TRUE)
	return grp;	/* Group found! */

    return NULL;
}


srcentry_t *find_source(uint32_t source)
{
    srcentry_t *src;

    if (!inet_valid_host(source))
	return NULL;

    if (search_srclist(source, &src) == TRUE)
	return src;	/* Source found! */

    return NULL;
}


mrtentry_t *find_route(uint32_t source, uint32_t group, uint16_t flags, char create)
{
    srcentry_t *src	   = NULL;
    grpentry_t *grp	   = NULL;
    mrtentry_t *mrt	   = NULL;
    mrtentry_t *mrt_wc	   = NULL;
    mrtentry_t *mrt_pmbr   = NULL;
    mrtentry_t *mrt2	   = NULL;
    rpentry_t  *rp	   = NULL;
    rp_grp_entry_t *rp_grp = NULL;

    if (flags & (MRTF_SG | MRTF_WC)) {
	if (!IN_MULTICAST(ntohl(group))) {
	    logit(LOG_WARNING, 0, "find_route: Not a multicast group address (%s) ...",
		  inet_fmt(group, s1, sizeof(s1)));
	    return NULL;
	}
    }

    if (flags & MRTF_SG) {
	if (!inet_valid_host(source) && !IN_PIM_SSM_RANGE(group)) {
	    logit(LOG_WARNING, 0, "find_route: Not a valid host (%s) ...",
		  inet_fmt(source, s1, sizeof(s1)));
	    return NULL;
	}
    }

    if (create == DONT_CREATE) {
	if (flags & (MRTF_SG | MRTF_WC)) {
	    if (search_grplist(group, &grp) == FALSE) {
		/* Group not found. Return the (*,*,RP) entry */
		if (flags & MRTF_PMBR) {
		    rp = rp_match(group);
		    if (rp) {
			logit(LOG_DEBUG, 0 , "find_route: Group not found. Return the (*,*,RP) entry");
			return rp->mrtlink;
		    }
		}

		logit(LOG_DEBUG, 0 , "find_route: Not PMBR, return NULL");
		return NULL;
	    }

	    /* Search for the source */
	    if (flags & MRTF_SG) {
		if (search_grpmrtlink(grp, source, &mrt) == TRUE) {
		    /* Exact (S,G) entry found */
		    logit(LOG_DEBUG, 0 , "find_route: exact (S,G) entry found");
		    return mrt;
		}

		logit(LOG_DEBUG, 0 , "find_route:(S,G) entry not found");
	    }

	    /* No (S,G) entry. Return the (*,G) entry (if exist) */
	    if ((flags & MRTF_WC) && grp->grp_route) {
		logit(LOG_DEBUG, 0 , "find_route: No (S,G) entry. Return the (*,G) entry");
		return grp->grp_route;
	    }
	}

	/* Return the (*,*,RP) entry */
	if (flags & MRTF_PMBR) {
	    rp = NULL;
	    if (group != INADDR_ANY_N)
		rp = rp_match(group);
	    else if (source != INADDR_ANY_N)
		rp = rp_find(source);

	    if (rp) {
		logit(LOG_DEBUG, 0 , "find_route: Return the (*,*,RP) entry");
		return rp->mrtlink;
	    }
	}

	logit(LOG_DEBUG, 0 , "find_route: No SG|WC, return NULL");
	return NULL;
    }


    /* Creation allowed */

    if (flags & (MRTF_SG | MRTF_WC)) {
	grp = create_grpentry(group);
	if (!grp)
	    return NULL;

	if (IN_PIM_SSM_RANGE(group)) {
	    if (rp_match(group) == (rpentry_t *) NULL) {
		/* For SSM, virtual RP entry has to be created. RP is at local link 169.254.0.1
		   to be sure not to send any register messages outside, although sending them
		   has been disabled for SSM also in PIM protocol.
		   The address does not need to be really configured in any interface.
		   TODO: Avoid need for virtual RP by implementing SSM-specific state structures */
		add_rp_grp_entry(&cand_rp_list, &grp_mask_list, 0x0100fea9, 20, 90, group,
				 0xffffffff, curr_bsr_hash_mask, curr_bsr_fragment_tag);
	    }
	}

	if (!grp->active_rp_grp) {
	    rp_grp = rp_grp_match(group);
	    if (!rp_grp) {
		if (!grp->mrtlink && !grp->grp_route)
		    /* New created grpentry. Delete it. */
		    delete_grpentry(grp);

		return NULL;
	    }

	    rp = rp_grp->rp->rpentry;
	    grp->active_rp_grp = rp_grp;
	    grp->rpaddr = rp->address;

	    /* Link to the top of the rp_grp_chain */
	    grp->rpnext = rp_grp->grplink;
	    rp_grp->grplink = grp;
	    if (grp->rpnext)
		grp->rpnext->rpprev = grp;
	} else {
	    rp = grp->active_rp_grp->rp->rpentry;
	}
    }

    mrt_wc = mrt_pmbr = NULL;

    if (flags & MRTF_WC) {
	/* Setup the (*,G) routing entry */
	mrt_wc = create_mrtentry(NULL, grp, MRTF_WC);
	if (!mrt_wc) {
	    if (!grp->mrtlink)
		/* New created grpentry. Delete it. */
		delete_grpentry(grp);

	    return NULL;
	}

	if (mrt_wc->flags & MRTF_NEW) {
	    mrt_pmbr = rp->mrtlink;

	    /* Copy the oif list from the (*,*,RP) entry */
	    if (mrt_pmbr)
		VOIF_COPY(mrt_pmbr, mrt_wc);

	    mrt_wc->incoming = rp->incoming;
	    mrt_wc->upstream = rp->upstream;
	    mrt_wc->metric   = rp->metric;
	    mrt_wc->preference = rp->preference;
	    move_kernel_cache(mrt_wc, 0);
#ifdef RSRR
	    rsrr_cache_bring_up(mrt_wc);
#endif /* RSRR */
	}

	if (!(flags & MRTF_SG))
	    return mrt_wc;
    }

    if (flags & MRTF_SG) {
	/* Setup the (S,G) routing entry */
	src = create_srcentry(source);
	if (!src) {
	    /* TODO: XXX: The MRTF_NEW flag check may be misleading?? check */
	    if ((!grp->grp_route || (grp->grp_route && (grp->grp_route->flags & MRTF_NEW)))
		&& !grp->mrtlink) {
		/* New created grpentry. Delete it. */
		delete_grpentry(grp);
	    }

	    return NULL;
	}

	mrt = create_mrtentry(src, grp, MRTF_SG);
	if (!mrt) {
	    if ((!grp->grp_route
		 || (grp->grp_route && (grp->grp_route->flags & MRTF_NEW)))
		&& !grp->mrtlink) {
		/* New created grpentry. Delete it. */
		delete_grpentry(grp);
	    }

	    if (!src->mrtlink)
		/* New created srcentry. Delete it. */
		delete_srcentry(src);

	    return NULL;
	}

	if (mrt->flags & MRTF_NEW) {
	    mrt2 = grp->grp_route;
	    if (!mrt2)
		mrt2 = rp->mrtlink;

	    /* Copy the oif list from the existing (*,G) or (*,*,RP) entry */
	    if (mrt2) {
		VOIF_COPY(mrt2, mrt);
		if (flags & MRTF_RP) {
		    /* ~(S,G) prune entry */
		    mrt->incoming    = mrt2->incoming;
		    mrt->upstream    = mrt2->upstream;
		    mrt->metric      = mrt2->metric;
		    mrt->preference  = mrt2->preference;
		    mrt->flags      |= MRTF_RP;
		}
	    }

	    if (!(mrt->flags & MRTF_RP)) {
		mrt->incoming   = src->incoming;
		mrt->upstream   = src->upstream;
		mrt->metric     = src->metric;
		mrt->preference = src->preference;
	    }
	    move_kernel_cache(mrt, 0);
#ifdef RSRR
	    rsrr_cache_bring_up(mrt);
#endif /* RSRR */
	}

	return mrt;
    }

    if (flags & MRTF_PMBR) {
	/* Get/return the (*,*,RP) routing entry */
	if (group != INADDR_ANY_N)
	    rp = rp_match(group);
	else if (source != INADDR_ANY_N)
	    rp = rp_find(source);
	else
	    return NULL; /* source == group == INADDR_ANY */

	if (!rp)
	    return NULL;

	if (rp->mrtlink)
	    return rp->mrtlink;

	mrt = create_mrtentry(rp, NULL, MRTF_PMBR);
	if (!mrt)
	    return NULL;

	mrt->incoming   = rp->incoming;
	mrt->upstream   = rp->upstream;
	mrt->metric     = rp->metric;
	mrt->preference = rp->preference;

	return mrt;
    }

    return NULL;
}


void delete_srcentry(srcentry_t *src)
{
    mrtentry_t *node;
    mrtentry_t *next;

    if (!src)
	return;

    /* TODO: XXX: the first entry is unused and always there */
    src->prev->next =  src->next;
    if (src->next)
	src->next->prev = src->prev;

    for (node = src->mrtlink; node; node = next) {
	next = node->srcnext;
	if (node->flags & MRTF_KERNEL_CACHE)
	    /* Delete the kernel cache first */
	    delete_mrtentry_all_kernel_cache(node);

	if (node->grpprev) {
	    node->grpprev->grpnext = node->grpnext;
	} else {
	    node->group->mrtlink = node->grpnext;
	    if (!node->grpnext && !node->group->grp_route)
		/* Delete the group entry if it has no (*,G) routing entry */
		delete_grpentry(node->group);
	}

	if (node->grpnext)
	    node->grpnext->grpprev = node->grpprev;

	FREE_MRTENTRY(node);
    }

    free(src);
}


void delete_grpentry(grpentry_t *grp)
{
    mrtentry_t *node;
    mrtentry_t *next;

    if (!grp)
	return;

    /* TODO: XXX: the first entry is unused and always there */
    grp->prev->next = grp->next;
    if (grp->next)
	grp->next->prev = grp->prev;

    if (grp->grp_route) {
	if (grp->grp_route->flags & MRTF_KERNEL_CACHE)
	    delete_mrtentry_all_kernel_cache(grp->grp_route);
	FREE_MRTENTRY(grp->grp_route);
    }

    /* Delete from the rp_grp_entry chain */
    if (grp->active_rp_grp) {
	if (grp->rpnext)
	    grp->rpnext->rpprev = grp->rpprev;

	if (grp->rpprev)
	    grp->rpprev->rpnext = grp->rpnext;
	else
	    grp->active_rp_grp->grplink = grp->rpnext;
    }

    for (node = grp->mrtlink; node; node = next) {
	next = node->grpnext;
	if (node->flags & MRTF_KERNEL_CACHE)
	    /* Delete the kernel cache first */
	    delete_mrtentry_all_kernel_cache(node);

	if (node->srcprev) {
	    node->srcprev->srcnext = node->srcnext;
	} else {
	    node->source->mrtlink = node->srcnext;
	    if (!node->srcnext) {
		/* Delete the srcentry if this was the last routing entry */
		delete_srcentry(node->source);
	    }
	}

	if (node->srcnext)
	    node->srcnext->srcprev = node->srcprev;

	FREE_MRTENTRY(node);
    }

    free(grp);
}


void delete_mrtentry(mrtentry_t *mrt)
{
    grpentry_t *grp;
    mrtentry_t *mrt_wc;
    mrtentry_t *mrt_rp;

    if (!mrt)
	return;

    /* Delete the kernel cache first */
    if (mrt->flags & MRTF_KERNEL_CACHE)
	delete_mrtentry_all_kernel_cache(mrt);

#ifdef RSRR
    /* Tell the reservation daemon */
    rsrr_cache_clean(mrt);
#endif /* RSRR */

    if (mrt->flags & MRTF_PMBR) {
	/* (*,*,RP) mrtentry */
	mrt->source->mrtlink = NULL;
    } else if (mrt->flags & MRTF_SG) {
	/* (S,G) mrtentry */

	/* Delete from the grpentry MRT chain */
	if (mrt->grpprev) {
	    mrt->grpprev->grpnext = mrt->grpnext;
	} else {
	    mrt->group->mrtlink = mrt->grpnext;
	    if (!mrt->grpnext) {
		/* All (S,G) MRT entries are gone. Allow creating (*,G) MFC entries. */
		mrt_rp = mrt->group->active_rp_grp->rp->rpentry->mrtlink;
		mrt_wc = mrt->group->grp_route;
		if (mrt_rp)
		    mrt_rp->flags &= ~MRTF_MFC_CLONE_SG;

		if (mrt_wc)
		    mrt_wc->flags &= ~MRTF_MFC_CLONE_SG;
		else
		    /* Delete the group entry if it has no (*,G) routing entry */
		    delete_grpentry(mrt->group);
	    }
	}

	if (mrt->grpnext)
	    mrt->grpnext->grpprev = mrt->grpprev;

	/* Delete from the srcentry MRT chain */
	if (mrt->srcprev) {
	    mrt->srcprev->srcnext = mrt->srcnext;
	} else {
	    mrt->source->mrtlink = mrt->srcnext;
	    if (!mrt->srcnext)
		/* Delete the srcentry if this was the last routing entry */
		delete_srcentry(mrt->source);
	}

	if (mrt->srcnext)
	    mrt->srcnext->srcprev = mrt->srcprev;
    } else {
	/* This mrtentry should be (*,G) */
	grp	       = mrt->group;
	grp->grp_route = NULL;

	/* Delete the group entry if it has no (S,G) entries */
	if (!grp->mrtlink)
	    delete_grpentry(grp);
    }

    FREE_MRTENTRY(mrt);
}


static int search_srclist(uint32_t source, srcentry_t **found)
{
    srcentry_t *prev, *node;
    uint32_t source_h = ntohl(source);

    for (prev = srclist, node = prev->next; node; prev = node, node = node->next) {
	/* The srclist is ordered with the smallest addresses first.
	 * The first entry is not used. */
	if (ntohl(node->address) < source_h)
	    continue;

	if (node->address == source) {
	    *found = node;
	    return TRUE;
	}
	break;
    }

    *found = prev;   /* The insertion point is between s_prev and s */

    return FALSE;
}


static int search_grplist(uint32_t group, grpentry_t **found)
{
    grpentry_t *prev, *node;
    uint32_t group_h = ntohl(group);

    for (prev = grplist, node = prev->next; node; prev = node, node = node->next) {
	/* The grplist is ordered with the smallest address first.
	 * The first entry is not used. */
	if (ntohl(node->group) < group_h)
	    continue;

	if (node->group == group) {
	    *found = node;
	    return TRUE;
	}
	break;
    }

    *found = prev;	 /* The insertion point is between prev and g */

    return FALSE;
}


static srcentry_t *create_srcentry(uint32_t source)
{
    srcentry_t *node;
    srcentry_t *prev;

    if (search_srclist(source, &prev) == TRUE)
	return prev;

    node = calloc(1, sizeof(srcentry_t));
    if (!node) {
	logit(LOG_WARNING, 0, "Memory allocation error for srcentry %s",
	      inet_fmt(source, s1, sizeof(s1)));
	return NULL;
    }

    node->address = source;
    /* Free the memory if there is error getting the iif and
     * the next hop (upstream) router. */
    if (set_incoming(node, PIM_IIF_SOURCE) == FALSE) {
	free(node);
	return NULL;
    }

    RESET_TIMER(node->timer);
    node->mrtlink = NULL;
    node->cand_rp = NULL;
    node->next	  = prev->next;
    prev->next    = node;
    node->prev    = prev;
    if (node->next)
	node->next->prev = node;

    IF_DEBUG(DEBUG_MFC) {
	logit(LOG_DEBUG, 0, "create source entry, source %s",
	      inet_fmt(source, s1, sizeof(s1)));
    }

    return node;
}


static grpentry_t *create_grpentry(uint32_t group)
{
    grpentry_t *node;
    grpentry_t *prev;

    /* If already exists, return it.  Otheriwse search_grplist() returns the
     * insertion point in prev. */
    if (search_grplist(group, &prev) == TRUE)
	return prev;

    node = calloc(1, sizeof(grpentry_t));
    if (!node) {
	logit(LOG_WARNING, 0, "Memory allocation error for grpentry %s",
	      inet_fmt(group, s1, sizeof(s1)));
	return NULL;
    }

    /*
     * TODO: XXX: Note that this is NOT a (*,G) routing entry, but simply
     * a group entry, probably used to search the routing table (to find
     * (S,G) entries for example.)
     * To become (*,G) routing entry, we must setup node->grp_route
     */
    node->group		= group;
    node->rpaddr	= INADDR_ANY_N;
    node->mrtlink	= NULL;
    node->active_rp_grp	= NULL;
    node->grp_route	= NULL;
    node->rpnext	= NULL;
    node->rpprev	= NULL;

    /* Now it is safe to include the new group entry */
    node->next		= prev->next;
    prev->next		= node;
    node->prev		= prev;
    if (node->next)
	node->next->prev = node;

    IF_DEBUG(DEBUG_MFC) {
	logit(LOG_DEBUG, 0, "create group entry, group %s", inet_fmt(group, s1, sizeof(s1)));
    }

    return node;
}


/*
 * Return TRUE if the entry is found and then *found is set to point to
 * that entry. Otherwise return FALSE and *found points the previous
 * entry, or NULL if first in the chain.
 */
static int search_srcmrtlink(srcentry_t *src, uint32_t group, mrtentry_t **found)
{
    mrtentry_t *node;
    mrtentry_t *prev = NULL;
    uint32_t group_h = ntohl(group);

    for (node = src->mrtlink; node; prev = node, node = node->srcnext) {
	/* The entries are ordered with the smaller group address first.
	 * The addresses are in network order. */
	if (ntohl(node->group->group) < group_h)
	    continue;

	if (node->group->group == group) {
	    *found = node;
	    return TRUE;
	}

	break;
    }

    *found = prev;

    return FALSE;
}


/*
 * Return TRUE if the entry is found and then *found is set to point to
 * that entry.  Otherwise return FALSE and *found points the previous
 * entry, or NULL if first in the chain.
 */
static int search_grpmrtlink(grpentry_t *grp, uint32_t source, mrtentry_t **found)
{
    mrtentry_t *node;
    mrtentry_t *prev = NULL;
    uint32_t source_h = ntohl(source);

    for (node = grp->mrtlink; node; prev = node, node = node->grpnext) {
	/* The entries are ordered with the smaller source address first.
	 * The addresses are in network order. */
	if (ntohl(node->source->address) < source_h)
	    continue;

	if (source == node->source->address) {
	    *found = node;
	    return TRUE;
	}

	break;
    }

    *found = prev;

    return FALSE;
}


static void insert_srcmrtlink(mrtentry_t *node, mrtentry_t *prev, srcentry_t *src)
{
    if (!prev) {
	/* Has to be insert as the head entry for this source */
	node->srcnext = src->mrtlink;
	node->srcprev = NULL;
	src->mrtlink  = node;
    } else {
	/* Insert right after the prev */
	node->srcnext = prev->srcnext;
	node->srcprev = prev;
	prev->srcnext = node;
    }

    if (node->srcnext)
	node->srcnext->srcprev = node;
}


static void insert_grpmrtlink(mrtentry_t *node, mrtentry_t *prev, grpentry_t *grp)
{
    if (!prev) {
	/* Has to be insert as the head entry for this group */
	node->grpnext = grp->mrtlink;
	node->grpprev = NULL;
	grp->mrtlink  = node;
    } else {
	/* Insert right after the prev */
	node->grpnext = prev->grpnext;
	node->grpprev = prev;
	prev->grpnext = node;
    }

    if (node->grpnext)
	node->grpnext->grpprev = node;
}


static mrtentry_t *alloc_mrtentry(srcentry_t *src, grpentry_t *grp)
{
    mrtentry_t *mrt;
    uint16_t i, *timer;
    uint8_t  vif_numbers;

    mrt = calloc(1, sizeof(mrtentry_t));
    if (!mrt) {
	logit(LOG_WARNING, 0, "alloc_mrtentry(): out of memory");
	return NULL;
    }

    /*
     * grpnext, grpprev, srcnext, srcprev will be setup when we link the
     * mrtentry to the source and group chains
     */
    mrt->source  = src;
    mrt->group   = grp;
    mrt->incoming = NO_VIF;
    VIFM_CLRALL(mrt->joined_oifs);
    VIFM_CLRALL(mrt->leaves);
    VIFM_CLRALL(mrt->pruned_oifs);
    VIFM_CLRALL(mrt->asserted_oifs);
    VIFM_CLRALL(mrt->oifs);
    mrt->upstream = NULL;
    mrt->metric = 0;
    mrt->preference = 0;
    mrt->pmbr_addr = INADDR_ANY_N;
#ifdef RSRR
    mrt->rsrr_cache = NULL;
#endif /* RSRR */

    /* XXX: TODO: if we are short in memory, we can reserve as few as possible
     * space for vif timers (per group and/or routing entry), but then everytime
     * when a new interfaces is configured, the router will be restarted and
     * will delete the whole routing table. The "memory is cheap" solution is
     * to reserve timer space for all potential vifs in advance and then no
     * need to delete the routing table and disturb the forwarding.
     */
#ifdef SAVE_MEMORY
    mrt->vif_timers	    = (uint16_t *)calloc(1, sizeof(uint16_t) * numvifs);
    mrt->vif_deletion_delay = (uint16_t *)calloc(1, sizeof(uint16_t) * numvifs);
    vif_numbers = numvifs;
#else
    mrt->vif_timers	    = (uint16_t *)calloc(1, sizeof(uint16_t) * total_interfaces);
    mrt->vif_deletion_delay = (uint16_t *)calloc(1, sizeof(uint16_t) * total_interfaces);
    vif_numbers = total_interfaces;
#endif /* SAVE_MEMORY */
    if (!mrt->vif_timers || !mrt->vif_deletion_delay) {
	logit(LOG_WARNING, 0, "alloc_mrtentry(): out of memory");
	FREE_MRTENTRY(mrt);
	return NULL;
    }

    /* Reset the timers */
    for (i = 0, timer = mrt->vif_timers; i < vif_numbers; i++, timer++)
	RESET_TIMER(*timer);

    for (i = 0, timer = mrt->vif_deletion_delay; i < vif_numbers; i++, timer++)
	RESET_TIMER(*timer);

    mrt->flags = MRTF_NEW;
    RESET_TIMER(mrt->timer);
    RESET_TIMER(mrt->jp_timer);
    RESET_TIMER(mrt->rs_timer);
    RESET_TIMER(mrt->assert_timer);
    RESET_TIMER(mrt->assert_rate_timer);
    mrt->kernel_cache = NULL;

    return mrt;
}


static mrtentry_t *create_mrtentry(srcentry_t *src, grpentry_t *grp, uint16_t flags)
{
    mrtentry_t *node;
    mrtentry_t *grp_insert, *src_insert; /* pointers to insert */
    uint32_t source;
    uint32_t group;

    if (flags & MRTF_SG) {
	/* (S,G) entry */
	source = src->address;
	group  = grp->group;

	if (search_grpmrtlink(grp, source, &grp_insert) == TRUE)
	    return grp_insert;

	if (search_srcmrtlink(src, group, &src_insert) == TRUE) {
	    /* Hmmm, search_grpmrtlink() didn't find the entry, but
	     * search_srcmrtlink() did find it!  Shouldn't happen.  Panic! */
	    logit(LOG_ERR, 0, "MRT inconsistency for src %s and grp %s\n",
		  inet_fmt(source, s1, sizeof(s1)), inet_fmt(group, s2, sizeof(s2)));

	    /* not reached but to make lint happy */
	    return NULL;
	}

	/* Create and insert in group mrtlink and source mrtlink chains. */
	node = alloc_mrtentry(src, grp);
	if (!node)
	    return NULL;

	/* node has to be insert right after grp_insert in the grp
	 * mrtlink chain and right after src_insert in the src mrtlink
	 * chain */
	insert_grpmrtlink(node, grp_insert, grp);
	insert_srcmrtlink(node, src_insert, src);
	node->flags |= MRTF_SG;

	return node;
    }

    if (flags & MRTF_WC) {
	/* (*,G) entry */
	if (grp->grp_route)
	    return grp->grp_route;

	node = alloc_mrtentry(src, grp);
	if (!node)
	    return NULL;

	grp->grp_route = node;
	node->flags |= (MRTF_WC | MRTF_RP);

	return node;
    }

    if (flags & MRTF_PMBR) {
	/* (*,*,RP) entry */
	if (src->mrtlink)
	    return src->mrtlink;

	node = alloc_mrtentry(src, grp);
	if (!node)
	    return NULL;

	src->mrtlink = node;
	node->flags |= (MRTF_PMBR | MRTF_RP);

	return node;
    }

    return NULL;
}


/*
 * Delete all kernel cache for this mrtentry
 */
void delete_mrtentry_all_kernel_cache(mrtentry_t *mrt)
{
    kernel_cache_t *prev;
    kernel_cache_t *node;

    if (!(mrt->flags & MRTF_KERNEL_CACHE))
	return;

    /* Free all kernel_cache entries */
    for (node = mrt->kernel_cache; node; ) {
	prev = node;
	node = node->next;

	logit(LOG_DEBUG, 0, "delete_mrtentry_all_kernel_cache: SG");
	k_del_mfc(igmp_socket, prev->source, prev->group);
	free(prev);
    }
    mrt->kernel_cache = NULL;

    /* turn off the cache flag(s) */
    mrt->flags &= ~(MRTF_KERNEL_CACHE | MRTF_MFC_CLONE_SG);
}


void delete_single_kernel_cache(mrtentry_t *mrt, kernel_cache_t *node)
{
    if (!mrt || !node)
	return;

    if (!node->prev) {
	mrt->kernel_cache = node->next;
	if (!mrt->kernel_cache)
	    mrt->flags &= ~(MRTF_KERNEL_CACHE | MRTF_MFC_CLONE_SG);
    } else {
	node->prev->next = node->next;
    }

    if (node->next)
	node->next->prev = node->prev;

    IF_DEBUG(DEBUG_MFC) {
	logit(LOG_DEBUG, 0, "Deleting MFC entry for source %s and group %s",
	      inet_fmt(node->source, s1, sizeof(s1)),
	      inet_fmt(node->group, s2, sizeof(s2)));
    }

    logit(LOG_DEBUG, 0, "delete_single_kernel_cache: SG");
    k_del_mfc(igmp_socket, node->source, node->group);
    free(node);
}


void delete_single_kernel_cache_addr(mrtentry_t *mrt, uint32_t source, uint32_t group)
{
    uint32_t source_h;
    uint32_t group_h;
    kernel_cache_t *node;

    if (!mrt)
	return;

    source_h = ntohl(source);
    group_h  = ntohl(group);

    /* Find the exact (S,G) kernel_cache entry */
    for (node = mrt->kernel_cache; node; node = node->next) {
	if (ntohl(node->group) < group_h)
	    continue;
	if (ntohl(node->group) > group_h)
	    return;	/* Not found */
	if (ntohl(node->source) < source_h)
	    continue;
	if (ntohl(node->source) > source_h)
	    return;	/* Not found */

	/* Found exact match */
	break;
    }

    if (!node)
	return;

    /* Found. Delete it */
    if (!node->prev) {
	mrt->kernel_cache = node->next;
	if (!mrt->kernel_cache)
	    mrt->flags &= ~(MRTF_KERNEL_CACHE | MRTF_MFC_CLONE_SG);
    } else{
	node->prev->next = node->next;
    }

    if (node->next)
	node->next->prev = node->prev;

    IF_DEBUG(DEBUG_MFC) {
	logit(LOG_DEBUG, 0, "Deleting MFC entry for source %s and group %s",
	      inet_fmt(node->source, s1, sizeof(s1)),
	      inet_fmt(node->group, s2, sizeof(s2)));
    }

    logit(LOG_DEBUG, 0, "delete_single_kernel_cache_addr: SG");
    k_del_mfc(igmp_socket, node->source, node->group);
    free(node);
}


/*
 * Installs kernel cache for (source, group). Assumes mrt
 * is the correct entry.
 */
void add_kernel_cache(mrtentry_t *mrt, uint32_t source, uint32_t group, uint16_t flags)
{
    uint32_t source_h;
    uint32_t group_h;
    kernel_cache_t *next;
    kernel_cache_t *prev;
    kernel_cache_t *node;

    if (!mrt)
	return;

    move_kernel_cache(mrt, flags);

    if (mrt->flags & MRTF_SG) {
	/* (S,G) */
	if (mrt->flags & MRTF_KERNEL_CACHE)
	    return;

	node = (kernel_cache_t *)calloc(1, sizeof(kernel_cache_t));
	node->next = NULL;
	node->prev = NULL;
	node->source = source;
	node->group = group;
	node->sg_count.pktcnt = 0;
	node->sg_count.bytecnt = 0;
	node->sg_count.wrong_if = 0;
	mrt->kernel_cache = node;
	mrt->flags |= MRTF_KERNEL_CACHE;

	return;
    }

    source_h = ntohl(source);
    group_h  = ntohl(group);
    prev     = NULL;

    for (next = mrt->kernel_cache; next; prev = next, next = next->next) {
	if (ntohl(next->group) < group_h)
	    continue;
	if (ntohl(next->group) > group_h)
	    break;
	if (ntohl(next->source) < source_h)
	    continue;
	if (ntohl(next->source) > source_h)
	    break;

	/* Found exact match. Nothing to change. */
	return;
    }

    /* The new entry must be placed between prev and
     * next */
    node = calloc(1, sizeof(kernel_cache_t));
    if (prev)
	prev->next = node;
    else
	mrt->kernel_cache = node;

    if (next)
	next->prev = node;

    node->prev   = prev;
    node->next   = next;
    node->source = source;
    node->group  = group;
    node->sg_count.pktcnt   = 0;
    node->sg_count.bytecnt  = 0;
    node->sg_count.wrong_if = 0;

    mrt->flags |= MRTF_KERNEL_CACHE;
}

/*
 * Bring the kernel cache "UP": from the (*,*,RP) to (*,G) or (S,G)
 */
static void move_kernel_cache(mrtentry_t *mrt, uint16_t flags)
{
    kernel_cache_t *node;
    kernel_cache_t *insert_node;
    kernel_cache_t *first_node;
    kernel_cache_t *last_node;
    kernel_cache_t *prev_node;
    mrtentry_t     *mrtentry_pmbr;
    mrtentry_t     *mrtentry_rp;
    uint32_t	    group_h;
    uint32_t	    source_h;
    int		    found;

    if (!mrt)
	return;

    if (mrt->flags & MRTF_PMBR)
	return;

    if (mrt->flags & MRTF_WC) {
	/* Move the cache info from (*,*,RP) to (*,G) */
	group_h       = ntohl(mrt->group->group);
	mrtentry_pmbr = mrt->group->active_rp_grp->rp->rpentry->mrtlink;
	if (!mrtentry_pmbr)
	    return;    /* Nothing to move */

	first_node = last_node = NULL;
	for (node = mrtentry_pmbr->kernel_cache; node; node = node->next) {
	    /*
	     * The order is: (1) smaller group;
	     *               (2) smaller source within group
	     */
	    if (ntohl(node->group) < group_h)
		continue;

	    if (ntohl(node->group) != group_h)
		break;

	    /* Select the kernel_cache entries to move  */
	    if (!first_node)
		first_node = last_node = node;
	    else
		last_node = node;
	}

	if (first_node) {
	    /* Fix the old chain */
	    if (first_node->prev)
		first_node->prev->next = last_node->next;
	    else
		mrtentry_pmbr->kernel_cache = last_node->next;

	    if (last_node->next)
		last_node->next->prev = first_node->prev;

	    if (!mrtentry_pmbr->kernel_cache)
		mrtentry_pmbr->flags &= ~(MRTF_KERNEL_CACHE | MRTF_MFC_CLONE_SG);

	    /* Insert in the new place */
	    prev_node       = NULL;
	    last_node->next = NULL;
	    mrt->flags |= MRTF_KERNEL_CACHE;

	    for (node = mrt->kernel_cache; node; ) {
		if (!first_node)
		    break;  /* All entries have been inserted */

		if (ntohl(node->source) > ntohl(first_node->source)) {
		    /* Insert the entry before node */
		    insert_node = first_node;
		    first_node = first_node->next;

		    if (node->prev)
			node->prev->next = insert_node;
		    else
			mrt->kernel_cache = insert_node;

		    insert_node->prev = node->prev;
		    insert_node->next = node;
		    node->prev = insert_node;
		}
		prev_node = node;
		node = node->next;
	    }

	    if (first_node) {
		/* Place all at the end after prev_node */
		if (prev_node)
		    prev_node->next   = first_node;
		else
		    mrt->kernel_cache = first_node;

		first_node->prev = prev_node;
	    }
	}

	return;
    }

    if (mrt->flags & MRTF_SG) {
	logit(LOG_DEBUG, 0, "move_kernel_cache: SG");
	/* (S,G) entry. Move the whole group cache from (*,*,RP) to (*,G) and
	 * then get the necessary entry from (*,G).
	 * TODO: Not optimized! The particular entry is moved first to (*,G),
	 * then we have to search again (*,G) to find it and move to (S,G).
	 */
	/* TODO: XXX: No need for this? Thinking.... */
/*	move_kernel_cache(mrt->group->grp_route, flags); */
	mrtentry_rp = mrt->group->grp_route;
	if (!mrtentry_rp) {
	    mrtentry_rp = mrt->group->active_rp_grp->rp->rpentry->mrtlink;
	    if (!mrtentry_rp)
		return;
	}

	if (mrtentry_rp->incoming != mrt->incoming) {
	    /* XXX: the (*,*,RP) (or (*,G)) iif is different from the
	     * (S,G) iif. No need to move the cache, because (S,G) don't
	     * need it. After the first packet arrives on the shortest path,
	     * the correct cache entry will be created.
	     * If (flags & MFC_MOVE_FORCE) then we must move the cache.
	     * This usually happens when switching to the shortest path.
	     * The calling function will immediately call k_chg_mfc()
	     * to modify the kernel cache.
	     */
	    if (!(flags & MFC_MOVE_FORCE))
		return;
	}

	/* Find the exact entry */
	source_h = ntohl(mrt->source->address);
	group_h  = ntohl(mrt->group->group);
	found = FALSE;
	for (node = mrtentry_rp->kernel_cache; node; node = node->next) {
	    if (ntohl(node->group) < group_h)
		continue;

	    if (ntohl(node->group) > group_h)
		break;

	    if (ntohl(node->source) < source_h)
		continue;

	    if (ntohl(node->source) > source_h)
		break;

	    /* We found it! */
	    if (node->prev)
		node->prev->next = node->next;
	    else
		mrtentry_rp->kernel_cache = node->next;

	    if (node->next)
		node->next->prev = node->prev;

	    found = TRUE;

	    break;
	}

	if (found == TRUE) {
	    if (!mrtentry_rp->kernel_cache)
		mrtentry_rp->flags &= ~(MRTF_KERNEL_CACHE | MRTF_MFC_CLONE_SG);

	    if (mrt->kernel_cache)
		free(mrt->kernel_cache);

	    mrt->flags	      |= MRTF_KERNEL_CACHE;
	    mrt->kernel_cache  = node;

	    node->prev = NULL;
	    node->next = NULL;
	}
    }
}

/**
 * Local Variables:
 *  version-control: t
 *  indent-tabs-mode: t
 *  c-file-style: "ellemtel"
 *  c-basic-offset: 4
 * End:
 */
