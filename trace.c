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
 *  $Id: trace.c,v 1.13 2002/09/26 00:59:30 pavlin Exp $
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
#include "trace.h"


/* TODO: XXX: implementation incompleted and buggy; check Kame's pim6sd */
/*
 * Traceroute function which returns traceroute replies to the requesting
 * router. Also forwards the request to downstream routers.
 */
void accept_mtrace(uint32_t src, uint32_t dst, uint32_t group, char *data, u_int no, int datalen)
{
    uint8_t type;
    mrtentry_t *mrt;
    struct tr_query *qry;
    struct tr_resp  *resp;
    int vifi;
    char *p;
    u_int rcount;
    int errcode = TR_NO_ERR;
    int resptype;
    struct timeval tp;
    struct sioc_vif_req v_req;
#if 0
    /* TODO */
    struct sioc_sg_req sg_req;
#endif /* 0 */
    uint32_t parent_address = INADDR_ANY;

    /* Remember qid across invocations */
    static uint32_t oqid = 0;

    /* timestamp the request/response */
    gettimeofday(&tp, 0);

    /*
     * Check if it is a query or a response
     */
    if (datalen == QLEN) {
        type = QUERY;
        IF_DEBUG(DEBUG_TRACE) {
            logit(LOG_DEBUG, 0, "Initial traceroute query rcvd from %s to %s",
                  inet_fmt(src, s1, sizeof(s1)), inet_fmt(dst, s2, sizeof(s2)));
	}
    } else if ((datalen - QLEN) % RLEN == 0) {
        type = RESP;
        IF_DEBUG(DEBUG_TRACE) {
            logit(LOG_DEBUG, 0, "In-transit traceroute query rcvd from %s to %s",
                  inet_fmt(src, s1, sizeof(s1)), inet_fmt(dst, s2, sizeof(s2)));
	}
        if (IN_MULTICAST(ntohl(dst))) {
            IF_DEBUG(DEBUG_TRACE) {
                logit(LOG_DEBUG, 0, "Dropping multicast response");
	    }
            return;
        }
    } else {
        logit(LOG_WARNING, 0, "%s from %s to %s",
              "Non decipherable traceroute request received",
              inet_fmt(src, s1, sizeof(s1)), inet_fmt(dst, s2, sizeof(s2)));
        return;
    }

    qry = (struct tr_query *)data;

    /*
     * if it is a packet with all reports filled, drop it
     */
    if ((rcount = (datalen - QLEN)/RLEN) == no) {
        IF_DEBUG(DEBUG_TRACE)
            logit(LOG_DEBUG, 0, "packet with all reports filled in");
        return;
    }

    IF_DEBUG(DEBUG_TRACE) {
        logit(LOG_DEBUG, 0, "s: %s g: %s d: %s ", inet_fmt(qry->tr_src, s1, sizeof(s1)),
              inet_fmt(group, s2, sizeof(s2)), inet_fmt(qry->tr_dst, s3, sizeof(s3)));
        logit(LOG_DEBUG, 0, "rttl: %d rd: %s", qry->tr_rttl,
              inet_fmt(qry->tr_raddr, s1, sizeof(s1)));
        logit(LOG_DEBUG, 0, "rcount:%d, qid:%06x", rcount, qry->tr_qid);
    }

    /* determine the routing table entry for this traceroute */
    mrt = find_route(qry->tr_src, group, MRTF_SG | MRTF_WC | MRTF_PMBR,
                     DONT_CREATE);
    IF_DEBUG(DEBUG_TRACE) {
        if (mrt != (mrtentry_t *)NULL) {
            if (mrt->upstream != (pim_nbr_entry_t *)NULL)
                parent_address = mrt->upstream->address;
            else
                parent_address = INADDR_ANY;
            logit(LOG_DEBUG, 0, "mrt parent vif: %d rtr: %s metric: %d",
                  mrt->incoming, inet_fmt(parent_address, s1, sizeof(s1)), mrt->metric);
/* TODO
   logit(LOG_DEBUG, 0, "mrt origin %s",
   RT_FMT(rt, s1));
*/
        } else {
            logit(LOG_DEBUG, 0, "...no route");
        }
    }

    /*
     * Query type packet - check if rte exists
     * Check if the query destination is a vif connected to me.
     * and if so, whether I should start response back
     */
    if (type == QUERY) {
        if (oqid == qry->tr_qid) {
            /*
             * If the multicast router is a member of the group being
             * queried, and the query is multicasted, then the router can
             * recieve multiple copies of the same query.  If we have already
             * replied to this traceroute, just ignore it this time.
             *
             * This is not a total solution, but since if this fails you
             * only get N copies, N <= the number of interfaces on the router,
             * it is not fatal.
             */
            IF_DEBUG(DEBUG_TRACE) {
                logit(LOG_DEBUG, 0, "ignoring duplicate traceroute packet");
	    }
            return;
        }

        if (!mrt) {
            IF_DEBUG(DEBUG_TRACE) {
                logit(LOG_DEBUG, 0, "Mcast traceroute: no route entry %s", inet_fmt(qry->tr_src, s1, sizeof(s1)));
	    }
            if (IN_MULTICAST(ntohl(dst)))
                return;
        }

        vifi = find_vif_direct(qry->tr_dst);
        if (vifi == NO_VIF) {
            /* The traceroute destination is not on one of my subnet vifs. */
            IF_DEBUG(DEBUG_TRACE) {
                logit(LOG_DEBUG, 0, "Destination %s not an interface", inet_fmt(qry->tr_dst, s1, sizeof(s1)));
	    }
            if (IN_MULTICAST(ntohl(dst)))
                return;
            errcode = TR_WRONG_IF;
        } else if (mrt && !VIFM_ISSET(vifi, mrt->oifs)) {
            IF_DEBUG(DEBUG_TRACE) {
                logit(LOG_DEBUG, 0,
                      "Destination %s not on forwarding tree for src %s",
                      inet_fmt(qry->tr_dst, s1, sizeof(s1)), inet_fmt(qry->tr_src, s2, sizeof(s2)));
	    }
            if (IN_MULTICAST(ntohl(dst)))
                return;
            errcode = TR_WRONG_IF;
        }
    } else {
        /*
         * determine which interface the packet came in on
         * RESP packets travel hop-by-hop so this either traversed
         * a tunnel or came from a directly attached mrouter.
         */
	vifi = find_vif_direct(src);
	if (vifi == NO_VIF) {
	    IF_DEBUG(DEBUG_TRACE) {
		logit(LOG_DEBUG, 0, "Wrong interface for packet");
	    }
            errcode = TR_WRONG_IF;
        }
    }

    /* Now that we've decided to send a response, save the qid */
    oqid = qry->tr_qid;

    IF_DEBUG(DEBUG_TRACE) {
        logit(LOG_DEBUG, 0, "Sending traceroute response");
    }

    /* copy the packet to the sending buffer */
    p = igmp_send_buf + IP_IGMP_HEADER_LEN + IGMP_MINLEN;
    bcopy(data, p, datalen);
    p += datalen;

    /*
     * If there is no room to insert our reply, coopt the previous hop
     * error indication to relay this fact.
     */
    if (p + sizeof(struct tr_resp) > igmp_send_buf + SEND_BUF_SIZE) {
        resp = (struct tr_resp *)p - 1;
        resp->tr_rflags = TR_NO_SPACE;
        mrt = NULL;
        goto sendit;
    }

    /*
     * fill in initial response fields
     */
    resp = (struct tr_resp *)p;
    memset(resp, 0, sizeof(struct tr_resp));
    datalen += RLEN;

    resp->tr_qarr    = htonl(((tp.tv_sec + JAN_1970) << 16) +
			     ((tp.tv_usec << 10) / 15625));
    resp->tr_rproto  = PROTO_PIM;
    resp->tr_outaddr = (vifi == NO_VIF) ? dst : uvifs[vifi].uv_lcl_addr;
    resp->tr_fttl    = (vifi == NO_VIF) ? 0   : uvifs[vifi].uv_threshold;
    resp->tr_rflags  = errcode;

    /*
     * obtain # of packets out on interface
     */
    v_req.vifi = vifi;
    if (vifi != NO_VIF && ioctl(udp_socket, SIOCGETVIFCNT, (char *)&v_req) >= 0)
        resp->tr_vifout  =  htonl(v_req.ocount);
    else
        resp->tr_vifout  =  0xffffffff;

    /*
     * fill in scoping & pruning information
     */
/* TODO */
#if 0
    if (mrt) {
        for (gt = rt->rt_groups; gt; gt = gt->gt_next) {
            if (gt->gt_mcastgrp >= group)
                break;
        }
    } else {
        gt = NULL;
    }

    if (gt && gt->gt_mcastgrp == group) {
        struct stable *st;

        for (st = gt->gt_srctbl; st; st = st->st_next) {
            if (qry->tr_src == st->st_origin)
                break;
	}

        sg_req.src.s_addr = qry->tr_src;
        sg_req.grp.s_addr = group;
        if (st && st->st_ctime != 0 &&
            ioctl(udp_socket, SIOCGETSGCNT, (char *)&sg_req) >= 0)
            resp->tr_pktcnt = htonl(sg_req.pktcnt + st->st_savpkt);
        else
            resp->tr_pktcnt = htonl(st ? st->st_savpkt : 0xffffffff);

        if (VIFM_ISSET(vifi, gt->gt_scope)) {
            resp->tr_rflags = TR_SCOPED;
	} else if (gt->gt_prsent_timer) {
            resp->tr_rflags = TR_PRUNED;
        } else if (!VIFM_ISSET(vifi, gt->gt_grpmems)) {
            if (VIFM_ISSET(vifi, rt->rt_children) &&
                NBRM_ISSETMASK(uvifs[vifi].uv_nbrmap, rt->rt_subordinates)) /*XXX*/
                resp->tr_rflags = TR_OPRUNED;
            else
                resp->tr_rflags = TR_NO_FWD;
	}
    } else {
        if (scoped_addr(vifi, group))
            resp->tr_rflags = TR_SCOPED;
        else if (rt && !VIFM_ISSET(vifi, rt->rt_children))
            resp->tr_rflags = TR_NO_FWD;
    }
#endif /* 0 */

    /*
     *  if no rte exists, set NO_RTE error
     */
    if (!mrt) {
        src = dst;		/* the dst address of resp. pkt */
        resp->tr_inaddr   = 0;
        resp->tr_rflags   = TR_NO_RTE;
        resp->tr_rmtaddr  = 0;
    } else {
        /* get # of packets in on interface */
        v_req.vifi = mrt->incoming;
        if (ioctl(udp_socket, SIOCGETVIFCNT, (char *)&v_req) >= 0)
            resp->tr_vifin = htonl(v_req.icount);
        else
            resp->tr_vifin = 0xffffffff;

        /* TODO
           MASK_TO_VAL(rt->rt_originmask, resp->tr_smask);
        */
        src = uvifs[mrt->incoming].uv_lcl_addr;
        resp->tr_inaddr = src;
        if (mrt->upstream)
            parent_address = mrt->upstream->address;
        else
            parent_address = INADDR_ANY;

        resp->tr_rmtaddr = parent_address;
        if (vifi != NO_VIF && !VIFM_ISSET(vifi, mrt->oifs)) {
            IF_DEBUG(DEBUG_TRACE)
                logit(LOG_DEBUG, 0, "Destination %s not on forwarding tree for src %s",
                      inet_fmt(qry->tr_dst, s1, sizeof(s1)), inet_fmt(qry->tr_src, s2, sizeof(s2)));
            resp->tr_rflags = TR_WRONG_IF;
        }
#if 0
        if (rt->rt_metric >= UNREACHABLE) {
            resp->tr_rflags = TR_NO_RTE;
            /* Hack to send reply directly */
            rt = NULL;
        }
#endif /* 0 */
    }

  sendit:
    /*
     * if metric is 1 or no. of reports is 1, send response to requestor
     * else send to upstream router.  If the upstream router can't handle
     * mtrace, set an error code and send to requestor anyway.
     */
    IF_DEBUG(DEBUG_TRACE)
        logit(LOG_DEBUG, 0, "rcount:%d, no:%d", rcount, no);

    if ((rcount + 1 == no) || (mrt == NULL) || (mrt->metric == 1)) {
        resptype = IGMP_MTRACE_RESP;
        dst = qry->tr_raddr;
    } else {
#if 0   /* TODO */
	if (!can_mtrace(rt->rt_parent, rt->rt_gateway)) {
	    dst = qry->tr_raddr;
	    resp->tr_rflags = TR_OLD_ROUTER;
	    resptype = IGMP_MTRACE_RESP;
	} else {
#endif  /* 0 */
	    if (mrt->upstream)
		parent_address = mrt->upstream->address;
	    else
		parent_address = INADDR_ANY;
	    dst = parent_address;
	    resptype = IGMP_MTRACE;
#if 0   /* TODO */
	}
#endif
    }

    if (IN_MULTICAST(ntohl(dst))) {
	/*
	 * Send the reply on a known multicast capable vif.
	 * If we don't have one, we can't source any multicasts anyway.
	 */
	if (phys_vif != -1) {
	    IF_DEBUG(DEBUG_TRACE)
		logit(LOG_DEBUG, 0, "Sending reply to %s from %s",
		      inet_fmt(dst, s1, sizeof(s1)),
		      inet_fmt(uvifs[phys_vif].uv_lcl_addr, s2, sizeof(s2)));
	    k_set_ttl(igmp_socket, qry->tr_rttl);
	    send_igmp(igmp_send_buf, uvifs[phys_vif].uv_lcl_addr, dst,
		      resptype, no, group, datalen);
	    k_set_ttl(igmp_socket, 1);
	} else {
	    logit(LOG_INFO, 0, "No enabled phyints -- dropping traceroute reply");
	}
    } else {
	IF_DEBUG(DEBUG_TRACE) {
	    logit(LOG_DEBUG, 0, "Sending %s to %s from %s",
		  resptype == IGMP_MTRACE_RESP ?  "reply" : "request on",
		  inet_fmt(dst, s1, sizeof(s1)), inet_fmt(src, s2, sizeof(s2)));
	}
	send_igmp(igmp_send_buf, src, dst, resptype, no, group, datalen);
    }
}


/*
 * accept_neighbor_request() supports some old DVMRP messages from mrinfo.
 * Haven't tested it, because I have only the new mrinfo.
 */
void accept_neighbor_request(uint32_t src, uint32_t dst __attribute__((unused)))
{
    vifi_t vifi;
    struct uvif *v;
    uint8_t *p, *ncount;
/*    struct listaddr *la; */
    pim_nbr_entry_t *pim_nbr;
    int datalen;
    uint32_t temp_addr, them = src;

#define PUT_ADDR(a)     temp_addr = ntohl(a);	\
    *p++ = temp_addr >> 24;			\
    *p++ = (temp_addr >> 16) & 0xFF;		\
    *p++ = (temp_addr >> 8) & 0xFF;		\
    *p++ = temp_addr & 0xFF;

    p = (uint8_t *) (igmp_send_buf + IP_IGMP_HEADER_LEN + IGMP_MINLEN);
    datalen = 0;

    for (vifi = 0, v = uvifs; vifi < numvifs; vifi++, v++) {
	if (v->uv_flags & VIFF_DISABLED)
	    continue;

	ncount = 0;

	/* TODO: XXX: if we are PMBR, then check the DVMRP interfaces too */
	for (pim_nbr = v->uv_pim_neighbors; pim_nbr != (pim_nbr_entry_t *)NULL;
	     pim_nbr = pim_nbr->next) {
	    /* Make sure that there's room for this neighbor... */
	    if (datalen + (ncount == 0 ? 4 + 3 + 4 : 4) > MAX_DVMRP_DATA_LEN) {
		send_igmp(igmp_send_buf, INADDR_ANY, them, IGMP_DVMRP,
			  DVMRP_NEIGHBORS, htonl(PIMD_LEVEL), datalen);
		p = (uint8_t *) (igmp_send_buf + IP_IGMP_HEADER_LEN + IGMP_MINLEN);
		datalen = 0;
		ncount = 0;
	    }

	    /* Put out the header for this neighbor list... */
	    if (ncount == 0) {
		PUT_ADDR(v->uv_lcl_addr);
		*p++ = v->uv_metric;
		*p++ = v->uv_threshold;
		ncount = p;
		*p++ = 0;
		datalen += 4 + 3;
	    }

	    PUT_ADDR(pim_nbr->address);
	    datalen += 4;
	    (*ncount)++;
	}
    }

    if (datalen != 0)
	send_igmp(igmp_send_buf, INADDR_ANY, them, IGMP_DVMRP, DVMRP_NEIGHBORS,
		  htonl(PIMD_LEVEL), datalen);
}


/*
 * Send a list of all of our neighbors to the requestor, `src'.
 * Used for mrinfo support.
 * XXX: currently, we cannot specify the used multicast routing protocol;
 * only a protocol version is returned.
 */
void accept_neighbor_request2(uint32_t src, uint32_t dst __attribute__((unused)))
{
    vifi_t vifi;
    struct uvif *v;
    uint8_t *p, *ncount;
/*    struct listaddr *la; */
    pim_nbr_entry_t *pim_nbr;
    int datalen;
    uint32_t them = src;

    p = (uint8_t *) (igmp_send_buf + IP_IGMP_HEADER_LEN + IGMP_MINLEN);
    datalen = 0;

    for (vifi = 0, v = uvifs; vifi < numvifs; vifi++, v++) {
	uint32_t vflags = v->uv_flags;
	uint8_t rflags = 0;

	if (vflags & VIFF_TUNNEL)
	    rflags |= DVMRP_NF_TUNNEL;
	if (vflags & VIFF_SRCRT)
	    rflags |= DVMRP_NF_SRCRT;
	if (vflags & VIFF_PIM_NBR)
	    rflags |= DVMRP_NF_PIM;
	if (vflags & VIFF_DOWN)
	    rflags |= DVMRP_NF_DOWN;
	if (vflags & VIFF_DISABLED)
	    rflags |= DVMRP_NF_DISABLED;
	if (vflags & VIFF_QUERIER)
	    rflags |= DVMRP_NF_QUERIER;
	if (vflags & VIFF_LEAF)
	    rflags |= DVMRP_NF_LEAF;

	ncount = 0;
	pim_nbr = v->uv_pim_neighbors;
	if (pim_nbr == (pim_nbr_entry_t *)NULL) {
	    /*
	     * include down & disabled interfaces and interfaces on
	     * leaf nets.
	     */
	    if (rflags & DVMRP_NF_TUNNEL)
		rflags |= DVMRP_NF_DOWN;

	    if (datalen > MAX_DVMRP_DATA_LEN - 12) {
		send_igmp(igmp_send_buf, INADDR_ANY, them, IGMP_DVMRP,
			  DVMRP_NEIGHBORS2, htonl(PIMD_LEVEL), datalen);
		p = (uint8_t *) (igmp_send_buf + IP_IGMP_HEADER_LEN + IGMP_MINLEN);
		datalen = 0;
	    }

	    *(u_int*)p = v->uv_lcl_addr;
	    p += 4;
	    *p++ = v->uv_metric;
	    *p++ = v->uv_threshold;
	    *p++ = rflags;
	    *p++ = 1;
	    *(u_int*)p =  v->uv_rmt_addr;
	    p += 4;
	    datalen += 12;
	} else {
	    for ( ; pim_nbr; pim_nbr = pim_nbr->next) {
		/* Make sure that there's room for this neighbor... */
		if (datalen + (ncount == 0 ? 4+4+4 : 4) > MAX_DVMRP_DATA_LEN) {
		    send_igmp(igmp_send_buf, INADDR_ANY, them, IGMP_DVMRP,
			      DVMRP_NEIGHBORS2, htonl(PIMD_LEVEL), datalen);
		    p = (uint8_t *) (igmp_send_buf + IP_IGMP_HEADER_LEN + IGMP_MINLEN);
		    datalen = 0;
		    ncount = 0;
		}

		/* Put out the header for this neighbor list... */
		if (ncount == 0) {
		    *(u_int*)p = v->uv_lcl_addr;
		    p += 4;
		    *p++ = v->uv_metric;
		    *p++ = v->uv_threshold;
		    *p++ = rflags;
		    ncount = p;
		    *p++ = 0;
		    datalen += 4 + 4;
		}

		*(u_int*)p = pim_nbr->address;
		p += 4;
		datalen += 4;
		(*ncount)++;
	    }
	}
    }

    if (datalen != 0)
	send_igmp(igmp_send_buf, INADDR_ANY, them, IGMP_DVMRP,
		  DVMRP_NEIGHBORS2, htonl(PIMD_LEVEL), datalen);
}

/**
 * Local Variables:
 *  version-control: t
 *  indent-tabs-mode: t
 *  c-file-style: "ellemtel"
 *  c-basic-offset: 4
 * End:
 */
