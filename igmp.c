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
char   *igmp_recv_buf;		/* input packet buffer               */
char   *igmp_send_buf;		/* output packet buffer              */
int     igmp_socket;		/* socket for all network I/O        */
uint32_t allhosts_group;		/* allhosts  addr in net order       */
uint32_t allrouters_group;	/* All-Routers addr in net order     */
uint32_t allreports_group;	/* All IGMP routers in net order     */

#ifdef RAW_OUTPUT_IS_RAW
extern int curttl;
#endif /* RAW_OUTPUT_IS_RAW */

/*
 * Local functions definitions.
 */
static void igmp_read   (int i, fd_set *rfd);
static void accept_igmp (ssize_t recvlen);


/*
 * Open and initialize the igmp socket, and fill in the non-changing
 * IP header fields in the output packet buffer.
 */
void init_igmp(void)
{
    struct ip *ip;
    char *router_alert;

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

    ip	       = (struct ip *)igmp_send_buf;
    memset(ip, 0, IP_IGMP_HEADER_LEN);
    ip->ip_v   = IPVERSION;
    ip->ip_hl  = IP_IGMP_HEADER_LEN >> 2;
    ip->ip_tos = 0xc0;			/* Internet Control   */
    ip->ip_id  = 0;			/* let kernel fill in */
    ip->ip_off = 0;
    ip->ip_ttl = MAXTTL;		/* applies to unicasts only */
    ip->ip_p   = IPPROTO_IGMP;
    ip->ip_sum = 0;			/* let kernel fill in */

    /* Enable RFC2113 IP Router Alert */
    router_alert    = igmp_send_buf + sizeof(struct ip);
    router_alert[0] = IPOPT_RA;
    router_alert[1] = 4;
    router_alert[2] = 0;
    router_alert[3] = 0;

    /* Everywhere in the daemon we use network-byte-order */
    allhosts_group   = htonl(INADDR_ALLHOSTS_GROUP);
    allrouters_group = htonl(INADDR_ALLRTRS_GROUP);
    allreports_group = htonl(INADDR_ALLRPTS_GROUP);

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
    int ipdatalen, iphdrlen, igmpdatalen;
    uint32_t src, dst, group;
    struct ip *ip;
    struct igmp *igmp;
    int igmp_version = 3;

    if (recvlen < MIN_IP_HEADER_LEN) {
	logit(LOG_INFO, 0, "Received packet too short (%u bytes) for IP header", recvlen);
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
#ifdef HAVE_IP_HDRINCL_BSD_ORDER
    ipdatalen = ip->ip_len - iphdrlen;
#else
    ipdatalen = ntohs(ip->ip_len) - iphdrlen;
#endif

    if (iphdrlen + ipdatalen != recvlen) {
	logit(LOG_INFO, 0, "Received packet from %s shorter (%u bytes) than hdr+data length (%u+%u)",
	    inet_fmt(src, s1, sizeof(s1)), recvlen, iphdrlen, ipdatalen);
	return;
    }

    igmp	= (struct igmp *)(igmp_recv_buf + iphdrlen);
    group       = igmp->igmp_group.s_addr;
    logit(LOG_DEBUG, 0, "igmp_packet length %u", recvlen);
        logit(LOG_DEBUG, 0, "src0 %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u",
                (unsigned char)(*(igmp_recv_buf + 20)),
                (unsigned char)(*(igmp_recv_buf + 21)),
                (unsigned char)(*(igmp_recv_buf + 22)),
                (unsigned char)(*(igmp_recv_buf + 23)),
                (unsigned char)(*(igmp_recv_buf + 24)),
                (unsigned char)(*(igmp_recv_buf + 25)),
                (unsigned char)(*(igmp_recv_buf + 26)),
                (unsigned char)(*(igmp_recv_buf + 27)),
                (unsigned char)(*(igmp_recv_buf + 28)),
                (unsigned char)(*(igmp_recv_buf + 29)),
                (unsigned char)(*(igmp_recv_buf + 30)),
                (unsigned char)(*(igmp_recv_buf + 31)),
                (unsigned char)(*(igmp_recv_buf + 32)),
                (unsigned char)(*(igmp_recv_buf + 33)),
                (unsigned char)(*(igmp_recv_buf + 34)),
                (unsigned char)(*(igmp_recv_buf + 35))
                );
    igmpdatalen = ipdatalen - IGMP_MINLEN;

    if (igmpdatalen < 0) {
	logit(LOG_INFO, 0, "Received IP data field too short (%u bytes) for IGMP, from %s",
	    ipdatalen, inet_fmt(src, s1, sizeof(s1)));
	return;
    }

    /* TODO: rate-limit logging? */
    logit(LOG_INFO, 0, "Received %s from %s to %s",
	  packet_kind(IPPROTO_IGMP, igmp->igmp_type, igmp->igmp_code),
	  inet_fmt(src, s1, sizeof(s1)), inet_fmt(dst, s2, sizeof(s2)));

    switch (igmp->igmp_type) {
	case IGMP_MEMBERSHIP_QUERY:
	    // RFC 3376:7.1
	    if (ipdatalen==8) {
		if (igmp->igmp_code==0) {
		    igmp_version = 1;
		} else {
		    igmp_version = 2;
		}
	    } else if (ipdatalen>=12) {
		igmp_version = 3;
	    } else {
		logit(LOG_DEBUG, 0, "Received invalid IGMP Membership query: Max Resp Code = %d, length = %d",
		igmp->igmp_code, ipdatalen);
	    }
	    logit(LOG_DEBUG, 0, "Received IGMPv%d Membership query", igmp_version);
	    accept_membership_query(src, dst, group, igmp->igmp_code, igmp_version);
	    return;

	case IGMP_V1_MEMBERSHIP_REPORT:
	case IGMP_V2_MEMBERSHIP_REPORT:
	    logit(LOG_DEBUG, 0, "Received IGMPv2 Membership Report from %s", inet_fmt(src, s1, sizeof(s1)));
	    accept_group_report(src, dst, group, igmp->igmp_type);
	    return;

	case IGMP_V2_LEAVE_GROUP:
	    accept_leave_message(src, dst, group);
	    return;

	case IGMP_V3_MEMBERSHIP_REPORT:
	    if (igmpdatalen < IGMP_V3_GROUP_RECORD_MIN_SIZE) {
		logit(LOG_DEBUG, 0, "Too short IGMPv3 Membership report: igmpdatalen(%d) < MIN(%d)", igmpdatalen, IGMP_V3_GROUP_RECORD_MIN_SIZE);
		return;
	    }
	    accept_membership_report(src, dst, (struct igmpv3_report *)(igmp_recv_buf + iphdrlen), recvlen - iphdrlen);
	    return;

	case IGMP_DVMRP:
	    /* XXX: TODO: most of the stuff below is not implemented. We are still
	     * only PIM router.
	     */
	    group = ntohl(group);

	    switch (igmp->igmp_code) {
		case DVMRP_PROBE:
		    dvmrp_accept_probe(src, dst, (uint8_t *)(igmp+1), igmpdatalen, group);
		    return;

		case DVMRP_REPORT:
		    dvmrp_accept_report(src, dst, (uint8_t *)(igmp+1), igmpdatalen, group);
		    return;

		case DVMRP_ASK_NEIGHBORS:
		    accept_neighbor_request(src, dst);
		    return;

		case DVMRP_ASK_NEIGHBORS2:
		    accept_neighbor_request2(src, dst);
		    return;

		case DVMRP_NEIGHBORS:
		    dvmrp_accept_neighbors(src, dst, (uint8_t *)(igmp+1), igmpdatalen, group);
		    return;

		case DVMRP_NEIGHBORS2:
		    dvmrp_accept_neighbors2(src, dst, (uint8_t *)(igmp+1), igmpdatalen, group);
		    return;

		case DVMRP_PRUNE:
		    dvmrp_accept_prune(src, dst, (uint8_t *)(igmp+1), igmpdatalen);
		    return;

		case DVMRP_GRAFT:
		    dvmrp_accept_graft(src, dst, (uint8_t *)(igmp+1), igmpdatalen);
		    return;

		case DVMRP_GRAFT_ACK:
		    dvmrp_accept_g_ack(src, dst, (uint8_t *)(igmp+1), igmpdatalen);
		    return;

		case DVMRP_INFO_REQUEST:
		    dvmrp_accept_info_request(src, dst, (uint8_t *)(igmp+1), igmpdatalen);
		    return;

		case DVMRP_INFO_REPLY:
		    dvmrp_accept_info_reply(src, dst, (uint8_t *)(igmp+1), igmpdatalen);
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

static void send_ip_frame(uint32_t src, uint32_t dst, int type, int code, char *buf, size_t len)
{
    int setloop = 0;
    struct ip *ip;
    struct sockaddr_in sin;

    /* Prepare the IP header */
    len		     += IP_IGMP_HEADER_LEN;
    ip		      = (struct ip *)buf;
    ip->ip_id	      = 0; /* let kernel fill in */
    ip->ip_off	      = 0;
    ip->ip_src.s_addr = src;
    ip->ip_dst.s_addr = dst;
#ifdef HAVE_IP_HDRINCL_BSD_ORDER
    ip->ip_len	      = len;
#else
    ip->ip_len	      = htons(len);
#endif

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
#endif
    }

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = dst;
#ifdef HAVE_SA_LEN
    sin.sin_len = sizeof(sin);
#endif

    /* Todo: rate-limit logging? */
    logit(LOG_INFO, 0, "Send %s from %s to %s",
	  packet_kind(IPPROTO_IGMP, type, code),
	  src == INADDR_ANY_N ? "INADDR_ANY" :
	  inet_fmt(src, s1, sizeof(s1)), inet_fmt(dst, s2, sizeof(s2)));

    while (sendto(igmp_socket, buf, len, 0, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
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

    IF_DEBUG(DEBUG_PKT | debug_kind(IPPROTO_IGMP, type, code)) {
	logit(LOG_DEBUG, 0, "SENT %5d bytes %s from %-15s to %s", len,
	      packet_kind(IPPROTO_IGMP, type, code),
	      src == INADDR_ANY_N
		  ? "INADDR_ANY"
		  : inet_fmt(src, s1, sizeof(s1)),
	      inet_fmt(dst, s2, sizeof(s2)));
    }
}

void send_igmp(char *buf, uint32_t src, uint32_t dst, int type, int code, uint32_t group, int datalen)
{
    size_t len = IGMP_MINLEN + datalen;
    struct igmpv3_query *igmp;

    igmp              = (struct igmpv3_query *)(buf + IP_IGMP_HEADER_LEN);
    igmp->type        = type;
    igmp->code        = code;
    igmp->group       = group;
    igmp->csum        = 0;
    igmp->csum        = inet_cksum((uint16_t *)igmp, len);

    send_ip_frame(src, dst, type, code, buf, len);
}

/**
 * Local Variables:
 *  version-control: t
 *  indent-tabs-mode: t
 *  c-file-style: "ellemtel"
 *  c-basic-offset: 4
 * End:
 */
