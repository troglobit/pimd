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

#ifndef __PIMD_IGMPV3_H__
#define __PIMD_IGMPV3_H__

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

#ifndef IGMP_V3_REPORT_MINLEN
struct igmpv3_query {
    __u8 type;
    __u8 code;
    __be16 csum;
    __be32 group;
    __u8 resv:4,
         suppress:1,
         qrv:3;
    __u8 qqic;
    __be16 nsrcs;
    __be32 srcs[0];
};

struct igmpv3_grec {
    __u8    grec_type;
    __u8    grec_auxwords;
    __be16  grec_nsrcs;
   __be32  grec_mca;
    __be32  grec_src[0];
};

#define IGMP_GRPREC_HDRLEN		8
#define IGMP_V3_GROUP_RECORD_MIN_SIZE	8

struct igmpv3_report {
    __u8 type;
    __u8 resv1;
    __be16 csum;
    __be16 resv2;
    __be16 ngrec;
    struct igmpv3_grec grec[0];
};

#define IGMP_V3_REPORT_MINLEN		8
#define IGMP_V3_REPORT_MAXRECS		65535
#endif /* IGMP_V3_REPORT_MINLEN */

#endif /* __PIMD_IGMPV3_H__ */

/**
 * Local Variables:
 *  version-control: t
 *  indent-tabs-mode: t
 *  c-file-style: "ellemtel"
 *  c-basic-offset: 4
 * End:
 */
