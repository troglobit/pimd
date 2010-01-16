/*
 *  Copyright (c) 1996-2000 by the University of Southern California.
 *  All rights reserved.
 *
 *  Permission to use, copy, modify, and distribute this software and
 *  its documentation in source and binary forms for lawful
 *  purposes and without fee is hereby granted, provided
 *  that the above copyright notice appear in all copies and that both
 *  the copyright notice and this permission notice appear in supporting
 *  documentation, and that any documentation, advertising materials,
 *  and other materials related to such distribution and use acknowledge
 *  that the software was developed by the University of Southern
 *  California and/or Information Sciences Institute.
 *  The name of the University of Southern California may not
 *  be used to endorse or promote products derived from this software
 *  without specific prior written permission.
 *
 *  THE UNIVERSITY OF SOUTHERN CALIFORNIA DOES NOT MAKE ANY REPRESENTATIONS
 *  ABOUT THE SUITABILITY OF THIS SOFTWARE FOR ANY PURPOSE.  THIS SOFTWARE IS
 *  PROVIDED "AS IS" AND WITHOUT ANY EXPRESS OR IMPLIED WARRANTIES,
 *  INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, TITLE, AND 
 *  NON-INFRINGEMENT.
 *
 *  IN NO EVENT SHALL USC, OR ANY OTHER CONTRIBUTOR BE LIABLE FOR ANY
 *  SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES, WHETHER IN CONTRACT,
 *  TORT, OR OTHER FORM OF ACTION, ARISING OUT OF OR IN CONNECTION WITH,
 *  THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 *  Other copyrights might apply to parts of this software and are so
 *  noted when applicable.
 */
/* $Id: pim.h,v 1.11 2001/05/04 02:37:00 pavlin Exp $ */

#ifndef _NETINET_PIM_H_
#define _NETINET_PIM_H_

/*
 * Protocol Independent Multicast (PIM) definitions.
 * Per RFC 2362, June 1998.
 *
 * Written by Ahmed Helmy, USC/SGI, July 1996.
 * Modified by George Edmond Eddy (Rusty), ISI, February 1998.
 * Modified by Pavlin Radoslavov, USC/ISI, May 1998, October 2000.
 */

#define PIM_VERSION	2

/*
 * PIM packet header
 */
struct pim {
#ifdef _PIM_VT
	u_int8_t	pim_vt;		/* PIM version and message type	*/
#define PIM_MAKE_VT(v, t)	(0xff & (((v) << 4) | (0x0f & (t))))
#define PIM_VT_V(x)		(((x) >> 4) & 0x0f)
#define PIM_VT_T(x)		((x) & 0x0f)
#else /* ! _PIM_VT   */
#if defined(BYTE_ORDER) && (BYTE_ORDER == BIG_ENDIAN)
	u_int		pim_vers:4,	/* PIM protocol version		*/
			pim_type:4;	/* PIM message type		*/
#elif defined(BYTE_ORDER) && (BYTE_ORDER == LITTLE_ENDIAN)
	u_int		pim_type:4,	/* PIM message type		*/
			pim_vers:4;	/* PIM protocol version		*/
#endif /* BYTE_ORDER */
#endif /* ! _PIM_VT  */
	u_int8_t	pim_reserved;	/* Reserved			*/
	u_int16_t	pim_cksum;	/* IP-style (CRC) checksum	*/
};

#define PIM_MINLEN	8		/* PIM message min. length	*/
#define PIM_REG_MINLEN	(PIM_MINLEN+20)	/* PIM Register header + inner IPv4 header */
#define PIM6_REG_MINLEN	(PIM_MINLEN+40)	/* PIM Register header + inner IPv6 header */

/*
 * PIM message types
 */
#define PIM_HELLO		0x0	/* PIM-SM and PIM-DM		*/
#define PIM_REGISTER		0x1	/* PIM-SM only			*/
#define PIM_REGISTER_STOP	0x2	/* PIM-SM only			*/
#define PIM_JOIN_PRUNE		0x3	/* PIM-SM and PIM-DM		*/
#define PIM_BOOTSTRAP		0x4	/* PIM-SM only			*/
#define PIM_ASSERT		0x5	/* PIM-SM and PIM-DM		*/
#define PIM_GRAFT		0x6	/* PIM-DM only			*/
#define PIM_GRAFT_ACK		0x7	/* PIM-DM only			*/
#define PIM_CAND_RP_ADV		0x8	/* PIM-SM only			*/

/*
 * PIM-Register message flags
 */
#define PIM_BORDER_REGISTER 0x80000000	/* The Border bit (host-order)	*/
#define PIM_NULL_REGISTER   0x40000000	/* The Null-Register bit (host-order) */

/*
 * All PIM routers IPv4 and IPv6 group
 */
#define INADDR_ALLPIM_ROUTERS_GROUP (u_int32_t)0xe000000d  /* 224.0.0.13 */
#define IN6ADDR_LINKLOCAL_ALLPIM_ROUTERS	"ff02::d"
#define IN6ADDR_LINKLOCAL_ALLPIM_ROUTERS_INIT \
	{{{ 0xff, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
	    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0d }}}

#endif /* _NETINET_PIM_H_ */
