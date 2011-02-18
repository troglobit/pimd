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


#define LINE_BUFSIZ 1024	/* Max. line length of the config file */

/*
 * Forward declarations.
 */
static char	*next_word	(char **);
static int	parse_phyint	(char *s);
static u_int32	ifname2addr	(char *s);

extern struct rp_hold *g_rp_hold;

/*
 * Query the kernel to find network interfaces that are multicast-capable
 * and install them in the uvifs array.
 */
void config_vifs_from_kernel(void)
{
    struct ifreq *ifrp, *ifend;
    struct uvif *v;
    vifi_t vifi;
    u_int32 n;
    u_int32 addr, mask, subnet;
    short flags;
    int num_ifreq = 64;
    struct ifconf ifc;
    char *newbuf;

    total_interfaces = 0; /* The total number of physical interfaces */

    ifc.ifc_len = num_ifreq * sizeof(struct ifreq);
    ifc.ifc_buf = calloc(ifc.ifc_len, sizeof(char));
    while (ifc.ifc_buf) {
        if (ioctl(udp_socket, SIOCGIFCONF, (char *)&ifc) < 0)
            logit(LOG_ERR, errno, "ioctl SIOCGIFCONF");

        /*
         * If the buffer was large enough to hold all the addresses
         * then break out, otherwise increase the buffer size and
         * try again.
         *
         * The only way to know that we definitely had enough space
         * is to know that there was enough space for at least one
         * more struct ifreq. ???
         */
        if ((num_ifreq * sizeof(struct ifreq)) >= ifc.ifc_len + sizeof(struct ifreq))
            break;

        num_ifreq *= 2;
        ifc.ifc_len = num_ifreq * sizeof(struct ifreq);
        newbuf = realloc(ifc.ifc_buf, ifc.ifc_len);
        if (newbuf == NULL)
            free(ifc.ifc_buf);
        ifc.ifc_buf = newbuf;
    }
    if (ifc.ifc_buf == NULL)
        logit(LOG_ERR, 0, "config_vifs_from_kernel: ran out of memory");

    ifrp = (struct ifreq *)ifc.ifc_buf;
    ifend = (struct ifreq *)(ifc.ifc_buf + ifc.ifc_len);
    /*
     * Loop through all of the interfaces.
     */
    for (; ifrp < ifend; ifrp = (struct ifreq *)((char *)ifrp + n)) {
        struct ifreq ifr;
#ifdef HAVE_SA_LEN
        n = ifrp->ifr_addr.sa_len + sizeof(ifrp->ifr_name);
        if (n < sizeof(*ifrp))
            n = sizeof(*ifrp);
#else
        n = sizeof(*ifrp);
#endif /* HAVE_SA_LEN */

        /*
         * Ignore any interface for an address family other than IP.
         */
        if (ifrp->ifr_addr.sa_family != AF_INET) {
            total_interfaces++;  /* Eventually may have IP address later */
            continue;
        }

        addr = ((struct sockaddr_in *)&ifrp->ifr_addr)->sin_addr.s_addr;

        /*
         * Need a template to preserve address info that is
         * used below to locate the next entry.  (Otherwise,
         * SIOCGIFFLAGS stomps over it because the requests
         * are returned in a union.)
         */
        memcpy(ifr.ifr_name, ifrp->ifr_name, sizeof(ifr.ifr_name));

        /*
         * Ignore loopback interfaces and interfaces that do not
         * support multicast.
         */
        if (ioctl(udp_socket, SIOCGIFFLAGS, (char *)&ifr) < 0)
            logit(LOG_ERR, errno, "ioctl SIOCGIFFLAGS for %s", ifr.ifr_name);
        flags = ifr.ifr_flags;
        if ((flags & (IFF_LOOPBACK | IFF_MULTICAST)) != IFF_MULTICAST)
            continue;

        /*
         * Everyone below is a potential vif interface.
         * We don't care if it has wrong configuration or not configured
         * at all.
         */
        total_interfaces++;

        /*
         * Ignore any interface whose address and mask do not define a
         * valid subnet number, or whose address is of the form
         * {subnet,0} or {subnet,-1}.
         */
        if (ioctl(udp_socket, SIOCGIFNETMASK, (char *)&ifr) < 0) {
            if (!(flags & IFF_POINTOPOINT)) {
                logit(LOG_ERR, errno, "ioctl SIOCGIFNETMASK for %s", ifr.ifr_name);
            }
	    mask = 0xffffffff;
        } else {
            mask = ((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr.s_addr;
        }

        subnet = addr & mask;
        if ((!inet_valid_subnet(subnet, mask))
            || (addr == subnet) || addr == (subnet | ~mask)) {
            if (!(inet_valid_host(addr) && (flags & IFF_POINTOPOINT))) {
                logit(LOG_WARNING, 0, "ignoring %s, has invalid address (%s) and/or mask (%s)",
                      ifr.ifr_name, inet_fmt(addr, s1, sizeof(s1)), inet_fmt(mask, s2, sizeof(s2)));
                continue;
            }
        }

        /*
         * Ignore any interface that is connected to the same subnet as
         * one already installed in the uvifs array.
         */
        /*
         * TODO: XXX: bug or "feature" is to allow only one interface per
         * subnet?
         */
        for (vifi = 0, v = uvifs; vifi < numvifs; ++vifi, ++v) {
            if (strcmp(v->uv_name, ifr.ifr_name) == 0) {
                logit(LOG_DEBUG, 0, "Skipping %s (%s on subnet %s) (alias for vif#%u?)",
                      v->uv_name, inet_fmt(addr, s1, sizeof(s1)),
                      netname(subnet, mask), vifi);
                break;
            }
            /* we don't care about point-to-point links in same subnet */
            if (flags & IFF_POINTOPOINT)
                continue;
            if (v->uv_flags & VIFF_POINT_TO_POINT)
                continue;
#if 0
            /*
             * TODO: to allow different interfaces belong to
             * overlapping subnet addresses, use this version instead
             */
            if (((addr & mask ) == v->uv_subnet) &&
                (v->uv_subnetmask == mask)) {
                logit(LOG_WARNING, 0, "Ignoring %s, same subnet as %s",
                      ifr.ifr_name, v->uv_name);
                break;
            }
#else
            if ((addr & v->uv_subnetmask) == v->uv_subnet ||
                (v->uv_subnet & mask) == subnet) {
                logit(LOG_WARNING, 0, "Ignoring %s, same subnet as %s",
                      ifr.ifr_name, v->uv_name);
                break;
            }
#endif /* 0 */
        }
        if (vifi != numvifs)
            continue;

        /*
         * If there is room in the uvifs array, install this interface.
         */
        if (numvifs == MAXVIFS) {
            logit(LOG_WARNING, 0, "Too many vifs, ignoring %s", ifr.ifr_name);
            continue;
        }
        v = &uvifs[numvifs];
        zero_vif(v, FALSE);
        v->uv_lcl_addr		= addr;
        v->uv_subnet		= subnet;
        v->uv_subnetmask	= mask;
        v->uv_subnetbcast	= subnet | ~mask;
        strlcpy(v->uv_name, ifr.ifr_name, IFNAMSIZ);

        if (flags & IFF_POINTOPOINT) {
            v->uv_flags |= (VIFF_REXMIT_PRUNES | VIFF_POINT_TO_POINT);
            if (ioctl(udp_socket, SIOCGIFDSTADDR, (char *)&ifr) < 0) {
                logit(LOG_ERR, errno, "ioctl SIOCGIFDSTADDR for %s", v->uv_name);
            } else {
                v->uv_rmt_addr
                    = ((struct sockaddr_in *)(&ifr.ifr_dstaddr))->sin_addr.s_addr;

            }
        }
#ifdef __linux__
        {
            struct ifreq ifridx;

            memset(&ifridx, 0, sizeof(ifridx));
            strlcpy(ifridx.ifr_name,v->uv_name, IFNAMSIZ);
            if (ioctl(udp_socket, SIOGIFINDEX, (char *) &ifridx) < 0) {
                logit(LOG_ERR, errno, "ioctl SIOGIFINDEX for %s",
                      ifridx.ifr_name);
                /* Not reached */
                return;
            }
            v->uv_ifindex = ifridx.ifr_ifindex;
        }
        if (flags & IFF_POINTOPOINT) {
            logit(LOG_INFO, 0, "Installing %s (%s -> %s) as vif #%u-%d - rate=%d",
                  v->uv_name, inet_fmt(addr, s1, sizeof(s1)), inet_fmt(v->uv_rmt_addr, s2, sizeof(s2)),
                  numvifs, v->uv_ifindex, v->uv_rate_limit);
        } else {
            logit(LOG_INFO, 0, "Installing %s (%s on subnet %s) as vif #%u-%d - rate=%d",
                  v->uv_name, inet_fmt(addr, s1, sizeof(s1)), netname(subnet, mask),
                  numvifs, v->uv_ifindex, v->uv_rate_limit);
        }
#else /* !__linux__ */
        if (flags & IFF_POINTOPOINT) {
            logit(LOG_INFO, 0, "Installing %s (%s -> %s) as vif #%u - rate=%d",
                  v->uv_name, inet_fmt(addr, s1, sizeof(s1)), inet_fmt(v->uv_rmt_addr, s2, sizeof(s2)),
                  numvifs, v->uv_rate_limit);
        } else {
            logit(LOG_INFO, 0, "Installing %s (%s on subnet %s) as vif #%u - rate=%d",
                  v->uv_name, inet_fmt(addr, s1, sizeof(s1)), netname(subnet, mask),
                  numvifs, v->uv_rate_limit);
        }
#endif /* __linux__ */

        ++numvifs;

        /*
         * If the interface is not yet up, set the vifs_down flag to
         * remind us to check again later.
         */
        if (!(flags & IFF_UP)) {
            v->uv_flags |= VIFF_DOWN;
            vifs_down = TRUE;
        }
    }
}


#define CONF_UNKNOWN				-1
#define CONF_EMPTY				1
#define CONF_PHYINT				2
#define CONF_CANDIDATE_RP			3
#define CONF_RP_ADDRESS				64
#define CONF_GROUP_PREFIX			4
#define CONF_BOOTSTRAP_RP			5
#define CONF_REG_THRESHOLD			6
#define CONF_DATA_THRESHOLD			7
#define CONF_DEFAULT_SOURCE_METRIC		8
#define CONF_DEFAULT_SOURCE_PREFERENCE		9
#define CONF_ALTNET				10
#define CONF_MASKLEN				11
#define CONF_SCOPED				12


/*
 * function name: wordToOption
 * input: char *word, a pointer to the word
 * output: int; a number corresponding to the code of the word
 * operation: converts the result of the string comparisons into numerics.
 * comments: called by config_vifs_from_file()
 */
static int wordToOption(char *word)
{
    if (EQUAL(word, ""))
        return CONF_EMPTY;
    if (EQUAL(word, "phyint"))
        return CONF_PHYINT;
    if (EQUAL(word, "cand_rp"))
        return CONF_CANDIDATE_RP;
    if (EQUAL(word, "rp_address"))
        return CONF_RP_ADDRESS;
    if (EQUAL(word, "group_prefix"))
        return CONF_GROUP_PREFIX;
    if (EQUAL(word, "cand_bootstrap_router"))
        return CONF_BOOTSTRAP_RP;
    if (EQUAL(word, "switch_register_threshold"))
        return CONF_REG_THRESHOLD;
    if (EQUAL(word, "switch_data_threshold"))
        return CONF_DATA_THRESHOLD;
    if (EQUAL(word, "default_source_metric"))
        return CONF_DEFAULT_SOURCE_METRIC;
    if (EQUAL(word, "default_source_preference"))
        return CONF_DEFAULT_SOURCE_PREFERENCE;
    if (EQUAL(word, "altnet"))
        return CONF_ALTNET;
    if  (EQUAL(word, "masklen"))
        return CONF_MASKLEN;
    if  (EQUAL(word, "scoped"))
        return CONF_SCOPED;

    return CONF_UNKNOWN;
}

/**
 * parse_phyint - parses the physical interface file configurations, if any.
 * @s: pointing to the parsing point of the file
 *
 * Syntax:
 * phyint <local-addr | ifname> [disable|enable]
 *                              [threshold <t>] [preference <p>] [metric <m>]
 *                              [altnet <net-addr> masklen <masklen>]
 *                              [scoped <net-addr> masklen <masklen>]
 *
 * Returns:
 * %TRUE if the parsing was successful, o.w. %FALSE
 */
static int parse_phyint(char *s)
{
    char *w, c;
    u_int32 local, altnet_addr, scoped_addr;
    vifi_t vifi;
    struct uvif *v;
    u_int n, altnet_masklen, scoped_masklen;
    struct phaddr *ph;
    struct vif_acl *v_acl;

    if (EQUAL((w = next_word(&s)), "")) {
        logit(LOG_WARNING, 0, "Missing phyint address in %s", configfilename);
        return(FALSE);
    }		/* if empty */
    local = ifname2addr(w);
    if (!local) {
        local = inet_parse(w, 4);
        if (!inet_valid_host(local)) {
            logit(LOG_WARNING, 0, "Invalid phyint address '%s' in %s", w,
                  configfilename);
            return(FALSE);
        }		/* invalid address */
    }
    for (vifi = 0, v = uvifs; vifi < numvifs; ++vifi, ++v) {
        if (vifi == numvifs) {
            logit(LOG_WARNING, 0, "phyint %s in %s is not a configured interface",
                  inet_fmt(local, s1, sizeof(s1)), configfilename);
            return(FALSE);
        }	/* if vifi == numvifs */

        if (local != v->uv_lcl_addr)
            continue;

        while (!EQUAL((w = next_word(&s)), "")) {
            if (EQUAL(w, "disable")) {
                v->uv_flags |= VIFF_DISABLED;
                continue;
            }

            if (EQUAL(w, "enable")) {
                v->uv_flags &= ~VIFF_DISABLED;
                continue;
            }

            if (EQUAL(w, "altnet")) {
                if (EQUAL((w = next_word(&s)), "")) {
                    logit(LOG_WARNING, 0, "Missing ALTNET for phyint %s in %s",
                          inet_fmt(local, s1, sizeof(s1)), configfilename);
                    continue;
                }
                altnet_addr = ifname2addr(w);
                if (!altnet_addr) {
                    altnet_addr = inet_parse(w, 4);
                    if (!inet_valid_host(altnet_addr)) {
                        logit(LOG_WARNING, 0, "Invalid altnet address '%s' in %s", w, configfilename);
                        return(FALSE);
                    } /* invalid address */
                }
                if (EQUAL((w = next_word(&s)), "masklen")) {
                    if (EQUAL((w = next_word(&s)), "")) {
                        logit(LOG_WARNING, 0, "Missing ALTNET masklen for phyint %s in %s",
                              inet_fmt(local, s1, sizeof (s1)), configfilename);
                        continue;
                    }
                    if (sscanf(w, "%u", &altnet_masklen) != 1) {
                        logit(LOG_WARNING, 0, "Invalid altnet masklen '%s' for phyint %s in %s",
                              w, inet_fmt(local, s1, sizeof(s1)), configfilename);
                        continue;
                    }
                }
                ph = (struct phaddr *)calloc(1, sizeof(struct phaddr));
                if (ph == NULL)
                    return(FALSE);
                if (altnet_masklen) {
                    VAL_TO_MASK(ph->pa_subnetmask, altnet_masklen);
                } else {
                    ph->pa_subnetmask = v->uv_subnetmask;
                }
                ph->pa_subnet = altnet_addr & ph->pa_subnetmask;
                ph->pa_subnetbcast = ph->pa_subnet | ~ph->pa_subnetmask;
                if (altnet_addr & ~ph->pa_subnetmask)
                    logit(LOG_WARNING,0, "Extra subnet %s/%d has host bits set",
                          inet_fmt(altnet_addr, s1, sizeof(s1)), altnet_masklen);
                ph->pa_next = v->uv_addrs;
                v->uv_addrs = ph;
                logit(LOG_DEBUG, 0, "ALTNET: %s/%d", inet_fmt(altnet_addr, s1, sizeof(s1)),
                      altnet_masklen);
            } /* altnet */

            /* scoped mcast groups/masklen */
            if (EQUAL(w, "scoped")) {
                if (EQUAL((w = next_word(&s)), "")) {
                    logit(LOG_WARNING, 0, "Missing SCOPED for phyint %s in %s",
                          inet_fmt(local, s1, sizeof(s1)), configfilename);
                    continue;
                }
                scoped_addr = ifname2addr(w);
                if (!scoped_addr) {
                    scoped_addr = inet_parse(w, 4);
                    if (!IN_MULTICAST(ntohl(scoped_addr))) {
                        logit(LOG_WARNING, 0, "Invalid scoped address '%s' in %s", w, configfilename);
                        return(FALSE);
                    } /* invalid address */
                }
                if (EQUAL((w = next_word(&s)), "masklen")) {
                    if (EQUAL((w = next_word(&s)), "")) {
                        logit(LOG_WARNING, 0, "Missing SCOPED masklen for phyint %s in %s",
                              inet_fmt(local, s1, sizeof(s1)), configfilename);
                        continue;
                    }
                    if (sscanf(w, "%u", &scoped_masklen) != 1) {
                        logit(LOG_WARNING, 0, "Invalid scoped masklen '%s' for phyint %s in %s",
                              w, inet_fmt(local, s1, sizeof(s1)), configfilename);
                        continue;
                    }
                }

                v_acl = (struct vif_acl *)calloc(1, sizeof(struct vif_acl));
                if (v_acl == NULL)
                    return(FALSE);
                VAL_TO_MASK(v_acl->acl_mask, scoped_masklen);
                v_acl->acl_addr = scoped_addr & v_acl->acl_mask;
                if (scoped_addr & ~v_acl->acl_mask)
                    logit(LOG_WARNING, 0, "Boundary spec %s/%d has host bits set",
                          inet_fmt(scoped_addr, s1, sizeof(s1)),scoped_masklen);
                v_acl->acl_next = v->uv_acl;
                v->uv_acl = v_acl;
                logit(LOG_DEBUG, 0, "SCOPED %s/%x",
		      inet_fmt(v_acl->acl_addr, s1, sizeof(s1)), v_acl->acl_mask);
            } /* scoped */

            if (EQUAL(w, "threshold")) {
                if (EQUAL((w = next_word(&s)), "")) {
                    logit(LOG_WARNING, 0, "Missing threshold for phyint %s in %s",
                          inet_fmt(local, s1, sizeof(s1)), configfilename);
                    continue;
                }
                if (sscanf(w, "%u%c", &n, &c) != 1 || n < 1 || n > 255 ) {
                    logit(LOG_WARNING, 0, "Invalid threshold '%s' for phyint %s in %s",
                          w, inet_fmt(local, s1, sizeof(s1)), configfilename);
                    continue;
                }
                v->uv_threshold = n;
                continue;
            }		/* threshold	*/
            if (EQUAL(w, "preference")) {
                if (EQUAL((w = next_word(&s)), "")) {
                    logit(LOG_WARNING, 0, "Missing preference for phyint %s in %s",
                          inet_fmt(local, s1, sizeof(s1)), configfilename);
                    continue;
                }
                if (sscanf(w, "%u%c", &n, &c) != 1 || n < 1 || n > 255 ) {
                    logit(LOG_WARNING, 0, "Invalid preference '%s' for phyint %s in %s",
                          w, inet_fmt(local, s1, sizeof(s1)),
                          configfilename);
                    continue;
                }
                IF_DEBUG(DEBUG_ASSERT)
                    logit(LOG_DEBUG, 0, "Config setting default local preference on %s to %d.",
                          inet_fmt(local, s1, sizeof(s1)), n);
                v->uv_local_pref = n;
                continue;
            }
            if (EQUAL(w, "metric")) {
                if (EQUAL((w = next_word(&s)), "")) {
                    logit(LOG_WARNING, 0, "Missing metric for phyint %s in %s",
                          inet_fmt(local, s1, sizeof(s1)), configfilename);
                    continue;
                }
                if (sscanf(w, "%u%c", &n, &c) != 1 || n < 1 || n > 1024 ) {
                    logit(LOG_WARNING, 0, "Invalid metric '%s' for phyint %s in %s",
                          w, inet_fmt(local, s1, sizeof(s1)),
                          configfilename);
                    continue;
                }
                IF_DEBUG(DEBUG_ASSERT) {
                    logit(LOG_DEBUG, 0, "Config setting default local metric on %s to %d.",
                          inet_fmt(local, s1, sizeof(s1)), n);
		}
                v->uv_local_metric = n;
                continue;
            }
        }		/* if not empty */
        break;
    }
    return TRUE;
}


/*
 * function name: parse_candidateRP
 * input: char *s
 * output: int (TRUE if the parsing was successful, o.w. FALSE)
 * operation: parses the candidate RP information.
 *	The general form is:
 *      'cand_rp <local-addr> [priority <number>] [time <number>]'.
 */
int parse_candidateRP(char *s)
{
    u_int time = PIM_DEFAULT_CAND_RP_ADV_PERIOD;
    u_int priority = PIM_DEFAULT_CAND_RP_PRIORITY;
    char *w;
    u_int32 local = INADDR_ANY_N;

    cand_rp_flag = FALSE;
    my_cand_rp_adv_period = PIM_DEFAULT_CAND_RP_ADV_PERIOD;
    while (!EQUAL((w = next_word(&s)), "")) {
        if (EQUAL(w, "priority")) {
            if (EQUAL((w = next_word(&s)), "")) {
                logit(LOG_WARNING, 0, "Missing priority; set to default %u (0 is highest)",
                      PIM_DEFAULT_CAND_RP_PRIORITY);
                priority = PIM_DEFAULT_CAND_RP_PRIORITY;
                continue;
            }
            if (sscanf(w, "%u", &priority) != 1) {
                logit(LOG_WARNING, 0, "Invalid priority %s; set to default %u (0 is highest)",
                      PIM_DEFAULT_CAND_RP_PRIORITY);
                priority = PIM_DEFAULT_CAND_RP_PRIORITY;
            }
            continue;
        }
        if (EQUAL(w, "time")) {
            if (EQUAL((w = next_word(&s)), "")) {
                logit(LOG_WARNING, 0, "Missing cand_rp_adv_period value; set to default %u",
                      PIM_DEFAULT_CAND_RP_ADV_PERIOD);
                time = PIM_DEFAULT_CAND_RP_ADV_PERIOD;
                continue;
            }
            if (sscanf(w, "%u", &time) != 1) {
                logit(LOG_WARNING, 0, "Invalid cand_rp_adv_period value; set to default %u",
                      PIM_DEFAULT_CAND_RP_ADV_PERIOD);
                time = PIM_DEFAULT_CAND_RP_ADV_PERIOD;
                continue;
            }
            if (time > (my_cand_rp_adv_period = ~0))
                time = my_cand_rp_adv_period;
            /* TODO: XXX: cannot be shorter than 10 seconds (not in the spec)*/
            if (time < 10)
                time = 10;
#if 0
            if (time > PIM_DEFAULT_CAND_RP_ADV_PERIOD)
                time = PIM_DEFAULT_CAND_RP_ADV_PERIOD;
#endif /* 0 */
            my_cand_rp_adv_period = time;
            continue;
        }
        /* Cand-RP address */
        local = inet_parse(w, 4);
        if (!inet_valid_host(local)) {
            local = max_local_address();
            logit(LOG_WARNING, 0, "Invalid Cand-RP address provided '%s' in %s. Will use the largest enabled local address.",
                  w, configfilename);
        } else if (local_address(local) == NO_VIF) {
            local = max_local_address();
            logit(LOG_WARNING, 0, "Cand-RP address is not local '%s' in %s. Will use the largest enabled local address.",
                  w, configfilename);
        }
    }           /* while not empty */

    if (local == INADDR_ANY_N) {
        /* If address not provided, use the max. local */
        local = max_local_address();
    }

    my_cand_rp_address = local;
    my_cand_rp_priority = priority;
    my_cand_rp_adv_period = time;
    cand_rp_flag = TRUE;

    logit(LOG_INFO, 0, "Local Cand-RP address is %s", inet_fmt(local, s1, sizeof(s1)));
    logit(LOG_INFO, 0, "Local Cand-RP priority is %u", priority);
    logit(LOG_INFO, 0, "Local Cand-RP advertisement period is %u sec.", time);

    return TRUE;
}


/*
 * function name: parse_group_prefix
 * input: char *s
 * output: int
 * operation: parse group_prefix configured information.
 *	General form: 'group_prefix <group-addr> [masklen <masklen>]'.
 */
int parse_group_prefix(char *s)
{
    char *w;
    u_int32 group_addr;
    u_int32  masklen;

    w = next_word(&s);
    if (EQUAL(w, "")) {
        logit(LOG_WARNING, 0, "Configuration error for 'group_prefix' in %s: no group_addr. Ignoring...", configfilename);
        return FALSE;
    }
    group_addr = inet_parse(w, 4);
    if (!IN_MULTICAST(ntohl(group_addr))) {
        logit(LOG_WARNING, 0, "Config error for 'group_prefix' in %s: %s is not a mcast addr. Ignoring...",
	      configfilename, inet_fmt(group_addr, s1, sizeof(s1)));
        return FALSE;
    }

    /* Was if (!(~(*cand_rp_adv_message.prefix_cnt_ptr))) which Arm GCC 4.4.2 dislikes:
     *  --> "config.c:693: warning: promoted ~unsigned is always non-zero"
     * The prefix_cnt_ptr is a u_int8 so it seems this check was to prevent overruns.
     * I've changed the check to see if we've already read 255 entries, if so the cnt
     * is maximized and we need to tell the user. --Joachim Nilsson 2010-01-16 */
    if (*cand_rp_adv_message.prefix_cnt_ptr == 255) {
        logit(LOG_WARNING, 0, "Too many group_prefix configured. Truncating...");
        return FALSE;
    }

    if (EQUAL((w = next_word(&s)), "masklen")) {
        w = next_word(&s);
        if (sscanf(w, "%u", &masklen) == 1) {
            if (masklen > (sizeof(group_addr)*8))
                masklen = (sizeof(group_addr)*8);
            else
                if (masklen < 4)
                    masklen = 4;
        }
        else
            masklen = PIM_GROUP_PREFIX_DEFAULT_MASKLEN;
    }
    else
        masklen = PIM_GROUP_PREFIX_DEFAULT_MASKLEN;

    PUT_EGADDR(group_addr, (u_int8)masklen, 0,
               cand_rp_adv_message.insert_data_ptr);
    (*cand_rp_adv_message.prefix_cnt_ptr)++;

    logit(LOG_INFO, 0, "Adding prefix %s/%d", inet_fmt(group_addr, s1, sizeof(s1)), masklen);

    return TRUE;
}


/*
 * function name: parseBSR
 * input: char *s
 * output: int
 * operation: parse the candidate BSR configured information.
 *	General form:
 *	'cand_bootstrap_router <local-addr> [priority <number>]'.
 */
int parseBSR(char *s)
{
    char *w;
    u_int32 local    = INADDR_ANY_N;
    u_int32 priority = PIM_DEFAULT_BSR_PRIORITY;

    cand_bsr_flag = FALSE;
    while (!EQUAL((w = next_word(&s)), "")) {
        if (EQUAL(w, "priority")) {
            if (EQUAL((w = next_word(&s)), "")) {
                logit(LOG_WARNING, 0, "Missing priority; set to default %u (0 is lowest)\n",
                      PIM_DEFAULT_BSR_PRIORITY);
                priority = PIM_DEFAULT_BSR_PRIORITY;
                continue;
            }
            if (sscanf(w, "%u", &priority) != 1) {
                logit(LOG_WARNING, 0, "Invalid priority %s; set to default %u (0 is lowest)",
                      PIM_DEFAULT_BSR_PRIORITY);
                priority = PIM_DEFAULT_BSR_PRIORITY;
                continue;
            }
            if (priority > (my_bsr_priority = ~0))
                priority = my_bsr_priority;
            my_bsr_priority = (u_int8)priority;
            continue;
        }

        /* BSR address */
        local = inet_parse(w, 4);
        if (!inet_valid_host(local)) {
            local = max_local_address();
            logit(LOG_WARNING, 0, "Invalid BSR address provided '%s' in %s. Will use the largest enabled local address.",
                  w, configfilename);
            continue;
        }
        if (local_address(local) == NO_VIF) {
            local = max_local_address();
            logit(LOG_WARNING, 0, "Cand-BSR address is not local '%s' in %s. Will use the largest enabled local address.",
                  w, configfilename);
        }
    }		/* while not empty */

    if (local == INADDR_ANY_N)
        /* If address not provided, use the max. local */
        local = max_local_address();
    my_bsr_address  = local;
    my_bsr_priority = priority;
    MASKLEN_TO_MASK(RP_DEFAULT_IPV4_HASHMASKLEN, my_bsr_hash_mask);
    cand_bsr_flag   = TRUE;
    logit(LOG_INFO, 0, "Local Cand-BSR address is %s", inet_fmt(local, s1, sizeof(s1)));
    logit(LOG_INFO, 0, "Local Cand-BSR priority is %u", priority);

    return TRUE;
}

/**
 * parse_rp_address - Parse rp_address config option.
 * @s: String token.
 *
 * This is an extension to the original pimd to add pimd.conf support for static
 * Rendez-Vous Point addresses.
 *
 * The function has been extended by pjf@asn.pl, of Lintrack, to allow specifying
 * multicast group addresses as well.
 *
 * Format:
 * rp_address <rp-address> [<group-addr> [masklen <masklen>] [priority <number>]]
 *
 * Returns:
 * When parsing @s is successful this function returns %TRUE, otherwise %FALSE.
 */
int parse_rp_address(char *s)
{
    char *w;
    u_int32 local = 0xffffff;
    u_int32 group_addr = htonl(INADDR_UNSPEC_GROUP);
    u_int32 masklen = PIM_GROUP_PREFIX_DEFAULT_MASKLEN;
    u_int priority = PIM_DEFAULT_CAND_RP_PRIORITY;
    struct rp_hold * rph;

    /* next is RP addr */
    w = next_word(&s);
    if (EQUAL(w, "")) {
        logit(LOG_WARNING, 0, "'rp_address' in %s: no <rp-addr> - ignoring", configfilename);
        return FALSE;
    }

    local = inet_parse(w, 4);
    if (local == 0xffffff) {
        logit(LOG_WARNING, 0, "'rp_address' in %s: invalid <rp-addr> provided: '%s'", configfilename, w);
        return FALSE;
    }

    /* next is group addr if exist */
    w = next_word(&s);
    if (!EQUAL(w, "")) {
        group_addr = inet_parse(w, 4);
        if (!IN_MULTICAST(ntohl(group_addr))) {
            logit(LOG_WARNING, 0, "'rp_address' in %s: %s is not a multicast addr", configfilename, inet_fmt(group_addr, s1, sizeof(s1)));
            return FALSE;
        }

	masklen = PIM_GROUP_PREFIX_DEFAULT_MASKLEN;
	priority = PIM_DEFAULT_CAND_RP_PRIORITY;

	/* next is prefix or priority if exist */
	while (!EQUAL((w = next_word(&s)), "")) {
	    if (EQUAL(w, "masklen")) {
		w = next_word(&s);
		if (sscanf(w, "%u", &masklen) == 1) {
		    if (masklen > (sizeof(group_addr) * 8)) {
			masklen = (sizeof(group_addr) * 8);
		    }
		    else if (masklen < PIM_GROUP_PREFIX_MIN_MASKLEN) {
			logit(LOG_WARNING, 0, "'rp_address' in %s: %s is too small. set to %d.", configfilename, w, PIM_GROUP_PREFIX_MIN_MASKLEN);
			masklen = PIM_GROUP_PREFIX_MIN_MASKLEN;
		    }
		}
		else {
		    logit(LOG_WARNING, 0, "'rp_address' in %s: %s is invalid masklen. set to default(%d)", configfilename, w, PIM_GROUP_PREFIX_DEFAULT_MASKLEN);
		    masklen = PIM_GROUP_PREFIX_DEFAULT_MASKLEN;
		}
	    }
	    else if (EQUAL(w, "priority")) {
		w = next_word(&s);
		if (sscanf(w, "%u", &priority) == 1) {
		    if (priority > PIM_MAX_CAND_RP_PRIORITY) {
			logit(LOG_WARNING, 0, "'rp_address' in %s: %s is too big. set to %d.", configfilename, w, PIM_MAX_CAND_RP_PRIORITY);
			priority = PIM_MAX_CAND_RP_PRIORITY;
		    }
		}
		else {
		    logit(LOG_WARNING, 0, "'rp_address' in %s: %s is invalid priority. set to default(%d)", configfilename, w, PIM_DEFAULT_CAND_RP_PRIORITY);
		    priority = PIM_DEFAULT_CAND_RP_PRIORITY;
		}
	    }
	}
    }
    else {
        group_addr = htonl(INADDR_UNSPEC_GROUP);
        masklen = PIM_GROUP_PREFIX_MIN_MASKLEN;
	priority = 1;
    }

    /* save */
    rph = calloc(1, sizeof(*rph));
    if (!rph) {
	logit(LOG_WARNING, 0, "Ran out of memory in parse_rp_address()");
	return FALSE;
    }
    rph->address = local;
    rph->group = group_addr;
    VAL_TO_MASK(rph->mask, masklen);
    rph->priority = priority;

    /* attach at the beginning */
    rph->next = g_rp_hold;
    g_rp_hold = rph;

    logit(LOG_INFO, 0, "Added static RP: %s, group %s/%d, prioriy %d", inet_fmt(local, s1, sizeof(s1)), inet_fmt(group_addr, s2, sizeof(s2)), masklen, priority);

    return TRUE;
}


/*
 * function name: parse_reg_threshold
 * input: char *s
 * output: int (TRUE if successful, FALSE o.w.)
 * operation: reads and assigns the switch to the spt threshold
 * due to registers for the router, if used as RP.
 * Maybe extended to support different thresholds
 *            for different groups(prefixes).
 *            General form:
 *		'switch_register_threshold [rate <number> interval <number>]'.
 * comments: called by config_vifs_from_file()
 */
int parse_reg_threshold(char *s)
{
    char *w;
    u_int rate;
    u_int interval;

    rate                        = PIM_DEFAULT_REG_RATE;
    interval                    = PIM_DEFAULT_REG_RATE_INTERVAL;
    pim_reg_rate_bytes          = (rate * interval) / 10;
    pim_reg_rate_check_interval = interval;

    while (!EQUAL((w = next_word(&s)), "")) {
        if (EQUAL(w, "rate")) {
            /* rate */
            if (EQUAL((w = next_word(&s)), "")) {
                logit(LOG_WARNING, 0, "Missing reg_rate value; set to default %u (bits/s)\n",
                      PIM_DEFAULT_REG_RATE);
                rate = PIM_DEFAULT_REG_RATE;
                continue;
            }
            if (sscanf(w, "%u", &rate) != 1) {
                logit(LOG_WARNING, 0, "Invalid reg_rate value %s; set to default %u (bits/s)",
                      w, PIM_DEFAULT_REG_RATE);
                rate = PIM_DEFAULT_REG_RATE;
            }
            continue;
        }	/* if rate */
        if (EQUAL(w, "interval")) {
            if (EQUAL((w = next_word(&s)), "")) {
                logit(LOG_WARNING, 0, "Missing reg_rate interval; set to default %u seconds",
                      PIM_DEFAULT_REG_RATE_INTERVAL);
                interval = PIM_DEFAULT_REG_RATE_INTERVAL;
                continue;
            }
            if (sscanf(w, "%u", &interval) != 1) {
                logit(LOG_WARNING, 0, "Invalid reg_rate interval %s; set to default %u seconds",
                      w, PIM_DEFAULT_REG_RATE_INTERVAL);
                interval = PIM_DEFAULT_REG_RATE_INTERVAL;
            }
            continue;
        }	/* if interval */
        logit(LOG_WARNING, 0, "Invalid parameter %s; setting rate and interval to default", w);
        rate     = PIM_DEFAULT_REG_RATE;
        interval = PIM_DEFAULT_REG_RATE_INTERVAL;
        break;
    }	/* while not empty */

    if (interval < TIMER_INTERVAL) {
        logit(LOG_WARNING, 0, "reg_rate interval too short; set to default %u seconds",
              PIM_DEFAULT_REG_RATE_INTERVAL);
        interval = PIM_DEFAULT_REG_RATE_INTERVAL;
    }

    logit(LOG_INFO, 0, "reg_rate_limit is %u (bits/s)", rate);
    logit(LOG_INFO, 0, "reg_rate_interval is %u (seconds)", interval);
    pim_reg_rate_bytes = (rate * interval) / 10;
    pim_reg_rate_check_interval = interval;

    return TRUE;
}


/*
 * function name: parse_data_threshold
 * input: char *s
 * output: int
 * operation: reads and assigns the switch to the spt threshold
 *            due to data packets, if used as DR.
 *            General form:
 *		'switch_data_threshold [rate <number> interval <number>]'.
 */
int parse_data_threshold(char *s)
{
    char *w;
    u_int rate;
    u_int interval;

    rate                         = PIM_DEFAULT_DATA_RATE;
    interval                     = PIM_DEFAULT_DATA_RATE_INTERVAL;
    pim_data_rate_bytes          = (rate * interval) / 10;
    pim_data_rate_check_interval = interval;

    while (!EQUAL((w = next_word(&s)), "")) {
        if (EQUAL(w, "rate")) {
            if (EQUAL((w = next_word(&s)), "")) {
                logit(LOG_WARNING, 0, "Missing data_rate value; set to default %u (bits/s)\n",
                      PIM_DEFAULT_DATA_RATE);
                rate = PIM_DEFAULT_DATA_RATE;
                continue;
            }
            if (sscanf(w, "%u", &rate) != 1) {
                logit(LOG_WARNING, 0, "Invalid data_rate value %s; set to default %u (bits/s)",
                      w, PIM_DEFAULT_DATA_RATE);
                rate = PIM_DEFAULT_DATA_RATE;
            }
            continue;
        }	/* if rate */
        if (EQUAL(w, "interval")) {
            if (EQUAL((w = next_word(&s)), "")) {
                logit(LOG_WARNING, 0, "Missing data_rate interval; set to default %u seconds",
                      PIM_DEFAULT_DATA_RATE_INTERVAL);
                interval = PIM_DEFAULT_DATA_RATE_INTERVAL;
                continue;
            }
            if (sscanf(w, "%u", &interval) != 1) {
                logit(LOG_WARNING, 0, "Invalid data_rate interval %s; set to default %u seconds",
		      w, PIM_DEFAULT_DATA_RATE_INTERVAL);
                interval = PIM_DEFAULT_DATA_RATE_INTERVAL;
            }
            continue;
        }	/* if interval */
        logit(LOG_WARNING, 0, "Invalid parameter %s; setting rate and interval to default", w);
        rate     = PIM_DEFAULT_DATA_RATE;
        interval = PIM_DEFAULT_DATA_RATE_INTERVAL;
        break;
    }	/* while not empty */

    if (interval < TIMER_INTERVAL) {
        logit(LOG_WARNING, 0, "data_rate interval too short; set to default %u seconds",
              PIM_DEFAULT_DATA_RATE_INTERVAL);
        interval = PIM_DEFAULT_DATA_RATE_INTERVAL;
    }

    logit(LOG_INFO, 0, "data_rate_limit is %u (bits/s)", rate);
    logit(LOG_INFO, 0, "data_rate_interval is %u (seconds)", interval);
    pim_data_rate_bytes = (rate * interval) / 10;
    pim_data_rate_check_interval = interval;

    return TRUE;
}


/*
 * function name: parse_default_source_metric
 * input: char *s
 * output: int
 * operation: reads and assigns the default source metric, if no reliable
 *            unicast routing information available.
 *            General form:
 *		'default_source_metric <number>'.
 *            default pref and metric statements should precede all phyint
 *            statements in the config file.
 */
int parse_default_source_metric(char *s)
{
    char *w;
    u_int value;
    vifi_t vifi;
    struct uvif *v;

    value = UCAST_DEFAULT_SOURCE_METRIC;
    if (EQUAL((w = next_word(&s)), "")) {
        logit(LOG_WARNING, 0, "Missing default source metric; set to default %u",
              UCAST_DEFAULT_SOURCE_METRIC);
    } else if (sscanf(w, "%u", &value) != 1) {
        logit(LOG_WARNING, 0, "Invalid default source metric; set to default %u",
              UCAST_DEFAULT_SOURCE_METRIC);
        value = UCAST_DEFAULT_SOURCE_METRIC;
    }
    default_source_metric = value;
    logit(LOG_INFO, 0, "default_source_metric is %u", value);

    for (vifi = 0, v = uvifs; vifi < MAXVIFS; ++vifi, ++v)
        v->uv_local_metric = default_source_metric;

    return TRUE;
}


/*
 * function name: parse_default_source_preference
 * input: char *s
 * output: int
 * operation: reads and assigns the default source preference, if no reliable
 *            unicast routing information available.
 *            General form:
 *		'default_source_preference <number>'.
 *            default pref and metric statements should precede all phyint
 *            statements in the config file.
 */
int parse_default_source_preference(char *s)
{
    char *w;
    u_int value;
    vifi_t vifi;
    struct uvif *v;

    value = UCAST_DEFAULT_SOURCE_PREFERENCE;
    if (EQUAL((w = next_word(&s)), "")) {
        logit(LOG_WARNING, 0, "Missing default source preference; set to default %u",
              UCAST_DEFAULT_SOURCE_PREFERENCE);
    } else if (sscanf(w, "%u", &value) != 1) {
        logit(LOG_WARNING, 0, "Invalid default source preference; set to default %u",
              UCAST_DEFAULT_SOURCE_PREFERENCE);
        value = UCAST_DEFAULT_SOURCE_PREFERENCE;
    }
    default_source_preference = value;
    logit(LOG_INFO, 0, "default_source_preference is %u", value);
    for (vifi = 0, v = uvifs; vifi < MAXVIFS; ++vifi, ++v)
        v->uv_local_pref = default_source_preference;

    return TRUE;
}


void config_vifs_from_file(void)
{
    FILE *f;
    char linebuf[LINE_BUFSIZ];
    char *w, *s;
    struct ifconf ifc;
    int option;
    char ifbuf[BUFSIZ];
    u_int8 *data_ptr;
    int error_flag;
    int line_num;

    error_flag = FALSE;
    line_num = 0;

    if ((f = fopen(configfilename, "r")) == NULL) {
        if (errno != ENOENT)
	    logit(LOG_WARNING, errno, "Cannot open %s", configfilename);
        return;
    }

    /* TODO: HARDCODING!!! */
    cand_rp_adv_message.buffer =
        (u_int8 *)calloc(1, 4 + sizeof(pim_encod_uni_addr_t)
                           + 255 * sizeof(pim_encod_grp_addr_t));
    if (!cand_rp_adv_message.buffer) {
	logit(LOG_ERR, errno, "Ran out of memory in config_vifs_from_file()");
    }
    cand_rp_adv_message.prefix_cnt_ptr  = cand_rp_adv_message.buffer;
    /* By default, if no group_prefix configured, then prefix_cnt == 0
     * implies group_prefix = 224.0.0.0 and masklen = 4.
     */
    *cand_rp_adv_message.prefix_cnt_ptr = 0;
    cand_rp_adv_message.insert_data_ptr = cand_rp_adv_message.buffer;
    /* TODO: XXX: HARDCODING!!! */
    cand_rp_adv_message.insert_data_ptr += (4 + 6);

    ifc.ifc_buf = ifbuf;
    ifc.ifc_len = sizeof(ifbuf);
    if (ioctl(udp_socket, SIOCGIFCONF, (char *)&ifc) < 0)
        logit(LOG_ERR, errno, "ioctl SIOCGIFCONF");

    while (fgets(linebuf, sizeof(linebuf), f) != NULL) {
        if (strlen(linebuf) >= (LINE_BUFSIZ - 1)) {
            logit(LOG_WARNING, 0, "line length must be shorter than %d in %s:%d",
                  LINE_BUFSIZ, configfilename, line_num);
            error_flag = TRUE;
        } else {
            line_num++;
        }

        s = linebuf;
        w = next_word(&s);
        option = wordToOption(w);

        switch(option) {
            case CONF_EMPTY:
                continue;
                break;
            case CONF_PHYINT:
                parse_phyint(s);
                break;
            case CONF_CANDIDATE_RP:
                parse_candidateRP(s);
                break;
            case CONF_RP_ADDRESS:
                parse_rp_address(s);
                break;
            case CONF_GROUP_PREFIX:
                parse_group_prefix(s);
                break;
            case CONF_BOOTSTRAP_RP:
                parseBSR(s);
                break;
            case CONF_REG_THRESHOLD:
                parse_reg_threshold(s);
                break;
            case CONF_DATA_THRESHOLD:
                parse_data_threshold(s);
                break;
            case CONF_DEFAULT_SOURCE_METRIC:
                parse_default_source_metric(s);
                break;
            case CONF_DEFAULT_SOURCE_PREFERENCE:
                parse_default_source_preference(s);
                break;
            default:
                logit(LOG_WARNING, 0, "unknown command '%s' in %s:%d",
                      w, configfilename, line_num);
                error_flag = TRUE;
        }
    }
    if (error_flag) {
        /*
         * XXX: let's be pedantic even about warnings. If this is a problem,
         * comment out this logit(LOG_ERR).
         */
        logit(LOG_ERR, 0, "Syntax Error in %s", configfilename);
    }

    cand_rp_adv_message.message_size = cand_rp_adv_message.insert_data_ptr - cand_rp_adv_message.buffer;
    if (cand_rp_flag != FALSE) {
        /* Prepare the RP info */
        my_cand_rp_holdtime = 2.5 * my_cand_rp_adv_period;
        /* TODO: HARDCODING! */
        data_ptr = cand_rp_adv_message.buffer + 1;
        PUT_BYTE(my_cand_rp_priority, data_ptr);
        PUT_HOSTSHORT(my_cand_rp_holdtime, data_ptr);
        PUT_EUADDR(my_cand_rp_address, data_ptr);
    }

    fclose(f);
}


static u_int32 ifname2addr(char *s)
{
    vifi_t vifi;
    struct uvif *v;

    for (vifi = 0, v = uvifs; vifi < numvifs; vifi++, v++) {
        if (!strcmp(v->uv_name, s))
            return v->uv_lcl_addr;
    }

    return 0;
}

static char *next_word(char **s)
{
    char *w;

    w = *s;
    while (*w == ' ' || *w == '\t')
        w++;

    *s = w;
    for(;;) {
        switch (**s) {
            case ' '  :
            case '\t' :
                **s = '\0';
            (*s)++;
            return(w);
            case '\n' :
            case '#'  :
                **s = '\0';
            return(w);
            case '\0' :
                return(w);
            default   :
                if (isascii((int)**s) && isupper((int)**s))
                    **s = tolower((int)**s);
                (*s)++;
        }
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
