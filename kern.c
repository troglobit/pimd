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

#include "defs.h"

#ifdef RAW_OUTPUT_IS_RAW
int curttl = 0;
#endif

/*
 * XXX: in *BSD there is only MRT_ASSERT, but in Linux there are
 * both MRT_ASSERT and MRT_PIM
 */
#ifndef MRT_PIM
#define MRT_PIM MRT_ASSERT
#endif /* MRT_PIM */

/*
 * Open/init the multicast routing in the kernel and sets the
 * MRT_PIM (aka MRT_ASSERT) flag in the kernel.
 */
void k_init_pim(int socket)
{
    int v = 1;

    if (setsockopt(socket, IPPROTO_IP, MRT_INIT, (char *)&v, sizeof(int)) < 0) {
	if (errno == EADDRINUSE)
	    logit(LOG_ERR, 0, "Another multicast routing application is already running.");
	else
	    logit(LOG_ERR, errno, "Cannot enable multicast routing in kernel");
    }

    if (setsockopt(socket, IPPROTO_IP, MRT_PIM, (char *)&v, sizeof(int)) < 0)
        logit(LOG_ERR, errno, "Cannot set PIM flag in kernel");
}


/*
 * Stops the multicast routing in the kernel and resets the
 * MRT_PIM (aka MRT_ASSERT) flag in the kernel.
 */
void k_stop_pim(int socket)
{
    int v = 0;

    if (setsockopt(socket, IPPROTO_IP, MRT_PIM, (char *)&v, sizeof(int)) < 0)
        logit(LOG_ERR, errno, "Cannot reset PIM flag in kernel");

    if (setsockopt(socket, IPPROTO_IP, MRT_DONE, (char *)NULL, 0) < 0)
        logit(LOG_ERR, errno, "Cannot disable multicast routing in kernel");
}


/*
 * Set the socket sending buffer. `bufsize` is the preferred size,
 * `minsize` is the smallest acceptable size.
 */
void k_set_sndbuf(int socket, int bufsize, int minsize)
{
    int delta = bufsize / 2;
    int iter = 0;

    /*
     * Set the socket buffer.  If we can't set it as large as we
     * want, search around to try to find the highest acceptable
     * value.  The highest acceptable value being smaller than
     * minsize is a fatal error.
     */
    if (setsockopt(socket, SOL_SOCKET, SO_SNDBUF, (char *)&bufsize, sizeof(bufsize)) < 0) {
        bufsize -= delta;
        while (1) {
            iter++;
            if (delta > 1)
                delta /= 2;

            if (setsockopt(socket, SOL_SOCKET, SO_SNDBUF, (char *)&bufsize, sizeof(bufsize)) < 0) {
                bufsize -= delta;
            } else {
                if (delta < 1024)
                    break;
                bufsize += delta;
            }
        }
        if (bufsize < minsize) {
            logit(LOG_ERR, 0, "OS-allowed send buffer size %u < app min %u",
                  bufsize, minsize);
            /*NOTREACHED*/
        }
    }
    IF_DEBUG(DEBUG_KERN) {
        logit(LOG_DEBUG, 0, "Got %d byte send buffer size in %d iterations",
              bufsize, iter);
    }
}


/*
 * Set the socket receiving buffer. `bufsize` is the preferred size,
 * `minsize` is the smallest acceptable size.
 */
void k_set_rcvbuf(int socket, int bufsize, int minsize)
{
    int delta = bufsize / 2;
    int iter = 0;

    /*
     * Set the socket buffer.  If we can't set it as large as we
     * want, search around to try to find the highest acceptable
     * value.  The highest acceptable value being smaller than
     * minsize is a fatal error.
     */
    if (setsockopt(socket, SOL_SOCKET, SO_RCVBUF, (char *)&bufsize, sizeof(bufsize)) < 0) {
        bufsize -= delta;
        while (1) {
            iter++;
            if (delta > 1)
                delta /= 2;

            if (setsockopt(socket, SOL_SOCKET, SO_RCVBUF, (char *)&bufsize, sizeof(bufsize)) < 0) {
                bufsize -= delta;
            } else {
                if (delta < 1024)
                    break;
                bufsize += delta;
            }
        }
        if (bufsize < minsize) {
            logit(LOG_ERR, 0, "OS-allowed recv buffer size %u < app min %u", bufsize, minsize);
            /*NOTREACHED*/
        }
    }
    IF_DEBUG(DEBUG_KERN) {
        logit(LOG_DEBUG, 0, "Got %d byte recv buffer size in %d iterations",
              bufsize, iter);
    }
}


/*
 * Set/reset the IP_HDRINCL option. My guess is we don't need it for raw
 * sockets, but having it here won't hurt. Well, unless you are running
 * an older version of FreeBSD (older than 2.2.2). If the multicast
 * raw packet is bigger than 208 bytes, then IP_HDRINCL triggers a bug
 * in the kernel and "panic". The kernel patch for netinet/ip_raw.c
 * coming with this distribution fixes it.
 */
void k_hdr_include(int socket, int bool)
{
#ifdef IP_HDRINCL
    if (setsockopt(socket, IPPROTO_IP, IP_HDRINCL, (char *)&bool, sizeof(bool)) < 0)
        logit(LOG_ERR, errno, "Failed %s IP_HDRINCL on socket %d",
	      ENABLINGSTR(bool), socket);
#endif
}


/*
 * Set the default TTL for the multicast packets outgoing from this
 * socket.
 * TODO: Does it affect the unicast packets?
 */
void k_set_ttl(int socket __attribute__((unused)), int t)
{
#ifdef RAW_OUTPUT_IS_RAW
    curttl = t;
#else
    u_char ttl;

    ttl = t;
    if (setsockopt(socket, IPPROTO_IP, IP_MULTICAST_TTL, (char *)&ttl, sizeof(ttl)) < 0)
        logit(LOG_ERR, errno, "Failed setting IP_MULTICAST_TTL %u on socket %d", ttl, socket);
#endif
}


/*
 * Set/reset the IP_MULTICAST_LOOP. Set/reset is specified by "flag".
 */
void k_set_loop(int socket, int flag)
{
    u_char loop;

    loop = flag;
    if (setsockopt(socket, IPPROTO_IP, IP_MULTICAST_LOOP, (char *)&loop, sizeof(loop)) < 0)
        logit(LOG_ERR, errno, "Failed %s IP_MULTICAST_LOOP on socket %d",
	      ENABLINGSTR(flag), socket);
}


/*
 * Set the IP_MULTICAST_IF option on local interface ifa.
 */
void k_set_if(int socket, u_int32 ifa)
{
    struct in_addr adr;

    adr.s_addr = ifa;
    if (setsockopt(socket, IPPROTO_IP, IP_MULTICAST_IF, (char *)&adr, sizeof(adr)) < 0) {
        if (errno == EADDRNOTAVAIL || errno == EINVAL)
            return;
        logit(LOG_ERR, errno, "Failed setting IP_MULTICAST_IF option on %s",
              inet_fmt(adr.s_addr, s1, sizeof(s1)));
    }
}


/*
 * Join a multicast group on virtual interface 'v'.
 */
void k_join(int socket, u_int32 grp, struct uvif *v)
{
#ifdef __linux__
    struct ip_mreqn mreq;
#else
    struct ip_mreq mreq;
#endif /* __linux__ */

#ifdef __linux__
    mreq.imr_ifindex = v->uv_ifindex;
    mreq.imr_address.s_addr = v->uv_lcl_addr;
#else
    mreq.imr_interface.s_addr = v->uv_lcl_addr;
#endif /* __linux__ */
    mreq.imr_multiaddr.s_addr = grp;

    if (setsockopt(socket, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                   (char *)&mreq, sizeof(mreq)) < 0) {
#ifdef __linux__
        logit(LOG_WARNING, errno,
              "Cannot join group %s on interface %s (ifindex %d)",
              inet_fmt(grp, s1, sizeof(s1)), inet_fmt(v->uv_lcl_addr, s2, sizeof(s2)), v->uv_ifindex);
#else
        logit(LOG_WARNING, errno,
              "Cannot join group %s on interface %s",
              inet_fmt(grp, s1, sizeof(s1)), inet_fmt(v->uv_lcl_addr, s2, sizeof(s2)));
#endif /* __linux__ */
    }
}


/*
 * Leave a multicast group on virtual interface 'v'.
 */
void k_leave(int socket, u_int32 grp, struct uvif *v)
{
#ifdef __linux__
    struct ip_mreqn mreq;
#else
    struct ip_mreq mreq;
#endif /* __linux__ */

#ifdef __linux__
    mreq.imr_ifindex = v->uv_ifindex;
    mreq.imr_address.s_addr = v->uv_lcl_addr;
#else
    mreq.imr_interface.s_addr = v->uv_lcl_addr;
#endif /* __linux__ */
    mreq.imr_multiaddr.s_addr = grp;

    if (setsockopt(socket, IPPROTO_IP, IP_DROP_MEMBERSHIP, (char *)&mreq, sizeof(mreq)) < 0) {
#ifdef __linux__
        logit(LOG_WARNING, errno,
              "Cannot leave group %s on interface %s (ifindex %d)",
              inet_fmt(grp, s1, sizeof(s1)), inet_fmt(v->uv_lcl_addr, s2, sizeof(s2)), v->uv_ifindex);
#else
        logit(LOG_WARNING, errno,
              "Cannot leave group %s on interface %s",
              inet_fmt(grp, s1, sizeof(s1)), inet_fmt(v->uv_lcl_addr, s2, sizeof(s2)));
#endif /* __linux__ */
    }
}

/*
 * Fill struct vifctl using corresponding fields from struct uvif.
 */
static void uvif_to_vifctl(struct vifctl *vc, struct uvif *v)
{
    /* XXX: we don't support VIFF_TUNNEL; VIFF_SRCRT is obsolete */
    vc->vifc_flags           = 0;
    if (v->uv_flags & VIFF_REGISTER)
        vc->vifc_flags      |= VIFF_REGISTER;
    vc->vifc_threshold       = v->uv_threshold;
    vc->vifc_rate_limit      = v->uv_rate_limit;
    vc->vifc_lcl_addr.s_addr = v->uv_lcl_addr;
    vc->vifc_rmt_addr.s_addr = v->uv_rmt_addr;
}

/*
 * Add a virtual interface in the kernel.
 */
void k_add_vif(int socket, vifi_t vifi, struct uvif *v)
{
    struct vifctl vc;

    vc.vifc_vifi = vifi;
    uvif_to_vifctl(&vc, v);
    if (setsockopt(socket, IPPROTO_IP, MRT_ADD_VIF, (char *)&vc, sizeof(vc)) < 0)
        logit(LOG_ERR, errno, "Failed adding VIF %d (MRT_ADD_VIF)", vifi);
}


/*
 * Delete a virtual interface in the kernel.
 */
void k_del_vif(int socket, vifi_t vifi, struct uvif *v __attribute__((unused)))
{
    /*
     * Unfortunately Linux MRT_DEL_VIF API differs a bit from the *BSD one.  It
     * expects to receive a pointer to struct vifctl that corresponds to the VIF
     * we're going to delete.  *BSD systems on the other hand exepect only the
     * index of that VIF.
     */
#ifdef __linux__
    struct vifctl vc;

    vc.vifc_vifi = vifi;
    uvif_to_vifctl(&vc, v);	       /* 'v' is used only on Linux systems. */

    if (setsockopt(socket, IPPROTO_IP, MRT_DEL_VIF, (char *)&vc, sizeof(vc)) < 0)
#else /* *BSD et al. */
    if (setsockopt(socket, IPPROTO_IP, MRT_DEL_VIF, (char *)&vifi, sizeof(vifi)) < 0)
#endif /* !__linux__ */
    {
        if (errno == EADDRNOTAVAIL || errno == EINVAL)
            return;

        logit(LOG_ERR, errno, "Failed removing VIF %d (MRT_DEL_VIF)", vifi);
    }
}


/*
 * Delete all MFC entries for particular routing entry from the kernel.
 */
int k_del_mfc(int socket, u_int32 source, u_int32 group)
{
    struct mfcctl mc;

    mc.mfcc_origin.s_addr   = source;
    mc.mfcc_mcastgrp.s_addr = group;

    if (setsockopt(socket, IPPROTO_IP, MRT_DEL_MFC, (char *)&mc, sizeof(mc)) < 0) {
        logit(LOG_WARNING, errno, "Failed removing MFC entry src %s, grp %s",
              inet_fmt(mc.mfcc_origin.s_addr, s1, sizeof(s1)),
              inet_fmt(mc.mfcc_mcastgrp.s_addr, s2, sizeof(s2)));

        return FALSE;
    }

    IF_DEBUG(DEBUG_MFC) {
        logit(LOG_DEBUG, 0, "Removed MFC entry src %s, grp %s",
              inet_fmt(mc.mfcc_origin.s_addr, s1, sizeof(s1)),
              inet_fmt(mc.mfcc_mcastgrp.s_addr, s2, sizeof(s2)));
    }

    return TRUE;
}


/*
 * Install/modify a MFC entry in the kernel
 */
int k_chg_mfc(int socket, u_int32 source, u_int32 group, vifi_t iif, vifbitmap_t oifs, u_int32 rp_addr __attribute__((unused)))
{
    struct mfcctl mc;
    vifi_t vifi;
    struct uvif *v;

    mc.mfcc_origin.s_addr = source;
#ifdef OLD_KERNEL
    mc.mfcc_originmas.s_addr = 0xffffffff;    /* Got it from mrouted-3.9 */
#endif /* OLD_KERNEL */
    mc.mfcc_mcastgrp.s_addr = group;
    mc.mfcc_parent = iif;
    /*
     * draft-ietf-pim-sm-v2-new-05.txt section 4.2 mentions iif is removed
     * at the packet forwarding phase
     */
    VIFM_CLR(mc.mfcc_parent, oifs);

    for (vifi = 0, v = uvifs; vifi < numvifs; vifi++, v++) {
        if (VIFM_ISSET(vifi, oifs))
            mc.mfcc_ttls[vifi] = v->uv_threshold;
        else
            mc.mfcc_ttls[vifi] = 0;
    }

#ifdef PIM_REG_KERNEL_ENCAP
    mc.mfcc_rp_addr.s_addr = rp_addr;
#endif
    if (setsockopt(socket, IPPROTO_IP, MRT_ADD_MFC, (char *)&mc, sizeof(mc)) < 0) {
        logit(LOG_WARNING, errno, "Failed adding MFC entry src %s grp %s",
              inet_fmt(mc.mfcc_origin.s_addr, s1, sizeof(s1)),
	      inet_fmt(mc.mfcc_mcastgrp.s_addr, s2, sizeof(s2)));

        return FALSE;
    }

    return TRUE;
}


/*
 * Get packet counters for particular interface
 * XXX: TODO: currently not used, but keep just in case we need it later.
 */
int k_get_vif_count(vifi_t vifi, struct vif_count *retval)
{
    struct sioc_vif_req vreq;

    vreq.vifi = vifi;
    if (ioctl(udp_socket, SIOCGETVIFCNT, (char *)&vreq) < 0) {
        logit(LOG_WARNING, errno, "Failed reading kernel packet count (SIOCGETVIFCNT) on vif %d", vifi);

        retval->icount =
            retval->ocount =
            retval->ibytes =
            retval->obytes = 0xffffffff;

        return 1;
    }

    retval->icount = vreq.icount;
    retval->ocount = vreq.ocount;
    retval->ibytes = vreq.ibytes;
    retval->obytes = vreq.obytes;

    return 0;
}


/*
 * Gets the number of packets, bytes, and number op packets arrived
 * on wrong if in the kernel for particular (S,G) entry.
 */
int k_get_sg_cnt(int socket, u_int32 source, u_int32 group, struct sg_count *retval)
{
    struct sioc_sg_req sgreq;

    sgreq.src.s_addr = source;
    sgreq.grp.s_addr = group;
    if ((ioctl(socket, SIOCGETSGCNT, (char *)&sgreq) < 0) || (sgreq.wrong_if == 0xffffffff)) {
        /* XXX: ipmulti-3.5 has bug in ip_mroute.c, get_sg_cnt():
         * the return code is always 0, so this is why we need to check
         * the wrong_if value.
         */
        logit(LOG_WARNING, errno, "Failed reading kernel count (SIOCGETSGCNT) for (S,G) on (%s, %s)",
              inet_fmt(source, s1, sizeof(s1)), inet_fmt(group, s2, sizeof(s2)));
        retval->pktcnt = retval->bytecnt = retval->wrong_if = ~0;

        return 1;
    }
    retval->pktcnt = sgreq.pktcnt;
    retval->bytecnt = sgreq.bytecnt;
    retval->wrong_if = sgreq.wrong_if;

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
