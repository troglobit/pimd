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
 *  $Id: trace.h,v 1.7 2001/09/10 20:31:37 pavlin Exp $
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


/*
 * The packet format for a traceroute request.
 */
struct tr_query {
    uint32_t  tr_src;		/* traceroute source */
    uint32_t  tr_dst;		/* traceroute destination */
    uint32_t  tr_raddr;		/* traceroute response address */
#if defined(BYTE_ORDER) && (BYTE_ORDER == LITTLE_ENDIAN)
    struct {
	u_int	qid : 24;	/* traceroute query id */
	u_int	ttl : 8;	/* traceroute response ttl */
    } q;
#else
    struct {
	u_int   ttl : 8;	/* traceroute response ttl */
	u_int   qid : 24;	/* traceroute query id */
    } q;
#endif /* BYTE_ORDER */
};

#define tr_rttl q.ttl
#define tr_qid  q.qid

/*
 * Traceroute response format.  A traceroute response has a tr_query at the
 * beginning, followed by one tr_resp for each hop taken.
 */
struct tr_resp {
    uint32_t tr_qarr;		/* query arrival time */
    uint32_t tr_inaddr;		/* incoming interface address */
    uint32_t tr_outaddr;	/* outgoing interface address */
    uint32_t tr_rmtaddr;	/* parent address in source tree */
    uint32_t tr_vifin;		/* input packet count on interface */
    uint32_t tr_vifout;		/* output packet count on interface */
    uint32_t tr_pktcnt;		/* total incoming packets for src-grp */
    uint8_t  tr_rproto;		/* routing protocol deployed on router */
    uint8_t  tr_fttl;		/* ttl required to forward on outvif */
    uint8_t  tr_smask;		/* subnet mask for src addr */
    uint8_t  tr_rflags;		/* forwarding error codes */
};

/* defs within mtrace */
#define QUERY	1
#define RESP	2
#define QLEN	sizeof(struct tr_query)
#define RLEN	sizeof(struct tr_resp)

/* fields for tr_rflags (forwarding error codes) */
#define TR_NO_ERR	0       /* No error */
#define TR_WRONG_IF	1       /* traceroute arrived on non-oif */
#define TR_PRUNED	2       /* router has sent a prune upstream */
#define TR_OPRUNED	3       /* stop forw. after request from next hop rtr*/
#define TR_SCOPED	4       /* group adm. scoped at this hop */
#define TR_NO_RTE	5       /* no route for the source */
#define TR_NO_LHR       6       /* not the last-hop router */
#define TR_NO_FWD	7       /* not forwarding for this (S,G). Reason = ? */
#define TR_RP           8       /* I am the RP/Core */
#define TR_IIF          9       /* request arrived on the iif */
#define TR_NO_MULTI     0x0a    /* multicast disabled on that interface */
#define TR_NO_SPACE	0x81    /* no space to insert responce data block */
#define TR_OLD_ROUTER	0x82    /* previous hop does not support traceroute */
#define TR_ADMIN_PROHIB 0x83    /* traceroute adm. prohibited */

/* fields for tr_smask */
#define TR_GROUP_ONLY   0x2f    /* forwarding solely on group state */
#define TR_SUBNET_COUNT 0x40    /* pkt count for (S,G) is for source network */

/* fields for packets count */
#define TR_CANT_COUNT   0xffffffff  /* no count can be reported */

/* fields for tr_rproto (routing protocol) */
#define PROTO_DVMRP	   1
#define PROTO_MOSPF	   2
#define PROTO_PIM	   3
#define PROTO_CBT 	   4
#define PROTO_PIM_SPECIAL  5
#define PROTO_PIM_STATIC   6
#define PROTO_DVMRP_STATIC 7

#define NBR_VERS(n)	(((n)->al_pv << 8) + (n)->al_mv)

/**
 * Local Variables:
 *  version-control: t
 *  indent-tabs-mode: t
 *  c-file-style: "ellemtel"
 *  c-basic-offset: 4
 * End:
 */
