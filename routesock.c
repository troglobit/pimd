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

#include <sys/param.h>
#include <sys/file.h>
#include "defs.h"
#include <sys/socket.h>
#include <net/route.h>
#ifdef HAVE_ROUTING_SOCKETS
#include <net/if_dl.h>
#endif
#include <arpa/inet.h>
#include <netdb.h>
#include <stdlib.h>

/* All the BSDs have routing sockets (not Netlink), but only Linux seems
 * to have SIOCGETRPF, which is used in the #else below ... the original
 * authors wanted to merge routesock.c and netlink.c, but I don't know
 * anymore. --Joachim */
#ifdef HAVE_ROUTING_SOCKETS
union sockunion {
    struct  sockaddr sa;
    struct  sockaddr_in sin;
    struct  sockaddr_dl sdl;
} so_dst, so_ifp;
typedef union sockunion *sup;
int routing_socket = -1;
int rtm_addrs;
static pid_t pid;
struct rt_metrics rt_metrics;
uint32_t rtm_inits;

struct {
    struct  rt_msghdr m_rtm;
    char    m_space[512];
} m_rtmsg;

/*
 * Local functions definitions.
 */
static int getmsg(struct rt_msghdr *, int, struct rpfctl *rpfinfo);

 /*
 * TODO: check again!
 */
#ifdef IRIX
#define ROUNDUP(a) ((a) > 0 ? (1 + (((a) - 1) | (sizeof(__uint64_t) - 1))) \
		    : sizeof(__uint64_t))
#else
#define ROUNDUP(a) ((a) > 0 ? (1 + (((a) - 1) | (sizeof(long) - 1))) \
		    : sizeof(long))
#endif /* IRIX */

#ifdef HAVE_SA_LEN
#define ADVANCE(x, n) (x += ROUNDUP((n)->sa_len))
#else
#define ADVANCE(x, n) (x += ROUNDUP(sizeof(*(n))))  /* XXX: sizeof(struct sockaddr) */
#endif

/* Open and initialize the routing socket */
int init_routesock(void)
{
#if 0
    int on = 0;
#endif

    routing_socket = socket(PF_ROUTE, SOCK_RAW, 0);
    if (routing_socket < 0) {
	logit(LOG_ERR, errno, "Failed creating routing socket");
	return -1;
    }

    if (fcntl(routing_socket, F_SETFL, O_NONBLOCK) == -1) {
	logit(LOG_ERR, errno, "Failed setting routing socket as non-blocking");
	return -1;
    }

#if 0
    /* XXX: if it is OFF, no queries will succeed (!?) */
    if (setsockopt(routing_socket, SOL_SOCKET, SO_USELOOPBACK, (char *)&on, sizeof(on)) < 0) {
	logit(LOG_ERR, errno , "setsockopt(SO_USELOOPBACK, 0)");
	return -1;
    }
#endif

    return 0;
}

/* get the rpf neighbor info */
int k_req_incoming(uint32_t source, struct rpfctl *rpfp)
{
    int rlen, l, flags = RTF_STATIC, retry_count = 3;
    sup su;
    static int seq;
    char *cp = m_rtmsg.m_space;
    struct rpfctl rpfinfo;

/* TODO: a hack!!!! */
#ifdef HAVE_SA_LEN
#define NEXTADDR(w, u)				\
    if (rtm_addrs & (w)) {			\
	l = ROUNDUP(u.sa.sa_len);		\
	memcpy(cp, &(u), l);			\
	cp += l;				\
    }
#else
#define NEXTADDR(w, u)				\
    if (rtm_addrs & (w)) {			\
	l = ROUNDUP(sizeof(struct sockaddr));	\
	memcpy(cp, &(u), l);			\
	cp += l;				\
    }
#endif /* HAVE_SA_LEN */

    /* initialize */
    rpfp->rpfneighbor.s_addr = INADDR_ANY_N;
    rpfp->source.s_addr = source;

    /* check if local address or directly connected before calling the
     * routing socket
     */
    if ((rpfp->iif = find_vif_direct_local(source)) != NO_VIF) {
	rpfp->rpfneighbor.s_addr = source;
	return TRUE;
    }

    /* prepare the routing socket params */
    rtm_addrs |= RTA_DST;
    rtm_addrs |= RTA_IFP;
    su = &so_dst;
    su->sin.sin_family = AF_INET;
#ifdef HAVE_SA_LEN
    su->sin.sin_len = sizeof(struct sockaddr_in);
#endif
    su->sin.sin_addr.s_addr = source;

    if (inet_lnaof(su->sin.sin_addr) == INADDR_ANY) {
	IF_DEBUG(DEBUG_RPF) {
	    logit(LOG_DEBUG, 0, "k_req_incoming: Invalid source %s",
		  inet_fmt(source, s1, sizeof(s1)));
	}

	return FALSE;
    }

    so_ifp.sa.sa_family = AF_LINK;
#ifdef HAVE_SA_LEN
    so_ifp.sa.sa_len = sizeof(struct sockaddr_dl);
#endif
    flags |= RTF_UP;
    flags |= RTF_HOST;
    flags |= RTF_GATEWAY;
    errno = 0;
    memset (&m_rtmsg, 0, sizeof(m_rtmsg));

#define rtm m_rtmsg.m_rtm
    rtm.rtm_type        = RTM_GET;
    rtm.rtm_flags       = flags;
    rtm.rtm_version     = RTM_VERSION;
    rtm.rtm_seq         = ++seq;
    rtm.rtm_addrs       = rtm_addrs;
    rtm.rtm_rmx         = rt_metrics;
    rtm.rtm_inits       = rtm_inits;

    NEXTADDR(RTA_DST, so_dst);
    NEXTADDR(RTA_IFP, so_ifp);
    rtm.rtm_msglen = l = cp - (char *)&m_rtmsg;

    rlen = write(routing_socket, &m_rtmsg, l);
    if (rlen <= 0) {
	IF_DEBUG(DEBUG_RPF | DEBUG_KERN) {
	    if (errno == ESRCH)
		logit(LOG_DEBUG, 0, "Writing to routing socket: no such route");
	    else
		logit(LOG_DEBUG, 0, "Error writing to routing socket");
	}

	return FALSE;
    }

    pid = getpid();

    while (1) {
	rlen = read(routing_socket, &m_rtmsg, sizeof(m_rtmsg));
	if (rlen < 0) {
	    if (errno == EINTR)
		continue;	/* Signalled, retry syscall. */
	    if (errno == EAGAIN && retry_count--) {
		logit(LOG_DEBUG, 0, "Kernel busy, retrying (%d/3) routing socket read in one sec ...", 3 - retry_count);
		sleep(1);
		continue;
	    }

	    IF_DEBUG(DEBUG_RPF | DEBUG_KERN)
		logit(LOG_DEBUG, errno, "Read from routing socket failed");

	    return FALSE;
	}

	if (rlen > 0 && (rtm.rtm_seq != seq || rtm.rtm_pid != pid))
	    continue;

	break;
    }

    memset(&rpfinfo, 0, sizeof(rpfinfo));
    if (getmsg(&rtm, l, &rpfinfo)) {
	rpfp->rpfneighbor.s_addr = rpfinfo.rpfneighbor.s_addr;
	rpfp->iif = rpfinfo.iif;
    }
#undef rtm

    return TRUE;
}

static void find_sockaddrs(struct rt_msghdr *rtm, struct sockaddr **dst, struct sockaddr **gate,
			   struct sockaddr **mask, struct sockaddr_dl **ifp)
{
    int i;
    char *cp = (char *)(rtm + 1);
    struct sockaddr *sa;

    if (rtm->rtm_addrs) {
	for (i = 1; i; i <<= 1) {
	    if (i & rtm->rtm_addrs) {
		sa = (struct sockaddr *)cp;

		switch (i) {
		    case RTA_DST:
			*dst = sa;
			break;

		    case RTA_GATEWAY:
			*gate = sa;
			break;

		    case RTA_NETMASK:
			*mask = sa;
			break;

		    case RTA_IFP:
			if (sa->sa_family == AF_LINK  && ((struct sockaddr_dl *)sa)->sdl_nlen)
			    *ifp = (struct sockaddr_dl *)sa;
			break;
		}
		ADVANCE(cp, sa);
	    }
	}
    }
}

/*
 * Returns TRUE on success, FALSE otherwise. rpfinfo contains the result.
 */
static int getmsg(struct rt_msghdr *rtm, int msglen __attribute__((unused)), struct rpfctl *rpf)
{
    struct sockaddr *dst = NULL, *gate = NULL, *mask = NULL;
    struct sockaddr_dl *ifp = NULL;
    struct in_addr in;
    vifi_t vifi;
    struct uvif *v;

    if (!rpf) {
	logit(LOG_WARNING, 0, "Missing rpf pointer to routesock.c:getmsg()!");
	return FALSE;
    }

    rpf->iif = NO_VIF;
    rpf->rpfneighbor.s_addr = INADDR_ANY;

    in = ((struct sockaddr_in *)&so_dst)->sin_addr;
    IF_DEBUG(DEBUG_RPF)
	logit(LOG_DEBUG, 0, "route to: %s", inet_fmt(in.s_addr, s1, sizeof(s1)));

    find_sockaddrs(rtm, &dst, &gate, &mask, &ifp);

    if (!ifp) {			/* No incoming interface */
	IF_DEBUG(DEBUG_RPF)
	    logit(LOG_DEBUG, 0, "No incoming interface for destination %s", inet_fmt(in.s_addr, s1, sizeof(s1)));

	return FALSE;
    }

    if (dst && mask)
	mask->sa_family = dst->sa_family;

    if (dst) {
	in = ((struct sockaddr_in *)dst)->sin_addr;
	IF_DEBUG(DEBUG_RPF)
	    logit(LOG_DEBUG, 0, " destination is: %s", inet_fmt(in.s_addr, s1, sizeof(s1)));
    }

    if (gate && (rtm->rtm_flags & RTF_GATEWAY)) {
	in = ((struct sockaddr_in *)gate)->sin_addr;
	IF_DEBUG(DEBUG_RPF)
	    logit(LOG_DEBUG, 0, " gateway is: %s", inet_fmt(in.s_addr, s1, sizeof(s1)));

	rpf->rpfneighbor = in;
    }

    for (vifi = 0, v = uvifs; vifi < numvifs; ++vifi, ++v) {
	/* get the number of the interface by matching the name */
	if ((strlen(v->uv_name) == ifp->sdl_nlen)
	    && !(strncmp(v->uv_name, ifp->sdl_data, ifp->sdl_nlen)))
	    break;
    }

    /* Found inbound interface in vifi */
    rpf->iif = vifi;

    IF_DEBUG(DEBUG_RPF)
	logit(LOG_DEBUG, 0, " iif is %d", vifi);

    if (vifi >= numvifs) {
	IF_DEBUG(DEBUG_RPF)
	    logit(LOG_DEBUG, 0, "Invalid incoming interface for destination %s, because of invalid virtual interface",
		  inet_fmt(in.s_addr, s1, sizeof(s1)));

	return FALSE;		/* invalid iif */
    }

    return TRUE;
}


#else /* !HAVE_ROUTING_SOCKETS -- if we want to run on Linux without Netlink */

/* API compat dummy. */
int init_routesock(void)
{
    return dup(udp_socket);
}

/*
 * Return in rpfcinfo the incoming interface and the next hop router
 * toward source.
 */
/* TODO: check whether next hop router address is in network or host order */
int k_req_incoming(uint32_t source, struct rpfctl *rpfcinfo)
{
    rpfcinfo->source.s_addr      = source;
    rpfcinfo->iif                = NO_VIF;     /* Initialize, will be changed in kernel */
    rpfcinfo->rpfneighbor.s_addr = INADDR_ANY; /* Initialize */

    if (ioctl(udp_socket, SIOCGETRPF, (char *) rpfcinfo) < 0) {
	logit(LOG_WARNING, errno, "Failed ioctl SIOCGETRPF in k_req_incoming()");
	return FALSE;
    }

    return TRUE;
}
#endif /* HAVE_ROUTING_SOCKETS */

/**
 * Local Variables:
 *  version-control: t
 *  indent-tabs-mode: t
 *  c-file-style: "ellemtel"
 *  c-basic-offset: 4
 * End:
 */
