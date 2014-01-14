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

/*
 * Exported variables.
 */
char	*pim_recv_buf;		/* input packet buffer   */
char	*pim_send_buf;		/* output packet buffer  */

u_int32	allpimrouters_group;	/* ALL_PIM_ROUTERS address in net order */
int	pim_socket;		/* socket for PIM control msgs */

#ifdef RAW_OUTPUT_IS_RAW
extern int curttl;
#endif /* RAW_OUTPUT_IS_RAW */

/*
 * Local function definitions.
 */
static void pim_read   (int f, fd_set *rfd);
static void accept_pim (ssize_t recvlen);
static int send_raw_ip(char *buf, size_t len, struct sockaddr_in *sdst);

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
    k_set_ttl(pim_socket, MINTTL);        /* restrict multicasts to one hop */
    k_set_loop(pim_socket, FALSE);        /* disable multicast loopback     */

    allpimrouters_group = htonl(INADDR_ALL_PIM_ROUTERS);

    pim_recv_buf = calloc(1, RECV_BUF_SIZE);
    pim_send_buf = calloc(1, SEND_BUF_SIZE);
    if (!pim_recv_buf || !pim_send_buf)
	logit(LOG_ERR, 0, "Ran out of memory in init_pim()");

    /* One time setup in the buffers */
    ip           = (struct ip *)pim_send_buf;
    memset(ip, 0, sizeof(*ip));
    ip->ip_v     = IPVERSION;
    ip->ip_hl    = (sizeof(struct ip) >> 2);
    ip->ip_tos   = 0;    /* TODO: setup?? */
    ip->ip_id    = 0;	 /* let kernel fill in */
    ip->ip_off   = 0;
    ip->ip_p     = IPPROTO_PIM;
#ifdef old_Linux
    ip->ip_csum = 0;		/* let kernel fill in */
#else
    ip->ip_sum = 0;		/* let kernel fill in */
#endif /* old_Linux */

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
#if defined(SYSV) || defined(__USE_SVID)
    sigset_t block, oblock;
#else
    int omask;
#endif

    while ((len = recvfrom(pim_socket, pim_recv_buf, RECV_BUF_SIZE, 0, NULL, &dummy)) < 0) {
	if (errno == EINTR)
	    continue;		/* Received signal, retry syscall. */

	logit(LOG_ERR, errno, "Failed recvfrom() in pim_read()");
        return;
    }

#if defined(SYSV) || defined(__USE_SVID)
    (void)sigemptyset(&block);
    (void)sigaddset(&block, SIGALRM);
    if (sigprocmask(SIG_BLOCK, &block, &oblock) < 0)
        logit(LOG_ERR, errno, "sigprocmask");
#else
    /* Use of omask taken from main() */
    omask = sigblock(sigmask(SIGALRM));
#endif /* SYSV */

    accept_pim(len);

#if defined(SYSV) || defined(__USE_SVID)
    (void)sigprocmask(SIG_SETMASK, &oblock, (sigset_t *)NULL);
#else
    (void)sigsetmask(omask);
#endif /* SYSV */
}

static void accept_pim(ssize_t recvlen)
{
    u_int32 src, dst;
    struct ip *ip;
    pim_header_t *pim;
    int iphdrlen, pimlen;

    if (recvlen < (ssize_t)sizeof(struct ip)) {
	logit(LOG_WARNING, 0, "packet too short (%u bytes) for IP header", recvlen);
	return;
    }

    ip          = (struct ip *)pim_recv_buf;
    src         = ip->ip_src.s_addr;
    dst         = ip->ip_dst.s_addr;
    iphdrlen    = ip->ip_hl << 2;

    pim         = (pim_header_t *)(pim_recv_buf + iphdrlen);
    pimlen	= recvlen - iphdrlen;
    if (pimlen < (ssize_t)sizeof(*pim)) {
        logit(LOG_WARNING, 0,
	      "IP data field too short (%u bytes) for PIM header, from %s to %s",
	      pimlen, inet_fmt(src, s1, sizeof(s1)), inet_fmt(dst, s2, sizeof(s2)));
        return;
    }

    IF_DEBUG(DEBUG_PIM_DETAIL) {
	IF_DEBUG(DEBUG_PIM) {
	    logit(LOG_DEBUG, 0, "RECV %s from %-15s to %s ",
		  packet_kind(IPPROTO_PIM, pim->pim_type, 0),
		  inet_fmt(src, s1, sizeof(s1)), inet_fmt(dst, s2, sizeof(s2)));
	    logit(LOG_DEBUG, 0, "PIM type is %u", pim->pim_type);
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
	    logit(LOG_INFO, 0, "ignore %s from %s to %s",
		  packet_kind(IPPROTO_PIM, pim->pim_type, 0), inet_fmt(src, s1, sizeof(s1)),
		  inet_fmt(dst, s2, sizeof(s2)));
	case PIM_CAND_RP_ADV:
	    receive_pim_cand_rp_adv(src, dst, (char *)(pim), pimlen);
	    break;
	default:
	    logit(LOG_INFO, 0,
		  "ignore unknown PIM message code %u from %s to %s",
		  pim->pim_type, inet_fmt(src, s1, sizeof(s1)), inet_fmt(dst, s2, sizeof(s2)));
	    break;
    }
}


/*
 * Send a multicast PIM packet from src to dst, PIM message type = "type"
 * and data length (after the PIM header) = "datalen"
 */
void send_pim(char *buf, u_int32 src, u_int32 dst, int type, int datalen)
{
    struct sockaddr_in sdst;
    struct ip *ip;
    pim_header_t *pim;
    int sendlen;
    int setloop = 0;

    /* Prepare the IP header */
    ip                 = (struct ip *)buf;
    ip->ip_id    = 0;	 /* let kernel fill in */
    ip->ip_off   = 0;
    ip->ip_len         = sizeof(struct ip) + sizeof(pim_header_t) + datalen;
    ip->ip_src.s_addr  = src;
    ip->ip_dst.s_addr  = dst;
    ip->ip_ttl         = MAXTTL;            /* applies to unicast only */
    sendlen            = ip->ip_len;
#if defined(RAW_OUTPUT_IS_RAW) || defined(OpenBSD)
    ip->ip_len         = htons(ip->ip_len);
#endif /* RAW_OUTPUT_IS_RAW || OpenBSD */

    /* Prepare the PIM packet */
    pim                = (pim_header_t *)(buf + sizeof(struct ip));
    pim->pim_type      = type;
    pim->pim_vers      = PIM_PROTOCOL_VERSION;
    pim->pim_reserved  = 0;
    pim->pim_cksum     = 0;
    /* TODO: XXX: if start using this code for PIM_REGISTERS, exclude the
     * encapsulated packet from the checsum.
     */
    pim->pim_cksum     = inet_cksum((u_int16 *)pim,
                                    sizeof(pim_header_t) + datalen);

    if (IN_MULTICAST(ntohl(dst))) {
        k_set_if(pim_socket, src);
        if ((dst == allhosts_group) || (dst == allrouters_group) ||
            (dst == allpimrouters_group)) {
            setloop = 1;
            k_set_loop(pim_socket, TRUE);
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
    while (sendto(pim_socket, buf, sendlen, 0, (struct sockaddr *)&sdst, sizeof(sdst)) < 0) {
	if (errno == EINTR)
	    continue;		/* Received signal, retry syscall. */
        else if (errno == ENETDOWN)
            check_vif_state();
	else if (errno == EPERM)
	    logit(LOG_WARNING, 0, "Not allowed (EPERM) to send PIM message from %s to %s, possibly firewall"
#ifdef __linux__
		  ", or SELinux policy violation,"
#endif
		  " related problem."
		  ,
		  inet_fmt(src, s1, sizeof(s1)), inet_fmt(dst, s2, sizeof(s2)));
        else
            logit(LOG_WARNING, errno, "sendto from %s to %s",
		  inet_fmt(src, s1, sizeof(s1)), inet_fmt(dst, s2, sizeof(s2)));
        if (setloop)
            k_set_loop(pim_socket, FALSE);
        return;
    }

    if (setloop)
        k_set_loop(pim_socket, FALSE);

    IF_DEBUG(DEBUG_PIM_DETAIL) {
        IF_DEBUG(DEBUG_PIM) {
            logit(LOG_DEBUG, 0, "SENT %s from %-15s to %s",
		  packet_kind(IPPROTO_PIM, type, 0),
		  src == INADDR_ANY_N ? "INADDR_ANY" :
		  inet_fmt(src, s1, sizeof(s1)), inet_fmt(dst, s2, sizeof(s2)));
        }
    }
}

u_int pim_send_cnt = 0;
#define SEND_DEBUG_NUMBER 50


/* TODO: This can be merged with the above procedure */
/*
 * Send an unicast PIM packet from src to dst, PIM message type = "type"
 * and data length (after the PIM common header) = "datalen"
 */
void send_pim_unicast(char *buf, u_int32 src, u_int32 dst, int type, int datalen)
{
    static int ip_identification = 0;
    struct sockaddr_in sdst;
    struct ip *ip;
    pim_header_t *pim;
    int sendlen;

    /* Prepare the IP header */
    ip                 = (struct ip *)buf;
    ip->ip_len         = sizeof(struct ip) + sizeof(pim_header_t) + datalen;
    /* We control the IP ID field for unicast msgs due to maybe fragmenting */
    ip->ip_id = htons(++ip_identification);
    ip->ip_src.s_addr  = src;
    ip->ip_dst.s_addr  = dst;
    sendlen            = ip->ip_len;
    /* TODO: XXX: setup the TTL from the inner mcast packet? */
    ip->ip_ttl         = MAXTTL;
#if defined(RAW_OUTPUT_IS_RAW) || defined(OpenBSD)
    ip->ip_len         = htons(ip->ip_len);
#endif /* RAW_OUTPUT_IS_RAW || OpenBSD */

    /* Prepare the PIM packet */
    pim                    = (pim_header_t *)(buf + sizeof(struct ip));
    pim->pim_vers          = PIM_PROTOCOL_VERSION;
    pim->pim_type          = type;
    pim->pim_reserved      = 0;
    pim->pim_cksum         = 0;

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
    pim->pim_cksum	= inet_cksum((u_int16 *)pim, sizeof(pim_header_t)
                                     + datalen);
#else /* !BROKEN_CISCO_CHECKSUM */
    if (PIM_REGISTER == type) {
        pim->pim_cksum	= inet_cksum((u_int16 *)pim, sizeof(pim_header_t)
                                     + sizeof(pim_register_t));
    } else {
        pim->pim_cksum	= inet_cksum((u_int16 *)pim, sizeof(pim_header_t)
                                     + datalen);
    }
#endif /* !BROKEN_CISCO_CHECKSUM */

    memset(&sdst, 0, sizeof(sdst));
    sdst.sin_family = AF_INET;
#ifdef HAVE_SA_LEN
    sdst.sin_len = sizeof(sdst);
#endif
    sdst.sin_addr.s_addr = dst;


    IF_DEBUG(DEBUG_PIM_DETAIL) {
        IF_DEBUG(DEBUG_PIM) {
#if 0 /* TODO: use pim_send_cnt? */
	    if (++pim_send_cnt > SEND_DEBUG_NUMBER) {
		pim_send_cnt = 0;
		logit(LOG_DEBUG, 0, "SENT %s from %-15s to %s",
		      packet_kind(IPPROTO_PIM, type, 0),
		      inet_fmt(src, s1, sizeof(s1)), inet_fmt(dst, s2, sizeof(s2)));
	    }
#endif
            logit(LOG_DEBUG, 0, "SENT %s from %-15s to %s",
		  packet_kind(IPPROTO_PIM, type, 0),
		  inet_fmt(src, s1, sizeof(s1)), inet_fmt(dst, s2, sizeof(s2)));
        }
    }

    send_raw_ip(buf, sendlen, &sdst);
}

/* send a raw IP packet in fragments if necessary */
static int send_raw_ip(char *buf, size_t len, struct sockaddr_in *sdst)
{
    struct ip *ip = (struct ip *)buf;

    IF_DEBUG(DEBUG_PIM_REGISTER) {
	logit(LOG_INFO, 0, "Sending unicast: len = %d to %s",
                  len, inet_fmt(ip->ip_dst.s_addr, s1, sizeof(s1)));
    }

    while (sendto(pim_socket, buf, len, 0, (struct sockaddr *)sdst, 
		  sizeof(*sdst)) < 0) {
	switch (errno) {
	    case EINTR:
		continue;		/* Received signal, retry syscall. */
	    case ENETDOWN:
		check_vif_state();
		return -1;
	    case EMSGSIZE: {
		/* split it in half and recursively send each half */
		struct ip *ip2;
		size_t hdrsize = sizeof(*ip);
		size_t newlen1 = (len-hdrsize)/2 & 0xFFF8; /* 8 byte boundary */
		size_t newlen2 = (len-hdrsize) - newlen1;
		size_t offset = ntohs(ip->ip_off);

		ip->ip_len = htons(newlen1+hdrsize);
		ip->ip_off = htons(offset | IP_MF);
		/* send first half */
		if (send_raw_ip(buf, newlen1+hdrsize, sdst) == 0) {
		    ip2 = (struct ip *)buf + newlen1;
		    memcpy(ip2, ip, hdrsize);
		    ip2->ip_len = htons(newlen2+hdrsize);
		    ip2->ip_off = htons(offset + (newlen1>>3)); /* keep flgs */
		    /* send second half */
		    return send_raw_ip((char *)ip2, newlen2+hdrsize, sdst);
		}
	    }
		return -1;
	    default:
		logit(LOG_WARNING, errno, "sendto from %s to %s",
		      inet_fmt(ip->ip_src.s_addr, s1, sizeof(s1)),
		      inet_fmt(ip->ip_dst.s_addr, s2, sizeof(s2)));
		return -1;
	}
    }
    return 0;
}


/**
 * Local Variables:
 *  version-control: t
 *  indent-tabs-mode: t
 *  c-file-style: "ellemtel"
 *  c-basic-offset: 4
 * End:
 */
