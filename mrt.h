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
 *  $Id: mrt.h,v 1.16 2001/09/10 20:31:36 pavlin Exp $
 */

#define MRTF_SPT                0x0001	/* iif toward source                */
#define MRTF_WC                 0x0002	/* (*,G) entry                      */
#define MRTF_RP                 0x0004	/* iif toward RP                    */
#define MRTF_NEW                0x0008	/* new created routing entry        */
#define MRTF_IIF_REGISTER	0x0020  /* ???                              */
#define MRTF_REGISTER		0x0080  /* ???                              */
#define MRTF_KERNEL_CACHE 	0x0200	/* a mirror for the kernel cache    */
#define MRTF_NULL_OIF 		0x0400	/* null oif cache..     ???         */
#define MRTF_REG_SUPP 		0x0800	/* register suppress    ???         */
#define MRTF_ASSERTED		0x1000	/* upstream is not that of src ???  */
#define MRTF_SG			0x2000	/* (S,G) pure, not hanging off of (*,G)*/
#define MRTF_PMBR		0x4000  /* (*,*,RP) entry (for interop)     */
#define MRTF_MFC_CLONE_SG	0x8000  /* clone (S,G) MFC from (*,G) or (*,*,RP) */

/* Macro to duplicate oif info (oif bits, timers) */
#define VOIF_COPY(from, to)                                             \
    do {								\
	VIFM_COPY((from)->joined_oifs, (to)->joined_oifs);		\
	VIFM_COPY((from)->oifs, (to)->oifs);				\
	VIFM_COPY((from)->leaves, (to)->leaves);			\
	VIFM_COPY((from)->pruned_oifs, (to)->pruned_oifs);		\
	VIFM_COPY((from)->asserted_oifs, (to)->asserted_oifs);		\
	memcpy((to)->vif_timers, (from)->vif_timers,			\
	       numvifs * sizeof((from)->vif_timers[0]));		\
	memcpy((to)->vif_deletion_delay, (from)->vif_deletion_delay,	\
	       numvifs * sizeof((from)->vif_deletion_delay[0]));	\
    } while (0)

#define FREE_MRTENTRY(mrtentry_ptr)				\
    do {							\
	kernel_cache_t *prev;					\
	kernel_cache_t *next;					\
								\
	free((char *)((mrtentry_ptr)->vif_timers));		\
	free((char *)((mrtentry_ptr)->vif_deletion_delay));	\
	for (next = (mrtentry_ptr)->kernel_cache;		\
	     next != (kernel_cache_t *)NULL; ) {		\
	    prev = next; next = next->next;			\
	    free(prev);						\
	}							\
	free((char *)((mrtentry_ptr)->kernel_cache));		\
	free((char *)(mrtentry_ptr));				\
    } while (0)


/*
 * The complicated structure used by the more complicated Join/Prune
 * message building
 */
typedef struct build_jp_message_ {
    struct build_jp_message_ *next; /* Used to chain the free entries       */
    u_int8 *jp_message;       /* The Join/Prune message                     */
    u_int32 jp_message_size;  /* Size of the Join/Prune message (in bytes)  */
    u_int16 holdtime;         /* Join/Prune message holdtime field          */
    u_int32 curr_group;       /* Current group address                      */
    u_int8  curr_group_msklen;/* Current group masklen                      */
    u_int8 *join_list;        /* The working area for the join addresses    */
    u_int32 join_list_size;   /* The size of the join_list (in bytes)       */
    u_int16 join_addr_number; /* Number of the join addresses in join_list  */
    u_int8 *prune_list;       /* The working area for the prune addresses   */
    u_int32 prune_list_size;  /* The size of the prune_list (in bytes)      */
    u_int16 prune_addr_number;/* Number of the prune addresses in prune_list*/
    u_int8 *rp_list_join;     /* The working area for RP join addresses     */
    u_int32 rp_list_join_size;/* The size of the rp_list_join (in bytes)    */
    u_int16 rp_list_join_number;/* Number of RP addresses in rp_list_join   */
    u_int8 *rp_list_prune;     /* The working area for RP prune addresses   */
    u_int32 rp_list_prune_size;/* The size of the rp_list_prune (in bytes)  */
    u_int16 rp_list_prune_number;/* Number of RP addresses in rp_list_prune */
    u_int8 *num_groups_ptr;   /* Pointer to number_of_groups in jp_message  */
} build_jp_message_t;


typedef struct pim_nbr_entry {
    struct	pim_nbr_entry *next;	  /* link to next neighbor	    */
    struct	pim_nbr_entry *prev;	  /* link to prev neighbor	    */
    u_int32	address;		  /* neighbor address		    */
    vifi_t	vifi;			  /* which interface		    */
    u_int16	timer;			  /* for timing out neighbor	    */
    build_jp_message_t *build_jp_message; /* A structure for fairly
					   * complicated Join/Prune
					   * message construction.
					   */
} pim_nbr_entry_t;


typedef struct srcentry {
    struct srcentry	*next;		/* link to next entry		    */
    struct srcentry	*prev;		/* link to prev entry		    */
    u_int32		address;	/* source or RP address 	    */
    struct mrtentry	*mrtlink;	/* link to routing entries	    */
    vifi_t		incoming;	/* incoming vif			    */
    struct pim_nbr_entry *upstream;	/* upstream router		    */
    u_int32             metric;     /* Unicast Routing Metric to the source */
    u_int32		preference;	/* The metric preference (for assers)*/
    u_int16		timer;		/* Entry timer??? Delete?      	    */
    struct cand_rp      *cand_rp;       /* Used if this is rpentry_t        */
} srcentry_t;
typedef srcentry_t rpentry_t;


/* (RP<->group) matching table related structures */
typedef struct cand_rp {
    struct cand_rp      *next;         /* Next candidate RP                 */
    struct cand_rp      *prev;         /* Previous candidate RP             */
    struct rp_grp_entry *rp_grp_next;  /* The rp_grp_entry chain for that RP*/
    rpentry_t           *rpentry;      /* Pointer to the RP entry           */
} cand_rp_t;

typedef struct grp_mask {
    struct grp_mask    *next;
    struct grp_mask    *prev;
    struct rp_grp_entry *grp_rp_next;
    u_int32          group_addr;
    u_int32          group_mask;
    u_int32          hash_mask;
    u_int16          fragment_tag;        /* Used for garbage collection    */
    u_int8           group_rp_number;     /* Used when assembling segments  */
} grp_mask_t;

typedef struct rp_grp_entry {
    struct	rp_grp_entry *rp_grp_next;/* Next entry for same RP         */
    struct	rp_grp_entry *rp_grp_prev;/* Prev entry for same RP         */
    struct	rp_grp_entry *grp_rp_next;/* Next entry for same grp prefix */
    struct	rp_grp_entry *grp_rp_prev;/* Prev entry for same grp prefix */
    struct      grpentry     *grplink;    /* Link to all grps via this entry*/
    u_int16	holdtime;	          /* The RP holdtime                */
    u_int16     fragment_tag;             /* The fragment tag from the
					   * received BSR message           */
    u_int8      priority;                 /* The RP priority                */
    grp_mask_t  *group;                   /* Pointer to (group,mask) entry  */
    cand_rp_t   *rp;                      /* Pointer to the RP              */
} rp_grp_entry_t;


typedef struct grpentry {
    struct grpentry	*next;	       /* link to next entry		    */
    struct grpentry	*prev;	       /* link to prev entry		    */
    struct grpentry     *rpnext;       /* next grp for the same RP          */
    struct grpentry     *rpprev;       /* prev grp for the same RP          */
    u_int32		group;	       /* subnet group of multicasts	    */
    u_int32             rpaddr;        /* The IP address of the RP          */
    struct mrtentry	*mrtlink;      /* link to (S,G) routing entries	    */
    rp_grp_entry_t      *active_rp_grp;/* Pointer to the active rp_grp entry*/
    struct mrtentry	*grp_route;    /* Pointer to the (*,G) routing entry*/
} grpentry_t;

typedef struct mrtentry {
    struct mrtentry	*grpnext;	/* next entry of same group	    */
    struct mrtentry	*grpprev;	/* prev entry of same group	    */
    struct mrtentry	*srcnext;	/* next entry of same source	    */
    struct mrtentry	*srcprev;	/* prev entry of same source	    */
    struct grpentry	*group;		/* pointer to group entry	    */
    struct srcentry	*source;	/* pointer to source entry (or RP)  */
    vifi_t              incoming;       /* the iif (either toward S or RP)  */
    vifbitmap_t         oifs;           /* The current result oifs          */
    vifbitmap_t	        joined_oifs;	/* The joined oifs (Join received)  */
    vifbitmap_t         pruned_oifs;    /* The pruned oifs (Prune received) */
    vifbitmap_t         asserted_oifs;  /* The asserted oifs (lost Assert)  */
    vifbitmap_t	        leaves;		/* Has directly connected members   */
    struct pim_nbr_entry *upstream;	/* upstream router, needed because
					 * of the asserts it may be different
					 * than the source (or RP) upstream
					 * router.
					 */
    u_int32             metric;         /* Routing Metric for this entry    */
    u_int32		preference;	/* The metric preference value      */
    u_int32             pmbr_addr;      /* The PMBR address (for interop)   */
    u_int16	        *vif_timers;    /* vifs timer list		    */
    u_int16	        *vif_deletion_delay; /* vifs deletion delay list    */
    u_int16	        flags;	        /* The MRTF_* flags                 */
    u_int16	        timer;	        /* entry timer			    */
    u_int16	        jp_timer;	/* The Join/Prune timer		    */
    u_int16             rs_timer;       /* Register-Suppression Timer       */
    u_int	        assert_timer;
    u_int	        assert_rate_timer;
    struct kernel_cache *kernel_cache;  /* List of the kernel cache entries */
#ifdef RSRR
    struct rsrr_cache   *rsrr_cache;    /* Used to save RSRR requests for
					 * routes change notification.
					 */
#endif /* RSRR */
} mrtentry_t;


/*
 * Used to get forwarded data related counts (number of packet, number of
 * bits, etc)
 */
struct sg_count {
    u_long pktcnt;     /*  Number of packets for (s,g) */
    u_long bytecnt;    /*  Number of bytes for (s,g)   */
    u_long wrong_if;   /*  Number of packets received on wrong iif for (s,g) */
};

struct vif_count {
    u_long icount;        /* Input packet count on vif            */
    u_long ocount;        /* Output packet count on vif           */
    u_long ibytes;        /* Input byte count on vif              */ 
    u_long obytes;        /* Output byte count on vif             */ 
};

/*
 * Structure to keep track of existing (S,G) MFC entries in the kernel
 * for particular (*,G) or (*,*,RP) entry. We must keep track for
 * each active source which doesn't have (S,G) entry in the daemon's
 * routing table. We need to keep track of such sources for two reasons:
 *
 *    (1) If the kernel does not support (*,G) MFC entries (currently, the
 * "official" mcast code doesn't), we must know all installed (s,G) entries
 * in the kernel and modify them if the iif or oif for the (*,G) changes.
 *
 *    (2) By checking periodically the traffic coming from the shared tree,
 * we can either delete the idle sources or switch to the shortest path.
 *
 * Note that even if we have (*,G) implemented in the kernel, we still
 * need to have this structure because of (2)
 */
typedef struct kernel_cache {
    struct	kernel_cache  *next;
    struct      kernel_cache  *prev;
    u_int32     source;
    u_int32     group;
    struct sg_count sg_count; /* The (s,g) data retated counters (see above) */
} kernel_cache_t;

/**
 * Local Variables:
 *  version-control: t
 *  indent-tabs-mode: t
 *  c-file-style: "ellemtel"
 *  c-basic-offset: 4
 * End:
 */
