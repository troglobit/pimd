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

#define RSRR_SERV_PATH "/tmp/.rsrr_svr"
/* Note this needs to be 14 chars for 4.3 BSD compatibility */
#define RSRR_CLI_PATH "/tmp/.rsrr_cli"

#define RSRR_MAX_LEN 2048
#define RSRR_HEADER_LEN (sizeof(struct rsrr_header))
#define RSRR_RQ_LEN (RSRR_HEADER_LEN + sizeof(struct rsrr_rq))
#define RSRR_RR_LEN (RSRR_HEADER_LEN + sizeof(struct rsrr_rr))
#define RSRR_VIF_LEN (sizeof(struct rsrr_vif))

/* Current maximum number of vifs. */
#define RSRR_MAX_VIFS 32

/* Maximum acceptable version */
#define RSRR_MAX_VERSION 1

/* RSRR message types */
#define RSRR_ALL_TYPES     0
#define RSRR_INITIAL_QUERY 1
#define RSRR_INITIAL_REPLY 2
#define RSRR_ROUTE_QUERY   3
#define RSRR_ROUTE_REPLY   4

/* Each definition represents the position of the bit from right to left. */
/* All not defined bits are zeroes */

/* RSRR Initial Reply (Vif) Status bits
 *
 * 0 = disabled bit, set if the vif is administratively disabled.
 */
#define RSRR_DISABLED_BIT 0

/* RSRR Route Query/Reply flag bits
 *
 * 0 = Route Change Notification bit, set if the reservation protocol
 *     wishes to receive notification of a route change for the
 *     source-destination pair listed in the query. Notification is in the
 *     form of an unsolicitied Route Reply.
 * 1 = Error bit, set if routing doesn't have a routing entry for
 *     the source-destination pair.
 * (TODO: XXX: currently not used by rsvpd?)
 * (2,3) = Shared tree (Reply only)
 *         = 01 if the listed sender is using a shared tree, but some other
 *              senders for the same destination use sender (source-specific)
 *              trees.
 *         = 10 if all senders for the destination use shared tree.
 *         = 00 otherwise
 */
#define RSRR_NOTIFICATION_BIT 0
#define RSRR_ERROR_BIT 1
#define RSRR_THIS_SENDER_SHARED_TREE 2
#define RSRR_ALL_SENDERS_SHARED_TREE 3
#define RSRR_SET_ALL_SENDERS_SHARED_TREE(X)             \
          BIT_SET((X), RSRR_ALL_SENDERS_SHARED_TREE);  \
          BIT_CLR((X), RSRR_THIS_SENDER_SHARED_TREE);
#define RSRR_THIS_SENDER_SHARED_TREE_SOME_OTHER_NOT(X)  \
          BIT_SET((X), RSRR_THIS_SENDER_SHARED_TREE);   \
          BIT_CLR((X), RSRR_ALL_SENDERS_SHARED_TREE)

/* Definition of an RSRR message header.
 * An Initial Query uses only the header, and an Initial Reply uses
 * the header and a list of vifs.
 */
struct rsrr_header {
    uint8_t version;			/* RSRR Version, currently 1        */
    uint8_t type;			/* type of message, as defined above*/
    uint8_t flags;			/* flags; defined by type           */
    uint8_t num;			/* number; defined by type          */
};

/* Definition of a vif as seen by the reservation protocol.
 *
 * Routing gives the reservation protocol a list of vifs in the
 * Initial Reply.
 *
 * We explicitly list the ID because we can't assume that all routing
 * protocols will use the same numbering scheme.
 *
 * The status is a bitmask of status flags, as defined above.  It is the
 * responsibility of the reservation protocol to perform any status checks
 * if it uses the MULTICAST_VIF socket option.
 *
 * The threshold indicates the ttl an outgoing packet needs in order to
 * be forwarded. The reservation protocol must perform this check itself if
 * it uses the MULTICAST_VIF socket option.
 *
 * The local address is the address of the physical interface over which
 * packets are sent.
 */
struct rsrr_vif {
    uint8_t id;				/* vif id             */
    uint8_t threshold;			/* vif threshold ttl  */
    uint16_t status;			/* vif status bitmask */
    uint32_t local_addr; 		/* vif local address  */
};

/* Definition of an RSRR Route Query.
 * 
 * The query asks routing for the forwarding entry for a particular
 * source and destination.  The query ID uniquely identifies the query
 * for the reservation protocol.  Thus, the combination of the client's
 * address and the query ID forms a unique identifier for routing.
 * Flags are defined above.
 */
struct rsrr_rq {
    uint32_t dest_addr;			/* destination */
    uint32_t source_addr;		/* source      */
    uint32_t query_id;			/* query ID    */
};

/* Definition of an RSRR Route Reply.
 *
 * Routing uses the reply to give the reservation protocol the
 * forwarding entry for a source-destination pair.  Routing copies the
 * query ID from the query and fills in the incoming vif and a bitmask
 * of the outgoing vifs.
 * Flags are defined above.
 */
/* TODO: XXX: in_vif is 16 bits here, but in rsrr_vif it is 8 bits.
 * Bug in the spec?
 */
struct rsrr_rr {
    uint32_t dest_addr;  		/* destination          */
    uint32_t source_addr;		/* source               */
    uint32_t query_id;			/* query ID             */
    uint16_t in_vif;			/* incoming vif         */
    uint16_t reserved;			/* reserved             */
    uint32_t out_vif_bm;		/* outgoing vif bitmask */
};


/* TODO: XXX: THIS IS NOT IN THE SPEC! (OBSOLETE?) */
#ifdef NOT_IN_THE_SPEC
/* Definition of an RSRR Service Query/Reply.
 * 
 * The query asks routing to perform a service for a particular
 * source/destination combination.  The query also lists the vif
 * that the service applies to.
 */
struct rsrr_sqr {
    uint32_t dest_addr;                  /* destination */
    uint32_t source_addr;                /* source      */
    uint32_t query_id;                   /* query ID    */
    uint16_t vif;                        /* vif         */
    uint16_t reserved;                   /* reserved    */
};
#endif /* NOT_IN_THE_SPEC */

/**
 * Local Variables:
 *  version-control: t
 *  indent-tabs-mode: t
 *  c-file-style: "ellemtel"
 *  c-basic-offset: 4
 * End:
 */
