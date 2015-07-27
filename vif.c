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

/*
 * Helper macros
 */
#define is_uv_subnet(src, v) \
    (src & v->uv_subnetmask) == v->uv_subnet && ((v->uv_subnetmask == 0xffffffff) || (src != v->uv_subnetbcast))

#define is_pa_subnet(src, v) \
    (src & p->pa_subnetmask) == p->pa_subnet && ((p->pa_subnetmask == 0xffffffff) || (src != p->pa_subnetbcast))

/*
 * Exported variables.
 */
struct uvif	uvifs[MAXVIFS]; /* array of all virtual interfaces          */
vifi_t		numvifs;	/* Number of vifs in use                    */
int             vifs_down;      /* 1=>some interfaces are down              */
int             phys_vif;       /* An enabled vif                           */
vifi_t		reg_vif_num;    /* really virtual interface for registers   */
int		udp_socket;	/* Since the honkin' kernel doesn't support
				 * ioctls on raw IP sockets, we need a UDP
				 * socket as well as our IGMP (raw) socket. */
int             total_interfaces; /* Number of all interfaces: including the
				   * non-configured, but excluding the
				   * loopback interface and the non-multicast
				   * capable interfaces.
				   */

uint32_t	default_route_metric   = UCAST_DEFAULT_ROUTE_METRIC;
uint32_t	default_route_distance = UCAST_DEFAULT_ROUTE_DISTANCE;

/*
 * Forward declarations
 */
static void start_vif      (vifi_t vifi);
static void stop_vif       (vifi_t vifi);
static void start_all_vifs (void);
static int init_reg_vif    (void);
static int update_reg_vif  (vifi_t register_vifi);


void init_vifs(void)
{
    vifi_t vifi;
    struct uvif *v;
    int enabled_vifs;

    numvifs    = 0;
    reg_vif_num = NO_VIF;
    vifs_down = FALSE;

    /* Configure the vifs based on the interface configuration of the the kernel and
     * the contents of the configuration file.  (Open a UDP socket for ioctl use in
     * the config procedures if the kernel can't handle IOCTL's on the IGMP socket.) */
#ifdef IOCTL_OK_ON_RAW_SOCKET
    udp_socket = igmp_socket;
#else
    if ((udp_socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	logit(LOG_ERR, errno, "UDP socket");
#endif

    /* Clean up all vifs */
    for (vifi = 0, v = uvifs; vifi < MAXVIFS; ++vifi, ++v)
	zero_vif(v, FALSE);

    logit(LOG_INFO, 0, "Getting vifs from kernel");
    config_vifs_from_kernel();
    if (disable_all_by_default) {
       logit(LOG_INFO, 0, "Disabling all vifs from kernel");

       for (vifi = 0, v = uvifs; vifi < numvifs; ++vifi, ++v)
          v->uv_flags |= VIFF_DISABLED;
    }

    logit(LOG_INFO, 0, "Getting vifs from %s", config_file);
    config_vifs_from_file();

    /*
     * Quit if there are fewer than two enabled vifs.
     */
    enabled_vifs    = 0;
    phys_vif        = -1;

    for (vifi = 0, v = uvifs; vifi < numvifs; ++vifi, ++v) {
	/* Initialize the outgoing timeout for each vif.  Currently use a fixed time
	 * 'PIM_JOIN_PRUNE_HOLDTIME'.  Later, may add a configurable array to feed
	 * these parameters, or compute them as function of the i/f bandwidth and the
	 * overall connectivity...etc. */
	SET_TIMER(v->uv_jp_timer, PIM_JOIN_PRUNE_HOLDTIME);
	if (v->uv_flags & (VIFF_DISABLED | VIFF_DOWN | VIFF_REGISTER | VIFF_TUNNEL))
	    continue;

	if (phys_vif == -1)
	    phys_vif = vifi;

	enabled_vifs++;
    }

    if (enabled_vifs < 1) /* XXX: TODO: */
	logit(LOG_ERR, 0, "Cannot forward: %s", enabled_vifs == 0 ? "no enabled vifs" : "only one enabled vif");

    k_init_pim(igmp_socket);	/* Call to kernel to initialize structures */

    /* Add a dummy virtual interface to support Registers in the kernel.
     * In order for this to work, the kernel has to have been modified
     * with the PIM patches to ip_mroute.{c,h} and ip.c
     */
    init_reg_vif();

    start_all_vifs();
}

/*
 * Initialize the passed vif with all appropriate default values.
 * "t" is true if a tunnel or register_vif, or false if a phyint.
 */
void zero_vif(struct uvif *v, int t)
{
    v->uv_flags		= 0;	/* Default to IGMPv3 */
    v->uv_metric	= DEFAULT_METRIC;
    v->uv_admetric	= 0;
    v->uv_threshold	= DEFAULT_THRESHOLD;
    v->uv_rate_limit	= t ? DEFAULT_REG_RATE_LIMIT : DEFAULT_PHY_RATE_LIMIT;
    v->uv_lcl_addr	= INADDR_ANY_N;
    v->uv_rmt_addr	= INADDR_ANY_N;
    v->uv_dst_addr	= t ? INADDR_ANY_N : allpimrouters_group;
    v->uv_subnet	= INADDR_ANY_N;
    v->uv_subnetmask	= INADDR_ANY_N;
    v->uv_subnetbcast	= INADDR_ANY_N;
    strlcpy(v->uv_name, "", IFNAMSIZ);
    v->uv_groups	= (struct listaddr *)NULL;
    v->uv_dvmrp_neighbors = (struct listaddr *)NULL;
    NBRM_CLRALL(v->uv_nbrmap);
    v->uv_querier	= (struct listaddr *)NULL;
    v->uv_igmpv1_warn	= 0;
    v->uv_prune_lifetime = 0;
    v->uv_acl		= (struct vif_acl *)NULL;
    RESET_TIMER(v->uv_leaf_timer);
    v->uv_addrs		= (struct phaddr *)NULL;
    v->uv_filter	= (struct vif_filter *)NULL;

    RESET_TIMER(v->uv_hello_timer);
    v->uv_dr_prio       = PIM_HELLO_DR_PRIO_DEFAULT;
    v->uv_genid         = 0;

    RESET_TIMER(v->uv_gq_timer);
    RESET_TIMER(v->uv_jp_timer);
    v->uv_pim_neighbors	= (struct pim_nbr_entry *)NULL;
    v->uv_local_pref	= default_route_distance;
    v->uv_local_metric	= default_route_metric;
#ifdef __linux__
    v->uv_ifindex	= -1;
#endif /* __linux__ */
}


/*
 * Add a (the) register vif to the vif table.
 */
static int init_reg_vif(void)
{
    struct uvif *v;
    vifi_t i;

    v = &uvifs[numvifs];
    v->uv_flags = 0;

    if ((numvifs + 1) == MAXVIFS) {
        /* Exit the program! The PIM router must have a Register vif */
	logit(LOG_ERR, 0, "Cannot install the Register vif: too many interfaces");

	return FALSE;
    }

    /*
     * So far in PIM we need only one register vif and we save its number in
     * the global reg_vif_num.
     */
    reg_vif_num = numvifs;

    /* set the REGISTER flag */
    v->uv_flags = VIFF_REGISTER;
#ifdef PIM_EXPERIMENTAL
    v->uv_flags |= VIFF_REGISTER_KERNEL_ENCAP;
#endif
    strlcpy(v->uv_name, "register_vif0", sizeof(v->uv_name));

    /* Use the address of the first available physical interface to
     * create the register vif.
     */
    for (i = 0; i < numvifs; i++) {
	if (uvifs[i].uv_flags & (VIFF_DOWN | VIFF_DISABLED | VIFF_REGISTER | VIFF_TUNNEL))
	    continue;

	break;
    }

    if (i >= numvifs) {
	logit(LOG_ERR, 0, "No physical interface enabled");
	return -1;
    }

    v->uv_lcl_addr = uvifs[i].uv_lcl_addr;
    v->uv_threshold = MINTTL;

    numvifs++;
    total_interfaces++;

    return 0;
}


static void start_all_vifs(void)
{
    vifi_t vifi;
    struct uvif *v;
    u_int action;

    /* Start first the NON-REGISTER vifs */
    for (action = 0; ; action = VIFF_REGISTER) {
	for (vifi = 0, v = uvifs; vifi < numvifs; ++vifi, ++v) {
	    /* If starting non-registers but the vif is a register or if starting
	     * registers, but the interface is not a register, then just continue. */
	    if ((v->uv_flags & VIFF_REGISTER) ^ action)
		continue;

	    /* Start vif if not DISABLED or DOWN */
	    if (v->uv_flags & (VIFF_DISABLED | VIFF_DOWN)) {
		if (v->uv_flags & VIFF_DISABLED)
		    logit(LOG_INFO, 0, "Interface %s is DISABLED; vif #%u out of service", v->uv_name, vifi);
		else
		    logit(LOG_INFO, 0, "Interface %s is DOWN; vif #%u out of service", v->uv_name, vifi);
	    } else {
		start_vif(vifi);
	    }
	}

	if (action == VIFF_REGISTER)
	    break;   /* We are done */
    }
}


/*
 * stop all vifs
 */
void stop_all_vifs(void)
{
    vifi_t vifi;
    struct uvif *v;

    for (vifi = 0; vifi < numvifs; vifi++) {
	v = &uvifs[vifi];
	if (!(v->uv_flags & VIFF_DOWN))
	    stop_vif(vifi);
    }
}


/*
 * Initialize the vif and add to the kernel. The vif can be either
 * physical, register or tunnel (tunnels will be used in the future
 * when this code becomes PIM multicast boarder router.
 */
static void start_vif(vifi_t vifi)
{
    struct uvif *v;

    v	= &uvifs[vifi];
    /* Initialy no router on any vif */
    if (v->uv_flags & VIFF_REGISTER)
	v->uv_flags = v->uv_flags & ~VIFF_DOWN;
    else {
	v->uv_flags = (v->uv_flags | VIFF_DR | VIFF_NONBRS) & ~VIFF_DOWN;

	/* https://tools.ietf.org/html/draft-ietf-pim-hello-genid-01 */
	v->uv_genid = RANDOM();

	SET_TIMER(v->uv_hello_timer, 1 + RANDOM() % pim_timer_hello_interval);
	SET_TIMER(v->uv_jp_timer, 1 + RANDOM() % PIM_JOIN_PRUNE_PERIOD);
	/* TODO: CHECK THE TIMERS!!!!! Set or reset? */
	RESET_TIMER(v->uv_gq_timer);
	v->uv_pim_neighbors = (pim_nbr_entry_t *)NULL;
    }

    /* Tell kernel to add, i.e. start this vif */
    k_add_vif(igmp_socket, vifi, &uvifs[vifi]);
    logit(LOG_INFO, 0, "Interface %s comes up; vif #%u now in service", v->uv_name, vifi);

    if (!(v->uv_flags & VIFF_REGISTER)) {
	/* Join the PIM multicast group on the interface. */
	k_join(pim_socket, allpimrouters_group, v);

	/* Join the ALL-ROUTERS multicast group on the interface.  This
	 * allows mtrace requests to loop back if they are run on the
	 * multicast router. */
	k_join(igmp_socket, allrouters_group, v);

	/* Join INADDR_ALLRPTS_GROUP to support IGMPv3 membership reports */
	k_join(igmp_socket, allreports_group, v);

	/* Until neighbors are discovered, assume responsibility for sending
	 * periodic group membership queries to the subnet.  Send the first
	 * query. */
	v->uv_flags |= VIFF_QUERIER;
	query_groups(v);

	/* Send a probe via the new vif to look for neighbors. */
	send_pim_hello(v, pim_timer_hello_holdtime);
    }
#ifdef __linux__
    else {
	struct ifreq ifr;

	memset(&ifr, 0, sizeof(struct ifreq));

	if (mrt_table_id != 0) {
	        logit(LOG_INFO, 0, "Initializing pimreg%u", mrt_table_id);
		snprintf(ifr.ifr_name, IFNAMSIZ, "pimreg%u", mrt_table_id);
	} else {
		strlcpy(ifr.ifr_name, "pimreg", IFNAMSIZ);
	}

	if (ioctl(udp_socket, SIOGIFINDEX, (char *) &ifr) < 0) {
	    logit(LOG_ERR, errno, "ioctl SIOGIFINDEX for %s", ifr.ifr_name);
	    /* Not reached */
	    return;
	}

	v->uv_ifindex = ifr.ifr_ifindex;
    }
#endif /* __linux__ */
}


/*
 * Stop a vif (either physical interface, tunnel or
 * register.) If we are running only PIM we don't have tunnels.
 */
static void stop_vif(vifi_t vifi)
{
    struct uvif *v;
    struct listaddr *a, *b;
    pim_nbr_entry_t *n, *next;
    struct vif_acl *acl;

    /*
     * TODO: make sure that the kernel viftable is
     * consistent with the daemon table
     */
    v = &uvifs[vifi];
    if (!(v->uv_flags & VIFF_REGISTER)) {
	k_leave(pim_socket, allpimrouters_group, v);
	k_leave(igmp_socket, allrouters_group, v);
	k_leave(igmp_socket, allreports_group, v);

	/* Discard all group addresses.  (No need to tell kernel;
	 * the k_del_vif() call will clean up kernel state.) */
	while (v->uv_groups) {
	    a = v->uv_groups;
	    v->uv_groups = a->al_next;

	    while (a->al_sources) {
		b = a->al_sources;
		a->al_sources = a->al_next;
		free(b);
	    }

	    free(a);
	}
    }

    /*
     * TODO: inform (eventually) the neighbors I am going down by sending
     * PIM_HELLO with holdtime=0 so someone else should become a DR.
     */
    /* TODO: dummy! Implement it!! Any problems if don't use it? */
    delete_vif_from_mrt(vifi);

    /* Delete the interface from the kernel's vif structure. */
    k_del_vif(igmp_socket, vifi, v);

    v->uv_flags = (v->uv_flags & ~VIFF_DR & ~VIFF_QUERIER & ~VIFF_NONBRS) | VIFF_DOWN;
    if (!(v->uv_flags & VIFF_REGISTER)) {
	RESET_TIMER(v->uv_hello_timer);
	RESET_TIMER(v->uv_jp_timer);
	RESET_TIMER(v->uv_gq_timer);

	for (n = v->uv_pim_neighbors; n; n = next) {
	    next = n->next;	/* Free the space for each neighbour */
	    delete_pim_nbr(n);
	}
	v->uv_pim_neighbors = NULL;
    }

    /* TODO: currently not used */
   /* The Access Control List (list with the scoped addresses) */
    while (v->uv_acl) {
	acl = v->uv_acl;
	v->uv_acl = acl->acl_next;
	free(acl);
    }

    vifs_down = TRUE;
    logit(LOG_INFO, 0, "Interface %s goes down; vif #%u out of service", v->uv_name, vifi);
}


/*
 * Update the register vif in the multicast routing daemon and the
 * kernel because the interface used initially to get its local address
 * is DOWN. register_vifi is the index to the Register vif which needs
 * to be updated. As a result the Register vif has a new uv_lcl_addr and
 * is UP (virtually :))
 */
static int update_reg_vif(vifi_t register_vifi)
{
    struct uvif *v;
    vifi_t vifi;

    /* Find the first useable vif with solid physical background */
    for (vifi = 0, v = uvifs; vifi < numvifs; ++vifi, ++v) {
	if (v->uv_flags & (VIFF_DISABLED | VIFF_DOWN | VIFF_REGISTER | VIFF_TUNNEL))
	    continue;

        /* Found. Stop the bogus Register vif first */
	stop_vif(register_vifi);
	uvifs[register_vifi].uv_lcl_addr = uvifs[vifi].uv_lcl_addr;

	start_vif(register_vifi);
	IF_DEBUG(DEBUG_PIM_REGISTER | DEBUG_IF) {
	    logit(LOG_NOTICE, 0, "Interface %s has come up; vif #%u now in service",
		  uvifs[register_vifi].uv_name, register_vifi);
	}

	return 0;
    }

    vifs_down = TRUE;
    logit(LOG_WARNING, 0, "Cannot start Register vif: %s", uvifs[vifi].uv_name);

    return -1;
}


/*
 * See if any interfaces have changed from up state to down, or vice versa,
 * including any non-multicast-capable interfaces that are in use as local
 * tunnel end-points.  Ignore interfaces that have been administratively
 * disabled.
 */
void check_vif_state(void)
{
    vifi_t vifi;
    struct uvif *v;
    struct ifreq ifr;
    static int checking_vifs = 0;

    /*
     * XXX: TODO: True only for DVMRP?? Check.
     * If we get an error while checking, (e.g. two interfaces go down
     * at once, and we decide to send a prune out one of the failed ones)
     * then don't go into an infinite loop!
     */
    if (checking_vifs)
	return;

    vifs_down = FALSE;
    checking_vifs = 1;

    /* TODO: Check all potential interfaces!!! */
    /* Check the physical and tunnels only */
    for (vifi = 0, v = uvifs; vifi < numvifs; ++vifi, ++v) {
	if (v->uv_flags & (VIFF_DISABLED | VIFF_REGISTER))
	    continue;

	/* get the interface flags */
	strlcpy(ifr.ifr_name, v->uv_name, sizeof(ifr.ifr_name));
	if (ioctl(udp_socket, SIOCGIFFLAGS, (char *)&ifr) < 0) {
           if (errno == ENODEV) {
              logit(LOG_NOTICE, 0, "Interface %s has gone; vif #%u taken out of service", v->uv_name, vifi);
              stop_vif(vifi);
              vifs_down = TRUE;
              continue;
           }

	   logit(LOG_ERR, errno, "check_vif_state: ioctl SIOCGIFFLAGS for %s", ifr.ifr_name);
	}

	if (v->uv_flags & VIFF_DOWN) {
	    if (ifr.ifr_flags & IFF_UP)
		start_vif(vifi);
	    else
		vifs_down = TRUE;
	} else {
	    if (!(ifr.ifr_flags & IFF_UP)) {
		logit(LOG_NOTICE, 0, "Interface %s has gone down; vif #%u taken out of service", v->uv_name, vifi);
		stop_vif(vifi);
		vifs_down = TRUE;
	    }
	}
    }

    /* Check the register(s) vif(s) */
    for (vifi = 0, v = uvifs; vifi < numvifs; ++vifi, ++v) {
	vifi_t vifi2;
	struct uvif *v2;
	int found;

	if (!(v->uv_flags & VIFF_REGISTER))
	    continue;

	found = 0;

	/* Find a physical vif with the same IP address as the
	 * Register vif. */
	for (vifi2 = 0, v2 = uvifs; vifi2 < numvifs; ++vifi2, ++v2) {
	    if (v2->uv_flags & (VIFF_DISABLED | VIFF_DOWN | VIFF_REGISTER | VIFF_TUNNEL))
		continue;

	    if (v->uv_lcl_addr != v2->uv_lcl_addr)
		continue;

	    found = 1;
	    break;
	}

	/* The physical interface with the IP address as the Register
	 * vif is probably DOWN. Get a replacement. */
	if (!found)
	    update_reg_vif(vifi);
    }

    checking_vifs = 0;
}


/*
 * If the source is directly connected to us, find the vif number for
 * the corresponding physical interface (Register and tunnels excluded).
 * Local addresses are excluded.
 * Return the vif number or NO_VIF if not found.
 */
vifi_t find_vif_direct(uint32_t src)
{
    vifi_t vifi;
    struct uvif *v;
    struct phaddr *p;

    for (vifi = 0, v = uvifs; vifi < numvifs; ++vifi, ++v) {
	if (v->uv_flags & (VIFF_DISABLED | VIFF_DOWN | VIFF_REGISTER | VIFF_TUNNEL))
	    continue;

	if (src == v->uv_lcl_addr)
	    return NO_VIF;	/* src is one of our IP addresses */

	if (is_uv_subnet(src, v))
	    return vifi;

	/* Check the extra subnets for this vif */
	/* TODO: don't think currently pimd can handle extra subnets */
	for (p = v->uv_addrs; p; p = p->pa_next) {
	    if (is_pa_subnet(src, v))
		return vifi;
	}

	/* POINTOPOINT but not VIFF_TUNNEL interface (e.g., GRE) */
	if ((v->uv_flags & VIFF_POINT_TO_POINT) && (src == v->uv_rmt_addr))
	    return vifi;
    }

    return NO_VIF;
}


/*
 * Checks if src is local address. If "yes" return the vif index,
 * otherwise return value is NO_VIF.
 */
vifi_t local_address(uint32_t src)
{
    vifi_t vifi;
    struct uvif *v;

    for (vifi = 0, v = uvifs; vifi < numvifs; ++vifi, ++v) {
	/* TODO: XXX: what about VIFF_TUNNEL? */
	if (v->uv_flags & (VIFF_DISABLED | VIFF_DOWN | VIFF_REGISTER))
	    continue;

	if (src == v->uv_lcl_addr)
	    return vifi;
    }

    /* Returning NO_VIF means not a local address */
    return NO_VIF;
}

/*
 * If the source is directly connected, or is local address,
 * find the vif number for the corresponding physical interface
 * (Register and tunnels excluded).
 * Return the vif number or NO_VIF if not found.
 */
vifi_t find_vif_direct_local(uint32_t src)
{
    vifi_t vifi;
    struct uvif *v;
    struct phaddr *p;

    for (vifi = 0, v = uvifs; vifi < numvifs; ++vifi, ++v) {
	/* TODO: XXX: what about VIFF_TUNNEL? */
	if (v->uv_flags & (VIFF_DISABLED | VIFF_DOWN | VIFF_REGISTER | VIFF_TUNNEL))
	    continue;

	if (src == v->uv_lcl_addr)
	    return vifi;	/* src is one of our IP addresses */

	if (is_uv_subnet(src, v))
	    return vifi;

	/* Check the extra subnets for this vif */
	/* TODO: don't think currently pimd can handle extra subnets */
	for (p = v->uv_addrs; p; p = p->pa_next) {
	    if (is_pa_subnet(src, v))
		return vifi;
	}

	/* POINTOPOINT but not VIFF_TUNNEL interface (e.g., GRE) */
	if ((v->uv_flags & VIFF_POINT_TO_POINT) && (src == v->uv_rmt_addr))
	    return vifi;
    }

    return NO_VIF;
}

/*
 * Returns the highest address of local vif that is UP and ENABLED.
 * The VIFF_REGISTER interface(s) is/are excluded.
 */
uint32_t max_local_address(void)
{
    vifi_t vifi;
    struct uvif *v;
    uint32_t max_address = 0;

    for (vifi = 0, v = uvifs; vifi < numvifs; ++vifi, ++v) {
	/* Count vif if not DISABLED or DOWN */
	/* TODO: XXX: What about VIFF_TUNNEL? */
	if (v->uv_flags & (VIFF_DISABLED | VIFF_DOWN | VIFF_REGISTER))
	    continue;

	if (ntohl(v->uv_lcl_addr) > ntohl(max_address))
	    max_address = v->uv_lcl_addr;
    }

    return max_address;
}

/**
 * Local Variables:
 *  version-control: t
 *  indent-tabs-mode: t
 *  c-file-style: "ellemtel"
 *  c-basic-offset: 4
 * End:
 */
