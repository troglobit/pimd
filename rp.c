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

#include "defs.h"


/*
 * The hash function. Stollen from Eddy's (eddy@isi.edu)
 * implementation (for compatibility ;)
 */
#define SEED1   1103515245
#define SEED2   12345
#define RP_HASH_VALUE(G, M, C) (((SEED1) * (((SEED1) * ((G) & (M)) + (SEED2)) ^ (C)) + (SEED2)) % 0x80000000)

cand_rp_t               *cand_rp_list;
grp_mask_t              *grp_mask_list;
cand_rp_t               *segmented_cand_rp_list;
grp_mask_t              *segmented_grp_mask_list;
uint16_t                 curr_bsr_fragment_tag;
uint8_t                  curr_bsr_priority;
uint32_t                 curr_bsr_address;
uint32_t                 curr_bsr_hash_mask;
uint16_t                 pim_bootstrap_timer;   /* For electing the BSR and
						 * sending Cand-RP-set msgs */
uint8_t                  my_bsr_priority;
uint32_t                 my_bsr_address;
uint32_t                 my_bsr_hash_mask;
uint8_t                  cand_bsr_flag = FALSE; /* Set to TRUE if I am
						 * a candidate BSR */
uint32_t                 my_cand_rp_address;
uint8_t                  my_cand_rp_priority;
uint16_t                 my_cand_rp_holdtime;
uint16_t                 my_cand_rp_adv_period; /* The locally configured
						 * Cand-RP adv. period. */
uint16_t                 pim_cand_rp_adv_timer;
uint8_t                  cand_rp_flag  = FALSE;  /* Candidate RP flag */
struct cand_rp_adv_message_ cand_rp_adv_message;
uint32_t                 rp_my_ipv4_hashmask;


/*
 * Local functions definition.
 */
static cand_rp_t  *add_cand_rp          (cand_rp_t **used_cand_rp_list, uint32_t address);
static grp_mask_t *add_grp_mask         (grp_mask_t **used_grp_mask_list,
					 uint32_t group_addr,
					 uint32_t group_mask,
					 uint32_t hash_mask);
static void       delete_grp_mask_entry (cand_rp_t **used_cand_rp_list,
					 grp_mask_t **used_grp_mask_list,
					 grp_mask_t *grp_mask_delete);
static void       delete_rp_entry       (cand_rp_t **used_cand_rp_list,
					 grp_mask_t **used_grp_mask_list,
					 cand_rp_t *cand_rp_ptr);


void init_rp_and_bsr(void)
{
    /* TODO: if the grplist is not NULL, remap all groups ASAP! */
    delete_rp_list(&cand_rp_list, &grp_mask_list);
    delete_rp_list(&segmented_cand_rp_list, &segmented_grp_mask_list);

    if (cand_bsr_flag == FALSE) {
	/*
	 * If I am not candidat BSR, initialize the "current BSR"
	 * as having the lowest priority.
	 */
	curr_bsr_fragment_tag = 0;
	curr_bsr_priority = 0;             /* Lowest priority */
	curr_bsr_address  = INADDR_ANY_N;  /* Lowest priority */
	MASKLEN_TO_MASK(RP_DEFAULT_IPV4_HASHMASKLEN, curr_bsr_hash_mask);
	SET_TIMER(pim_bootstrap_timer, PIM_BOOTSTRAP_TIMEOUT);
    } else {
	curr_bsr_fragment_tag = RANDOM();
	curr_bsr_priority = my_bsr_priority;
	curr_bsr_address = my_bsr_address;
	curr_bsr_hash_mask = my_bsr_hash_mask;
	SET_TIMER(pim_bootstrap_timer, bootstrap_initial_delay());
    }

    if (cand_rp_flag != FALSE) {
	MASKLEN_TO_MASK(RP_DEFAULT_IPV4_HASHMASKLEN, rp_my_ipv4_hashmask);
	/* Setup the Cand-RP-Adv-Timer */
	SET_TIMER(pim_cand_rp_adv_timer, RANDOM() % my_cand_rp_adv_period);
    }
}


uint16_t bootstrap_initial_delay(void)
{
    uint32_t addr_delay;
    uint32_t delay;
    uint32_t log_mask;
    int32_t  log_of_2;
    uint8_t  best_priority;

    /*
     * The bootstrap timer initial value (if Cand-BSR).
     * It depends of the bootstrap router priority:
     * higher priority has shorter value:
     *
     * delay = 5 + 2 * log_2(1 + best_priority - myPriority) + addr_delay;
     *
     *    best_priority = Max(storedPriority, myPriority);
     *    if (best_priority == myPriority)
     *        addr_delay = log_2(bestAddr - myAddr)/16;
     *    else
     *        addr_delay = 2 - (myAddr/2^31);
     */

    best_priority = max(curr_bsr_priority, my_bsr_priority);
    if (best_priority == my_bsr_priority) {
	addr_delay = ntohl(curr_bsr_address) - ntohl(my_bsr_address);
	/* Calculate the integer part of log_2 of (bestAddr - myAddr) */
	/* To do so, have to find the position number of the first bit
	 * from left which is `1`
	 */
	log_mask = sizeof(addr_delay) << 3;
	log_mask = (1 << (log_mask - 1));  /* Set the leftmost bit to `1` */
	for (log_of_2 = (sizeof(addr_delay) << 3) - 1 ; log_of_2; log_of_2--) {
	    if (addr_delay & log_mask)
		break;

	    log_mask >>= 1;  /* Start shifting `1` on right */
	}
	addr_delay = log_of_2 / 16;
    } else {
	addr_delay = 2 - (ntohl(my_bsr_address) / ( 1 << 31));
    }

    delay = 1 + best_priority - my_bsr_priority;
    /* Calculate log_2(delay) */
    log_mask = sizeof(delay) << 3;
    log_mask = (1 << (log_mask - 1));  /* Set the leftmost bit to `1` */
    for (log_of_2 = (sizeof(delay) << 3) - 1 ; log_of_2; log_of_2--) {
	if (delay & log_mask)
	    break;

	log_mask >>= 1;  /* Start shifting `1` on right */
    }

    delay = 5 + 2 * log_of_2 + addr_delay;

    return (uint16_t)delay;
}


static cand_rp_t *add_cand_rp(cand_rp_t **used_cand_rp_list, uint32_t address)
{
    cand_rp_t *prev = NULL;
    cand_rp_t *next;
    cand_rp_t *ptr;
    rpentry_t *entry;
    uint32_t addr_h = ntohl(address);

    /* The ordering is the bigger first */
    for (next = *used_cand_rp_list; next; prev = next, next = next->next) {
	if (ntohl(next->rpentry->address) > addr_h)
	    continue;

	if (next->rpentry->address == address)
	    return next;
	else
	    break;
    }

    /* Create and insert the new entry between prev and next */
    ptr = (cand_rp_t *)calloc(1, sizeof(cand_rp_t));
    if (!ptr)
	logit(LOG_ERR, 0, "Ran out of memory in add_cand_rp()");
    ptr->rp_grp_next = NULL;
    ptr->next = next;
    ptr->prev = prev;
    if (next)
	next->prev = ptr;
    if (prev == NULL)
	*used_cand_rp_list = ptr;
    else
	prev->next = ptr;

    entry = (rpentry_t *)calloc(1, sizeof(rpentry_t));
    if (!entry)
	logit(LOG_ERR, 0, "Ran out of memory in add_cand_rp()");
    ptr->rpentry = entry;
    entry->next = NULL;
    entry->prev  = NULL;
    entry->address = address;
    entry->mrtlink = NULL;
    entry->incoming = NO_VIF;
    entry->upstream = NULL;
    /* TODO: setup the metric and the preference as ~0 (the lowest)? */
    entry->metric = ~0;
    entry->preference = ~0;
    RESET_TIMER(entry->timer);
    entry->cand_rp = ptr;

    /* TODO: XXX: check whether there is a route to that RP: if return value
     * is FALSE, then no route.
     */
    if (local_address(entry->address) == NO_VIF)
	/* TODO: check for error and delete */
	set_incoming(entry, PIM_IIF_RP);
    else
	/* TODO: XXX: CHECK!!! */
	entry->incoming = reg_vif_num;

    return ptr;
}


static grp_mask_t *add_grp_mask(grp_mask_t **used_grp_mask_list, uint32_t group_addr, uint32_t group_mask, uint32_t hash_mask)
{
    grp_mask_t *prev = NULL;
    grp_mask_t *next;
    grp_mask_t *ptr;
    uint32_t prefix_h = ntohl(group_addr & group_mask);

    /* The ordering of group_addr is: bigger first */
    for (next = *used_grp_mask_list; next; prev = next, next = next->next) {
	if (ntohl(next->group_addr & next->group_mask) > prefix_h)
	    continue;

	/* The ordering of group_mask is: bigger (longer) first */
	if ((next->group_addr & next->group_mask) == (group_addr & group_mask)) {
	    if (ntohl(next->group_mask) > ntohl(group_mask))
		continue;
	    else if (next->group_mask == group_mask)
		return next;
	    else
		break;
	}
    }

    ptr = (grp_mask_t *)calloc(1, sizeof(grp_mask_t));
    if (!ptr)
	logit(LOG_ERR, 0, "Ran out of memory in add_grp_mask()");

    ptr->grp_rp_next = (rp_grp_entry_t *)NULL;
    ptr->next = next;
    ptr->prev = prev;
    if (next)
	next->prev = ptr;
    if (prev == NULL)
	*used_grp_mask_list = ptr;
    else
	prev->next = ptr;

    ptr->group_addr = group_addr;
    ptr->group_mask = group_mask;
    ptr->hash_mask  = hash_mask;
    ptr->group_rp_number = 0;
    ptr->fragment_tag = 0;

    return ptr;
}


/* TODO: XXX: BUG: a remapping for some groups currently using some other
 * grp_mask may be required by the addition of the new entry!!!
 * Remapping all groups might be a costly process...
 */
rp_grp_entry_t *add_rp_grp_entry(cand_rp_t  **used_cand_rp_list,
				 grp_mask_t **used_grp_mask_list,
				 uint32_t rp_addr,
				 uint8_t  rp_priority,
				 uint16_t rp_holdtime,
				 uint32_t group_addr,
				 uint32_t group_mask,
				 uint32_t bsr_hash_mask,
				 uint16_t fragment_tag)
{
    cand_rp_t *cand_rp_ptr;
    grp_mask_t *mask_ptr;
    rpentry_t *rpentry_ptr;
    rp_grp_entry_t *entry_next;
    rp_grp_entry_t *entry_new;
    rp_grp_entry_t *entry_prev = NULL;
    grpentry_t *grpentry_ptr_prev;
    grpentry_t *grpentry_ptr_next;
    uint32_t rp_addr_h;
    uint8_t old_highest_priority = ~0;  /* Smaller value means "higher" */

    /* Input data verification */
    if (!inet_valid_host(rp_addr))
	return NULL;
    if (!IN_CLASSD(ntohl(group_addr)))
	return NULL;

    mask_ptr = add_grp_mask(used_grp_mask_list, group_addr, group_mask, bsr_hash_mask);
    if (mask_ptr == NULL)
	return NULL;

/* TODO: delete */
#if 0
    if (mask_ptr->grp_rp_next) {
	/* Check for obsolete grp_rp chain */
	if ((my_bsr_address != curr_bsr_address) && (mask_ptr->grp_rp_next->fragment_tag != fragment_tag)) {
	    /* This grp_rp chain is obsolete. Delete it. */
	    delete_grp_mask(used_cand_rp_list, used_grp_mask_list, group_addr, group_mask);
	    mask_ptr = add_grp_mask(used_grp_mask_list, group_addr, group_mask, bsr_hash_mask);

	    if (mask_ptr == NULL)
		return NULL;
	}
    }
#endif /* 0 */

    cand_rp_ptr = add_cand_rp(used_cand_rp_list, rp_addr);
    if (cand_rp_ptr == NULL) {
	if (mask_ptr->grp_rp_next == NULL)
	    delete_grp_mask(used_cand_rp_list, used_grp_mask_list,
			    group_addr, group_mask);
	return NULL;
    }

    rpentry_ptr = cand_rp_ptr->rpentry;
    SET_TIMER(rpentry_ptr->timer, rp_holdtime);
    rp_addr_h = ntohl(rp_addr);
    mask_ptr->fragment_tag = fragment_tag;   /* For garbage collection */

    entry_prev = NULL;
    entry_next = mask_ptr->grp_rp_next;
    /* TODO: improve it */
    if (entry_next != NULL)
	old_highest_priority = entry_next->priority;
    for ( ; entry_next; entry_prev = entry_next,
	      entry_next = entry_next->grp_rp_next) {
	/* Smaller value means higher priority. The entries are
	 * sorted with the highest priority first.
	 */
	if (entry_next->priority < rp_priority)
	    continue;
	if (entry_next->priority > rp_priority)
	    break;

	/*
	 * Here we don't care about higher/lower addresses, because
	 * higher address does not guarantee higher hash_value,
	 * but anyway we do order with the higher address first,
	 * so it will be easier to find an existing entry and update the
	 * holdtime.
	 */
	if (ntohl(entry_next->rp->rpentry->address) > rp_addr_h)
	    continue;
	if (ntohl(entry_next->rp->rpentry->address) < rp_addr_h)
	    break;
	/* We already have this entry. Update the holdtime */
	/* TODO: We shoudn't have old existing entry, because with the
	 * current implementation all of them will be deleted
	 * (different fragment_tag). Debug and check and eventually
	 * delete.
	 */
	entry_next->holdtime = rp_holdtime;
	entry_next->fragment_tag = fragment_tag;

	return entry_next;
    }

    /* Create and link the new entry */
    entry_new = (rp_grp_entry_t *)calloc(1, sizeof(rp_grp_entry_t));
    if (!entry_new)
	logit(LOG_ERR, 0, "Ran out of memory in add_rp_grp_entry()");
    entry_new->grp_rp_next = entry_next;
    entry_new->grp_rp_prev = entry_prev;
    if (entry_next)
	entry_next->grp_rp_prev = entry_new;
    if (entry_prev == NULL)
	mask_ptr->grp_rp_next = entry_new;
    else
	entry_prev->grp_rp_next = entry_new;

    /*
     * The rp_grp_entry chain is not ordered, so just plug
     * the new entry at the head.
     */
    entry_new->rp_grp_next = cand_rp_ptr->rp_grp_next;
    if (cand_rp_ptr->rp_grp_next)
	cand_rp_ptr->rp_grp_next->rp_grp_prev = entry_new;
    entry_new->rp_grp_prev = NULL;
    cand_rp_ptr->rp_grp_next = entry_new;

    entry_new->holdtime = rp_holdtime;
    entry_new->fragment_tag = fragment_tag;
    entry_new->priority = rp_priority;
    entry_new->group = mask_ptr;
    entry_new->rp = cand_rp_ptr;
    entry_new->grplink = NULL;

    /* If I am BSR candidate and rp_addr is NOT hacked SSM address, then log it */
    if( cand_bsr_flag && rp_addr != 0x0100fea9 ) {
	uint32_t mask;
	MASK_TO_MASKLEN(group_mask, mask);
	logit(LOG_INFO, 0, "New RP candidate %s for group %s/%d, priority %d",
	      inet_fmt(rp_addr, s1, sizeof(s1)), inet_fmt(group_addr, s2, sizeof(s2)), mask, rp_priority);
    }

    mask_ptr->group_rp_number++;

    if (mask_ptr->grp_rp_next->priority == rp_priority) {
	/* The first entries are with the best priority. */
	/* Adding this rp_grp_entry may result in group_to_rp remapping */
	for (entry_next = mask_ptr->grp_rp_next; entry_next; entry_next = entry_next->grp_rp_next) {
	    if (entry_next->priority > old_highest_priority)
		break;

	    for (grpentry_ptr_prev = entry_next->grplink; grpentry_ptr_prev; ) {
		grpentry_ptr_next = grpentry_ptr_prev->rpnext;
		remap_grpentry(grpentry_ptr_prev);
		grpentry_ptr_prev = grpentry_ptr_next;
	    }
	}
    }

    return entry_new;
}


void delete_rp_grp_entry(cand_rp_t **used_cand_rp_list, grp_mask_t **used_grp_mask_list, rp_grp_entry_t *entry)
{
    grpentry_t *ptr;
    grpentry_t *ptr_next;

    if (entry == NULL)
	return;
    entry->group->group_rp_number--;

    /* Free the rp_grp* and grp_rp* links */
    if (entry->rp_grp_prev)
	entry->rp_grp_prev->rp_grp_next = entry->rp_grp_next;
    else
	entry->rp->rp_grp_next = entry->rp_grp_next;
    if (entry->rp_grp_next)
	entry->rp_grp_next->rp_grp_prev = entry->rp_grp_prev;

    if (entry->grp_rp_prev)
	entry->grp_rp_prev->grp_rp_next = entry->grp_rp_next;
    else
	entry->group->grp_rp_next = entry->grp_rp_next;

    if (entry->grp_rp_next)
	entry->grp_rp_next->grp_rp_prev = entry->grp_rp_prev;

    /* Delete Cand-RP or Group-prefix if useless */
    if (entry->group->grp_rp_next == NULL)
	delete_grp_mask_entry(used_cand_rp_list, used_grp_mask_list, entry->group);

    if (entry->rp->rp_grp_next == NULL)
	delete_rp_entry(used_cand_rp_list, used_grp_mask_list, entry->rp);

    /* Remap all affected groups */
    for (ptr = entry->grplink; ptr; ptr = ptr_next) {
	ptr_next = ptr->rpnext;
	remap_grpentry(ptr);
    }

    free((char *)entry);
}

/* TODO: XXX: the affected group entries will be partially
 * setup, because may have group routing entry, but NULL pointers to RP.
 * After the call to this function, must remap all group entries ASAP.
 */
void delete_rp_list(cand_rp_t  **used_cand_rp_list, grp_mask_t **used_grp_mask_list)
{
    cand_rp_t      *cand_ptr,  *cand_next;
    rp_grp_entry_t *entry_ptr, *entry_next;
    grp_mask_t     *mask_ptr, *mask_next;
    grpentry_t     *gentry_ptr, *gentry_ptr_next;

    for (cand_ptr = *used_cand_rp_list; cand_ptr; ) {
	cand_next = cand_ptr->next;

	/* Free the mrtentry (if any) for this RP */
	if (cand_ptr->rpentry->mrtlink) {
	    if (cand_ptr->rpentry->mrtlink->flags & MRTF_KERNEL_CACHE)
		delete_mrtentry_all_kernel_cache(cand_ptr->rpentry->mrtlink);
	    FREE_MRTENTRY(cand_ptr->rpentry->mrtlink);
	}
	free(cand_ptr->rpentry);

	/* Free the whole chain of entry for this RP */
	for (entry_ptr = cand_ptr->rp_grp_next; entry_ptr; entry_ptr = entry_next) {
	    entry_next = entry_ptr->rp_grp_next;

	    /* Clear the RP related invalid pointers for all group entries */
	    for (gentry_ptr = entry_ptr->grplink; gentry_ptr; gentry_ptr = gentry_ptr_next) {
		gentry_ptr_next = gentry_ptr->rpnext;
		gentry_ptr->rpnext = NULL;
		gentry_ptr->rpprev = NULL;
		gentry_ptr->active_rp_grp = NULL;
		gentry_ptr->rpaddr = INADDR_ANY_N;
	    }

	    free(entry_ptr);
	}

	free(cand_ptr);
	cand_ptr = cand_next;
    }
    *used_cand_rp_list = NULL;

    for (mask_ptr = *used_grp_mask_list; mask_ptr; mask_ptr = mask_next) {
	mask_next = mask_ptr->next;
	free(mask_ptr);
    }
    *used_grp_mask_list = NULL;
}


void delete_grp_mask(cand_rp_t **used_cand_rp_list, grp_mask_t **used_grp_mask_list, uint32_t group_addr, uint32_t group_mask)
{
    grp_mask_t *ptr;
    uint32_t prefix_h = ntohl(group_addr & group_mask);

    for (ptr = *used_grp_mask_list; ptr; ptr = ptr->next) {
	if (ntohl(ptr->group_addr & ptr->group_mask) > prefix_h)
	    continue;

	if (ptr->group_addr == group_addr) {
	    if (ntohl(ptr->group_mask) > ntohl(group_mask))
		continue;
	    else if (ptr->group_mask == group_mask)
		break;
	    else
		return;   /* Not found */
	}
    }

    if (ptr == (grp_mask_t *)NULL)
	return;       /* Not found */

    delete_grp_mask_entry(used_cand_rp_list, used_grp_mask_list, ptr);
}

static void delete_grp_mask_entry(cand_rp_t **used_cand_rp_list, grp_mask_t **used_grp_mask_list, grp_mask_t *grp_mask_delete)
{
    grpentry_t *grp_ptr, *grp_ptr_next;
    rp_grp_entry_t *entry_ptr;
    rp_grp_entry_t *entry_next;

    if (grp_mask_delete == NULL)
	return;

    /* Remove from the grp_mask_list first */
    if (grp_mask_delete->prev)
	grp_mask_delete->prev->next = grp_mask_delete->next;
    else
	*used_grp_mask_list = grp_mask_delete->next;

    if (grp_mask_delete->next)
	grp_mask_delete->next->prev = grp_mask_delete->prev;

    /* Remove all grp_rp entries for this grp_mask */
    for (entry_ptr = grp_mask_delete->grp_rp_next; entry_ptr; entry_ptr = entry_next) {
	entry_next = entry_ptr->grp_rp_next;

	/* Remap all related grpentry */
	for (grp_ptr = entry_ptr->grplink; grp_ptr; grp_ptr = grp_ptr_next) {
	    grp_ptr_next = grp_ptr->rpnext;
	    remap_grpentry(grp_ptr);
	}

	if (entry_ptr->rp_grp_prev != (rp_grp_entry_t *)NULL)
	    entry_ptr->rp_grp_prev->rp_grp_next = entry_ptr->rp_grp_next;
	else
	    entry_ptr->rp->rp_grp_next = entry_ptr->rp_grp_next;

	if (entry_ptr->rp_grp_next != NULL)
	    entry_ptr->rp_grp_next->rp_grp_prev = entry_ptr->rp_grp_prev;

	/* Delete the RP entry */
	if (entry_ptr->rp->rp_grp_next == NULL)
	    delete_rp_entry(used_cand_rp_list, used_grp_mask_list, entry_ptr->rp);

	free(entry_ptr);
    }

    free(grp_mask_delete);
}

/*
 * TODO: currently not used.
 */
void delete_rp(cand_rp_t **used_cand_rp_list, grp_mask_t **used_grp_mask_list, uint32_t rp_addr)
{
    cand_rp_t *ptr;
    uint32_t rp_addr_h = ntohl(rp_addr);

    for(ptr = *used_cand_rp_list; ptr != NULL; ptr = ptr->next) {
	if (ntohl(ptr->rpentry->address) > rp_addr_h)
	    continue;

	if (ptr->rpentry->address == rp_addr)
	    break;
	else
	    return;   /* Not found */
    }

    if (ptr == NULL)
	return;       /* Not found */

    delete_rp_entry(used_cand_rp_list, used_grp_mask_list, ptr);
}


static void delete_rp_entry(cand_rp_t **used_cand_rp_list, grp_mask_t **used_grp_mask_list, cand_rp_t *cand_rp_delete)
{
    rp_grp_entry_t *entry_ptr;
    rp_grp_entry_t *entry_next;
    grpentry_t *grp_ptr;
    grpentry_t *grp_ptr_next;

    if (cand_rp_delete == NULL)
	return;

    /* Remove from the cand-RP chain */
    if (cand_rp_delete->prev)
	cand_rp_delete->prev->next = cand_rp_delete->next;
    else
	*used_cand_rp_list = cand_rp_delete->next;

    if (cand_rp_delete->next)
	cand_rp_delete->next->prev = cand_rp_delete->prev;

    if (cand_rp_delete->rpentry->mrtlink) {
	if (cand_rp_delete->rpentry->mrtlink->flags & MRTF_KERNEL_CACHE)
	    delete_mrtentry_all_kernel_cache(cand_rp_delete->rpentry->mrtlink);

	FREE_MRTENTRY(cand_rp_delete->rpentry->mrtlink);
    }
    free ((char *)cand_rp_delete->rpentry);

    /* Remove all rp_grp entries for this RP */
    for (entry_ptr = cand_rp_delete->rp_grp_next; entry_ptr; entry_ptr = entry_next) {
	entry_next = entry_ptr->rp_grp_next;
	entry_ptr->group->group_rp_number--;

	/* First take care of the grp_rp chain */
	if (entry_ptr->grp_rp_prev)
	    entry_ptr->grp_rp_prev->grp_rp_next = entry_ptr->grp_rp_next;
	else
	    entry_ptr->group->grp_rp_next = entry_ptr->grp_rp_next;

	if (entry_ptr->grp_rp_next)
	    entry_ptr->grp_rp_next->grp_rp_prev = entry_ptr->grp_rp_prev;

	if (entry_ptr->grp_rp_next == NULL)
	    delete_grp_mask_entry(used_cand_rp_list, used_grp_mask_list, entry_ptr->group);

	/* Remap the related groups */
	for (grp_ptr = entry_ptr->grplink; grp_ptr; grp_ptr = grp_ptr_next) {
	    grp_ptr_next = grp_ptr->rpnext;
	    remap_grpentry(grp_ptr);
	}

	free(entry_ptr);
    }

    free((char *)cand_rp_delete);
}


/*
 * Rehash the RP for the group.
 * XXX: currently, every time when remap_grpentry() is called, there has
 * being a good reason to change the RP, so for performancy reasons
 * no check is performed whether the RP will be really different one.
 */
int remap_grpentry(grpentry_t *grpentry_ptr)
{
    rpentry_t *rpentry_ptr;
    rp_grp_entry_t *entry_ptr;
    mrtentry_t *grp_route;
    mrtentry_t *mrtentry_ptr;

    if (grpentry_ptr == NULL)
	return FALSE;

    /* Remove from the list of all groups matching to the same RP */
    if (grpentry_ptr->rpprev) {
	grpentry_ptr->rpprev->rpnext = grpentry_ptr->rpnext;
    } else {
	if (grpentry_ptr->active_rp_grp)
	    grpentry_ptr->active_rp_grp->grplink = grpentry_ptr->rpnext;
    }

    if (grpentry_ptr->rpnext)
	grpentry_ptr->rpnext->rpprev = grpentry_ptr->rpprev;

    entry_ptr = rp_grp_match(grpentry_ptr->group);
    if (entry_ptr == NULL) {
	/* If cannot remap, delete the group */
	delete_grpentry(grpentry_ptr);
	return FALSE;
    }
    rpentry_ptr = entry_ptr->rp->rpentry;

    /* Add to the new chain of all groups mapping to the same RP */
    grpentry_ptr->rpaddr  = rpentry_ptr->address;
    grpentry_ptr->active_rp_grp = entry_ptr;
    grpentry_ptr->rpnext = entry_ptr->grplink;
    if (grpentry_ptr->rpnext)
	grpentry_ptr->rpnext->rpprev = grpentry_ptr;
    grpentry_ptr->rpprev = NULL;
    entry_ptr->grplink = grpentry_ptr;

    grp_route = grpentry_ptr->grp_route;
    if (grp_route) {
	grp_route->upstream   = rpentry_ptr->upstream;
	grp_route->metric     = rpentry_ptr->metric;
	grp_route->preference = rpentry_ptr->preference;
	change_interfaces(grp_route, rpentry_ptr->incoming,
			  grp_route->joined_oifs,
			  grp_route->pruned_oifs,
			  grp_route->leaves,
			  grp_route->asserted_oifs, MFC_UPDATE_FORCE);
    }

    for (mrtentry_ptr = grpentry_ptr->mrtlink; mrtentry_ptr; mrtentry_ptr = mrtentry_ptr->grpnext) {
	if (!(mrtentry_ptr->flags & MRTF_RP))
	    continue;

	mrtentry_ptr->upstream = rpentry_ptr->upstream;
	mrtentry_ptr->metric   = rpentry_ptr->metric;
	mrtentry_ptr->preference = rpentry_ptr->preference;
	change_interfaces(mrtentry_ptr, rpentry_ptr->incoming,
			  mrtentry_ptr->joined_oifs,
			  mrtentry_ptr->pruned_oifs,
			  mrtentry_ptr->leaves,
			  mrtentry_ptr->asserted_oifs, MFC_UPDATE_FORCE);
    }

    return TRUE;
}


rpentry_t *rp_match(uint32_t group)
{
    rp_grp_entry_t *ptr;

    ptr = rp_grp_match(group);
    if (ptr)
	return ptr->rp->rpentry;

    return NULL;
}

rp_grp_entry_t *rp_grp_match(uint32_t group)
{
    grp_mask_t *mask_ptr;
    rp_grp_entry_t *entry_ptr;
    rp_grp_entry_t *best_entry = NULL;
    uint8_t best_priority       = ~0; /* Smaller is better */
    uint32_t best_hash_value    = 0;  /* Bigger is better */
    uint32_t best_address_h     = 0;  /* Bigger is better */
    uint32_t curr_hash_value    = 0;
    uint32_t curr_address_h     = 0;
    uint32_t group_h            = ntohl(group);

    if (grp_mask_list == NULL)
	return NULL;

    for (mask_ptr = grp_mask_list; mask_ptr; mask_ptr = mask_ptr->next) {
	/* Search the grp_mask (group-prefix) list */
	if ((group_h & ntohl(mask_ptr->group_mask))
	    != ntohl(mask_ptr->group_mask & mask_ptr->group_addr))
	    continue;

	for (entry_ptr = mask_ptr->grp_rp_next; entry_ptr; entry_ptr = entry_ptr->grp_rp_next) {
	    if (best_priority < entry_ptr->priority)
		break;

	    curr_hash_value = RP_HASH_VALUE(group_h, mask_ptr->hash_mask, curr_address_h);
	    curr_address_h = ntohl(entry_ptr->rp->rpentry->address);

	    if (best_priority == entry_ptr->priority) {
		/* Compare the hash_value and then the addresses */
		if (curr_hash_value < best_hash_value)
		    continue;

		if (curr_hash_value == best_hash_value) {
		    if (curr_address_h < best_address_h)
			continue;
		}
	    }

	    /* The current entry in the loop is preferred */
	    best_entry = entry_ptr;
	    best_priority = best_entry->priority;
	    best_address_h = curr_address_h;
	    best_hash_value = curr_hash_value;
	}
    }

    return best_entry;
}


rpentry_t *rp_find(uint32_t rp_address)
{
    cand_rp_t *cand_rp_ptr;
    uint32_t address_h = ntohl(rp_address);

    for(cand_rp_ptr = cand_rp_list; cand_rp_ptr != NULL; cand_rp_ptr = cand_rp_ptr->next) {
	if (ntohl(cand_rp_ptr->rpentry->address) > address_h)
	    continue;

	if (cand_rp_ptr->rpentry->address == rp_address)
	    return cand_rp_ptr->rpentry;

	return NULL;
    }

    return NULL;
}


/*
 * Create a bootstrap message in "send_buff" and returns the data size
 * (excluding the IP header and the PIM header) Can be used both by the
 * Bootstrap router to multicast the RP-set or by the DR to unicast it to
 * a new neighbor. It DOES NOT change any timers.
 */
int create_pim_bootstrap_message(char *send_buff)
{
    uint8_t *data_ptr;
    grp_mask_t *mask_ptr;
    rp_grp_entry_t *entry_ptr;
    int datalen;
    uint8_t masklen;

    if (curr_bsr_address == INADDR_ANY_N)
	return 0;

    data_ptr = (uint8_t *)(send_buff + sizeof(struct ip) + sizeof(pim_header_t));
    if (curr_bsr_address == my_bsr_address)
	curr_bsr_fragment_tag++;

    PUT_HOSTSHORT(curr_bsr_fragment_tag, data_ptr);
    MASK_TO_MASKLEN(curr_bsr_hash_mask, masklen);
    PUT_BYTE(masklen, data_ptr);
    PUT_BYTE(curr_bsr_priority, data_ptr);
    PUT_EUADDR(curr_bsr_address, data_ptr);

    /* TODO: XXX: No fragmentation support (yet) */
    for (mask_ptr = grp_mask_list; mask_ptr; mask_ptr = mask_ptr->next) {
	if (IN_PIM_SSM_RANGE(mask_ptr->group_addr)) {
	    continue;  /* Do not advertise internal virtual RP for SSM groups */
	}
	MASK_TO_MASKLEN(mask_ptr->group_mask, masklen);
	PUT_EGADDR(mask_ptr->group_addr, masklen, 0, data_ptr);
	PUT_BYTE(mask_ptr->group_rp_number, data_ptr);
	PUT_BYTE(mask_ptr->group_rp_number, data_ptr); /* TODO: if frag.*/
	PUT_HOSTSHORT(0, data_ptr);

	for (entry_ptr = mask_ptr->grp_rp_next; entry_ptr; entry_ptr = entry_ptr->grp_rp_next) {
	    PUT_EUADDR(entry_ptr->rp->rpentry->address, data_ptr);
	    PUT_HOSTSHORT(entry_ptr->holdtime, data_ptr);
	    PUT_BYTE(entry_ptr->priority, data_ptr);
	    PUT_BYTE(0, data_ptr);  /* The reserved field */
	}
    }

    datalen = (data_ptr - (uint8_t *)send_buff) - sizeof(struct ip) - sizeof(pim_header_t);

    return datalen;
}


/*
 * Check if the addr is the RP for the group corresponding to mrt.
 * Return TRUE or FALSE.
 */
int check_mrtentry_rp(mrtentry_t *mrt, uint32_t addr)
{
    rp_grp_entry_t *ptr;

    if (!mrt)
	return FALSE;

    if (addr == INADDR_ANY_N)
	return FALSE;

    ptr = mrt->group->active_rp_grp;
    if (!ptr)
	return FALSE;

    if (mrt->group->rpaddr == addr)
	return TRUE;

    return FALSE;
}

/**
 * Local Variables:
 *  version-control: t
 *  indent-tabs-mode: t
 *  c-file-style: "ellemtel"
 *  c-basic-offset: 4
 * End:
 */
