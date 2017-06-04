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
 *  $Id: debug.h,v 1.8 2001/09/10 20:31:36 pavlin Exp $
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

#ifndef PIMD_DEBUG_H_
#define PIMD_DEBUG_H_

extern unsigned long	debug;
extern int log_nmsgs;
#define IF_DEBUG(l)	if (debug && (debug & (l)))

#define LOG_MAX_MSGS	100	/* if > 100/minute then shut up for a while */
#define LOG_SHUT_UP	600	/* shut up for 10 minutes */

/* Debug values definition */
/* DVMRP reserved for future use */
#define DEBUG_DVMRP_PRUNE     0x00000001
#define DEBUG_DVMRP_ROUTE     0x00000002
#define DEBUG_DVMRP_PEER      0x00000004
#define DEBUG_DVMRP_TIMER     0x00000008
#define DEBUG_DVMRP_DETAIL    0x01000000
#define DEBUG_DVMRP           ( DEBUG_DVMRP_PRUNE | DEBUG_DVMRP_ROUTE | \
				DEBUG_DVMRP_PEER )

/* IGMP related */
#define DEBUG_IGMP_PROTO      0x00000010
#define DEBUG_IGMP_TIMER      0x00000020
#define DEBUG_IGMP_MEMBER     0x00000040
#define DEBUG_MEMBER          DEBUG_IGMP_MEMBER
#define DEBUG_IGMP            ( DEBUG_IGMP_PROTO | DEBUG_IGMP_TIMER | \
				DEBUG_IGMP_MEMBER )

/* Misc */
#define DEBUG_TRACE           0x00000080
#define DEBUG_TIMEOUT         0x00000100
#define DEBUG_PKT             0x00000200

/* Kernel related */
#define DEBUG_IF              0x00000400
#define DEBUG_KERN            0x00000800
#define DEBUG_MFC             0x00001000
#define DEBUG_RSRR            0x00002000

/* PIM related */
#define DEBUG_PIM_HELLO       0x00004000
#define DEBUG_PIM_REGISTER    0x00008000
#define DEBUG_PIM_JOIN_PRUNE  0x00010000
#define DEBUG_PIM_BOOTSTRAP   0x00020000
#define DEBUG_PIM_ASSERT      0x00040000
#define DEBUG_PIM_CAND_RP     0x00080000
#define DEBUG_PIM_MRT         0x00100000
#define DEBUG_PIM_TIMER       0x00200000
#define DEBUG_PIM_RPF         0x00400000
#define DEBUG_RPF             DEBUG_PIM_RPF
#define DEBUG_PIM_DETAIL      0x00800000
#define DEBUG_PIM             ( DEBUG_PIM_HELLO | DEBUG_PIM_REGISTER | \
				DEBUG_PIM_JOIN_PRUNE | DEBUG_PIM_BOOTSTRAP | \
				DEBUG_PIM_ASSERT | DEBUG_PIM_CAND_RP | \
				DEBUG_PIM_MRT | DEBUG_PIM_TIMER | \
				DEBUG_PIM_RPF ) 

#define DEBUG_MRT             ( DEBUG_DVMRP_ROUTE | DEBUG_PIM_MRT )
#define DEBUG_NEIGHBORS       ( DEBUG_DVMRP_PEER | DEBUG_PIM_HELLO )
#define DEBUG_TIMER           ( DEBUG_IGMP_TIMER | DEBUG_DVMRP_TIMER | \
				DEBUG_PIM_TIMER )
#define DEBUG_ASSERT          ( DEBUG_PIM_ASSERT )
#define DEBUG_ALL             0xffffffff

#define DEBUG_DEFAULT   0xffffffff/*  default if "-d" given without value */

int loglvl(char *level);

#endif /* PIMD_DEBUG_H_ */
