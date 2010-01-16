/*
 * Internet Group Management Protocol (IGMP) definitions.
 *
 * Written by Steve Deering, Stanford, May 1988.
 *
 * MULTICAST $Revision: 3.5 $
 */

/*
 * IGMP packet format.
 */
struct igmp {
	u_char		igmp_type;	/* version & type of IGMP message  */
	u_char		igmp_code;	/* code for routing sub-msgs       */
	u_short		igmp_cksum;	/* IP-style checksum               */
	struct in_addr	igmp_group;	/* group address being reported    */
};					/*  (zero for queries)             */

#define IGMP_MINLEN		     8

/*
 * Message types, including version number.
 */
#define IGMP_HOST_MEMBERSHIP_QUERY   0x11	/* Host membership query    */
#define IGMP_HOST_MEMBERSHIP_REPORT  0x12	/* Old membership report    */
#define IGMP_DVMRP		     0x13	/* DVMRP routing message    */
#define IGMP_PIM		     0x14	/* PIM routing message	    */

#define IGMP_HOST_NEW_MEMBERSHIP_REPORT 0x16	/* New membership report    */

#define IGMP_HOST_LEAVE_MESSAGE      0x17	/* Leave-group message	    */

#define IGMP_MTRACE_RESP	     0x1e  /* traceroute resp. (to sender)  */
#define IGMP_MTRACE		     0x1f  /* mcast traceroute messages     */

#define IGMP_MAX_HOST_REPORT_DELAY   10    /* max delay for response to     */
					   /*  query (in seconds)           */


#define IGMP_TIMER_SCALE     10	    /* denotes that the igmp->timer filed */
				    /* specifies time in 10th of seconds  */

/*
 * States for the IGMPv2 state table
 */
#define IGMP_DELAYING_MEMBER                     1
#define IGMP_IDLE_MEMBER                         2
#define IGMP_LAZY_MEMBER                         3 
#define IGMP_SLEEPING_MEMBER                     4 
#define IGMP_AWAKENING_MEMBER                    5 

/*
 * We must remember whether the querier is an old or a new router.
 */
#define IGMP_OLD_ROUTER                          0
#define IGMP_NEW_ROUTER                          1

/*
 * Revert to new router if we haven't heard from an old router in
 * this amount of time.
 */
#define IGMP_AGE_THRESHOLD		         540
