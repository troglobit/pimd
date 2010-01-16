/*
 * Definitions for IP multicast forwarding.
 *
 * Written by David Waitzman, BBN Labs, August 1988.
 * Modified by Steve Deering, Stanford, February 1989.
 * Modified by Ajit Thyagarajan, PARC, August 1993.
 * Modified by Ajit Thyagarajan, PARC, August 1994.
 * Modified by Ahmed Helmy, USC, September 1996.
 *
 * MROUTING $Revision: 3.5 $
 */


/*
 * Multicast Routing set/getsockopt commands.
 */
#define MRT_INIT		100	/* initialize forwarder */
#define MRT_DONE		101	/* shut down forwarder */
#define MRT_ADD_VIF		102	/* create virtual interface */
#define MRT_DEL_VIF		103	/* delete virtual interface */
#define MRT_ADD_MFC		104	/* insert forwarding cache entry */
#define MRT_DEL_MFC		105	/* delete forwarding cache entry */
#define MRT_VERSION		106	/* get kernel version number */
#define MRT_ASSERT		107	/* enable assert (wrong iif) processing */


#if BSD >= 199103 
#define GET_TIME(t)	microtime(&t)
#elif defined(sun)
#define GET_TIME(t)	uniqtime(&t)
#else
#define GET_TIME(t)	((t) = time)
#endif

/*
 * Types and macros for handling bitmaps with one bit per virtual interface.
 */
#define MAXVIFS 32
typedef u_long vifbitmap_t;
typedef u_short vifi_t;		/* type of a vif index */
#define ALL_VIFS (vifi_t)-1

#define	VIFM_SET(n, m)			((m) |=  (1 << (n)))
#define	VIFM_CLR(n, m)			((m) &= ~(1 << (n)))
#define	VIFM_ISSET(n, m)		((m) &   (1 << (n)))
#define VIFM_CLRALL(m)			((m) = 0x00000000)
#define VIFM_COPY(mfrom, mto)		((mto) = (mfrom))
#define VIFM_SAME(m1, m2)		((m1) == (m2))
								  
/*
 * Argument structure for MRT_ADD_VIF.
 * (MRT_DEL_VIF takes a single vifi_t argument.)
 */
struct vifctl {
    vifi_t	    vifc_vifi;	    	/* the index of the vif to be added  */
    u_char	    vifc_flags;     	/* VIFF_ flags defined below         */
    u_char	    vifc_threshold; 	/* min ttl required to forward on vif*/
    u_int	    vifc_rate_limit;    /* max rate           		     */
    struct in_addr  vifc_lcl_addr;  	/* local interface address           */
    struct in_addr  vifc_rmt_addr;  	/* remote address (tunnels only)     */
};

#define VIFF_TUNNEL	  0x1	    	/* vif represents a tunnel end-point */
#define VIFF_SRCRT	  0x2	    	/* tunnel uses IP src routing	     */
#define VIFF_REGISTER	  0x4           /* vif used for register en/decap    */
#ifdef PIM_REG_KERNEL_ENCAP
#define VIFF_REGISTER_KERNEL_ENCAP 0x8	/* vif register with kernel encap    */
#endif



/*
 * Argument structure for MRT_ADD_MFC and MRT_DEL_MFC
 * (mfcc_tos to be added at a future point)
 */
struct mfcctl {
    struct in_addr  mfcc_origin;		/* ip origin of mcasts       */
    struct in_addr  mfcc_mcastgrp; 		/* multicast group associated*/
    vifi_t	    mfcc_parent;   		/* incoming vif              */
    u_char	    mfcc_ttls[MAXVIFS]; 	/* forwarding ttls on vifs   */
#ifdef PIM_REG_KERNEL_ENCAP
    struct in_addr  mfcc_rp_addr;		/* The RP address for encap. */
#endif
};

/*
 * The kernel's multicast routing statistics.
 */
struct mrtstat {
    u_long	mrts_mfc_lookups;	/* # forw. cache hash table hits   */
    u_long	mrts_mfc_misses;	/* # forw. cache hash table misses */
    u_long	mrts_upcalls;		/* # calls to mrouted              */
    u_long	mrts_no_route;		/* no route for packet's origin    */
    u_long	mrts_bad_tunnel;	/* malformed tunnel options        */
    u_long	mrts_cant_tunnel;	/* no room for tunnel options      */
    u_long	mrts_wrong_if;		/* arrived on wrong interface	   */
    u_long	mrts_upq_ovflw;		/* upcall Q overflow		   */
    u_long	mrts_cache_cleanups;	/* # entries with no upcalls 	   */
    u_long  	mrts_drop_sel;     	/* pkts dropped selectively        */
    u_long  	mrts_q_overflow;    	/* pkts dropped - Q overflow       */
    u_long  	mrts_pkt2large;     	/* pkts dropped - size > BKT SIZE  */
    u_long	mrts_upq_sockfull;	/* upcalls dropped - socket full   */
};

/*
 * Argument structure used by mrouted to get src-grp pkt counts
 */
struct sioc_sg_req {
    struct in_addr src;
    struct in_addr grp;
    u_long pktcnt;
    u_long bytecnt;
    u_long wrong_if;
};

/*
 * Argument structure used by mrouted to get vif pkt counts
 */
struct sioc_vif_req {
    vifi_t vifi;		/* vif number				*/
    u_long icount;		/* Input packet count on vif		*/
    u_long ocount;		/* Output packet count on vif		*/
    u_long ibytes;		/* Input byte count on vif		*/
    u_long obytes;		/* Output byte count on vif		*/
};

#ifdef KERNEL
/* 
 * Argument structure used by PIM to get the RPF neighbor and IIF info
 * for a given source, from the unicast routing tables. 
 * Maybe be substituted by routing sockets for the supporting systems
 */
struct rpfctl {
   struct in_addr source; /* the source for which we want iif and rpfnbr */
   struct in_addr rpfneighbor;/* next hop towards the source */
   vifi_t iif;/* the incoming interface to reach the next hop */
};

/*
 * The kernel's virtual-interface structure.
 */
struct vif {
    u_char   		v_flags;     	/* VIFF_ flags defined above         */
    u_char   		v_threshold;	/* min ttl required to forward on vif*/
    u_int      		v_rate_limit; 	/* max rate			     */
    struct tbf 	       *v_tbf;       	/* token bucket structure at intf.   */
    struct in_addr 	v_lcl_addr;   	/* local interface address           */
    struct in_addr 	v_rmt_addr;   	/* remote address (tunnels only)     */
    struct ifnet       *v_ifp;	     	/* pointer to interface              */
    u_long		v_pkt_in;	/* # pkts in on interface            */
    u_long		v_pkt_out;	/* # pkts out on interface           */
    u_long		v_bytes_in;	/* # bytes in on interface	     */
    u_long		v_bytes_out;	/* # bytes out on interface	     */
    struct route	v_route;	/* Cached route if this is a tunnel  */
#ifdef RSVP_ISI
    u_int               v_rsvp_on;      /* # RSVP listening on this vif      */
    struct socket      *v_rsvpd;	/* # RSVPD daemon                    */
#endif /* RSVP_ISI */
};

/*
 * The kernel's multicast forwarding cache entry structure 
 * (A field for the type of service (mfc_tos) is to be added 
 * at a future point)
 */
struct mfc {
    struct in_addr  mfc_origin;	 		/* ip origin of mcasts       */
    struct in_addr  mfc_mcastgrp;  		/* multicast group associated*/
    vifi_t	    mfc_parent; 		/* incoming vif              */
    u_char	    mfc_ttls[MAXVIFS]; 		/* forwarding ttls on vifs   */
    u_long	    mfc_pkt_cnt;		/* pkt count for src-grp     */
    u_long	    mfc_byte_cnt;		/* byte count for src-grp    */
    u_long	    mfc_wrong_if;		/* wrong if for src-grp	     */
    int		    mfc_expire;			/* time to clean entry up    */
    struct timeval  mfc_last_assert;		/* last time I sent an assert*/
#ifdef PIM_REG_KERNEL_ENCAP
    struct in_addr  mfc_rp_addr;		/* The RP address for encap. */
#endif
};
#endif /* KERNEL */
/*
 * Struct used to communicate from kernel to multicast router
 * note the convenient similarity to an IP packet
 */
struct igmpmsg {
    u_long	    unused1;
    u_long	    unused2;
    u_char	    im_msgtype;			/* what type of message	    */
#define IGMPMSG_NOCACHE		1
#define IGMPMSG_WRONGVIF	2
#define IGMPMSG_WHOLEPKT	3               /* used for user level encap */	
    u_char	    im_mbz;			/* must be zero		    */
    u_char	    im_vif;			/* vif rec'd on		    */
    u_char	    unused3;
    struct in_addr  im_src, im_dst;
};

#ifdef KERNEL
/*
 * Argument structure used for pkt info. while upcall is made
 */
struct rtdetq {
    struct mbuf 	*m;		/* A copy of the packet	    	    */
    struct ifnet	*ifp;		/* Interface pkt came in on 	    */
#ifdef UPCALL_TIMING
    struct timeval	t;		/* Timestamp */
#endif /* UPCALL_TIMING */
};

#define MFCTBLSIZ	256
#if (MFCTBLSIZ & (MFCTBLSIZ - 1)) == 0	  /* from sys:route.h */
#define MFCHASHMOD(h)	((h) & (MFCTBLSIZ - 1))
#else
#define MFCHASHMOD(h)	((h) % MFCTBLSIZ)
#endif

#define MAX_UPQ	4		/* max. no of pkts in upcall Q */

/*
 * Token Bucket filter code 
 */
#define MAX_BKT_SIZE    10000             /* 10K bytes size 		*/
#define MAXQSIZE        10                /* max # of pkts in queue 	*/

/*
 * queue structure at each vif
 */
struct pkt_queue 
{
    u_long pkt_len;               /* length of packet in queue 	*/
    struct mbuf *pkt_m;           /* pointer to packet mbuf	*/
    struct ip  *pkt_ip;           /* pointer to ip header	*/
};

/*
 * the token bucket filter at each vif
 */
struct tbf
{
    u_long last_pkt_t;	/* arr. time of last pkt 	*/
    u_long n_tok;      	/* no of tokens in bucket 	*/
    u_long q_len;    	/* length of queue at this vif	*/
};

#endif /* KERNEL */

