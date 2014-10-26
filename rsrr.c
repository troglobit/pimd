/*
 * Copyright (c) 1993, 1998-2001.
 * The University of Southern California/Information Sciences Institute.
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

/* RSRR code written by Daniel Zappala, USC Information Sciences Institute,
 * April 1995.
 */

/* May 1995 -- Added support for Route Change Notification */

#ifdef RSRR

#include "defs.h"
#include <sys/param.h>
#ifdef HAVE_SA_LEN
#include <stddef.h>		/* for offsetof */
#endif

/* 
 * Local RSRR variables.
 */
static int rsrr_socket;		/* interface to reservation protocol */
static char *rsrr_recv_buf;    	/* RSRR receive buffer */
static char *rsrr_send_buf;    	/* RSRR send buffer */

static struct sockaddr_un client_addr;
static socklen_t client_length = sizeof(client_addr);

/*
 * Procedure definitions needed internally.
 */
static void	rsrr_accept    (size_t recvlen);
static void	rsrr_accept_iq (void);
static int	rsrr_accept_rq (struct rsrr_rq *route_query, uint8_t flags, struct gtable *gt_notify);
static void	rsrr_read      (int, fd_set *);
static int	rsrr_send      (int sendlen);
static void	rsrr_cache     (struct gtable *gt, struct rsrr_rq *route_query);

/* Initialize RSRR socket */
void rsrr_init(void)
{
    int servlen;
    struct sockaddr_un serv_addr;

    rsrr_recv_buf = (char *)calloc(1, RSRR_MAX_LEN);
    rsrr_send_buf = (char *)calloc(1, RSRR_MAX_LEN);
    if (!rsrr_recv_buf || !rsrr_send_buf)
	logit(LOG_ERR, 0, "Ran out of memory in rsrr_init()");

    if ((rsrr_socket = socket(AF_UNIX, SOCK_DGRAM, 0)) < 0)
	logit(LOG_ERR, errno, "Cannot create RSRR socket");

    unlink(RSRR_SERV_PATH);
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sun_family = AF_UNIX;
    strlcpy(serv_addr.sun_path, RSRR_SERV_PATH, sizeof(serv_addr.sun_path));
#ifdef HAVE_SA_LEN
    servlen = offsetof(struct sockaddr_un, sun_path) + strlen(serv_addr.sun_path);
    serv_addr.sun_len = servlen;
#else
    servlen = sizeof(serv_addr.sun_family) + strlen(serv_addr.sun_path);
#endif
 
    if (bind(rsrr_socket, (struct sockaddr *) &serv_addr, servlen) < 0)
	logit(LOG_ERR, errno, "Cannot bind RSRR socket");

    if (register_input_handler(rsrr_socket, rsrr_read) < 0)
	logit(LOG_ERR, 0, "Could not register RSRR as an input handler");
}

/* Read a message from the RSRR socket */
static void rsrr_read(int fd, fd_set *rfd __attribute__ ((unused)))
{
    ssize_t len;
    
    memset(&client_addr, 0, sizeof(client_addr));
    while ((len = recvfrom(fd, rsrr_recv_buf, sizeof(rsrr_recv_buf),
			   0, (struct sockaddr *)&client_addr, &client_length)) < 0) {
	if (errno == EINTR)
	    continue;		/* Received signal, retry syscall. */

	logit(LOG_ERR, errno, "Failed recvfrom() in rsrr_read()");
	return;
    }

    rsrr_accept(len);
}

/* Accept a message from the reservation protocol and take
 * appropriate action.
 */
static void rsrr_accept(size_t recvlen)
{
    struct rsrr_header *rsrr = (struct rsrr_header *)rsrr_recv_buf;
    struct rsrr_rq *route_query;
    
    if (recvlen < RSRR_HEADER_LEN) {
	logit(LOG_WARNING, 0, "Received RSRR packet of %d bytes, which is less than MIN size %d.",
	      recvlen, RSRR_HEADER_LEN);
	return;
    }
    
    if (rsrr->version > RSRR_MAX_VERSION || rsrr->version != 1) {
	logit(LOG_WARNING, 0, "Received RSRR packet version %d, which I don't understand",
	      rsrr->version);
	return;
    }

    switch (rsrr->type) {
	case RSRR_INITIAL_QUERY:
	    /* Send Initial Reply to client */
	    IF_DEBUG(DEBUG_RSRR) {
		logit(LOG_DEBUG, 0, "Received Initial Query\n");
	    }
	    rsrr_accept_iq();
	    break;

	case RSRR_ROUTE_QUERY:
	    /* Check size */
	    if (recvlen < RSRR_RQ_LEN) {
		logit(LOG_WARNING, 0, "Received Route Query of %d bytes, which is too small", recvlen);
		break;
	    }
	    /* Get the query */
	    route_query = (struct rsrr_rq *) (rsrr_recv_buf + RSRR_HEADER_LEN);
	    IF_DEBUG(DEBUG_RSRR) {
		logit(LOG_DEBUG, 0,
		      "Received Route Query for src %s grp %s notification %d",
		      inet_fmt(route_query->source_addr, s1, sizeof(s1)),
		      inet_fmt(route_query->dest_addr, s2, sizeof(s2)),
		      BIT_TST(rsrr->flags,RSRR_NOTIFICATION_BIT));
	    }
	    /* Send Route Reply to client */
	    rsrr_accept_rq(route_query, rsrr->flags, NULL);
	    break;

	default:
	    logit(LOG_WARNING, 0, "Received RSRR packet type %d, which I don't handle", rsrr->type);
	    break;
    }
}

/* Send an Initial Reply to the reservation protocol. */
/*
 * TODO: XXX: if a new interfaces come up and _IF_ the multicast routing
 * daemon automatically include it, have to inform the RSVP daemon.
 * However, this is not in the RSRRv1 draft (just expired and is not
 * available anymore from the internet-draft ftp sites). Probably has to
 * be included in RSRRv2.
 */
static void rsrr_accept_iq(void)
{
    struct rsrr_header *rsrr = (struct rsrr_header *)rsrr_send_buf;
    struct rsrr_vif *vif_list;
    struct uvif *v;
    vifi_t vifi;
    int sendlen;
    
    /* Check for space.  There should be room for plenty of vifs,
     * but we should check anyway.
     */
    if (numvifs > RSRR_MAX_VIFS) {
	logit(LOG_WARNING, 0,
	    "Cannot send RSRR Route Reply because %d is too many vifs %d",
	    numvifs);
	return;
    }
    
    /* Set up message */
    rsrr->version = 1;
    rsrr->type = RSRR_INITIAL_REPLY;
    rsrr->flags = 0;
    rsrr->num = numvifs;
    
    vif_list = (struct rsrr_vif *)(rsrr_send_buf + RSRR_HEADER_LEN);
    
    /* Include the vif list. */
    for (vifi = 0, v = uvifs; vifi < numvifs; vifi++, v++) {
	vif_list[vifi].id = vifi;
	vif_list[vifi].status = 0;
	if (v->uv_flags & VIFF_DISABLED)
	    BIT_SET(vif_list[vifi].status,RSRR_DISABLED_BIT);
	vif_list[vifi].threshold = v->uv_threshold;
	vif_list[vifi].local_addr = v->uv_lcl_addr;
    }
    
    /* Get the size. */
    sendlen = RSRR_HEADER_LEN + numvifs*RSRR_VIF_LEN;
    
    /* Send it. */
    IF_DEBUG(DEBUG_RSRR) {
	logit(LOG_DEBUG, 0, "Send RSRR Initial Reply");
    }
    rsrr_send(sendlen);
}

/* Send a Route Reply to the reservation protocol.  The Route Query
 * contains the query to which we are responding.  The flags contain
 * the incoming flags from the query or, for route change
 * notification, the flags that should be set for the reply.  The
 * kernel table entry contains the routing info to use for a route
 * change notification.
 */
/* XXX: must modify if your routing table structure/search is different */
static int rsrr_accept_rq(struct rsrr_rq *route_query, uint8_t flags, struct gtable *gt_notify)
{
    struct rsrr_header *rsrr = (struct rsrr_header *)rsrr_send_buf;
    struct rsrr_rr *route_reply;
    int sendlen;
    struct gtable *gt;
    int status_ok;
#ifdef PIM
    int found;
    uint8_t tmp_flags;
    rp_grp_entry_t *rp_grp_entry;
    grpentry_t *grpentry_ptr;
#else
    struct gtable local_g;
    struct rtentry *r;
    uint32_t mcastgrp;
#endif /* PIM */
    
    /* Set up message */
    rsrr->version = 1;
    rsrr->type = RSRR_ROUTE_REPLY;
    rsrr->flags = flags;
    rsrr->num = 0;
    
    route_reply = (struct rsrr_rr *) (rsrr_send_buf + RSRR_HEADER_LEN);
    route_reply->dest_addr = route_query->dest_addr;
    route_reply->source_addr = route_query->source_addr;
    route_reply->query_id = route_query->query_id;
    
    /* Blank routing entry for error. */
    route_reply->in_vif = 0;
    route_reply->reserved = 0;
    route_reply->out_vif_bm = 0;
    
    /* Get the size. */
    sendlen = RSRR_RR_LEN;
    
    /* If routing table entry is defined, then we are sending a Route Reply
     * due to a Route Change Notification event.  Use the routing table entry
     * to supply the routing info.
     */
    status_ok = FALSE;
#ifdef PIM
    if (gt_notify) {
	/* Include the routing entry. */
	route_reply->in_vif = gt_notify->incoming;
	route_reply->out_vif_bm = gt_notify->oifs;
	gt = gt_notify;
	status_ok = TRUE;
    } else if ((gt = find_route(route_query->source_addr,
				route_query->dest_addr,
				MRTF_SG | MRTF_WC | MRTF_PMBR,
				DONT_CREATE)) != (struct gtable *)NULL) {
	status_ok = TRUE;
	route_reply->in_vif = gt->incoming;
	route_reply->out_vif_bm = gt->oifs;
    }
    if (status_ok != TRUE) {
	/* Set error bit. */
	rsrr->flags = 0;
	BIT_SET(rsrr->flags, RSRR_ERROR_BIT);
    }
    else {
	if (gt->flags & (MRTF_WC | MRTF_PMBR)) {
	    tmp_flags = 0;
	    BIT_SET(tmp_flags, RSRR_THIS_SENDER_SHARED_TREE);
	    BIT_SET(tmp_flags, RSRR_ALL_SENDERS_SHARED_TREE);
	    if (!(flags & tmp_flags)) {
		/* Check whether need to setup the (*,G) related flags */
		found = FALSE;
		if (gt->flags & MRTF_PMBR) {
		    /* Check whether there is at least one (S,G) entry which is
		     * a longer match than this (*,*,RP) entry.
		     */
		    for (rp_grp_entry = gt->source->cand_rp->rp_grp_next;
			 rp_grp_entry != (rp_grp_entry_t *)NULL;
			 rp_grp_entry = rp_grp_entry->rp_grp_next) {
			for (grpentry_ptr = rp_grp_entry->grplink;
			     grpentry_ptr != (grpentry_t *)NULL;
			     grpentry_ptr = grpentry_ptr->rpnext) {
			    if (grpentry_ptr->mrtlink != (mrtentry_t *)NULL) {
				found = TRUE;
				break;
			    }
			}
			if (found == TRUE)
			    break;
		    }
		}
		else if (gt->flags & MRTF_WC) {
		    if (gt->group->mrtlink != (mrtentry_t *)NULL)
			found = TRUE;
		}
		if (found == TRUE) {
		    RSRR_THIS_SENDER_SHARED_TREE_SOME_OTHER_NOT(rsrr->flags);
		}
		else {
		    RSRR_SET_ALL_SENDERS_SHARED_TREE(rsrr->flags);
		}
	    }
	}
	/* Cache reply if using route change notification. */
	if (BIT_TST(flags, RSRR_NOTIFICATION_BIT)) {
	    /* TODO: XXX: Originally the rsrr_cache() call was first, but
	     * I think this is incorrect, because rsrr_cache() checks the
	     * rsrr_send_buf "flag" first.
	     */
	    BIT_SET(rsrr->flags, RSRR_NOTIFICATION_BIT);
	    rsrr_cache(gt, route_query);
	}
    }
#else /* Not PIM */
    if (gt_notify) {
	/* Set flags */
	rsrr->flags = flags;
	/* Include the routing entry. */
	/* The original code from mrouted-3.9b2 */
	route_reply->in_vif = gt_notify->gt_route->rt_parent;
	/* TODO: XXX: bug? See the PIM code above */
	if (BIT_TST(flags, RSRR_NOTIFICATION_BIT))
	    route_reply->out_vif_bm = gt_notify->gt_grpmems;
	else
	    route_reply->out_vif_bm = 0;
    } else if (find_src_grp(route_query->source_addr, 0,
			    route_query->dest_addr)) {

	/* Found kernel entry. Code taken from add_table_entry() */
	gt = gtp ? gtp->gt_gnext : kernel_table;
	
	/* Include the routing entry. */
	route_reply->in_vif = gt->gt_route->rt_parent;
	route_reply->out_vif_bm = gt->gt_grpmems;
	
	/* Cache reply if using route change notification. */
	if (BIT_TST(flags, RSRR_NOTIFICATION_BIT)) {
	    /* TODO: XXX: Originally the rsrr_cache() call was first, but
	     * I think this is incorrect, because rsrr_cache() checks the
	     * rsrr_send_buf "flag" first.
	     */
	    BIT_SET(rsrr->flags, RSRR_NOTIFICATION_BIT);
	    rsrr_cache(gt, route_query);
	}
    } else {
	/* No kernel entry; use routing table. */
	r = determine_route(route_query->source_addr);
	
	if (r != NULL) {
	    /* We need to mimic what will happen if a data packet
	     * is forwarded by multicast routing -- the kernel will
	     * make an upcall and mrouted will install a route in the kernel.
	     * Our outgoing vif bitmap should reflect what that table
	     * will look like.  Grab code from add_table_entry().
	     * This is gross, but it's probably better to be accurate.
	     */
	    
	    gt = &local_g;
	    mcastgrp = route_query->dest_addr;
	    
	    gt->gt_mcastgrp    	= mcastgrp;
	    gt->gt_grpmems	= 0;
	    gt->gt_scope	= 0;
	    gt->gt_route        = r;
	    
	    /* obtain the multicast group membership list */
	    determine_forwvifs(gt);
	    
	    /* Include the routing entry. */
	    route_reply->in_vif = gt->gt_route->rt_parent;
	    route_reply->out_vif_bm = gt->gt_grpmems;
	} else {
	    /* Set error bit. */
	    BIT_SET(rsrr->flags, RSRR_ERROR_BIT);
	}
    }
#endif /* pimd - mrouted specific code */
    
    IF_DEBUG(DEBUG_RSRR) {
	logit(LOG_DEBUG, 0, "%sSend RSRR Route Reply for src %s dst %s in vif %d out vif %d\n",
	      gt_notify ? "Route Change: " : "",
	      inet_fmt(route_reply->source_addr, s1, sizeof(s1)),
	      inet_fmt(route_reply->dest_addr, s2, sizeof(s2)),
	      route_reply->in_vif,route_reply->out_vif_bm);
    }

    /* Send it. */
    return rsrr_send(sendlen);
}

/* Send an RSRR message. */
static int rsrr_send(int sendlen)
{
    int error;
    
    while ((error = sendto(rsrr_socket, rsrr_send_buf, sendlen, 0,
			   (struct sockaddr *)&client_addr, client_length)) < 0)
    {
	if (errno == EINTR)
	    continue;		/* Received signal, retry syscall. */

	logit(LOG_WARNING, errno, "Failed sendto() in rsrr_send()");

	return error;
    } 

    if (error != sendlen)
	logit(LOG_WARNING, 0, "Sent only %d out of %d bytes on RSRR socket", error, sendlen);

    return error;
}

/* TODO: need to sort the rsrr_cache entries for faster access */
/* Cache a message being sent to a client.  Currently only used for
 * caching Route Reply messages for route change notification.
 */
static void rsrr_cache(struct gtable *gt, struct rsrr_rq *route_query)
{
    struct rsrr_cache *rc, **rcnp;
    struct rsrr_header *rsrr = (struct rsrr_header *)rsrr_send_buf;

#ifdef PIM
    rcnp = &gt->rsrr_cache;
#else
    rcnp = &gt->gt_rsrr_cache;
#endif /* PIM */
    while ((rc = *rcnp) != NULL) {
	if ((rc->route_query.source_addr == 
	     route_query->source_addr) &&
	    (rc->route_query.dest_addr == 
	     route_query->dest_addr) &&
	    (!strcmp(rc->client_addr.sun_path,client_addr.sun_path))) {
	    /* Cache entry already exists.
	     * Check if route notification bit has been cleared.
	     */
	    if (!BIT_TST(rsrr->flags, RSRR_NOTIFICATION_BIT)) {
		/* Delete cache entry. */
		*rcnp = rc->next;
		free(rc);
	    } else {
		/* Update */
		/* TODO: XXX: No need to update iif, oifs, flags */
		rc->route_query.query_id = route_query->query_id;
		IF_DEBUG(DEBUG_RSRR) {
		    logit(LOG_DEBUG, 0,
			  "Update cached query id %ld from client %s\n",
			  rc->route_query.query_id, rc->client_addr.sun_path);
		}
	    }
	    return;
	}
	rcnp = &rc->next;
    }

    /* Cache entry doesn't already exist.  Create one and insert at
     * front of list.
     */
    rc = (struct rsrr_cache *)calloc(1, sizeof(struct rsrr_cache));
    if (rc == NULL)
	logit(LOG_ERR, 0, "Ran out of memory in rsrr_cache()");

    rc->route_query.source_addr = route_query->source_addr;
    rc->route_query.dest_addr = route_query->dest_addr;
    rc->route_query.query_id = route_query->query_id;
    strlcpy(rc->client_addr.sun_path, client_addr.sun_path, sizeof(rc->client_addr.sun_path));
    rc->client_length = client_length;
#ifdef PIM
    rc->next = gt->rsrr_cache;
    gt->rsrr_cache = rc;
#else
    rc->next = gt->gt_rsrr_cache;
    gt->gt_rsrr_cache = rc;
#endif /* PIM */
    IF_DEBUG(DEBUG_RSRR) {
	logit(LOG_DEBUG, 0, "Cached query id %ld from client %s\n",
	      rc->route_query.query_id, rc->client_addr.sun_path);
    }
}

/* Send all the messages in the cache for particular routing entry.
 * Currently this is used to send all the cached Route Reply messages
 * for route change notification.
 */
void rsrr_cache_send(struct gtable *gt, int notify)
{
    struct rsrr_cache *rc, **rcnp;
    uint8_t flags = 0;

    if (notify) {
	BIT_SET(flags, RSRR_NOTIFICATION_BIT);
    }

#ifdef PIM
    rcnp = &gt->rsrr_cache;
#else
    rcnp = &gt->gt_rsrr_cache;
#endif /* PIM */

    while ((rc = *rcnp) != NULL) {
	if (rsrr_accept_rq(&rc->route_query, flags, gt) < 0) {
	    IF_DEBUG(DEBUG_RSRR) {
		logit(LOG_DEBUG, 0, "Deleting cached query id %ld from client %s\n",
		      rc->route_query.query_id,rc->client_addr.sun_path);
	    }
	    /* Delete cache entry. */
	    *rcnp = rc->next;
	    free(rc);
	} else {
	    rcnp = &rc->next;
	}
    }
}

/* Bring "up" the RSRR cache entries: the (S,G) entry brings up any
 * matching entry from (*,*,RP) or (*,G). The (*,G) entry brings up
 * any matching entries from (*,*,RP)
 */
void rsrr_cache_bring_up(struct gtable *gt)
{
    struct gtable *gt_rp, *gt_wide;
    uint8_t flags = 0;
    struct rsrr_cache *rc, **rcnp;

    if (gt == (struct gtable *)NULL)
	return;
    if (gt->flags & MRTF_PMBR)
	/* (*,*,RP) */
	return;
    if (gt->flags & MRTF_WC) {
	/* (*,G) */
	if (((gt_rp = gt->group->active_rp_grp->rp->rpentry->mrtlink) ==
	    (struct gtable *)NULL)
	    || (gt_rp->rsrr_cache == (struct rsrr_cache *)NULL))
	    return;
	if ((gt_rp->incoming == gt->incoming)
	    && (VIFM_SAME(gt->oifs, gt_rp->oifs))) {
	    /* The (iif, oifs) are the same. Just link to the new routing
	     * table entry. No need to send message to rsvpd */
	    rcnp = &gt_rp->rsrr_cache;
	    while ((rc = *rcnp) != NULL) {
		if (rc->route_query.dest_addr == gt->group->group) {
		    *rcnp = rc->next;
		    rc->next = gt->rsrr_cache;
		    gt->rsrr_cache = rc;
		}
		else {
		    rcnp = &rc->next;
		}
	    }
	}
	else {
	    /* Have to move the entries and at the same time
	     * send an update message to rsvpd for each of them.
	     */
	    /* TODO: XXX: this can be done faster */
	    rcnp = &gt_rp->rsrr_cache;
	    BIT_SET(flags, RSRR_NOTIFICATION_BIT);
	    if (gt->group->mrtlink != (mrtentry_t *)NULL) {
		RSRR_THIS_SENDER_SHARED_TREE_SOME_OTHER_NOT(flags);
	    }
            else {
                RSRR_SET_ALL_SENDERS_SHARED_TREE(flags);
	    }
	    while ((rc = *rcnp) != NULL) {
		if (rc->route_query.dest_addr == gt->group->group) {
		    *rcnp = rc->next;
		    if (rsrr_accept_rq(&rc->route_query, flags, gt) < 0) {
			IF_DEBUG(DEBUG_RSRR) {
			    logit(LOG_DEBUG, 0,
				  "Deleting cached query id %ld from client %s\n",
				  rc->route_query.query_id,
				  rc->client_addr.sun_path);
			}
		    }
		    /* Even on success have to delete it. */
		    *rcnp = rc->next;
		    free(rc);
		}
	    }
	}
	return;
    } /* end of (*,G) */
    
    if (gt->flags & MRTF_SG) {
	/* (S,G) */
	/* Check first (*,*,RP) */
	if (((gt_wide = gt->group->active_rp_grp->rp->rpentry->mrtlink) ==
	    (struct gtable *)NULL)
	    || (gt_wide->rsrr_cache == (struct rsrr_cache *)NULL)) {
	    if (((gt_wide = gt->group->grp_route) == (struct gtable *)NULL)
		|| (gt_wide->rsrr_cache == (struct rsrr_cache *)NULL))
		return;
	}
	BIT_SET(flags, RSRR_NOTIFICATION_BIT);
    try_again:
	rcnp = &gt_wide->rsrr_cache;
	while((rc = *rcnp) != NULL) {
	    if ((rc->route_query.dest_addr == gt->group->group)
		&& (rc->route_query.source_addr ==
		    gt->source->address)) {
		/* Found it. Need just this entry */
		*rcnp = rc->next; /* Free from the original chain */
		if ((gt_wide->incoming == gt->incoming)
		    && (VIFM_SAME(gt_wide->oifs, gt->oifs))) {
		    /* The (iif, oifs) are the same. Just link to the
		     * new routing table entry. No need to send
		     * message to rsvpd
		     */
		    rc->next = gt->rsrr_cache;
		    gt->rsrr_cache = rc;
		}
		else {
		    /* The iif and/or oifs are different. Send a message
		     * to rsvpd
		     */
		    if (rsrr_accept_rq(&rc->route_query, flags, gt) < 0) {
			IF_DEBUG(DEBUG_RSRR) {
			    logit(LOG_DEBUG, 0,
				  "Deleting cached query id %ld from client %s\n",
				  rc->route_query.query_id,
				  rc->client_addr.sun_path);
			}
		    }
		    /* Even on success have to delete it. */
		    free(rc);
		}
		return;
	    }
	}
	if (gt_wide->flags & MRTF_PMBR) {
	    if (((gt_wide = gt->group->grp_route) == (struct gtable *)NULL)
		|| (gt_wide->rsrr_cache == (struct rsrr_cache *)NULL))
		return;
	    goto try_again;
	}
    }
}

/* Clean the cache by deleting or moving all entries. */
/* XXX: for PIM, if the routing entry is (S,G), will try first to
 * "transfer" the RSRR cache entry to the (*,G) or (*,*,RP) routing entry
 * (if any). If the current routing entry is (*,G), it will move the
 * cache entries to the (*,*,RP) routing entry (if existing).
 * If the old and the new (iif, oifs) are the same, then no need to send
 * route change message to the reservation daemon: just plug all entries at
 * the front of the rsrr_cache chain.
 */
void rsrr_cache_clean(struct gtable *gt)
{
    struct rsrr_cache *rc, *rc_next, **rcnp;
    struct gtable *gt_wide;
#ifdef PIM
    uint8_t flags = 0;

    IF_DEBUG(DEBUG_RSRR) {
	if (gt->flags & MRTF_SG)
	    logit(LOG_DEBUG, 0, "cleaning cache for source %s and group %s",
		  inet_fmt(gt->source->address, s1, sizeof(s1)),
		  inet_fmt(gt->group->group, s2, sizeof(s2)));
	else if (gt->flags & MRTF_WC)
	    logit(LOG_DEBUG, 0, "cleaning cache for group %s and ANY sources",
		  inet_fmt(gt->group->group, s1, sizeof(s1)));
	else if (gt->flags & MRTF_PMBR)
	    logit(LOG_DEBUG, 0,
		  "cleaning cache for ALL groups matching to RP %s",
		  inet_fmt(gt->source->address, s1, sizeof(s1)));
    }
    rc = gt->rsrr_cache;
    if (rc == (struct rsrr_cache *)NULL)
	return;
    if (gt->flags & MRTF_SG) {
	if ((gt_wide = gt->group->grp_route) == (struct gtable *)NULL)
	    gt_wide = gt->group->active_rp_grp->rp->rpentry->mrtlink;
    }
    else if (gt->flags & MRTF_WC)
	gt_wide = gt->group->active_rp_grp->rp->rpentry->mrtlink;
    else
	gt_wide = (struct gtable *)NULL;
    
    if (gt_wide == (struct gtable *)NULL) {
	/* No routing entry where to move down the rsrr cache entry.
	 * Send a message with "cannot_notify" bit set.
	 */
	rsrr_cache_send(gt, 0);
	while (rc) {
	    rc_next = rc->next;
	    free(rc);
	    rc = rc_next;
	}
    } else if ((gt_wide->incoming == gt->incoming)
	       && (VIFM_SAME(gt->oifs, gt_wide->oifs))) {
	/* The (iif, oifs) are the same. Just move to the beginning of the
	 * RSRR cache chain. No need to send message */
	while (rc->next != (struct rsrr_cache *)NULL)
	    rc = rc->next;
	rc->next = gt_wide->rsrr_cache;
	gt_wide->rsrr_cache = gt->rsrr_cache;
    }
    else {
	/* Have to move to the RSRR cache entries and at the same time
	 * send an update for each of them.
	 */
	rcnp = &gt->rsrr_cache;
	BIT_SET(flags, RSRR_NOTIFICATION_BIT);
	if (gt->group->mrtlink != (mrtentry_t *)NULL) {
	    RSRR_THIS_SENDER_SHARED_TREE_SOME_OTHER_NOT(flags);
	}
	else {
	    RSRR_SET_ALL_SENDERS_SHARED_TREE(flags);
	}
 	while ((rc = *rcnp) != NULL) {
	    if (rsrr_accept_rq(&rc->route_query, flags, gt_wide) < 0) {
		IF_DEBUG(DEBUG_RSRR) {
		    logit(LOG_DEBUG, 0,
			  "Deleting cached query id %ld from client %s\n",
			  rc->route_query.query_id, rc->client_addr.sun_path);
		}
		/* Delete cache entry. */
		*rcnp = rc->next;
		free(rc);
	    } else {
		rcnp = &rc->next;
	    }
	}
    }
    gt->rsrr_cache = (struct rsrr_cache *)NULL;
    
#else
    IF_DEBUG(DEBUG_RSRR) {
        logit(LOG_DEBUG, 0, "cleaning cache for group %s\n",
	      inet_fmt(gt->gt_mcastgrp, s1, sizeof(s1)));
    }
    rc = gt->gt_rsrr_cache;
    while (rc) {
	rc_next = rc->next;
	free(rc);
	rc = rc_next;
    }
    gt->gt_rsrr_cache = NULL;
#endif /* PIM */
}

void rsrr_clean(void)
{
    unlink(RSRR_SERV_PATH);
}

#else  /* !RSRR */
static int dummy __attribute__((unused));
#endif /* RSRR */

/**
 * Local Variables:
 *  version-control: t
 *  indent-tabs-mode: t
 *  c-file-style: "ellemtel"
 *  c-basic-offset: 4
 * End:
 */
