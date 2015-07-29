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
 *  $Id: pim.c,v 1.24 2002/09/26 00:59:30 pavlin Exp $
 */

#include "defs.h"

#define SEND_DEBUG_NUMBER 50	/* For throttling log messages */

/*
 * Exported variables.
 */
char	*pim_recv_buf;		/* input packet buffer   */
char	*pim_send_buf;		/* output packet buffer  */

uint32_t	allpimrouters_group;	/* ALL_PIM_ROUTERS address in net order */
int	pim_socket;		/* socket for PIM control msgs */

#ifdef RAW_OUTPUT_IS_RAW
extern int curttl;
#endif /* RAW_OUTPUT_IS_RAW */

/*
 * Local variables.
 */
static uint16_t ip_id = 0;
//static u_int pim_send_cnt = 0;


/*
 * Local function definitions.
 */
static void pim_read   (int f, fd_set *rfd);
static void accept_pim (ssize_t recvlen);
static int  send_frame (char *buf, size_t len, size_t frag, size_t mtu, struct sockaddr *dst, size_t salen);

/*
 * Setup raw kernel socket for PIM protocol and send/receive buffers.
 */
void init_pim(void)
{
    struct ip *ip;

    /* Setup the PIM raw socket */
    if ((pim_socket = socket(AF_INET, SOCK_RAW, IPPROTO_PIM)) < 0)
	logit(LOG_ERR, errno, "Failed creating PIM socket");
    k_hdr_include(pim_socket, TRUE);      /* include IP header when sending */
    k_set_sndbuf(pim_socket, SO_SEND_BUF_SIZE_MAX,
		 SO_SEND_BUF_SIZE_MIN);   /* lots of output buffering        */
    k_set_rcvbuf(pim_socket, SO_RECV_BUF_SIZE_MAX,
		 SO_RECV_BUF_SIZE_MIN);   /* lots of input buffering        */
    k_set_ttl(pim_socket, MINTTL);	  /* restrict multicasts to one hop */
    k_set_loop(pim_socket, FALSE);	  /* disable multicast loopback	    */

    allpimrouters_group = htonl(INADDR_ALL_PIM_ROUTERS);

    pim_recv_buf = calloc(1, RECV_BUF_SIZE);
    pim_send_buf = calloc(1, SEND_BUF_SIZE);
    if (!pim_recv_buf || !pim_send_buf)
	logit(LOG_ERR, 0, "Ran out of memory in init_pim()");

    /* One time setup in the buffers */
    ip		 = (struct ip *)pim_send_buf;
    memset(ip, 0, sizeof(*ip));
    ip->ip_v     = IPVERSION;
    ip->ip_hl    = (sizeof(struct ip) >> 2);
    ip->ip_tos   = 0;    /* TODO: setup?? */
    ip->ip_id    = 0;    /* Make sure to update ID field, maybe fragmenting below */
    ip->ip_off   = 0;
    ip->ip_p     = IPPROTO_PIM;
    ip->ip_sum   = 0;	 /* let kernel fill in */

    if (register_input_handler(pim_socket, pim_read) < 0)
	logit(LOG_ERR, 0,  "Failed registering pim_read() as an input handler");

    /* Initialize the building Join/Prune messages working area */
    build_jp_message_pool = (build_jp_message_t *)NULL;
    build_jp_message_pool_counter = 0;
}


/* Read a PIM message */
static void pim_read(int f __attribute__((unused)), fd_set *rfd __attribute__((unused)))
{
    ssize_t len;
    socklen_t dummy = 0;
    sigset_t block, oblock;

    while ((len = recvfrom(pim_socket, pim_recv_buf, RECV_BUF_SIZE, 0, NULL, &dummy)) < 0) {
	if (errno == EINTR)
	    continue;		/* Received signal, retry syscall. */

	logit(LOG_ERR, errno, "Failed recvfrom() in pim_read()");
	return;
    }

    sigemptyset(&block);
    sigaddset(&block, SIGALRM);
    if (sigprocmask(SIG_BLOCK, &block, &oblock) < 0)
	logit(LOG_ERR, errno, "sigprocmask");

    accept_pim(len);

    sigprocmask(SIG_SETMASK, &oblock, (sigset_t *)NULL);
}

static void accept_pim(ssize_t recvlen)
{
    uint32_t src, dst;
    struct ip *ip;
    pim_header_t *pim;
    int iphdrlen, pimlen;
    char source[20], dest[20];

    if (recvlen < (ssize_t)sizeof(struct ip)) {
	logit(LOG_WARNING, 0, "Received PIM packet too short (%u bytes) for IP header", recvlen);
	return;
    }

    ip		= (struct ip *)pim_recv_buf;
    src		= ip->ip_src.s_addr;
    dst		= ip->ip_dst.s_addr;
    iphdrlen	= ip->ip_hl << 2;

    pim		= (pim_header_t *)(pim_recv_buf + iphdrlen);
    pimlen	= recvlen - iphdrlen;

    /* Sanity check packet length */
    if (pimlen < (ssize_t)sizeof(*pim)) {
	logit(LOG_WARNING, 0, "IP data field too short (%d bytes) for PIM header, from %s to %s",
	      pimlen, inet_fmt(src, source, sizeof(source)), inet_fmt(dst, dest, sizeof(dest)));
	return;
    }

    IF_DEBUG(DEBUG_PIM_DETAIL) {
	IF_DEBUG(DEBUG_PIM) {
	    logit(LOG_DEBUG, 0, "RECV %5d bytes %s from %-15s to %s ", recvlen,
		  packet_kind(IPPROTO_PIM, pim->pim_type, 0),
		  inet_fmt(src, source, sizeof(source)), inet_fmt(dst, dest, sizeof(dest)));
	}
    }

    /* TODO: Check PIM version */
    /* TODO: check the dest. is ALL_PIM_ROUTERS (if multicast address) */
    /* TODO: Checksum verification is done in each of the processing functions.
     * No need for checksum, if already done in the kernel?
     */
    switch (pim->pim_type) {
	case PIM_HELLO:
	    receive_pim_hello(src, dst, (char *)(pim), pimlen);
	    break;

	case PIM_REGISTER:
	    receive_pim_register(src, dst, (char *)(pim), pimlen);
	    break;

	case PIM_REGISTER_STOP:
	    receive_pim_register_stop(src, dst, (char *)(pim), pimlen);
	    break;

	case PIM_JOIN_PRUNE:
	    receive_pim_join_prune(src, dst, (char *)(pim), pimlen);
	    break;

	case PIM_BOOTSTRAP:
	    receive_pim_bootstrap(src, dst, (char *)(pim), pimlen);
	    break;

	case PIM_ASSERT:
	    receive_pim_assert(src, dst, (char *)(pim), pimlen);
	    break;

	case PIM_GRAFT:
	case PIM_GRAFT_ACK:
	    logit(LOG_INFO, 0, "ignore %s from %s to %s", packet_kind(IPPROTO_PIM, pim->pim_type, 0),
		  inet_fmt(src, source, sizeof(source)), inet_fmt(dst, dest, sizeof(dest)));
	    break;

	case PIM_CAND_RP_ADV:
	    receive_pim_cand_rp_adv(src, dst, (char *)(pim), pimlen);
	    break;

	default:
	    logit(LOG_INFO, 0, "ignore unknown PIM message code %u from %s to %s", pim->pim_type,
		  inet_fmt(src, source, sizeof(source)), inet_fmt(dst, dest, sizeof(dest)));
	    break;
    }
}


/*
 * Send a multicast PIM packet from src to dst, PIM message type = "type"
 * and data length (after the PIM header) = "len"
 */
void send_pim(char *buf, uint32_t src, uint32_t dst, int type, size_t len)
{
    struct sockaddr_in sin;
    struct ip *ip;
    pim_header_t *pim;
    int sendlen = sizeof(struct ip) + sizeof(pim_header_t) + len;
    int setloop = 0;
    char source[20], dest[20];

    /* Prepare the IP header */
    ip                 = (struct ip *)buf;
    ip->ip_id	       = htons(++ip_id);
    ip->ip_off         = 0;
    ip->ip_src.s_addr  = src;
    ip->ip_dst.s_addr  = dst;
    ip->ip_ttl         = MAXTTL;            /* applies to unicast only */
#ifdef HAVE_IP_HDRINCL_BSD_ORDER
    ip->ip_len         = sendlen;
#else
    ip->ip_len         = htons(sendlen);
#endif

    /* Prepare the PIM packet */
    pim		       = (pim_header_t *)(buf + sizeof(struct ip));
    pim->pim_type      = type;
    pim->pim_vers      = PIM_PROTOCOL_VERSION;
    pim->pim_reserved  = 0;
    pim->pim_cksum     = 0;

    /* TODO: XXX: if start using this code for PIM_REGISTERS, exclude the
     * encapsulated packet from the checsum. */
    pim->pim_cksum     = inet_cksum((uint16_t *)pim, sizeof(pim_header_t) + len);

    if (IN_MULTICAST(ntohl(dst))) {
	k_set_if(pim_socket, src);
	if ((dst == allhosts_group) ||
	    (dst == allrouters_group) ||
	    (dst == allpimrouters_group) ||
	    (dst == allreports_group)) {
	    setloop = 1;
	    k_set_loop(pim_socket, TRUE);
	}
#ifdef RAW_OUTPUT_IS_RAW
	ip->ip_ttl = curttl;
    } else {
	ip->ip_ttl = MAXTTL;
#endif /* RAW_OUTPUT_IS_RAW */
    }

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = dst;
#ifdef HAVE_SA_LEN
    sin.sin_len = sizeof(sin);
#endif

    while (sendto(pim_socket, buf, sendlen, 0, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
	if (errno == EINTR)
	    continue;		/* Received signal, retry syscall. */
	if (errno == ENETDOWN || errno == ENODEV)
	    check_vif_state();
	else if (errno == EPERM || errno == EHOSTUNREACH)
	    logit(LOG_WARNING, 0, "Not allowed to send PIM message from %s to %s, possibly firewall"
#ifdef __linux__
		  ", or SELinux policy violation,"
#endif
		  " related problem."
		  ,
		  inet_fmt(src, source, sizeof(source)), inet_fmt(dst, dest, sizeof(dest)));
	else
	    logit(LOG_WARNING, errno, "sendto from %s to %s",
		  inet_fmt(src, source, sizeof(source)), inet_fmt(dst, dest, sizeof(dest)));
	if (setloop)
	    k_set_loop(pim_socket, FALSE);
	return;
    }

    if (setloop)
	k_set_loop(pim_socket, FALSE);

    IF_DEBUG(DEBUG_PIM_DETAIL) {
	IF_DEBUG(DEBUG_PIM) {
	    logit(LOG_DEBUG, 0, "SENT %5d bytes %s from %-15s to %s",
		  sendlen, packet_kind(IPPROTO_PIM, type, 0),
		  src == INADDR_ANY_N ? "INADDR_ANY" :
		  inet_fmt(src, source, sizeof(source)), inet_fmt(dst, dest, sizeof(dest)));
	}
    }
}


/* TODO: This can be merged with the above procedure */
/*
 * Send an unicast PIM packet from src to dst, PIM message type = "type"
 * and data length (after the PIM common header) = "len"
 */
void send_pim_unicast(char *buf, int mtu, uint32_t src, uint32_t dst, int type, size_t len)
{
    struct sockaddr_in sin;
    struct ip *ip;
    pim_header_t *pim;
    int result, sendlen = sizeof(struct ip) + sizeof(pim_header_t) + len;
    char source[20], dest[20];

    /* Prepare the IP header */
    ip                 = (struct ip *)buf;
    ip->ip_id	       = htons(++ip_id);
    ip->ip_src.s_addr  = src;
    ip->ip_dst.s_addr  = dst;
    ip->ip_ttl         = MAXTTL; /* TODO: XXX: setup TTL from the inner mcast packet? */
#ifdef HAVE_IP_HDRINCL_BSD_ORDER
    ip->ip_len         = sendlen;
#else
    ip->ip_len         = htons(sendlen);
#endif

    /* Prepare the PIM packet */
    pim                = (pim_header_t *)(buf + sizeof(struct ip));
    pim->pim_type      = type;
    pim->pim_vers      = PIM_PROTOCOL_VERSION;
    pim->pim_reserved  = 0;
    pim->pim_cksum     = 0;

    /* XXX: The PIM_REGISTERs don't include the encapsulated
     * inner packet in the checksum.
     * Well, try to explain this to cisco...
     * If your RP is cisco and if it shows many PIM_REGISTER checksum
     * errors from this router, then #define BROKEN_CISCO_CHECKSUM here
     * or in your Makefile.
     * Note that such checksum is not in the spec, and such PIM_REGISTERS
     * may be dropped by some implementations (pimd should be OK).
     */
#ifdef BROKEN_CISCO_CHECKSUM
    pim->pim_cksum	= inet_cksum((uint16_t *)pim, sizeof(pim_header_t) + len);
#else
    if (PIM_REGISTER == type) {
	pim->pim_cksum	= inet_cksum((uint16_t *)pim, sizeof(pim_header_t) + sizeof(pim_register_t));
    } else {
        pim->pim_cksum	= inet_cksum((uint16_t *)pim, sizeof(pim_header_t) + len);
    }
#endif /* BROKEN_CISCO_CHECKSUM */

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = dst;
#ifdef HAVE_SA_LEN
    sin.sin_len = sizeof(sin);
#endif

    IF_DEBUG(DEBUG_PIM_DETAIL) {
	IF_DEBUG(DEBUG_PIM) {
	    logit(LOG_DEBUG, 0, "SEND %5d bytes %s from %-15s to %s ...",
		  sendlen, packet_kind(IPPROTO_PIM, type, 0),
		  inet_fmt(src, source, sizeof(source)), inet_fmt(dst, dest, sizeof(dest)));
	}
    }

    result = send_frame(buf, sendlen, 0, mtu, (struct sockaddr *)&sin, sizeof(sin));
    if (result) {
	logit(LOG_WARNING, errno, "sendto from %s to %s",
	      inet_fmt(ip->ip_src.s_addr, source, sizeof(source)),
	      inet_fmt(ip->ip_dst.s_addr, dest, sizeof(dest)));
	return;
    }

    IF_DEBUG(DEBUG_PIM_DETAIL) {
	IF_DEBUG(DEBUG_PIM) {
#if 0 /* TODO: use pim_send_cnt? */
	    if (++pim_send_cnt > SEND_DEBUG_NUMBER) {
		pim_send_cnt = 0;
		logit(LOG_DEBUG, 0, "SENT %5d bytes %s from %-15s to %s",
		      sendlen, packet_kind(IPPROTO_PIM, type, 0),
		      inet_fmt(src, source, sizeof(source)), inet_fmt(dst, dest, sizeof(dest)));
	    }
#endif
	    logit(LOG_DEBUG, 0, "SENT %5d bytes %s from %-15s to %s",
		  sendlen, packet_kind(IPPROTO_PIM, type, 0),
		  inet_fmt(src, source, sizeof(source)), inet_fmt(dst, dest, sizeof(dest)));
	}
    }
}


#if 1
/*
 * send unicast register frames
 * Version: Michael Fine
 * Staus:   Works, albeit non-optimal
 * Design:  Only fragments if sendto() fails with EMSGSIZE
 *          It then tries to re-send by splitting the frame in two equal halves,
 *          calling send_frame() recursively until the frame has been sent.
 */
static int send_frame(char *buf, size_t len, size_t frag, size_t mtu, struct sockaddr *dst, size_t salen)
{
    struct ip *ip = (struct ip *)buf;
    char source[20], dest[20];

    IF_DEBUG(DEBUG_PIM_REGISTER) {
	logit(LOG_INFO, 0, "Sending unicast: len = %d, frag %zd, mtu %zd, to %s",
	      len, frag, mtu, inet_fmt(ip->ip_dst.s_addr, source, sizeof(source)));
	dump_frame(NULL, buf, len);
    }

    while (sendto(pim_socket, buf, len, 0, dst, salen) < 0) {
	switch (errno) {
	    case EINTR:
		continue; /* Received signal, retry syscall. */

	    case ENETDOWN:
		check_vif_state();
		return -1;

	    case EMSGSIZE:
	    {
		/* split it in half and recursively send each half */
		size_t hdrsize = sizeof(struct ip);
		size_t newlen1 = ((len - hdrsize) / 2) & 0xFFF8; /* 8 byte boundary */
		size_t sendlen = newlen1 + hdrsize;
		size_t offset  = ntohs(ip->ip_off);

		/* send first half */
		ip->ip_len = htons(sendlen);
		ip->ip_off = htons(offset | IP_MF);
		if (send_frame(buf, sendlen, 1, newlen1, dst, salen) == 0) {
		    /* send second half */
		    struct ip *ip2 = (struct ip *)(buf + newlen1);
		    size_t newlen2 = len - sendlen;
		           sendlen = newlen2 + hdrsize;

		    memcpy(ip2, ip, hdrsize);
		    ip2->ip_len = htons(sendlen);
		    ip2->ip_off = htons(offset + (newlen1 >> 3)); /* keep flgs */
		    return send_frame((char *)ip2, sendlen, 1, newlen2, dst, salen);
		}

		return -1;
	    }

	    default:
		logit(LOG_WARNING, errno, "sendto from %s to %s",
		      inet_fmt(ip->ip_src.s_addr, source, sizeof(source)),
		      inet_fmt(ip->ip_dst.s_addr, dest, sizeof(dest)));
		return -1;
	}
    }

    return 0;
}

#else
/*
 * send unicast register frames
 * Version: Joachim Nilsson
 * Staus:   Does not work (yet!)
 * Design:  Fragment IP frames when the frame length exceeds the MTU
 *          reported from the interface.  Optimizes for less fragments
 *          and fewer syscalls, should get better network utilization.
 *          Can be easily modified to use the PMTU instead.
 *
 * Feel free to debug this version and submit your patches -- it should work! --Joachim
 */
static int send_frame(char *buf, size_t len, size_t frag, size_t mtu, struct sockaddr *dst, size_t salen)
{
    struct ip *next, *ip = (struct ip *)buf;
    size_t xferlen, offset;
    char source[20], dest[20];

    if (!mtu)
	mtu = IP_MSS;

    if (len > mtu)
	xferlen = (mtu - sizeof(struct ip)) & 0xFFF8;
    else
	xferlen = len;

    offset     = (ntohs(ip->ip_off) & IP_OFFMASK) + (frag >> 3);
    ip->ip_off = offset;
    len	       = len - xferlen;
    if (len)
	ip->ip_off |= IP_MF;
    ip->ip_off = htons(ip->ip_off);
    ip->ip_len = htons(xferlen);

    IF_DEBUG(DEBUG_PIM_REGISTER) {
	logit(LOG_INFO, 0, "Sending %-4d bytes %sunicast (MTU %-4d, offset %zd) to %s",
	      xferlen, len ? "fragmented " : "", mtu, offset,
	      inet_fmt(ip->ip_dst.s_addr, source, sizeof(source)));
	dump_frame(NULL, buf, xferlen);
    }

    /* send first fragment */
    while (sendto(pim_socket, ip, xferlen, 0, dst, salen) < 0) {
	switch (errno) {
	    case EINTR:
		continue;		/* Received signal, retry syscall. */

	    case ENETDOWN:
		check_vif_state();
		return -1;

	    case EMSGSIZE:
		if (mtu > IP_MSS)
		    return send_frame((char *)ip, xferlen, frag, IP_MSS, dst, salen);
		/* fall through */

	    default:
		return -1;
	}
    }

    /* send reminder */
    if (len) {
	size_t hdrsz = sizeof(struct ip);

	/* Update data pointers */
	next = (struct ip *)(buf + xferlen - hdrsz);
	memcpy(next, ip, hdrsz);

	return send_frame((char *)next, len + hdrsz, xferlen, mtu, dst, salen);
    }

    return 0;
}
#endif

/**
 * Local Variables:
 *  version-control: t
 *  indent-tabs-mode: t
 *  c-file-style: "ellemtel"
 *  c-basic-offset: 4
 * End:
 */
