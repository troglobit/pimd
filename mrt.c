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
static srcentry_t *create_srcentry  (u_int32 source);
static int        search_srclist    (u_int32 source, srcentry_t **sourceEntry);
static int        search_srcmrtlink (srcentry_t *srcentry_ptr, u_int32 group, mrtentry_t **mrtPtr);
static void       insert_srcmrtlink (mrtentry_t *elementPtr, mrtentry_t *insertPtr, srcentry_t *srcListPtr);
static grpentry_t *create_grpentry  (u_int32 group);
static int        search_grplist    (u_int32 group, grpentry_t **groupEntry);
static int        search_grpmrtlink (grpentry_t *grpentry_ptr, u_int32 source, mrtentry_t **mrtPtr);
static void       insert_grpmrtlink (mrtentry_t *elementPtr, mrtentry_t *insertPtr, grpentry_t *grpListPtr);
static mrtentry_t *alloc_mrtentry   (srcentry_t *srcentry_ptr, grpentry_t *grpentry_ptr);
static mrtentry_t *create_mrtentry  (srcentry_t *srcentry_ptr, grpentry_t *grpentry_ptr, u_int16 flags);
static void       move_kernel_cache (mrtentry_t *mrtentry_ptr, u_int16 flags);

void init_pim_mrt(void)
{
    /* TODO: delete any existing routing table */

    /* Initialize the source list */
    /* The first entry has address 'INADDR_ANY' and is not used */
    /* The order is the smallest address first. */
    srclist             = (srcentry_t *)calloc(1, sizeof(srcentry_t));
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
    grplist             = (grpentry_t *)calloc(1, sizeof(grpentry_t));
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


grpentry_t *find_group(u_int32 group)
{
    grpentry_t *grpentry_ptr;

    if (!IN_MULTICAST(ntohl(group)))
        return NULL;

    if (search_grplist(group, &grpentry_ptr) == TRUE) {
        /* Group found! */
        return grpentry_ptr;
    }
    return NULL;
}


srcentry_t *find_source(u_int32 source)
{
    srcentry_t *srcentry_ptr;

    if (!inet_valid_host(source))
        return NULL;

    if (search_srclist(source, &srcentry_ptr) == TRUE) {
        /* Source found! */
        return srcentry_ptr;
    }
    return NULL;
}


mrtentry_t *find_route(u_int32 source, u_int32 group, u_int16 flags, char create)
{
    srcentry_t *srcentry_ptr         = NULL;
    grpentry_t *grpentry_ptr         = NULL;
    mrtentry_t *mrtentry_ptr         = NULL;
    mrtentry_t *mrtentry_ptr_wc      = NULL;
    mrtentry_t *mrtentry_ptr_pmbr    = NULL;
    mrtentry_t *mrtentry_ptr_2       = NULL;
    rpentry_t  *rpentry_ptr          = NULL;
    rp_grp_entry_t *rp_grp_entry_ptr = NULL;

    if (flags & (MRTF_SG | MRTF_WC)) {
        if (!IN_MULTICAST(ntohl(group)))
            return NULL;
    }

    if (flags & MRTF_SG) {
        if (!inet_valid_host(source))
            return NULL;
    }

    if (create == DONT_CREATE) {
        if (flags & (MRTF_SG | MRTF_WC)) {
            if (search_grplist(group, &grpentry_ptr) == FALSE) {
                /* Group not found. Return the (*,*,RP) entry */
                if (flags & MRTF_PMBR) {
                    rpentry_ptr = rp_match(group);
                    if (rpentry_ptr)
                        return rpentry_ptr->mrtlink;
                }

                return NULL;
            }

            /* Search for the source */
            if (flags & MRTF_SG) {
                if (search_grpmrtlink(grpentry_ptr, source, &mrtentry_ptr) == TRUE) {
                    /* Exact (S,G) entry found */
                    return mrtentry_ptr;
                }
            }

            /* No (S,G) entry. Return the (*,G) entry (if exist) */
            if ((flags & MRTF_WC) && grpentry_ptr->grp_route)
                return grpentry_ptr->grp_route;
        }

        /* Return the (*,*,RP) entry */
        if (flags & MRTF_PMBR) {
            rpentry_ptr = NULL;
            if (group != INADDR_ANY_N)
                rpentry_ptr = rp_match(group);
            else if (source != INADDR_ANY_N)
                rpentry_ptr = rp_find(source);

            if (rpentry_ptr)
                return rpentry_ptr->mrtlink;
        }

        return NULL;
    }


    /* Creation allowed */

    if (flags & (MRTF_SG | MRTF_WC)) {
        grpentry_ptr = create_grpentry(group);
        if (!grpentry_ptr)
            return NULL;

        if (!grpentry_ptr->active_rp_grp) {
            rp_grp_entry_ptr = rp_grp_match(group);
            if (!rp_grp_entry_ptr) {
                if (!grpentry_ptr->mrtlink && !grpentry_ptr->grp_route) {
                    /* New created grpentry. Delete it. */
                    delete_grpentry(grpentry_ptr);
                }

                return NULL;
            }

            rpentry_ptr = rp_grp_entry_ptr->rp->rpentry;
            grpentry_ptr->active_rp_grp = rp_grp_entry_ptr;
            grpentry_ptr->rpaddr = rpentry_ptr->address;

            /* Link to the top of the rp_grp_chain */
            grpentry_ptr->rpnext = rp_grp_entry_ptr->grplink;
            rp_grp_entry_ptr->grplink = grpentry_ptr;
            if (grpentry_ptr->rpnext)
                grpentry_ptr->rpnext->rpprev = grpentry_ptr;
        }
        else {
            rpentry_ptr = grpentry_ptr->active_rp_grp->rp->rpentry;
	}
    }

    mrtentry_ptr_wc = mrtentry_ptr_pmbr = NULL;

    if (flags & MRTF_WC) {
        /* Setup the (*,G) routing entry */
        mrtentry_ptr_wc = create_mrtentry(NULL, grpentry_ptr, MRTF_WC);
        if (!mrtentry_ptr_wc) {
            if (!grpentry_ptr->mrtlink) {
                /* New created grpentry. Delete it. */
                delete_grpentry(grpentry_ptr);
            }

            return NULL;
        }

        if (mrtentry_ptr_wc->flags & MRTF_NEW) {
            mrtentry_ptr_pmbr = rpentry_ptr->mrtlink;
            /* Copy the oif list from the (*,*,RP) entry */
            if (mrtentry_ptr_pmbr) {
                VOIF_COPY(mrtentry_ptr_pmbr, mrtentry_ptr_wc);
            }
            mrtentry_ptr_wc->incoming = rpentry_ptr->incoming;
            mrtentry_ptr_wc->upstream = rpentry_ptr->upstream;
            mrtentry_ptr_wc->metric   = rpentry_ptr->metric;
            mrtentry_ptr_wc->preference = rpentry_ptr->preference;
            move_kernel_cache(mrtentry_ptr_wc, 0);
#ifdef RSRR
            rsrr_cache_bring_up(mrtentry_ptr_wc);
#endif /* RSRR */
        }

        if (!(flags & MRTF_SG)) {
            return mrtentry_ptr_wc;
        }
    }

    if (flags & MRTF_SG) {
        /* Setup the (S,G) routing entry */
        srcentry_ptr = create_srcentry(source);
        if (!srcentry_ptr) {
            /* TODO: XXX: The MRTF_NEW flag check may be misleading?? check */
            if ((!grpentry_ptr->grp_route || (grpentry_ptr->grp_route && (grpentry_ptr->grp_route->flags & MRTF_NEW)))
                && !grpentry_ptr->mrtlink) {
                /* New created grpentry. Delete it. */
                delete_grpentry(grpentry_ptr);
            }

            return NULL;
        }

        mrtentry_ptr = create_mrtentry(srcentry_ptr, grpentry_ptr, MRTF_SG);
        if (!mrtentry_ptr) {
            if ((!grpentry_ptr->grp_route
		 || (grpentry_ptr->grp_route && (grpentry_ptr->grp_route->flags & MRTF_NEW)))
                && !grpentry_ptr->mrtlink) {
                /* New created grpentry. Delete it. */
                delete_grpentry(grpentry_ptr);
            }
            if (!srcentry_ptr->mrtlink) {
                /* New created srcentry. Delete it. */
                delete_srcentry(srcentry_ptr);
            }

            return NULL;
        }

        if (mrtentry_ptr->flags & MRTF_NEW) {
	    mrtentry_ptr_2 = grpentry_ptr->grp_route;
            if (!mrtentry_ptr_2) {
                mrtentry_ptr_2 = rpentry_ptr->mrtlink;
            }
            /* Copy the oif list from the existing (*,G) or (*,*,RP) entry */
            if (mrtentry_ptr_2) {
                VOIF_COPY(mrtentry_ptr_2, mrtentry_ptr);
                if (flags & MRTF_RP) {
                    /* ~(S,G) prune entry */
                    mrtentry_ptr->incoming = mrtentry_ptr_2->incoming;
                    mrtentry_ptr->upstream = mrtentry_ptr_2->upstream;
                    mrtentry_ptr->metric   = mrtentry_ptr_2->metric;
                    mrtentry_ptr->preference = mrtentry_ptr_2->preference;
                    mrtentry_ptr->flags |= MRTF_RP;
                }
            }
            if (!(mrtentry_ptr->flags & MRTF_RP)) {
                mrtentry_ptr->incoming = srcentry_ptr->incoming;
                mrtentry_ptr->upstream = srcentry_ptr->upstream;
                mrtentry_ptr->metric   = srcentry_ptr->metric;
                mrtentry_ptr->preference = srcentry_ptr->preference;
            }
            move_kernel_cache(mrtentry_ptr, 0);
#ifdef RSRR
            rsrr_cache_bring_up(mrtentry_ptr);
#endif /* RSRR */
        }

        return mrtentry_ptr;
    }

    if (flags & MRTF_PMBR) {
        /* Get/return the (*,*,RP) routing entry */
        if (group != INADDR_ANY_N) {
            rpentry_ptr = rp_match(group);
	} else if (source != INADDR_ANY_N) {
            rpentry_ptr = rp_find(source);
            if (!rpentry_ptr)
                return NULL;
        } else {
            return NULL; /* source == group == INADDR_ANY */
	}

        if (rpentry_ptr->mrtlink)
            return rpentry_ptr->mrtlink;

        mrtentry_ptr = create_mrtentry(rpentry_ptr, NULL, MRTF_PMBR);
        if (!mrtentry_ptr)
            return NULL;

        mrtentry_ptr->incoming = rpentry_ptr->incoming;
        mrtentry_ptr->upstream = rpentry_ptr->upstream;
        mrtentry_ptr->metric   = rpentry_ptr->metric;
        mrtentry_ptr->preference = rpentry_ptr->preference;

        return mrtentry_ptr;
    }

    return NULL;
}


void delete_srcentry(srcentry_t *srcentry_ptr)
{
    mrtentry_t *ptr;
    mrtentry_t *next;

    if (!srcentry_ptr)
        return;

    /* TODO: XXX: the first entry is unused and always there */
    srcentry_ptr->prev->next =  srcentry_ptr->next;
    if (srcentry_ptr->next)
        srcentry_ptr->next->prev = srcentry_ptr->prev;

    for (ptr = srcentry_ptr->mrtlink; ptr; ptr = next) {
        next = ptr->srcnext;
        if (ptr->flags & MRTF_KERNEL_CACHE)
            /* Delete the kernel cache first */
            delete_mrtentry_all_kernel_cache(ptr);

        if (ptr->grpprev) {
            ptr->grpprev->grpnext = ptr->grpnext;
	} else {
            ptr->group->mrtlink = ptr->grpnext;
            if (!ptr->grpnext && !ptr->group->grp_route) {
                /* Delete the group entry if it has no (*,G) routing entry */
                delete_grpentry(ptr->group);
            }
        }

        if (ptr->grpnext)
            ptr->grpnext->grpprev = ptr->grpprev;

        FREE_MRTENTRY(ptr);
    }

    free((char *)srcentry_ptr);
}


void delete_grpentry(grpentry_t *grpentry_ptr)
{
    mrtentry_t *ptr;
    mrtentry_t *next;

    if (!grpentry_ptr)
        return;

    /* TODO: XXX: the first entry is unused and always there */
    grpentry_ptr->prev->next = grpentry_ptr->next;
    if (grpentry_ptr->next)
        grpentry_ptr->next->prev = grpentry_ptr->prev;

    if (grpentry_ptr->grp_route) {
        if (grpentry_ptr->grp_route->flags & MRTF_KERNEL_CACHE)
            delete_mrtentry_all_kernel_cache(grpentry_ptr->grp_route);
        FREE_MRTENTRY(grpentry_ptr->grp_route);
    }

    /* Delete from the rp_grp_entry chain */
    if (grpentry_ptr->active_rp_grp != NULL) {
        if (grpentry_ptr->rpnext != NULL)
            grpentry_ptr->rpnext->rpprev = grpentry_ptr->rpprev;

        if (grpentry_ptr->rpprev != NULL)
            grpentry_ptr->rpprev->rpnext = grpentry_ptr->rpnext;
        else
            grpentry_ptr->active_rp_grp->grplink = grpentry_ptr->rpnext;
    }

    for (ptr = grpentry_ptr->mrtlink; ptr; ptr = next) {
        next = ptr->grpnext;
        if (ptr->flags & MRTF_KERNEL_CACHE)
            /* Delete the kernel cache first */
            delete_mrtentry_all_kernel_cache(ptr);

        if (ptr->srcprev) {
            ptr->srcprev->srcnext = ptr->srcnext;
        } else {
            ptr->source->mrtlink = ptr->srcnext;
            if (!ptr->srcnext) {
                /* Delete the srcentry if this was the last routing entry */
                delete_srcentry(ptr->source);
            }
        }

        if (ptr->srcnext)
            ptr->srcnext->srcprev = ptr->srcprev;

        FREE_MRTENTRY(ptr);
    }

    free((char *)grpentry_ptr);
}


void delete_mrtentry(mrtentry_t *mrtentry_ptr)
{
    grpentry_t *grpentry_ptr;
    mrtentry_t *mrtentry_wc;
    mrtentry_t *mrtentry_rp;

    if (mrtentry_ptr == NULL)
        return;

    /* Delete the kernel cache first */
    if (mrtentry_ptr->flags & MRTF_KERNEL_CACHE)
        delete_mrtentry_all_kernel_cache(mrtentry_ptr);

#ifdef RSRR
    /* Tell the reservation daemon */
    rsrr_cache_clean(mrtentry_ptr);
#endif /* RSRR */

    if (mrtentry_ptr->flags & MRTF_PMBR) {
        /* (*,*,RP) mrtentry */
        mrtentry_ptr->source->mrtlink = NULL;
    } else if (mrtentry_ptr->flags & MRTF_SG) {
        /* (S,G) mrtentry */

        /* Delete from the grpentry MRT chain */
        if (mrtentry_ptr->grpprev != NULL) {
            mrtentry_ptr->grpprev->grpnext = mrtentry_ptr->grpnext;
	} else {
            mrtentry_ptr->group->mrtlink = mrtentry_ptr->grpnext;
            if (mrtentry_ptr->grpnext == NULL) {
                /* All (S,G) MRT entries are gone. Allow creating (*,G) MFC
                 * entries.
                 */
                mrtentry_rp = mrtentry_ptr->group->active_rp_grp->rp->rpentry->mrtlink;
                mrtentry_wc = mrtentry_ptr->group->grp_route;
                if (mrtentry_rp != NULL)
                    mrtentry_rp->flags &= ~MRTF_MFC_CLONE_SG;

                if (mrtentry_wc != NULL) {
                    mrtentry_wc->flags &= ~MRTF_MFC_CLONE_SG;
                } else {
                    /* Delete the group entry if it has no (*,G)
                     * routing entry
                     */
                    delete_grpentry(mrtentry_ptr->group);
                }
            }
        }
        if (mrtentry_ptr->grpnext != NULL)
            mrtentry_ptr->grpnext->grpprev = mrtentry_ptr->grpprev;

        /* Delete from the srcentry MRT chain */
        if (mrtentry_ptr->srcprev != NULL) {
            mrtentry_ptr->srcprev->srcnext = mrtentry_ptr->srcnext;
        } else {
            mrtentry_ptr->source->mrtlink = mrtentry_ptr->srcnext;
            if (mrtentry_ptr->srcnext == NULL) {
                /* Delete the srcentry if this was the last routing entry */
                delete_srcentry(mrtentry_ptr->source);
            }
        }

        if (mrtentry_ptr->srcnext != NULL)
            mrtentry_ptr->srcnext->srcprev = mrtentry_ptr->srcprev;
    } else {
        /* This mrtentry should be (*,G) */
        grpentry_ptr = mrtentry_ptr->group;
        grpentry_ptr->grp_route = NULL;

        if (grpentry_ptr->mrtlink == NULL)
            /* Delete the group entry if it has no (S,G) entries */
            delete_grpentry(grpentry_ptr);
    }

    FREE_MRTENTRY(mrtentry_ptr);
}


static int search_srclist(u_int32 source, srcentry_t **sourceEntry)
{
    srcentry_t *s_prev,*s;
    u_int32 source_h = ntohl(source);

    for (s_prev = srclist, s = s_prev->next; s != NULL; s_prev = s, s = s->next) {
        /* The srclist is ordered with the smallest addresses first.
         * The first entry is not used.
         */
        if (ntohl(s->address) < source_h)
            continue;

        if (s->address == source) {
            *sourceEntry = s;
            return TRUE;
        }
        break;
    }
    *sourceEntry = s_prev;   /* The insertion point is between s_prev and s */

    return FALSE;
}


static int search_grplist(u_int32 group, grpentry_t **groupEntry)
{
    grpentry_t *g_prev, *g;
    u_int32 group_h = ntohl(group);

    for (g_prev = grplist, g = g_prev->next; g != NULL; g_prev = g, g = g->next) {
        /* The grplist is ordered with the smallest address first.
         * The first entry is not used.
         */
        if (ntohl(g->group) < group_h)
            continue;
        if (g->group == group) {
            *groupEntry = g;
            return TRUE;
        }
        break;
    }
    *groupEntry = g_prev;    /* The insertion point is between g_prev and g */

    return FALSE;
}


static srcentry_t *create_srcentry(u_int32 source)
{
    srcentry_t *srcentry_ptr;
    srcentry_t *srcentry_prev;

    if (search_srclist(source, &srcentry_prev) == TRUE)
        return srcentry_prev;

    srcentry_ptr = (srcentry_t *)calloc(1, sizeof(srcentry_t));
    if (!srcentry_ptr) {
        logit(LOG_WARNING, 0, "Memory allocation error for srcentry %s",
	      inet_fmt(source, s1, sizeof(s1)));
        return NULL;
    }

    srcentry_ptr->address = source;
    /*
     * Free the memory if there is error getting the iif and
     * the next hop (upstream) router.
     */
    if (set_incoming(srcentry_ptr, PIM_IIF_SOURCE) == FALSE) {
        free((char *)srcentry_ptr);
        return NULL;
    }

    RESET_TIMER(srcentry_ptr->timer);
    srcentry_ptr->mrtlink = NULL;
    srcentry_ptr->cand_rp = NULL;
    srcentry_ptr->next	  = srcentry_prev->next;
    srcentry_prev->next   = srcentry_ptr;
    srcentry_ptr->prev    = srcentry_prev;
    if (srcentry_ptr->next)
        srcentry_ptr->next->prev = srcentry_ptr;

    IF_DEBUG(DEBUG_MFC) {
        logit(LOG_DEBUG, 0, "create source entry, source %s",
	      inet_fmt(source, s1, sizeof(s1)));
    }

    return srcentry_ptr;
}


static grpentry_t *create_grpentry(u_int32 group)
{
    grpentry_t *grpentry_ptr;
    grpentry_t *grpentry_prev;

    /* If already exists, return it.  Otheriwse search_grplist() returns the
     * insertion point in grpentry_prev. */
    if (search_grplist(group, &grpentry_prev) == TRUE)
        return grpentry_prev;

    grpentry_ptr = (grpentry_t *)calloc(1, sizeof(grpentry_t));
    if (!grpentry_ptr) {
        logit(LOG_WARNING, 0, "Memory allocation error for grpentry %s",
	      inet_fmt(group, s1, sizeof(s1)));
        return NULL;
    }

    /*
     * TODO: XXX: Note that this is NOT a (*,G) routing entry, but simply
     * a group entry, probably used to search the routing table (to find
     * (S,G) entries for example.)
     * To become (*,G) routing entry, we must setup grpentry_ptr->grp_route
     */
    grpentry_ptr->group         = group;
    grpentry_ptr->rpaddr        = INADDR_ANY_N;
    grpentry_ptr->mrtlink       = NULL;
    grpentry_ptr->active_rp_grp	= NULL;
    grpentry_ptr->grp_route     = NULL;
    grpentry_ptr->rpnext        = NULL;
    grpentry_ptr->rpprev        = NULL;

    /* Now it is safe to include the new group entry */
    grpentry_ptr->next		= grpentry_prev->next;
    grpentry_prev->next         = grpentry_ptr;
    grpentry_ptr->prev          = grpentry_prev;
    if (grpentry_ptr->next)
        grpentry_ptr->next->prev = grpentry_ptr;

    IF_DEBUG(DEBUG_MFC) {
        logit(LOG_DEBUG, 0, "create group entry, group %s", inet_fmt(group, s1, sizeof(s1)));
    }

    return grpentry_ptr;
}


/*
 * Return TRUE if the entry is found and then *mrtPtr is set to point to that
 * entry. Otherwise return FALSE and *mrtPtr points the previous entry
 * (or NULL if first in the chain.
 */
static int search_srcmrtlink(srcentry_t *srcentry_ptr, u_int32 group, mrtentry_t **mrtPtr)
{
    mrtentry_t *mrtentry_ptr;
    mrtentry_t *m_prev = NULL;
    u_int32 group_h = ntohl(group);

    for (mrtentry_ptr = srcentry_ptr->mrtlink;
	 mrtentry_ptr != NULL;
	 m_prev = mrtentry_ptr, mrtentry_ptr = mrtentry_ptr->srcnext) {
        /* The entries are ordered with the smaller group address first.
         * The addresses are in network order.
         */
        if (ntohl(mrtentry_ptr->group->group) < group_h)
            continue;

        if (mrtentry_ptr->group->group == group) {
            *mrtPtr = mrtentry_ptr;
            return TRUE;
        }

        break;
    }

    *mrtPtr = m_prev;

    return FALSE;
}


/*
 * Return TRUE if the entry is found and then *mrtPtr is set to point to that
 * entry. Otherwise return FALSE and *mrtPtr points the previous entry
 * (or NULL if first in the chain.
 */
static int search_grpmrtlink(grpentry_t *grpentry_ptr, u_int32 source, mrtentry_t **mrtPtr)
{
    mrtentry_t *mrtentry_ptr;
    mrtentry_t *m_prev = NULL;
    u_int32 source_h = ntohl(source);

    for (mrtentry_ptr = grpentry_ptr->mrtlink;
         mrtentry_ptr != NULL;
         m_prev = mrtentry_ptr, mrtentry_ptr = mrtentry_ptr->grpnext) {
        /* The entries are ordered with the smaller source address first.
         * The addresses are in network order.
         */
        if (ntohl(mrtentry_ptr->source->address) < source_h)
            continue;

        if (source == mrtentry_ptr->source->address) {
            *mrtPtr = mrtentry_ptr;
            return TRUE;
        }

        break;
    }

    *mrtPtr = m_prev;

    return FALSE;
}


static void insert_srcmrtlink(mrtentry_t *mrtentry_new, mrtentry_t *mrtentry_prev, srcentry_t *srcentry_ptr)
{
    if (mrtentry_prev == NULL) {
        /* Has to be insert as the head entry for this source */
        mrtentry_new->srcnext = srcentry_ptr->mrtlink;
        mrtentry_new->srcprev = NULL;
        srcentry_ptr->mrtlink = mrtentry_new;
    } else {
        /* Insert right after the mrtentry_prev */
        mrtentry_new->srcnext = mrtentry_prev->srcnext;
        mrtentry_new->srcprev = mrtentry_prev;
        mrtentry_prev->srcnext = mrtentry_new;
    }

    if (mrtentry_new->srcnext != NULL)
        mrtentry_new->srcnext->srcprev = mrtentry_new;
}


static void insert_grpmrtlink(mrtentry_t *mrtentry_new, mrtentry_t *mrtentry_prev, grpentry_t *grpentry_ptr)
{
    if (mrtentry_prev == NULL) {
        /* Has to be insert as the head entry for this group */
        mrtentry_new->grpnext = grpentry_ptr->mrtlink;
        mrtentry_new->grpprev = NULL;
        grpentry_ptr->mrtlink = mrtentry_new;
    } else {
        /* Insert right after the mrtentry_prev */
        mrtentry_new->grpnext = mrtentry_prev->grpnext;
        mrtentry_new->grpprev = mrtentry_prev;
        mrtentry_prev->grpnext = mrtentry_new;
    }

    if (mrtentry_new->grpnext != NULL)
        mrtentry_new->grpnext->grpprev = mrtentry_new;
}


static mrtentry_t *alloc_mrtentry(srcentry_t *srcentry_ptr, grpentry_t *grpentry_ptr)
{
    mrtentry_t *mrtentry_ptr;
    u_int16 i, *i_ptr;
    u_int8  vif_numbers;

    mrtentry_ptr = (mrtentry_t *)calloc(1, sizeof(mrtentry_t));
    if (mrtentry_ptr == NULL) {
        logit(LOG_WARNING, 0, "alloc_mrtentry(): out of memory");
        return NULL;
    }

    /*
     * grpnext, grpprev, srcnext, srcprev will be setup when we link the
     * mrtentry to the source and group chains
     */
    mrtentry_ptr->source  = srcentry_ptr;
    mrtentry_ptr->group   = grpentry_ptr;
    mrtentry_ptr->incoming = NO_VIF;
    VIFM_CLRALL(mrtentry_ptr->joined_oifs);
    VIFM_CLRALL(mrtentry_ptr->leaves);
    VIFM_CLRALL(mrtentry_ptr->pruned_oifs);
    VIFM_CLRALL(mrtentry_ptr->asserted_oifs);
    VIFM_CLRALL(mrtentry_ptr->oifs);
    mrtentry_ptr->upstream = NULL;
    mrtentry_ptr->metric = 0;
    mrtentry_ptr->preference = 0;
    mrtentry_ptr->pmbr_addr = INADDR_ANY_N;
#ifdef RSRR
    mrtentry_ptr->rsrr_cache = NULL;
#endif /* RSRR */

    /* XXX: TODO: if we are short in memory, we can reserve as few as possible
     * space for vif timers (per group and/or routing entry), but then everytime
     * when a new interfaces is configured, the router will be restarted and
     * will delete the whole routing table. The "memory is cheap" solution is
     * to reserve timer space for all potential vifs in advance and then no
     * need to delete the routing table and disturb the forwarding.
     */
#ifdef SAVE_MEMORY
    mrtentry_ptr->vif_timers = (u_int16 *)calloc(1, sizeof(u_int16) * numvifs);
    mrtentry_ptr->vif_deletion_delay = (u_int16 *)calloc(1, sizeof(u_int16) * numvifs);
    vif_numbers = numvifs;
#else
    mrtentry_ptr->vif_timers = (u_int16 *)calloc(1, sizeof(u_int16) * total_interfaces);
    mrtentry_ptr->vif_deletion_delay = (u_int16 *)calloc(1, sizeof(u_int16) * total_interfaces);
    vif_numbers = total_interfaces;
#endif /* SAVE_MEMORY */
    if ((mrtentry_ptr->vif_timers == NULL) ||
        (mrtentry_ptr->vif_deletion_delay == NULL)) {
        logit(LOG_WARNING, 0, "alloc_mrtentry(): out of memory");
        FREE_MRTENTRY(mrtentry_ptr);
        return NULL;
    }

    /* Reset the timers */
    for (i = 0, i_ptr = mrtentry_ptr->vif_timers; i < vif_numbers; i++, i_ptr++) {
        RESET_TIMER(*i_ptr);
    }
    for (i = 0, i_ptr = mrtentry_ptr->vif_deletion_delay; i < vif_numbers; i++, i_ptr++) {
        RESET_TIMER(*i_ptr);
    }

    mrtentry_ptr->flags = MRTF_NEW;
    RESET_TIMER(mrtentry_ptr->timer);
    RESET_TIMER(mrtentry_ptr->jp_timer);
    RESET_TIMER(mrtentry_ptr->rs_timer);
    RESET_TIMER(mrtentry_ptr->assert_timer);
    RESET_TIMER(mrtentry_ptr->assert_rate_timer);
    mrtentry_ptr->kernel_cache = NULL;

    return mrtentry_ptr;
}


static mrtentry_t *create_mrtentry(srcentry_t *srcentry_ptr, grpentry_t *grpentry_ptr, u_int16 flags)
{
    mrtentry_t *r_new;
    mrtentry_t *r_grp_insert, *r_src_insert; /* pointers to insert */
    u_int32 source;
    u_int32 group;

    if (flags & MRTF_SG) {
        /* (S,G) entry */
        source = srcentry_ptr->address;
        group  = grpentry_ptr->group;

        if (search_grpmrtlink(grpentry_ptr, source, &r_grp_insert) == TRUE) {
            return r_grp_insert;
        }
        if (search_srcmrtlink(srcentry_ptr, group, &r_src_insert) == TRUE) {
            /*
             * Hmmm, search_grpmrtlink() didn't find the entry, but
             * search_srcmrtlink() did find it! Shoudn't happen. Panic!
             */
            logit(LOG_ERR, 0, "MRT inconsistency for src %s and grp %s\n",
		  inet_fmt(source, s1, sizeof(s1)), inet_fmt(group, s2, sizeof(s2)));
            /* not reached but to make lint happy */
            return NULL;
        }
        /*
         * Create and insert in group mrtlink and source mrtlink chains.
         */
        r_new = alloc_mrtentry(srcentry_ptr, grpentry_ptr);
        if (r_new == NULL)
            return NULL;
        /*
         * r_new has to be insert right after r_grp_insert in the
         * grp mrtlink chain and right after r_src_insert in the
         * src mrtlink chain
         */
        insert_grpmrtlink(r_new, r_grp_insert, grpentry_ptr);
        insert_srcmrtlink(r_new, r_src_insert, srcentry_ptr);
        r_new->flags |= MRTF_SG;
        return r_new;
    }

    if (flags & MRTF_WC) {
        /* (*,G) entry */
        if (grpentry_ptr->grp_route != NULL)
            return grpentry_ptr->grp_route;
        r_new = alloc_mrtentry(srcentry_ptr, grpentry_ptr);
        if (r_new == NULL)
            return NULL;
        grpentry_ptr->grp_route = r_new;
        r_new->flags |= (MRTF_WC | MRTF_RP);
        return r_new;
    }

    if (flags & MRTF_PMBR) {
        /* (*,*,RP) entry */
        if (srcentry_ptr->mrtlink != NULL)
            return srcentry_ptr->mrtlink;
        r_new = alloc_mrtentry(srcentry_ptr, grpentry_ptr);
        if (r_new == NULL)
            return NULL;
        srcentry_ptr->mrtlink = r_new;
        r_new->flags |= (MRTF_PMBR | MRTF_RP);
        return r_new;
    }

    return NULL;
}


/*
 * Delete all kernel cache for this mrtentry
 */
void delete_mrtentry_all_kernel_cache(mrtentry_t *mrtentry_ptr)
{
    kernel_cache_t *kernel_cache_prev;
    kernel_cache_t *kernel_cache_ptr;

    if (!(mrtentry_ptr->flags & MRTF_KERNEL_CACHE)) {
        return;
    }

    /* Free all kernel_cache entries */
    for (kernel_cache_ptr = mrtentry_ptr->kernel_cache; kernel_cache_ptr != NULL; ) {
        kernel_cache_prev        = kernel_cache_ptr;
        kernel_cache_ptr         = kernel_cache_ptr->next;
        k_del_mfc(igmp_socket, kernel_cache_prev->source,
                  kernel_cache_prev->group);
        free((char *)kernel_cache_prev);
    }
    mrtentry_ptr->kernel_cache = NULL;

    /* turn off the cache flag(s) */
    mrtentry_ptr->flags &= ~(MRTF_KERNEL_CACHE | MRTF_MFC_CLONE_SG);
}


void delete_single_kernel_cache(mrtentry_t *mrtentry_ptr, kernel_cache_t *kernel_cache_ptr)
{
    if (kernel_cache_ptr->prev == NULL) {
        mrtentry_ptr->kernel_cache = kernel_cache_ptr->next;
        if (mrtentry_ptr->kernel_cache == NULL)
            mrtentry_ptr->flags &= ~(MRTF_KERNEL_CACHE | MRTF_MFC_CLONE_SG);
    } else {
        kernel_cache_ptr->prev->next = kernel_cache_ptr->next;
    }

    if (kernel_cache_ptr->next != NULL)
        kernel_cache_ptr->next->prev = kernel_cache_ptr->prev;

    IF_DEBUG(DEBUG_MFC) {
        logit(LOG_DEBUG, 0, "Deleting MFC entry for source %s and group %s",
	      inet_fmt(kernel_cache_ptr->source, s1, sizeof(s1)),
	      inet_fmt(kernel_cache_ptr->group, s2, sizeof(s2)));
    }

    k_del_mfc(igmp_socket, kernel_cache_ptr->source, kernel_cache_ptr->group);
    free((char *)kernel_cache_ptr);
}


void delete_single_kernel_cache_addr(mrtentry_t *mrtentry_ptr, u_int32 source, u_int32 group)
{
    u_int32 source_h;
    u_int32 group_h;
    kernel_cache_t *kernel_cache_ptr;

    if (mrtentry_ptr == NULL)
        return;

    source_h = ntohl(source);
    group_h  = ntohl(group);

    /* Find the exact (S,G) kernel_cache entry */
    for (kernel_cache_ptr = mrtentry_ptr->kernel_cache;
         kernel_cache_ptr != NULL;
         kernel_cache_ptr = kernel_cache_ptr->next) {
        if (ntohl(kernel_cache_ptr->group) < group_h)
            continue;
        if (ntohl(kernel_cache_ptr->group) > group_h)
            return;	/* Not found */
        if (ntohl(kernel_cache_ptr->source) < source_h)
            continue;
        if (ntohl(kernel_cache_ptr->source) > source_h)
            return;	/* Not found */

        /* Found exact match */
        break;
    }

    if (kernel_cache_ptr == NULL)
        return;

    /* Found. Delete it */
    if (kernel_cache_ptr->prev == NULL) {
        mrtentry_ptr->kernel_cache = kernel_cache_ptr->next;
        if (mrtentry_ptr->kernel_cache == NULL)
            mrtentry_ptr->flags &= ~(MRTF_KERNEL_CACHE | MRTF_MFC_CLONE_SG);
    } else{
        kernel_cache_ptr->prev->next = kernel_cache_ptr->next;
    }

    if (kernel_cache_ptr->next != NULL)
        kernel_cache_ptr->next->prev = kernel_cache_ptr->prev;

    IF_DEBUG(DEBUG_MFC) {
        logit(LOG_DEBUG, 0, "Deleting MFC entry for source %s and group %s",
	      inet_fmt(kernel_cache_ptr->source, s1, sizeof(s1)),
	      inet_fmt(kernel_cache_ptr->group, s2, sizeof(s2)));
    }

    k_del_mfc(igmp_socket, kernel_cache_ptr->source, kernel_cache_ptr->group);
    free((char *)kernel_cache_ptr);
}


/*
 * Installs kernel cache for (source, group). Assumes mrtentry_ptr
 * is the correct entry.
 */
void add_kernel_cache(mrtentry_t *mrtentry_ptr, u_int32 source, u_int32 group, u_int16 flags)
{
    u_int32 source_h;
    u_int32 group_h;
    kernel_cache_t *kernel_cache_next;
    kernel_cache_t *kernel_cache_prev;
    kernel_cache_t *kernel_cache_new;

    if (mrtentry_ptr == NULL)
        return;

    move_kernel_cache(mrtentry_ptr, flags);

    if (mrtentry_ptr->flags & MRTF_SG) {
        /* (S,G) */
        if (mrtentry_ptr->flags & MRTF_KERNEL_CACHE)
            return;

        kernel_cache_new = (kernel_cache_t *)calloc(1, sizeof(kernel_cache_t));
        kernel_cache_new->next = NULL;
        kernel_cache_new->prev = NULL;
        kernel_cache_new->source = source;
        kernel_cache_new->group = group;
        kernel_cache_new->sg_count.pktcnt = 0;
        kernel_cache_new->sg_count.bytecnt = 0;
        kernel_cache_new->sg_count.wrong_if = 0;
        mrtentry_ptr->kernel_cache = kernel_cache_new;
        mrtentry_ptr->flags |= MRTF_KERNEL_CACHE;

        return;
    }

    source_h = ntohl(source);
    group_h = ntohl(group);
    kernel_cache_prev = NULL;

    for (kernel_cache_next = mrtentry_ptr->kernel_cache;
         kernel_cache_next != NULL;
         kernel_cache_prev = kernel_cache_next,
             kernel_cache_next = kernel_cache_next->next) {
        if (ntohl(kernel_cache_next->group) < group_h)
            continue;
        if (ntohl(kernel_cache_next->group) > group_h)
            break;
        if (ntohl(kernel_cache_next->source) < source_h)
            continue;
        if (ntohl(kernel_cache_next->source) > source_h)
            break;

        /* Found exact match. Nothing to change. */
        return;
    }

    /*
     * The new entry must be placed between kernel_cache_prev and
     * kernel_cache_next
     */
    kernel_cache_new = (kernel_cache_t *)calloc(1, sizeof(kernel_cache_t));
    if (kernel_cache_prev != NULL)
        kernel_cache_prev->next = kernel_cache_new;
    else
        mrtentry_ptr->kernel_cache = kernel_cache_new;

    if (kernel_cache_next != NULL)
        kernel_cache_next->prev = kernel_cache_new;

    kernel_cache_new->prev = kernel_cache_prev;
    kernel_cache_new->next = kernel_cache_next;
    kernel_cache_new->source = source;
    kernel_cache_new->group = group;
    kernel_cache_new->sg_count.pktcnt = 0;
    kernel_cache_new->sg_count.bytecnt = 0;
    kernel_cache_new->sg_count.wrong_if = 0;
    mrtentry_ptr->flags |= MRTF_KERNEL_CACHE;
}

/*
 * Bring the kernel cache "UP": from the (*,*,RP) to (*,G) or (S,G)
 */
static void move_kernel_cache(mrtentry_t *mrtentry_ptr, u_int16 flags)
{
    kernel_cache_t *kernel_cache_ptr;
    kernel_cache_t *insert_kernel_cache_ptr;
    kernel_cache_t *first_kernel_cache_ptr;
    kernel_cache_t *last_kernel_cache_ptr;
    kernel_cache_t *prev_kernel_cache_ptr;
    mrtentry_t     *mrtentry_pmbr;
    mrtentry_t     *mrtentry_rp;
    u_int32 group_h;
    u_int32 source_h;
    int found;

    if (mrtentry_ptr == NULL)
        return;

    if (mrtentry_ptr->flags & MRTF_PMBR)
        return;

    if (mrtentry_ptr->flags & MRTF_WC) {
        /* Move the cache info from (*,*,RP) to (*,G) */
        group_h = ntohl(mrtentry_ptr->group->group);
        mrtentry_pmbr =
            mrtentry_ptr->group->active_rp_grp->rp->rpentry->mrtlink;
        if (mrtentry_pmbr == NULL)
            return;    /* Nothing to move */

        first_kernel_cache_ptr = last_kernel_cache_ptr = NULL;
        for (kernel_cache_ptr = mrtentry_pmbr->kernel_cache;
             kernel_cache_ptr != NULL;
             kernel_cache_ptr = kernel_cache_ptr->next) {
            /*
             * The order is: (1) smaller group;
             *               (2) smaller source within group
             */
            if (ntohl(kernel_cache_ptr->group) < group_h)
                continue;

            if (ntohl(kernel_cache_ptr->group) != group_h)
                break;

            /* Select the kernel_cache entries to move  */
            if (first_kernel_cache_ptr == NULL) {
                first_kernel_cache_ptr = last_kernel_cache_ptr =
                    kernel_cache_ptr;
            } else {
                last_kernel_cache_ptr = kernel_cache_ptr;
	    }
        }

        if (first_kernel_cache_ptr != NULL) {
            /* Fix the old chain */
            if (first_kernel_cache_ptr->prev != NULL) {
                first_kernel_cache_ptr->prev->next =
                    last_kernel_cache_ptr->next;
            } else {
                mrtentry_pmbr->kernel_cache = last_kernel_cache_ptr->next;
	    }

            if (last_kernel_cache_ptr->next != NULL)
                last_kernel_cache_ptr->next->prev = first_kernel_cache_ptr->prev;

            if (mrtentry_pmbr->kernel_cache == NULL)
                mrtentry_pmbr->flags &= ~(MRTF_KERNEL_CACHE | MRTF_MFC_CLONE_SG);

            /* Insert in the new place */
            prev_kernel_cache_ptr = NULL;
            last_kernel_cache_ptr->next = NULL;
            mrtentry_ptr->flags |= MRTF_KERNEL_CACHE;

            for (kernel_cache_ptr = mrtentry_ptr->kernel_cache;
                 kernel_cache_ptr != NULL; ) {
                if (first_kernel_cache_ptr == NULL)
                    break;  /* All entries have been inserted */

                if (ntohl(kernel_cache_ptr->source) >
                    ntohl(first_kernel_cache_ptr->source)) {
                    /* Insert the entry before kernel_cache_ptr */
                    insert_kernel_cache_ptr = first_kernel_cache_ptr;
                    first_kernel_cache_ptr = first_kernel_cache_ptr->next;

                    if (kernel_cache_ptr->prev != NULL)
                        kernel_cache_ptr->prev->next = insert_kernel_cache_ptr;
                    else
                        mrtentry_ptr->kernel_cache = insert_kernel_cache_ptr;

                    insert_kernel_cache_ptr->prev = kernel_cache_ptr->prev;
                    insert_kernel_cache_ptr->next = kernel_cache_ptr;
                    kernel_cache_ptr->prev = insert_kernel_cache_ptr;
                }
                prev_kernel_cache_ptr = kernel_cache_ptr;
                kernel_cache_ptr = kernel_cache_ptr->next;
            }

            if (first_kernel_cache_ptr != NULL) {
                /* Place all at the end after prev_kernel_cache_ptr */
                if (prev_kernel_cache_ptr != NULL)
                    prev_kernel_cache_ptr->next = first_kernel_cache_ptr;
                else
                    mrtentry_ptr->kernel_cache = first_kernel_cache_ptr;

                first_kernel_cache_ptr->prev = prev_kernel_cache_ptr;
            }
        }

        return;
    }

    if (mrtentry_ptr->flags & MRTF_SG) {
        /* (S,G) entry. Move the whole group cache from (*,*,RP) to (*,G) and
         * then get the necessary entry from (*,G).
         * TODO: Not optimized! The particular entry is moved first to (*,G),
         * then we have to search again (*,G) to find it and move to (S,G).
         */
        /* TODO: XXX: No need for this? Thinking.... */
/*	move_kernel_cache(mrtentry_ptr->group->grp_route, flags); */
        if ((mrtentry_rp = mrtentry_ptr->group->grp_route) == NULL)
            mrtentry_rp = mrtentry_ptr->group->active_rp_grp->rp->rpentry->mrtlink;

        if (mrtentry_rp == NULL)
            return;

        if (mrtentry_rp->incoming != mrtentry_ptr->incoming) {
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
        source_h = ntohl(mrtentry_ptr->source->address);
        group_h  = ntohl(mrtentry_ptr->group->group);
        found = FALSE;
        for (kernel_cache_ptr = mrtentry_rp->kernel_cache;
             kernel_cache_ptr != NULL;
             kernel_cache_ptr = kernel_cache_ptr->next) {
            if (ntohl(kernel_cache_ptr->group) < group_h)
                continue;

            if (ntohl(kernel_cache_ptr->group) > group_h)
                break;

            if (ntohl(kernel_cache_ptr->source) < source_h)
                continue;

            if (ntohl(kernel_cache_ptr->source) > source_h)
                break;

            /* We found it! */
            if (kernel_cache_ptr->prev != NULL){
                kernel_cache_ptr->prev->next = kernel_cache_ptr->next;
            } else {
                mrtentry_rp->kernel_cache = kernel_cache_ptr->next;
            }

            if (kernel_cache_ptr->next != NULL)
                kernel_cache_ptr->next->prev = kernel_cache_ptr->prev;

            found = TRUE;

            break;
        }

        if (found == TRUE) {
            if (mrtentry_rp->kernel_cache == NULL)
                mrtentry_rp->flags &= ~(MRTF_KERNEL_CACHE | MRTF_MFC_CLONE_SG);

            if (mrtentry_ptr->kernel_cache != NULL)
                free ((char *)mrtentry_ptr->kernel_cache);

            mrtentry_ptr->flags |= MRTF_KERNEL_CACHE;
            mrtentry_ptr->kernel_cache = kernel_cache_ptr;
            kernel_cache_ptr->prev = NULL;
            kernel_cache_ptr->next = NULL;
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
