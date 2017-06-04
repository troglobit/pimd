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
 *  $Id: pimd.h,v 1.27 2003/05/21 10:40:28 pavlin Exp $
 */


#include <netdb.h>
#include <netinet/pim.h>

#define PIM_PROTOCOL_VERSION	2
#define PIMD_VERSION		PIM_PROTOCOL_VERSION
#define PIMD_SUBVERSION         1
#if 0
#define PIM_CONSTANT            0x000eff00      /* constant portion of 'group' field */
#endif
#define PIM_CONSTANT            0
#define PIMD_LEVEL (PIM_CONSTANT | PIMD_VERSION | (PIMD_SUBVERSION << 8))

#define INADDR_ALL_PIM_ROUTERS  (uint32_t)0xe000000D	     /* 224.0.0.13 */
#if !defined(INADDR_UNSPEC_GROUP)
#define INADDR_UNSPEC_GROUP     (uint32_t)0xe0000000	     /* 224.0.0.0 */
#endif /* !defined(INADDR_UNSPEC_GROUP) */


/* PIM protocol timers (in seconds) */
#ifndef TIMER_INTERVAL
#define TIMER_INTERVAL		          5 /* virtual timer granularity */
#endif /* TIMER_INTERVAL */

#define PIM_REGISTER_SUPPRESSION_TIMEOUT 60
#define PIM_REGISTER_PROBE_TIME	          5 /* Used to send NULL_REGISTER */
#define PIM_DATA_TIMEOUT                210

#define PIM_TIMER_HELLO_INTERVAL         30
#define PIM_JOIN_PRUNE_PERIOD	         60
#define PIM_JOIN_PRUNE_HOLDTIME        (3.5 * PIM_JOIN_PRUNE_PERIOD)
#define PIM_RANDOM_DELAY_JOIN_TIMEOUT   4.5

/* TODO: XXX: cannot be shorter than 10 seconds (not in the spec)
 * MAX: Cisco max value (16383) for ip pim rp-candidate interval. */
#define PIM_MIN_CAND_RP_ADV_PERIOD       10
#define PIM_DEFAULT_CAND_RP_ADV_PERIOD   60
#define PIM_MAX_CAND_RP_ADV_PERIOD       16383

/* TODO: 60 is the original value. Temporarily set to 30 for debugging.
#define PIM_BOOTSTRAP_PERIOD             60 */
#define PIM_BOOTSTRAP_PERIOD             30

#define PIM_BOOTSTRAP_TIMEOUT	       (2.5 * PIM_BOOTSTRAP_PERIOD + 10)
#define PIM_TIMER_HELLO_HOLDTIME       (3.5 * PIM_TIMER_HELLO_INTERVAL)
#define PIM_ASSERT_TIMEOUT              180

/* Misc definitions */
#define PIM_DEFAULT_CAND_RP_PRIORITY      0 /* 0 is the highest. Don't know
					     * why this is the default.
					     * See the PS version (Mar' 97),
					     * pp.22 bottom of the spec.
					     */
#define PIM_MAX_CAND_RP_PRIORITY        255 /* 255 is the highest. */

#define PIM_DEFAULT_BSR_PRIORITY          0 /* 0 is the lowest               */
#define PIM_MAX_CAND_BSR_PRIORITY         PIM_MAX_CAND_RP_PRIORITY

#define RP_DEFAULT_IPV4_HASHMASKLEN      30 /* the default group msklen used
					     * by the hash function to 
					     * calculate the group-to-RP
					     * mapping
					     */
#define SINGLE_SRC_MSKLEN	         32 /* the single source mask length */
#define SINGLE_GRP_MSKLEN	         32 /* the single group mask length  */
#define PIM_GROUP_PREFIX_DEFAULT_MASKLEN 16 /* The default group masklen if
					     * omitted in the config file.
					     * XXX: not set to 4, because
					     * a small mis-configuration
					     * may concentrate all multicast
					     * traffic in a single RP
					     */
#define PIM_GROUP_PREFIX_MIN_MASKLEN	  4 /* group prefix minimum length */

/* Datarate related definitions */

/*
 * This is the threshold for the last hop router, or the RP, to
 * initiate switching to the shortest path tree.  Like Cisco we
 * change to SPT on first packet to the (S,G), but only after
 * 100 seconds (Xorp does this and Pavlin knows his PIM-SM-FU)
 */
#define SPT_THRESHOLD_DEFAULT_MODE        SPT_PACKETS
#define SPT_THRESHOLD_DEFAULT_RATE        0
#define SPT_THRESHOLD_DEFAULT_PACKETS     0
#define SPT_THRESHOLD_MAX_PACKETS         0
#define SPT_THRESHOLD_DEFAULT_INTERVAL    100

#define UCAST_ROUTING_CHECK_INTERVAL      20 /* Unfortunately, if the unicast
                                              * routing changes, the kernel
                                              * or any of the existing
                                              * unicast routing daemons
                                              * don't send us a signal.
                                              * Have to ask periodically the
                                              * kernel for any route changes.
                                              * Default: every 20 seconds.
                                              * Sigh.
                                              */


#define DEFAULT_PHY_RATE_LIMIT  0             /* default phyint rate limit  */
#define DEFAULT_REG_RATE_LIMIT  0             /* default register_vif rate limit  */

/**************************************************************************
 * PIM Encoded-Unicast, Encoded-Group and Encoded-Source Address formats  *
 *************************************************************************/
/* Address families definition */
#define ADDRF_RESERVED  0
#define ADDRF_IPv4      1
#define ADDRF_IPv6      2
#define ADDRF_NSAP      3
#define ADDRF_HDLC      4
#define ADDRF_BBN1822   5
#define ADDRF_802       6
#define ADDRF_ETHERNET  ADDRF_802
#define ADDRF_E163      7
#define ADDRF_E164      8
#define ADDRF_SMDS      ADDRF_E164
#define ADDRF_ATM       ADDRF_E164
#define ADDRF_F69       9
#define ADDRF_TELEX     ADDRF_F69
#define ADDRF_X121      10
#define ADDRF_X25       ADDRF_X121
#define ADDRF_IPX       11
#define ADDRF_APPLETALK 12
#define ADDRF_DECNET_IV 13
#define ADDRF_BANYAN    14
#define ADDRF_E164_NSAP 15

/* Addresses Encoding Type (specific for each Address Family */
#define ADDRT_IPv4      0


/* Encoded-Unicast: 6 bytes long */
typedef struct pim_encod_uni_addr_ {
    uint8_t      addr_family;
    uint8_t      encod_type;
    uint32_t     unicast_addr;        /* XXX: Note the 32-bit boundary
				      * misalignment for  the unicast
				      * address when placed in the
				      * memory. Must read it byte-by-byte!
				      */
} pim_encod_uni_addr_t;
#define PIM_ENCODE_UNI_ADDR_LEN 6

/* Encoded-Group */
typedef struct pim_encod_grp_addr_ {
    uint8_t      addr_family;
    uint8_t      encod_type;
    uint8_t      reserved;
    uint8_t      masklen;
    uint32_t     mcast_addr;
} pim_encod_grp_addr_t;
#define PIM_ENCODE_GRP_ADDR_LEN 8

/* Encoded-Source */
typedef struct pim_encod_src_addr_ {
    uint8_t      addr_family;
    uint8_t      encod_type;
    uint8_t      flags;
    uint8_t      masklen;
    uint32_t     src_addr;
} pim_encod_src_addr_t;
#define PIM_ENCODE_SRC_ADDR_LEN 8

#define USADDR_RP_BIT 0x1
#define USADDR_WC_BIT 0x2
#define USADDR_S_BIT  0x4

/**************************************************************************
 *                       PIM Messages formats                             *
 *************************************************************************/
/* TODO: XXX: some structures are probably not used at all */

typedef struct pim pim_header_t;

/* PIM Hello */
typedef struct pim_hello_ {
    uint16_t     option_type;   /* Option type */
    uint16_t     option_length; /* Length of the Option Value field in bytes */
} pim_hello_t;

/* PIM Register */
typedef struct pim_register_ {
    uint32_t     reg_flags;
} pim_register_t;

/* PIM Register-Stop */
typedef struct pim_register_stop_ {
    pim_encod_grp_addr_t encod_grp;
    pim_encod_uni_addr_t encod_src; /* XXX: 6 bytes long, misaligned */
} pim_register_stop_t;

/* PIM Join/Prune: XXX: all 32-bit addresses misaligned! */
typedef struct pim_jp_header_ {
    pim_encod_uni_addr_t encod_upstream_nbr;
    uint8_t     reserved;
    uint8_t     num_groups;
    uint16_t    holdtime;
} pim_jp_header_t;

typedef struct pim_jp_encod_grp_ {
    pim_encod_grp_addr_t   encod_grp;
    uint16_t                number_join_src;
    uint16_t                number_prune_src;
} pim_jp_encod_grp_t;

#define PIM_ACTION_NOTHING 0
#define PIM_ACTION_JOIN    1
#define PIM_ACTION_PRUNE   2

#define PIM_IIF_SOURCE     1
#define PIM_IIF_RP         2

#define PIM_ASSERT_RPT_BIT 0x80000000


/* PIM messages type */
#ifndef PIM_HELLO
#define PIM_HELLO               0
#endif
#ifndef PIM_REGISTER
#define PIM_REGISTER            1
#endif
#ifndef PIM_REGISTER_STOP
#define PIM_REGISTER_STOP       2
#endif
#ifndef PIM_JOIN_PRUNE
#define PIM_JOIN_PRUNE          3
#endif
#ifndef PIM_BOOTSTRAP
#define PIM_BOOTSTRAP		4
#endif
#ifndef PIM_ASSERT
#define PIM_ASSERT              5
#endif
#ifndef PIM_GRAFT
#define PIM_GRAFT               6
#endif
#ifndef PIM_GRAFT_ACK
#define PIM_GRAFT_ACK           7
#endif
#ifndef PIM_CAND_RP_ADV
#define PIM_CAND_RP_ADV         8
#endif

#define PIM_V2_HELLO            PIM_HELLO
#define PIM_V2_REGISTER         PIM_REGISTER
#define PIM_V2_REGISTER_STOP    PIM_REGISTER_STOP
#define PIM_V2_JOIN_PRUNE       PIM_JOIN_PRUNE
#define PIM_V2_BOOTSTRAP	PIM_BOOTSTRAP
#define PIM_V2_ASSERT           PIM_ASSERT
#define PIM_V2_GRAFT            PIM_GRAFT
#define PIM_V2_GRAFT_ACK        PIM_GRAFT_ACK
#define PIM_V2_CAND_RP_ADV      PIM_CAND_RP_ADV

#define PIM_V1_QUERY            0
#define PIM_V1_REGISTER         1
#define PIM_V1_REGISTER_STOP    2
#define PIM_V1_JOIN_PRUNE       3
#define PIM_V1_RP_REACHABILITY  4
#define PIM_V1_ASSERT           5
#define PIM_V1_GRAFT            6
#define PIM_V1_GRAFT_ACK        7

/* Vartious options from PIM messages definitions */
/* PIM_HELLO definitions */
#define PIM_HELLO_HOLDTIME              1
#define PIM_HELLO_HOLDTIME_LEN          2
#define PIM_HELLO_HOLDTIME_FOREVER      0xffff

#define PIM_HELLO_DR_PRIO               19
#define PIM_HELLO_DR_PRIO_LEN           4
#define PIM_HELLO_DR_PRIO_DEFAULT       1

#define PIM_HELLO_GENID                 20
#define PIM_HELLO_GENID_LEN             4

/* PIM_REGISTER definitions */
#define PIM_REGISTER_BORDER_BIT         0x80000000
#define PIM_REGISTER_NULL_REGISTER_BIT  0x40000000


#define MASK_TO_MASKLEN(mask, masklen)                           \
    do {                                                         \
        uint32_t tmp_mask = ntohl((mask));                        \
        uint8_t  tmp_masklen = sizeof((mask)) << 3;               \
        for ( ; tmp_masklen > 0; tmp_masklen--, tmp_mask >>= 1)  \
            if (tmp_mask & 0x1)                                  \
                break;                                           \
        (masklen) = tmp_masklen;                                 \
    } while (0)

#define MASKLEN_TO_MASK(masklen, mask)					    \
  do {									    \
    (mask) = masklen ? htonl(~0U << ((sizeof(mask) << 3) - (masklen))) : 0; \
} while (0)


/*
 * A bunch of macros because of the lack of 32-bit boundary alignment.
 * All because of one misalligned address format. Hopefully this will be
 * fixed in PIMv3. (cp) must be (uint8_t *) .
 */
/* Originates from Eddy Rusty's (eddy@isi.edu) PIM-SM implementation for
 * gated.
 */

#include <sys/types.h>
#ifdef HOST_OS_WINDOWS
#define LITTLE_ENDIAN 1234
#define BYTE_ORDER LITTLE_ENDIAN
#endif /* HOST_OS_WINDOWS */

#if !defined(BYTE_ORDER)
#if defined(__BYTE_ORDER)
#define BYTE_ORDER	__BYTE_ORDER
#define LITTLE_ENDIAN	__LITTLE_ENDIAN
#define BIG_ENDIAN	__BIG_ENDIAN
#else
#error "BYTE_ORDER not defined! Define it to either LITTLE_ENDIAN (e.g. i386, vax) or BIG_ENDIAN (e.g.  68000, ibm, net) based on your architecture!"
#endif
#endif

/* PUT_NETLONG puts "network ordered" data to the datastream.
 * PUT_HOSTLONG puts "host ordered" data to the datastream.
 * GET_NETLONG gets the data and keeps it in "network order" in the memory
 * GET_HOSTLONG gets the data, but in the memory it is in "host order"
 * The same for all {PUT,GET}_{NET,HOST}{SHORT,LONG}
 */
#define GET_BYTE(val, cp)       ((val) = *(cp)++)
#define PUT_BYTE(val, cp)       (*(cp)++ = (uint8_t)(val))

#define GET_HOSTSHORT(val, cp)                  \
        do {                                    \
                uint16_t Xv;                    \
                Xv = (*(cp)++) << 8;            \
                Xv |= *(cp)++;                  \
                (val) = Xv;                     \
        } while (0)

#define PUT_HOSTSHORT(val, cp)                  \
        do {                                    \
                uint16_t Xv;                    \
                Xv = (uint16_t)(val);            \
                *(cp)++ = (uint8_t)(Xv >> 8);   \
                *(cp)++ = (uint8_t)Xv;          \
        } while (0)

#if defined(BYTE_ORDER) && (BYTE_ORDER == LITTLE_ENDIAN)
#define GET_NETSHORT(val, cp)                   \
        do {                                    \
                uint16_t Xv;                    \
                Xv = *(cp)++;                   \
                Xv |= (*(cp)++) << 8;           \
                (val) = Xv;                     \
        } while (0)
#define PUT_NETSHORT(val, cp)                   \
        do {                                    \
                uint16_t Xv;                    \
                Xv = (uint16_t)(val);            \
                *(cp)++ = (uint8_t)Xv;          \
                *(cp)++ = (uint8_t)(Xv >> 8);   \
        } while (0)
#else
#define GET_NETSHORT(val, cp) GET_HOSTSHORT(val, cp)
#define PUT_NETSHORT(val, cp) PUT_HOSTSHORT(val, cp)
#endif /* {GET,PUT}_NETSHORT */

#define GET_HOSTLONG(val, cp)                   \
        do {                                    \
                uint32_t Xv;                    \
                Xv  = (*(cp)++) << 24;          \
                Xv |= (*(cp)++) << 16;          \
                Xv |= (*(cp)++) <<  8;          \
                Xv |= *(cp)++;                  \
                (val) = Xv;                     \
        } while (0)

#define PUT_HOSTLONG(val, cp)                   \
        do {                                    \
                uint32_t Xv;                     \
                Xv = (uint32_t)(val);            \
                *(cp)++ = (uint8_t)(Xv >> 24);  \
                *(cp)++ = (uint8_t)(Xv >> 16);  \
                *(cp)++ = (uint8_t)(Xv >>  8);  \
                *(cp)++ = (uint8_t)Xv;          \
        } while (0)

#if defined(BYTE_ORDER) && (BYTE_ORDER == LITTLE_ENDIAN)
#define GET_NETLONG(val, cp)                    \
        do {                                    \
                uint32_t Xv;                    \
                Xv  = *(cp)++;                  \
                Xv |= (*(cp)++) <<  8;          \
                Xv |= (*(cp)++) << 16;          \
                Xv |= (*(cp)++) << 24;          \
                (val) = Xv;                     \
        } while (0)

#define PUT_NETLONG(val, cp)                    \
        do {                                    \
                uint32_t Xv;                    \
                Xv = (uint32_t)(val);            \
                *(cp)++ = (uint8_t)Xv;          \
                *(cp)++ = (uint8_t)(Xv >>  8);  \
                *(cp)++ = (uint8_t)(Xv >> 16);  \
                *(cp)++ = (uint8_t)(Xv >> 24);  \
        } while (0)
#else
#define GET_NETLONG(val, cp) GET_HOSTLONG(val, cp)
#define PUT_NETLONG(val, cp) PUT_HOSTLONG(val, cp)
#endif /* {GET,PUT}_HOSTLONG */


#define GET_ESADDR(esa, cp)                     \
        do {                                    \
            (esa)->addr_family = *(cp)++;       \
            (esa)->encod_type  = *(cp)++;       \
            (esa)->flags       = *(cp)++;       \
            (esa)->masklen     = *(cp)++;       \
            GET_NETLONG((esa)->src_addr, (cp)); \
        } while(0)

#define PUT_ESADDR(addr, masklen, flags, cp)    \
        do {                                    \
            uint32_t mask;                      \
            MASKLEN_TO_MASK((masklen), mask);   \
            *(cp)++ = ADDRF_IPv4; /* family */  \
            *(cp)++ = ADDRT_IPv4; /* type   */  \
            *(cp)++ = (flags);    /* flags  */  \
            *(cp)++ = (masklen);                \
            PUT_NETLONG((addr) & mask, (cp));   \
        } while(0)

#define GET_EGADDR(ega, cp)                     \
        do {                                    \
            (ega)->addr_family = *(cp)++;       \
            (ega)->encod_type  = *(cp)++;       \
            (ega)->reserved    = *(cp)++;       \
            (ega)->masklen     = *(cp)++;       \
            GET_NETLONG((ega)->mcast_addr, (cp)); \
        } while(0)

#define PUT_EGADDR(addr, masklen, reserved, cp) \
        do {                                    \
            uint32_t mask;                      \
            MASKLEN_TO_MASK((masklen), mask);   \
            *(cp)++ = ADDRF_IPv4; /* family */  \
            *(cp)++ = ADDRT_IPv4; /* type   */  \
            *(cp)++ = (reserved); /* reserved; should be 0 */  \
            *(cp)++ = (masklen);                \
            PUT_NETLONG((addr) & mask, (cp)); \
        } while(0)

#define GET_EUADDR(eua, cp)                     \
        do {                                    \
            (eua)->addr_family = *(cp)++;       \
            (eua)->encod_type  = *(cp)++;       \
            GET_NETLONG((eua)->unicast_addr, (cp)); \
        } while(0)

#define PUT_EUADDR(addr, cp)                    \
        do {                                    \
            *(cp)++ = ADDRF_IPv4; /* family */  \
            *(cp)++ = ADDRT_IPv4; /* type   */  \
            PUT_NETLONG((addr), (cp));          \
        } while(0)

/* Check if group is in PIM-SSM range, x must be in network byte order */
#define IN_PIM_SSM_RANGE(x) ((ntohl((unsigned)(x)) & 0xff000000) == 0xe8000000)

/* Check if address is in link-local range, x must be in network byte order */
#define IN_LINK_LOCAL_RANGE(x) ((ntohl((unsigned)(x)) & 0xffff0000) == 0xa9fe0000)
 
/* TODO: Currently not used. Probably not need at all. Delete! */
#if 0
/* This is completely IGMP related stuff? */
#define PIM_LEAF_TIMEOUT               (3.5 * IGMP_QUERY_INTERVAL)

#define PIM_REGISTER_BIT_TIMEOUT 30 	/* TODO: check if need to be 60 */

#define PIM_ASSERT_RATE_TIMER   15
#endif /* 0 */

#if (defined(__bsdi__) || defined(NetBSD) || defined(OpenBSD) || defined(IRIX))
/*
 * Struct used to communicate from kernel to multicast router
 * note the convenient similarity to an IP packet
 */ 
struct igmpmsg {
    uint32_t        unused1;
    uint32_t        unused2;
    uint8_t         im_msgtype;                 /* what type of message     */
#define IGMPMSG_NOCACHE         1
#define IGMPMSG_WRONGVIF        2
#define IGMPMSG_WHOLEPKT        3               /* used for user level encap*/
    uint8_t         im_mbz;                     /* must be zero             */
    uint8_t         im_vif;                     /* vif rec'd on             */
    uint8_t         unused3;
    struct in_addr  im_src, im_dst;
};
#endif
