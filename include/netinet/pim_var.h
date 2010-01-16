/*
 *  Copyright (c) 1998-2000 by the University of Southern California.
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
/* $Id: pim_var.h,v 1.14 2001/05/04 02:37:00 pavlin Exp $ */

#ifndef _NETINET_PIM_VAR_H_
#define _NETINET_PIM_VAR_H_

/*
 * Protocol Independent Multicast (PIM),
 * kernel variables and implementation-specific definitions.
 *
 * Written by George Edmond Eddy (Rusty), ISI, February 1998.
 * Modified by Pavlin Radoslavov, USC/ISI, May 1998, Aug 1999, October 2000.
 * Modified by Hitoshi Asaeda, WIDE, August 1998.
 */

/*
 * PIM statistics kept in the kernel
 */
struct pimstat {
	u_quad_t pims_rcv_total_msgs;	   /* total PIM messages received    */
	u_quad_t pims_rcv_total_bytes;	   /* total PIM bytes received	     */
	u_quad_t pims_rcv_tooshort;	   /* rcvd with too few bytes	     */
	u_quad_t pims_rcv_badsum;	   /* rcvd with bad checksum	     */
	u_quad_t pims_rcv_badversion;	   /* rcvd bad PIM version	     */
	u_quad_t pims_rcv_registers_msgs;  /* rcvd regs. msgs (data only)    */
	u_quad_t pims_rcv_registers_bytes; /* rcvd regs. bytes (data only)   */
	u_quad_t pims_rcv_registers_wrongiif; /* rcvd regs. on wrong iif     */
	u_quad_t pims_rcv_badregisters;	   /* rcvd invalid registers	     */
	u_quad_t pims_snd_registers_msgs;  /* sent regs. msgs (data only)    */
	u_quad_t pims_snd_registers_bytes; /* sent regs. bytes (data only)   */
};

#ifndef __P
#ifdef __STDC__
#define __P(x)  x
#else
#define __P(x)  ()
#endif
#endif

#if (defined(KERNEL)) || (defined(_KERNEL))
extern struct pimstat pimstat;

#if defined(NetBSD) || defined(__OpenBSD__)
void pim_input __P((struct mbuf *, ...));
#elif (defined(__FreeBSD__) && (__FreeBSD_version >= 400000))
void pim_input __P((struct mbuf *, int, int));
#else
void pim_input __P((struct mbuf *, int));
#endif /* ! (NetBSD || OpenBSD) */
#endif /* KERNEL */

/*
 * Names for PIM sysctl objects
 */
#if (defined(__FreeBSD__)) || (defined(NetBSD)) || defined(__OpenBSD__) || (defined(__bsdi__))
#define PIMCTL_STATS		1	/* statistics (read-only) */
#define PIMCTL_MAXID		2

#define PIMCTL_NAMES {			\
	{ 0, 0 },			\
	{ "stats", CTLTYPE_STRUCT },	\
}
#endif /* FreeBSD || NetBSD || OpenBSD || bsdi */

#if (defined(__FreeBSD__) && (__FreeBSD_version >= 400000))
#if ((defined(KERNEL)) || (defined(_KERNEL)))
#if defined(SYSCTL_DECL)
SYSCTL_DECL(_net_inet_pim);
#endif /* SYSCTL_DECL	 */
#endif /* KERNEL	 */
#endif /* FreeBSD >= 4.0 */

#if (defined(NetBSD) || defined(__OpenBSD__) || defined(__bsdi__))
#define PIMCTL_VARS {			\
	0,				\
	0,				\
}
int	pim_sysctl __P((int *, u_int, void *, size_t *, void *, size_t));
#endif /* NetBSD || OpenBSD || bsdi */

#endif /* _NETINET_PIM_VAR_H_ */

