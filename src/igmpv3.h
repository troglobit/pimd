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

#ifndef PIMD_IGMPV3_H_
#define PIMD_IGMPV3_H_

/*
 * IGMPv3 report modes.
 */
#ifndef IGMP_MODE_IS_INCLUDE
#define IGMP_DO_NOTHING			0	/* don't send a record */
#define IGMP_MODE_IS_INCLUDE		1	/* MODE_IN */
#define IGMP_MODE_IS_EXCLUDE		2	/* MODE_EX */
#define IGMP_CHANGE_TO_INCLUDE_MODE	3	/* TO_IN */
#define IGMP_CHANGE_TO_EXCLUDE_MODE	4	/* TO_EX */
#define IGMP_ALLOW_NEW_SOURCES		5	/* ALLOW_NEW */
#define IGMP_BLOCK_OLD_SOURCES		6	/* BLOCK_OLD */
#endif

struct igmpv3_query {
    uint8_t  type;
    uint8_t  code;
    uint16_t csum;
    uint32_t group;
#if defined(BYTE_ORDER) && (BYTE_ORDER == LITTLE_ENDIAN)
    uint8_t  qrv:3,
             suppress:1,
             resv:4;
#else
    uint8_t  resv:4,
	     suppress:1,
	     qrv:3;
#endif
    uint8_t  qqic;
    uint16_t nsrcs;
    uint32_t srcs[0];
};

struct igmpv3_grec {
    uint8_t  grec_type;
    uint8_t  grec_auxwords;
    uint16_t grec_nsrcs;
    uint32_t grec_mca;
    uint32_t grec_src[0];
};

#define IGMP_GRPREC_HDRLEN		8
#define IGMP_V3_GROUP_RECORD_MIN_SIZE	8

struct igmpv3_report {
    uint8_t  type;
    uint8_t  resv1;
    uint16_t csum;
    uint16_t resv2;
    uint16_t ngrec;
    struct igmpv3_grec grec[0];
};

#ifndef IGMP_V3_REPORT_MINLEN
#define IGMP_V3_REPORT_MINLEN		8
#define IGMP_V3_REPORT_MAXRECS		65535
#endif

#endif /* PIMD_IGMPV3_H_ */

/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "cc-mode"
 * End:
 */
