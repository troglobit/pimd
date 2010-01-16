/*
 * Copyright (c) 1993, 1998 by the University of Southern California
 * All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation in source and binary forms for lawful purposes
 * and without fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both the copyright notice and
 * this permission notice appear in supporting documentation. and that
 * any documentation, advertising materials, and other materials related
 * to such distribution and use acknowledge that the software was
 * developed by the University of Southern California, Information
 * Sciences Institute.  The name of the University may not be used to
 * endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THE UNIVERSITY OF SOUTHERN CALIFORNIA makes no representations about
 * the suitability of this software for any purpose.  THIS SOFTWARE IS
 * PROVIDED "AS IS" AND WITHOUT ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Other copyrights might apply to parts of this software and are so
 * noted when applicable.
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
    u_int8 version;			/* RSRR Version, currently 1        */
    u_int8 type;			/* type of message, as defined above*/
    u_int8 flags;			/* flags; defined by type           */
    u_int8 num;				/* number; defined by type          */
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
    u_int8 id;				/* vif id             */
    u_int8 threshold;			/* vif threshold ttl  */
    u_int16 status;			/* vif status bitmask */
    u_int32 local_addr; 		/* vif local address  */
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
    u_int32   dest_addr;		/* destination */
    u_int32 source_addr;		/* source      */
    u_int32 query_id;			/* query ID    */
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
    u_int32 dest_addr;  		/* destination          */
    u_int32 source_addr;		/* source               */
    u_int32 query_id;			/* query ID             */
    u_int16 in_vif;			/* incoming vif         */
    u_int16 reserved;			/* reserved             */
    u_int32 out_vif_bm;			/* outgoing vif bitmask */
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
    u_int32 dest_addr;                  /* destination */
    u_int32 source_addr;                /* source      */
    u_int32 query_id;                   /* query ID    */
    u_int16 vif;                        /* vif         */
    u_int16 reserved;                   /* reserved    */
};
#endif /* NOT_IN_THE_SPEC */
