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
 *  $Id: igmp.c,v 1.18 2002/09/26 00:59:29 pavlin Exp $
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

/*
 * Exported variables.
 */
char    *igmp_recv_buf;		/* input packet buffer               */
char    *igmp_send_buf;  	/* output packet buffer              */
int     igmp_socket;	      	/* socket for all network I/O        */
u_int32 allhosts_group;	      	/* allhosts  addr in net order       */
u_int32 allrouters_group;	/* All-Routers addr in net order     */

#ifdef RAW_OUTPUT_IS_RAW
extern int curttl;
#endif /* RAW_OUTPUT_IS_RAW */

/*
 * Local functions definitions.
 */
static void igmp_read        (int i, fd_set *rfd);
static void accept_igmp      (ssize_t recvlen);


/*
 * Open and initialize the igmp socket, and fill in the non-changing
 * IP header fields in the output packet buffer.
 */
void init_igmp(void)
{
    struct ip *ip;
    
    igmp_recv_buf = calloc(1, RECV_BUF_SIZE);
    igmp_send_buf = calloc(1, SEND_BUF_SIZE);
    if (!igmp_recv_buf || !igmp_send_buf)
	logit(LOG_ERR, 0, "Ran out of memory in init_igmp()");

    if ((igmp_socket = socket(AF_INET, SOCK_RAW, IPPROTO_IGMP)) < 0) 
	logit(LOG_ERR, errno, "Failed creating IGMP socket in init_igmp()");
    
    k_hdr_include(igmp_socket, TRUE);	/* include IP header when sending */
    k_set_sndbuf(igmp_socket, SO_SEND_BUF_SIZE_MAX,
		 SO_SEND_BUF_SIZE_MIN); /* lots of output buffering        */
    k_set_rcvbuf(igmp_socket, SO_RECV_BUF_SIZE_MAX,
		 SO_RECV_BUF_SIZE_MIN); /* lots of input buffering        */
    k_set_ttl(igmp_socket, MINTTL);	/* restrict multicasts to one hop */
    k_set_loop(igmp_socket, FALSE);	/* disable multicast loopback     */
    
    ip         = (struct ip *)igmp_send_buf;
    memset(ip, 0, sizeof(*ip));
    ip->ip_v   = IPVERSION;
    ip->ip_hl  = (sizeof(struct ip) >> 2);
    ip->ip_tos = 0xc0;                  /* Internet Control   */
    ip->ip_id  = 0;                     /* let kernel fill in */
    ip->ip_off = 0;
    ip->ip_ttl = MAXTTL;		/* applies to unicasts only */
    ip->ip_p   = IPPROTO_IGMP;
#ifdef old_Linux
    ip->ip_csum = 0;                     /* let kernel fill in               */
#else
    ip->ip_sum = 0;                     /* let kernel fill in               */
#endif /* old_Linux */

    /* Everywhere in the daemon we use network-byte-order */    
    allhosts_group = htonl(INADDR_ALLHOSTS_GROUP);
    allrouters_group = htonl(INADDR_ALLRTRS_GROUP);
    
    if (register_input_handler(igmp_socket, igmp_read) < 0)
	logit(LOG_ERR, 0, "Failed registering igmp_read() as an input handler in init_igmp()");
}


/* Read an IGMP message */
static void igmp_read(int i __attribute__((unused)), fd_set *rfd __attribute__((unused)))
{
    ssize_t len;
    socklen_t dummy = 0;
    
    while ((len = recvfrom(igmp_socket, igmp_recv_buf, RECV_BUF_SIZE, 0, NULL, &dummy)) < 0) {
	if (errno == EINTR)
	    continue;		/* Received signal, retry syscall. */

	logit(LOG_ERR, errno, "Failed recvfrom() in igmp_read()");
	return;
    }
    
    accept_igmp(len);
}


/*
 * Process a newly received IGMP packet that is sitting in the input
 * packet buffer.
 */
static void accept_igmp(ssize_t recvlen)
{
    register u_int32 src, dst, group;
    struct ip *ip;
    struct igmp *igmp;
    int ipdatalen, iphdrlen, igmpdatalen;
    
    if (recvlen < (ssize_t)sizeof(struct ip)) {
	logit(LOG_WARNING, 0, "Received packet too short (%u bytes) for IP header", recvlen);
	return;
    }
    
    ip  = (struct ip *)igmp_recv_buf;
    src = ip->ip_src.s_addr;
    dst = ip->ip_dst.s_addr;
    
    /* packets sent up from kernel to daemon have ip->ip_p = 0 */
    if (ip->ip_p == 0) {
#if 0				/* XXX */
	if (src == 0 || dst == 0)
	    logit(LOG_WARNING, 0, "Kernel request not accurate, src %s dst %s",
		inet_fmt(src, s1, sizeof(s1)), inet_fmt(dst, s2, sizeof(s2)));
	else
#endif
	    process_kernel_call();
	return;
    }
    
    iphdrlen  = ip->ip_hl << 2;
#ifdef RAW_INPUT_IS_RAW
    ipdatalen = ntohs(ip->ip_len) - iphdrlen;
#else
    ipdatalen = ip->ip_len;
#endif
    if (iphdrlen + ipdatalen != recvlen) {
	logit(LOG_WARNING, 0, "Received packet from %s shorter (%u bytes) than hdr+data length (%u+%u)",
	    inet_fmt(src, s1, sizeof(s1)), recvlen, iphdrlen, ipdatalen);
	return;
    }
    
    igmp        = (struct igmp *)(igmp_recv_buf + iphdrlen);
    group       = igmp->igmp_group.s_addr;
    igmpdatalen = ipdatalen - IGMP_MINLEN;
    if (igmpdatalen < 0) {
	logit(LOG_WARNING, 0, "Received IP data field too short (%u bytes) for IGMP, from %s",
	    ipdatalen, inet_fmt(src, s1, sizeof(s1)));
	return;
    }

/* TODO: too noisy. Remove it? */
#if 0
    IF_DEBUG(DEBUG_PKT | debug_kind(IPPROTO_IGMP, igmp->igmp_type,
				    igmp->igmp_code))
	logit(LOG_DEBUG, 0, "RECV %s from %-15s to %s",
	      packet_kind(IPPROTO_IGMP, igmp->igmp_type, igmp->igmp_code),
	      inet_fmt(src, s1, sizeof(s1)), inet_fmt(dst, s2, sizeof(s2)));
#endif /* 0 */
    
    switch (igmp->igmp_type) {
	case IGMP_MEMBERSHIP_QUERY:
	    accept_membership_query(src, dst, group, igmp->igmp_code);
	    return;
	
	case IGMP_V1_MEMBERSHIP_REPORT:
	case IGMP_V2_MEMBERSHIP_REPORT:
	    accept_group_report(src, dst, group, igmp->igmp_type);
	    return;
	
	case IGMP_V2_LEAVE_GROUP:
	    accept_leave_message(src, dst, group);
	    return;
	
	case IGMP_DVMRP:
	    /* XXX: TODO: most of the stuff below is not implemented. We are still
	     * only PIM router.
	     */
	    group = ntohl(group);

	    switch (igmp->igmp_code) {
		case DVMRP_PROBE:
		    dvmrp_accept_probe(src, dst, (u_char *)(igmp+1), igmpdatalen, group);
		    return;
	    
		case DVMRP_REPORT:
		    dvmrp_accept_report(src, dst, (u_char *)(igmp+1), igmpdatalen, group);
		    return;

		case DVMRP_ASK_NEIGHBORS:
		    accept_neighbor_request(src, dst);
		    return;

		case DVMRP_ASK_NEIGHBORS2:
		    accept_neighbor_request2(src, dst);
		    return;
	    
		case DVMRP_NEIGHBORS:
		    dvmrp_accept_neighbors(src, dst, (u_char *)(igmp+1), igmpdatalen, group);
		    return;

		case DVMRP_NEIGHBORS2:
		    dvmrp_accept_neighbors2(src, dst, (u_char *)(igmp+1), igmpdatalen,
					    group);
		    return;
	    
		case DVMRP_PRUNE:
		    dvmrp_accept_prune(src, dst, (u_char *)(igmp+1), igmpdatalen);
		    return;
	    
		case DVMRP_GRAFT:
		    dvmrp_accept_graft(src, dst, (u_char *)(igmp+1), igmpdatalen);
		    return;
	    
		case DVMRP_GRAFT_ACK:
		    dvmrp_accept_g_ack(src, dst, (u_char *)(igmp+1), igmpdatalen);
		    return;
	    
		case DVMRP_INFO_REQUEST:
		    dvmrp_accept_info_request(src, dst, (u_char *)(igmp+1), igmpdatalen);
		    return;

		case DVMRP_INFO_REPLY:
		    dvmrp_accept_info_reply(src, dst, (u_char *)(igmp+1), igmpdatalen);
		    return;
	    
		default:
		    logit(LOG_INFO, 0, "Ignoring unknown DVMRP message code %u from %s to %s",
			  igmp->igmp_code, inet_fmt(src, s1, sizeof(s1)), inet_fmt(dst, s2, sizeof(s2)));
		    return;
	    }
	
	case IGMP_PIM:
	    return;    /* TODO: this is PIM v1 message. Handle it?. */
	
	case IGMP_MTRACE_RESP:
	    return;    /* TODO: implement it */
	
	case IGMP_MTRACE:
	    accept_mtrace(src, dst, group, (char *)(igmp+1), igmp->igmp_code,
			  igmpdatalen);
	    return;
	
	default:
	    logit(LOG_INFO, 0, "Ignoring unknown IGMP message type %x from %s to %s",
		  igmp->igmp_type, inet_fmt(src, s1, sizeof(s1)), inet_fmt(dst, s2, sizeof(s2)));
	    return;
    }
}

void send_igmp(char *buf, u_int32 src, u_int32 dst, int type, int code, u_int32 group, int datalen)
{
    struct sockaddr_in sdst;
    struct ip *ip;
    struct igmp *igmp;
    int sendlen;
    int setloop = 0;

    /* Prepare the IP header */
    ip 			    = (struct ip *)buf;
    ip->ip_len              = sizeof(struct ip) + IGMP_MINLEN + datalen;
    ip->ip_src.s_addr       = src; 
    ip->ip_dst.s_addr       = dst;
    sendlen                 = ip->ip_len;
#if defined(RAW_OUTPUT_IS_RAW) || defined(OpenBSD)
    ip->ip_len              = htons(ip->ip_len);
#endif /* RAW_OUTPUT_IS_RAW || OpenBSD */

    igmp                    = (struct igmp *)(buf + sizeof(struct ip));
    igmp->igmp_type         = type;
    igmp->igmp_code         = code;
    igmp->igmp_group.s_addr = group;
    igmp->igmp_cksum        = 0;
    igmp->igmp_cksum        = inet_cksum((u_int16 *)igmp,
					 IGMP_MINLEN + datalen);
    
    if (IN_MULTICAST(ntohl(dst))) {
	k_set_if(igmp_socket, src);
	if (type != IGMP_DVMRP || dst == allhosts_group) {
	    setloop = 1;
	    k_set_loop(igmp_socket, TRUE);
	}
#ifdef RAW_OUTPUT_IS_RAW
	ip->ip_ttl = curttl;
    } else {
	ip->ip_ttl = MAXTTL;
#endif /* RAW_OUTPUT_IS_RAW */
    }
    
    memset(&sdst, 0, sizeof(sdst));
    sdst.sin_family = AF_INET;
#ifdef HAVE_SA_LEN
    sdst.sin_len = sizeof(sdst);
#endif
    sdst.sin_addr.s_addr = dst;
    while (sendto(igmp_socket, igmp_send_buf, sendlen, 0, (struct sockaddr *)&sdst, sizeof(sdst)) < 0) {
	if (errno == EINTR)
	    continue;		/* Received signal, retry syscall. */
	else if (errno == ENETDOWN || errno == ENODEV)
	    check_vif_state();
	else
	    logit(log_level(IPPROTO_IGMP, type, code), errno, "Sendto to %s on %s",
		   inet_fmt(dst, s1, sizeof(s1)), inet_fmt(src, s2, sizeof(s2)));

	 if (setloop)
	     k_set_loop(igmp_socket, FALSE);

	 return;
     }

     if (setloop)
	 k_set_loop(igmp_socket, FALSE);

     IF_DEBUG(DEBUG_PKT|debug_kind(IPPROTO_IGMP, type, code)) {
	 logit(LOG_DEBUG, 0, "SENT %s from %-15s to %s",
	       packet_kind(IPPROTO_IGMP, type, code),
	       src == INADDR_ANY_N ? "INADDR_ANY" :
	       inet_fmt(src, s1, sizeof(s1)), inet_fmt(dst, s2, sizeof(s2)));
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
