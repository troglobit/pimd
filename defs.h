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
 * Part of this program has been derived from mrouted.
 * The mrouted program is covered by the license in the accompanying file
 * named "LICENSE.mrouted".
 *
 * The mrouted program is COPYRIGHT 1989 by The Board of Trustees of
 * Leland Stanford Junior University.
 *
 */
#ifndef __PIMD_DEFS_H__
#define __PIMD_DEFS_H__

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> 
#include <ctype.h>
#include <errno.h>
#include <syslog.h>
#include <signal.h> 
#include <string.h> 
#include <sys/param.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#if ((defined(SYSV)) || (defined(__bsdi__)) || ((defined SunOS) && (SunOS < 50)))
#include <sys/sockio.h>
#endif /* SYSV || bsdi || SunOS 4.x */
#include <time.h>
#include <sys/time.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/igmp.h>
#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
#include <osreldate.h>
#endif /* __FreeBSD__ */
#if defined(__bsdi__) || defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
#define rtentry kernel_rtentry
#include <net/route.h>
#undef rtentry
#endif /* bsdi or __FreeBSD_version >= 220000 */
#ifdef __linux__
#define _LINUX_IN_H             /* For Linux <= 2.6.25 */
#include <linux/types.h>
#include <linux/mroute.h>
#else
#include <netinet/ip_mroute.h>
#endif /* __linux__ */
#if defined(HAVE_STRLCPY)
#include <string.h>
#endif
#if defined(HAVE_STRTONUM)
#include <stdlib.h>
#endif
#if defined(HAVE_PIDFILE)
#if defined(OpenBSD) || defined(NetBSD)
#include <util.h>
#else
#include <libutil.h>
#endif
#endif
#include <strings.h>
#ifdef RSRR
#include <sys/un.h>
#endif /* RSRR */

typedef u_int   u_int32;
typedef u_short u_int16;
typedef u_char  u_int8;

#ifndef BYTE_ORDER
#if (BSD >= 199103)
#include <machine/endian.h>
#else
#ifdef __linux__
#include <endian.h>
#else
#define LITTLE_ENDIAN	1234	/* least-significant byte first (vax, pc) */
#define BIG_ENDIAN	4321	/* most-significant byte first (IBM, net) */
#define PDP_ENDIAN	3412	/* LSB first in word, MSW first in long (pdp) */

#if defined(vax) || defined(ns32000) || defined(sun386) || defined(i386) || \
    defined(__ia64) || \
    defined(MIPSEL) || defined(_MIPSEL) || defined(BIT_ZERO_ON_RIGHT) || \
    defined(__alpha__) || defined(__alpha)
#define BYTE_ORDER	LITTLE_ENDIAN
#endif

#if defined(sel) || defined(pyr) || defined(mc68000) || defined(sparc) || \
    defined(is68k) || defined(tahoe) || defined(ibm032) || defined(ibm370) || \
    defined(MIPSEB) || defined(_MIPSEB) || defined(_IBMR2) || defined(DGUX) ||\
    defined(apollo) || defined(__convex__) || defined(_CRAY) || \
    defined(__hppa) || defined(__hp9000) || \
    defined(__hp9000s300) || defined(__hp9000s700) || \
    defined(BIT_ZERO_ON_LEFT) || defined(m68k)
#define BYTE_ORDER	BIG_ENDIAN
#endif
#endif /* linux */
#endif /* BSD */
#endif /* BYTE_ORDER */

typedef void (*cfunc_t) (void *);
typedef void (*ihfunc_t) (int, fd_set *);

#include "dvmrp.h"     /* Added for further compatibility and convenience */
#include "pimd.h"
#include "mrt.h"
#include "igmpv2.h"
#include "vif.h"
#include "debug.h"
#include "pathnames.h"
#ifdef RSRR
#include "rsrr.h"
#include "rsrr_var.h"
#endif /* RSRR */

/*
 * Miscellaneous constants and macros
 */
/* #if (!(defined(__bsdi__)) && !(defined(KERNEL))) */
#ifndef KERNEL
#define max(a, b)               ((a) < (b) ? (b) : (a))
#define min(a, b)               ((a) > (b) ? (b) : (a))
#endif

#define ENABLINGSTR(bool)       (bool) ? "enabling" : "disabling"

/*
 * Various definitions to make it working for different platforms
 */
/* The old style sockaddr definition doesn't have sa_len */
#if defined(_AIX) || (defined(BSD) && (BSD >= 199006)) /* sa_len was added with 4.3-Reno */
#define HAVE_SA_LEN
#endif

/* Versions of Solaris older than 2.6 don't have routing sockets. */
/* XXX TODO: check FreeBSD version and add all other platforms */
#if defined(__linux__) || (defined(SunOS) && SunOS >=56) || defined (__FreeBSD__) || defined(__FreeBSD_kernel__) || defined (IRIX) || defined (__bsdi__) || defined(NetBSD) || defined(OpenBSD)
#define HAVE_ROUTING_SOCKETS	1
#endif

#define TRUE			1
#define FALSE			0

#ifndef MAX
#define MAX(a,b) (((a) >= (b))? (a) : (b))
#define MIN(a,b) (((a) <= (b))? (a) : (b))
#endif /* MAX & MIN */

#define CREATE                  TRUE
#define DONT_CREATE             FALSE

#define MFC_MOVE_FORCE		0x1
#define MFC_UPDATE_FORCE	0x2

#define EQUAL(s1, s2)		(strcmp((s1), (s2)) == 0)
#define ARRAY_LEN(a)            (sizeof((a)) / sizeof((a)[0]))

#define JAN_1970                2208988800UL    /* 1970 - 1900 in seconds */

#define MINTTL			1  /* min TTL in the packets send locally */

#define MAX_IP_PACKET_LEN       576
#define MIN_IP_HEADER_LEN       20
#define MAX_IP_HEADER_LEN       60


/*
 * The IGMPv2 <netinet/in.h> defines INADDR_ALLRTRS_GROUP, but earlier
 * ones don't, so we define it conditionally here.
 */
#ifndef INADDR_ALLRTRS_GROUP
					/* address for multicast mtrace msg */
#define INADDR_ALLRTRS_GROUP	(u_int32)0xe0000002	/* 224.0.0.2 */
#endif

#ifndef INADDR_MAX_LOCAL_GROUP
#define INADDR_MAX_LOCAL_GROUP	(u_int32)0xe00000ff	/* 224.0.0.255 */
#endif

#define INADDR_ANY_N            (u_int32)0x00000000     /* INADDR_ANY in
							 * network order */
#define CLASSD_PREFIX           (u_int32)0xe0000000     /* 224.0.0.0 */
#define STAR_STAR_RP_MSKLEN     4                       /* Masklen for
							 * 224.0.0.0 :
							 * to encode (*,*,RP)
							 */
#define ALL_MCAST_GROUPS_ADDR   (u_int32)0xe0000000     /* 224.0.0.0 */
#define ALL_MCAST_GROUPS_LENGTH 4

/* Used by DVMRP */
#define DEFAULT_METRIC		1	/* default subnet/tunnel metric     */
#define DEFAULT_THRESHOLD	1	/* default subnet/tunnel threshold  */

/* Used if no relaible unicast routing information available */
#define UCAST_DEFAULT_SOURCE_METRIC     1024
#define UCAST_DEFAULT_SOURCE_PREFERENCE 1024

#define TIMER_INTERVAL		5	/* 5 sec virtual timer granularity  */

/*
 * TODO: recalculate the messages sizes, probably with regard to the MTU
 * TODO: cleanup
 */
#define MAX_JP_MESSAGE_SIZE     8192
#define MAX_JP_MESSAGE_POOL_NUMBER 8
#define MAX_JOIN_LIST_SIZE      1500
#define MAX_PRUNE_LIST_SIZE     1500


#ifdef RSRR
#define BIT_ZERO(X)		((X) = 0)
#define BIT_SET(X,n)		((X) |= 1 << (n))
#define BIT_CLR(X,n)		((X) &= ~(1 << (n)))
#define BIT_TST(X,n)		((X) & 1 << (n))
#endif /* RSRR */

#ifdef SYSV
#define setlinebuf(s)		setvbuf((s), (NULL), (_IOLBF), 0)
#define RANDOM()                lrand48()
#else
#define RANDOM()                random()
#endif /* SYSV */

/*
 * External declarations for global variables and functions.
 */
#define			SEND_BUF_SIZE (128*1024)  /* Maximum buff size to
						   * send a packet */
#define			RECV_BUF_SIZE (128*1024)  /* Maximum buff size to
						   * receive a packet */
#define                 SO_SEND_BUF_SIZE_MAX (256*1024)
#define                 SO_SEND_BUF_SIZE_MIN (48*1024)
#define                 SO_RECV_BUF_SIZE_MAX (256*1024)
#define                 SO_RECV_BUF_SIZE_MIN (48*1024)

/* TODO: describe the variables and clean up */
extern char		*igmp_recv_buf;
extern char		*igmp_send_buf;
extern char		*pim_recv_buf;
extern char		*pim_send_buf;
extern int		igmp_socket;
extern int		pim_socket;
extern u_int32		allhosts_group;
extern u_int32		allrouters_group;
extern u_int32		allpimrouters_group;
extern build_jp_message_t *build_jp_message_pool;
extern int		build_jp_message_pool_counter;

extern u_long		virtual_time;
extern char	       *configfilename;
extern int              haveterminal;
extern char            *__progname;

extern struct cand_rp_adv_message_ {
    u_int8    *buffer;
    u_int8    *insert_data_ptr;
    u_int8    *prefix_cnt_ptr;
    u_int16   message_size;
} cand_rp_adv_message;

extern int disable_all_by_default;

/*
 * Used to contol the switching to the shortest path:
 *  `reg_rate`  used by the RP
 *  `data_rate` used by the last hop router
 */
extern u_int32		pim_reg_rate_bytes;
extern u_int32		pim_reg_rate_check_interval;
extern u_int32		pim_data_rate_bytes;
extern u_int32		pim_data_rate_check_interval;

extern cand_rp_t        *cand_rp_list;
extern grp_mask_t       *grp_mask_list;
extern cand_rp_t        *segmented_cand_rp_list;
extern grp_mask_t       *segmented_grp_mask_list;

extern u_int16          curr_bsr_fragment_tag;
extern u_int8           curr_bsr_priority;
extern u_int32          curr_bsr_address;
extern u_int32          curr_bsr_hash_mask;
extern u_int8		cand_bsr_flag;		   /* candidate BSR flag */
extern u_int8           my_bsr_priority;
extern u_int32          my_bsr_address;
extern u_int32          my_bsr_hash_mask;
extern u_int8           cand_rp_flag;              /* Candidate RP flag */
extern u_int32          my_cand_rp_address;
extern u_int8           my_cand_rp_priority;
extern u_int16          my_cand_rp_holdtime;
extern u_int16          my_cand_rp_adv_period;     /* The locally configured
						    * Cand-RP adv. period.
						    */
extern u_int16          pim_bootstrap_timer;
extern u_int32          rp_my_ipv4_hashmask;
extern u_int16          pim_cand_rp_adv_timer;

extern u_int32		default_source_metric;
extern u_int32		default_source_preference;

extern srcentry_t 	*srclist;
extern grpentry_t 	*grplist;
extern rpentry_t        *rplist;

extern struct uvif	uvifs[MAXVIFS];
extern vifi_t		numvifs;
extern int              total_interfaces;
extern vifi_t		reg_vif_num;
extern int              phys_vif;
extern int		udp_socket;

extern int		vifs_down;

#define MAX_INET_BUF_LEN 19
extern char		s1[MAX_INET_BUF_LEN];
extern char		s2[MAX_INET_BUF_LEN];
extern char		s3[MAX_INET_BUF_LEN];
extern char		s4[MAX_INET_BUF_LEN];

#if !((defined(BSD) && (BSD >= 199103)) || (defined(__linux__)))
extern int		errno;
#endif


#ifndef IGMP_MEMBERSHIP_QUERY
#define IGMP_MEMBERSHIP_QUERY		IGMP_HOST_MEMBERSHIP_QUERY
#if !(defined(NetBSD) || defined(OpenBSD) || defined(__FreeBSD__))
#define IGMP_V1_MEMBERSHIP_REPORT	IGMP_HOST_MEMBERSHIP_REPORT
#define IGMP_V2_MEMBERSHIP_REPORT	IGMP_HOST_NEW_MEMBERSHIP_REPORT
#else
#define IGMP_V1_MEMBERSHIP_REPORT	IGMP_v1_HOST_MEMBERSHIP_REPORT
#define IGMP_V2_MEMBERSHIP_REPORT	IGMP_v2_HOST_MEMBERSHIP_REPORT
#endif
#define IGMP_V2_LEAVE_GROUP		IGMP_HOST_LEAVE_MESSAGE
#endif

#if defined(NetBSD) || defined(OpenBSD) || defined(__FreeBSD__)
#define IGMP_MTRACE_RESP		IGMP_MTRACE_REPLY
#define IGMP_MTRACE			IGMP_MTRACE_QUERY
#endif

/* For timeout. The timers count down */
#define SET_TIMER(timer, value) (timer) = (value)
#define RESET_TIMER(timer) (timer) = 0
#define COPY_TIMER(timer_1, timer_2) (timer_2) = (timer_1)
#define IF_TIMER_SET(timer) if ((timer) > 0)
#define IF_TIMER_NOT_SET(timer) if ((timer) <= 0)
#define FIRE_TIMER(timer)       (timer) = 0

#define IF_TIMEOUT(timer)		\
	if (!((timer) -= (MIN(timer, TIMER_INTERVAL))))

#define IF_NOT_TIMEOUT(timer)		\
	if ((timer) -= (MIN(timer, TIMER_INTERVAL)))

#define TIMEOUT(timer)			\
	(!((timer) -= (MIN(timer, TIMER_INTERVAL))))

#define NOT_TIMEOUT(timer)		\
	((timer) -= (MIN(timer, TIMER_INTERVAL)))

#if 0
#define IF_TIMEOUT(value)     \
  if (!(((value) >= TIMER_INTERVAL) && ((value) -= TIMER_INTERVAL)))

#define IF_NOT_TIMEOUT(value) \
  if (((value) >= TIMER_INTERVAL) && ((value) -= TIMER_INTERVAL))

#define TIMEOUT(value)        \
     (!(((value) >= TIMER_INTERVAL) && ((value) -= TIMER_INTERVAL)))

#define NOT_TIMEOUT(value)    \
     (((value) >= TIMER_INTERVAL) && ((value) -= TIMER_INTERVAL))
#endif /* 0 */

#define ELSE else           /* To make emacs cc-mode happy */      

#define MASK_TO_VAL(x, i) {		   \
	u_int32 _x = ntohl(x);		   \
	(i) = 1;			   \
	while ((_x) <<= 1)		   \
	    (i)++;			   \
    };

#define VAL_TO_MASK(x, i) {			\
	x = htonl(~((1 << (32 - (i))) - 1));	\
    };

/*
 * External function definitions
 */

/* callout.c */
extern void	callout_init		(void);
extern void	free_all_callouts	(void);
extern void	age_callout_queue	(int);
extern int	timer_nextTimer		(void);
extern int	timer_setTimer		(int, cfunc_t, void *);
extern void	timer_clearTimer	(int);
extern int	timer_leftTimer		(int);

/* config.c */
extern void	config_vifs_from_kernel	(void);
extern void	config_vifs_from_file	(void);

/* debug.c */
extern char	*packet_kind		(u_int proto, u_int type, u_int code);
extern int	debug_kind		(u_int proto, u_int type, u_int code);
extern void	logit			(int, int, const char *, ...);
extern int	log_level		(u_int proto, u_int type, u_int code);
extern void	dump			(int i);
extern void	fdump			(int i);
extern void	cdump			(int i);
extern void	dump_vifs		(FILE *fp);
extern void	dump_pim_mrt		(FILE *fp);
extern int	dump_rp_set		(FILE *fp);

/* dvmrp_proto.c */
extern void	dvmrp_accept_probe	(u_int32 src, u_int32 dst, u_char *p, int datalen, u_int32 level);
extern void	dvmrp_accept_report	(u_int32 src, u_int32 dst, u_char *p, int datalen, u_int32 level);
extern void	dvmrp_accept_info_request (u_int32 src, u_int32 dst, u_char *p, int datalen);
extern void	dvmrp_accept_info_reply	(u_int32 src, u_int32 dst, u_char *p, int datalen);
extern void	dvmrp_accept_neighbors	(u_int32 src, u_int32 dst, u_char *p, int datalen, u_int32 level);
extern void	dvmrp_accept_neighbors2	(u_int32 src, u_int32 dst, u_char *p, int datalen, u_int32 level);
extern void	dvmrp_accept_prune	(u_int32 src, u_int32 dst, u_char *p, int datalen);
extern void	dvmrp_accept_graft	(u_int32 src, u_int32 dst, u_char *p, int datalen);
extern void	dvmrp_accept_g_ack	(u_int32 src, u_int32 dst, u_char *p, int datalen);

/* igmp.c */
extern void	init_igmp		(void);
extern void	send_igmp		(char *buf, u_int32 src, u_int32 dst, int type, int code, u_int32 group, int datalen);

/* igmp_proto.c */
extern void	query_groups		(struct uvif *v);
extern void	accept_membership_query	(u_int32 src, u_int32 dst, u_int32 group, int tmo);
extern void	accept_group_report	(u_int32 src, u_int32 dst, u_int32 group, int r_type);
extern void	accept_leave_message	(u_int32 src, u_int32 dst, u_int32 group);

/* inet.c */
extern int	inet_cksum		(u_int16 *addr, u_int len);
extern int	inet_valid_host		(u_int32 naddr);
extern int	inet_valid_mask		(u_int32 mask);
extern int	inet_valid_subnet	(u_int32 nsubnet, u_int32 nmask);
extern char	*inet_fmt		(u_int32 addr, char *s, size_t len);
extern char	*netname		(u_int32 addr, u_int32 mask);
extern u_int32	inet_parse		(char *s, int n);

/* kern.c */
extern void	k_set_sndbuf		(int socket, int bufsize, int minsize);
extern void	k_set_rcvbuf		(int socket, int bufsize, int minsize);
extern void	k_hdr_include		(int socket, int bool);
extern void	k_set_ttl		(int socket, int t);
extern void	k_set_loop		(int socket, int l);
extern void	k_set_if		(int socket, u_int32 ifa);
extern void	k_join			(int socket, u_int32 grp, struct uvif *v);
extern void	k_leave			(int socket, u_int32 grp, struct uvif *v);
extern void	k_init_pim		(int socket);
extern void	k_stop_pim		(int socket);
extern int	k_del_mfc		(int socket, u_int32 source, u_int32 group);
extern int	k_chg_mfc		(int socket, u_int32 source, u_int32 group, vifi_t iif, vifbitmap_t oifs,
                                         u_int32 rp_addr);
extern void	k_add_vif		(int socket, vifi_t vifi, struct uvif *v);
extern void	k_del_vif		(int socket, vifi_t vifi, struct uvif *v);
extern int	k_get_vif_count		(vifi_t vifi, struct vif_count *retval);
extern int	k_get_sg_cnt		(int socket, u_int32 source, u_int32 group, struct sg_count *retval);

/* main.c */
extern int	register_input_handler	(int fd, ihfunc_t func);

/* mrt.c */
extern void	init_pim_mrt		(void);
extern mrtentry_t *find_route		(u_int32 source, u_int32 group, u_int16 flags, char create);
extern grpentry_t *find_group		(u_int32 group);
extern srcentry_t *find_source		(u_int32 source);
extern void	delete_mrtentry		(mrtentry_t *mrtentry_ptr);
extern void	delete_srcentry		(srcentry_t *srcentry_ptr);
extern void	delete_grpentry		(grpentry_t *grpentry_ptr);
extern void	delete_mrtentry_all_kernel_cache (mrtentry_t *mrtentry_ptr);
extern void	delete_single_kernel_cache (mrtentry_t *mrtentry_ptr, kernel_cache_t *kernel_cache_ptr);
extern void	delete_single_kernel_cache_addr (mrtentry_t *mrtentry_ptr, u_int32 source, u_int32 group);
extern void	add_kernel_cache	(mrtentry_t *mrtentry_ptr, u_int32 source, u_int32 group, u_int16 flags);
/* pim.c */
extern void	init_pim		(void);
extern void	send_pim		(char *buf, u_int32 src, u_int32 dst, int type, int datalen);
extern void	send_pim_unicast	(char *buf, u_int32 src, u_int32 dst, int type, int datalen);

/* pim_proto.c */
extern int	receive_pim_hello	(u_int32 src, u_int32 dst, char *pim_message, size_t datalen);
extern int	send_pim_hello		(struct uvif *v, u_int16 holdtime);
extern void	delete_pim_nbr		(pim_nbr_entry_t *nbr_delete);
extern int	receive_pim_register	(u_int32 src, u_int32 dst, char *pim_message, size_t datalen);
extern int	send_pim_null_register	(mrtentry_t *r);
extern int	receive_pim_register_stop (u_int32 src, u_int32 dst, char *pim_message, size_t datalen);
extern int	send_pim_register	(char *pkt);
extern int	receive_pim_join_prune	(u_int32 src, u_int32 dst, char *pim_message, int datalen);
extern int	join_or_prune		(mrtentry_t *mrtentry_ptr, pim_nbr_entry_t *upstream_router);
extern int	receive_pim_assert	(u_int32 src, u_int32 dst, char *pim_message, int datalen);
extern int	send_pim_assert		(u_int32 source, u_int32 group, vifi_t vifi, mrtentry_t *mrtentry_ptr);
extern int	send_periodic_pim_join_prune (vifi_t vifi, pim_nbr_entry_t *pim_nbr, u_int16 holdtime);
extern int	add_jp_entry		(pim_nbr_entry_t *pim_nbr, u_int16 holdtime, u_int32 group, u_int8 grp_msklen,
                                         u_int32 source, u_int8 src_msklen,  u_int16 addr_flags, u_int8 join_prune);
extern void	pack_and_send_jp_message (pim_nbr_entry_t *pim_nbr);
extern int	receive_pim_cand_rp_adv	(u_int32 src, u_int32 dst, char *pim_message, int datalen);
extern int	receive_pim_bootstrap	(u_int32 src, u_int32 dst, char *pim_message, int datalen);
extern int	send_pim_cand_rp_adv	(void);
extern void	send_pim_bootstrap	(void);

/* route.c */
extern int	set_incoming		(srcentry_t *srcentry_ptr, int srctype);
extern vifi_t	get_iif			(u_int32 source);
extern pim_nbr_entry_t *find_pim_nbr	(u_int32 source);
extern int	add_sg_oif		(mrtentry_t *mrtentry_ptr, vifi_t vifi, u_int16 holdtime, int update_holdtime);
extern void	add_leaf		(vifi_t vifi, u_int32 source, u_int32 group);
extern void	delete_leaf		(vifi_t vifi, u_int32 source, u_int32 group);
extern int	change_interfaces	(mrtentry_t *mrtentry_ptr,  vifi_t new_iif,
                                         vifbitmap_t new_joined_oifs_, vifbitmap_t new_pruned_oifs,
                                         vifbitmap_t new_leaves_, vifbitmap_t new_asserted_oifs, u_int16 flags);
extern void	calc_oifs		(mrtentry_t *mrtentry_ptr, vifbitmap_t *oifs_ptr);
extern void	process_kernel_call	(void);
extern int	delete_vif_from_mrt	(vifi_t vifi);
extern mrtentry_t *switch_shortest_path	(u_int32 source, u_int32 group);

/* routesock.c */
extern int	k_req_incoming		(u_int32 source, struct rpfctl *rpfp);
#ifdef HAVE_ROUTING_SOCKETS
extern int	init_routesock		(void);
extern int	routing_socket;
#endif /* HAVE_ROUTING_SOCKETS */

/* rp.c */
extern void	init_rp_and_bsr		(void);
extern u_int16	bootstrap_initial_delay (void);
extern rp_grp_entry_t *add_rp_grp_entry (cand_rp_t  **used_cand_rp_list,
                                         grp_mask_t **used_grp_mask_list,
                                         u_int32 rp_addr,
                                         u_int8  rp_priority,
                                         u_int16 rp_holdtime,
                                         u_int32 group_addr,
                                         u_int32 group_mask,
                                         u_int32 bsr_hash_mask,
                                         u_int16 fragment_tag);
extern void	delete_rp_grp_entry	(cand_rp_t  **used_cand_rp_list, grp_mask_t **used_grp_mask_list,
                                         rp_grp_entry_t *rp_grp_entry_delete);
extern void	delete_grp_mask		(cand_rp_t  **used_cand_rp_list, grp_mask_t **used_grp_mask_list,
                                         u_int32 group_addr, u_int32 group_mask);
extern void	delete_rp		(cand_rp_t  **used_cand_rp_list, grp_mask_t **used_grp_mask_list,
                                         u_int32 rp_addr);
extern void	delete_rp_list		(cand_rp_t  **used_cand_rp_list, grp_mask_t **used_grp_mask_list);
extern rpentry_t *rp_match		(u_int32 group);
extern rp_grp_entry_t *rp_grp_match	(u_int32 group);
extern rpentry_t *rp_find		(u_int32 rp_address);
extern int	remap_grpentry		(grpentry_t *grpentry_ptr);
extern int	create_pim_bootstrap_message (char *send_buff);
extern int	check_mrtentry_rp	(mrtentry_t *mrtentry_ptr, u_int32 rp_addr);

#ifdef RSRR
#ifdef PIM
#define gtable				mrtentry
#endif /* PIM */
#define RSRR_NOTIFICATION_OK		TRUE
#define RSRR_NOTIFICATION_FALSE		FALSE

/* rsrr.c */
extern void	rsrr_init		(void);
extern void	rsrr_clean		(void);
extern void	rsrr_cache_send		(struct gtable *, int);
extern void	rsrr_cache_clean	(struct gtable *);
extern void	rsrr_cache_bring_up	(struct gtable *);
#endif /* RSRR */

/* timer.c */
extern void	init_timers		(void);
extern void	age_vifs		(void);
extern void	age_routes		(void);
extern void	age_misc		(void);
extern int	unicast_routing_changes	(srcentry_t *src_ent);
extern int	clean_srclist		(void);

/* trace.c */
/* u_int is promoted u_char */
extern void	accept_mtrace		(u_int32 src, u_int32 dst, u_int32 group, char *data, u_int no, int datalen);
extern void	accept_neighbor_request	(u_int32 src, u_int32 dst);
extern void	accept_neighbor_request2 (u_int32 src, u_int32 dst);

/* vif.c */
extern void	init_vifs		(void);
extern void	zero_vif		(struct uvif *, int);
extern void	stop_all_vifs		(void);
extern void	check_vif_state		(void);
extern vifi_t	local_address		(u_int32 src);
extern vifi_t	find_vif_direct		(u_int32 src);
extern vifi_t	find_vif_direct_local	(u_int32 src);
extern u_int32	max_local_address	(void);

struct rp_hold {
	struct rp_hold *next;
	u_int32	address;
	u_int32	group;
	u_int32	mask;
	u_int8	priority;
};

#ifndef HAVE_STRLCPY
size_t strlcpy(char *dst, const char *src, size_t siz);
#endif
#ifndef HAVE_PIDFILE
int pidfile(const char *basename);
#endif

#endif /* __PIMD_DEFS_H__ */

/**
 * Local Variables:
 *  version-control: t
 *  indent-tabs-mode: t
 *  c-file-style: "ellemtel"
 *  c-basic-offset: 4
 * End:
 */
