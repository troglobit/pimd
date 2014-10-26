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
 *  $Id: dvmrp_proto.c,v 1.10 2001/09/10 20:31:36 pavlin Exp $
 */


#include "defs.h"


/* TODO */
/*
 * Process an incoming neighbor probe message.
 */
void
dvmrp_accept_probe(src, dst, p, datalen, level)
    uint32_t src __attribute__((unused));
    uint32_t dst __attribute__((unused));
    uint8_t *p __attribute__((unused));
    int datalen __attribute__((unused));
    uint32_t level __attribute__((unused));
{
    return;
}


/* TODO */
/*
 * Process an incoming route report message.
 */
void
dvmrp_accept_report(src, dst, p, datalen, level)
    uint32_t src __attribute__((unused));
    uint32_t dst __attribute__((unused));
    uint8_t *p __attribute__((unused));
    int datalen __attribute__((unused));
    uint32_t level __attribute__((unused));
{
    return;
}


/* TODO */
void
dvmrp_accept_info_request(src, dst, p, datalen)
    uint32_t src __attribute__((unused));
    uint32_t dst __attribute__((unused));
    uint8_t *p __attribute__((unused));
    int datalen __attribute__((unused));
{
    return;
}


/*
 * Process an incoming info reply message.
 */
void
dvmrp_accept_info_reply(src, dst, p, datalen)
    uint32_t src;
    uint32_t dst;
    uint8_t *p __attribute__((unused));
    int datalen __attribute__((unused));
{
    IF_DEBUG(DEBUG_PKT)
        logit(LOG_DEBUG, 0, "ignoring spurious DVMRP info reply from %s to %s",
              inet_fmt(src, s1, sizeof(s1)), inet_fmt(dst, s2, sizeof(s2)));
}


/*
 * Process an incoming neighbor-list message.
 */
void
dvmrp_accept_neighbors(src, dst, p, datalen, level)
    uint32_t src;
    uint32_t dst;
    uint8_t *p __attribute__((unused));
    int datalen __attribute__((unused));
    uint32_t level __attribute__((unused));
{
    logit(LOG_INFO, 0, "ignoring spurious DVMRP neighbor list from %s to %s",
          inet_fmt(src, s1, sizeof(s1)), inet_fmt(dst, s2, sizeof(s2)));
}


/*
 * Process an incoming neighbor-list message.
 */
void
dvmrp_accept_neighbors2(src, dst, p, datalen, level)
    uint32_t src;
    uint32_t dst;
    uint8_t *p __attribute__((unused));
    int datalen __attribute__((unused));
    uint32_t level __attribute__((unused));
{
    IF_DEBUG(DEBUG_PKT)
        logit(LOG_DEBUG, 0,
              "ignoring spurious DVMRP neighbor list2 from %s to %s",
              inet_fmt(src, s1, sizeof(s1)), inet_fmt(dst, s2, sizeof(s2)));
}


/* TODO */
/*
 * Takes the prune message received and then strips it to
 * determine the (src, grp) pair to be pruned.
 *
 * Adds the router to the (src, grp) entry then.
 *
 * Determines if further packets have to be sent down that vif
 *
 * Determines if a corresponding prune message has to be generated
 */
void
dvmrp_accept_prune(src, dst, p, datalen)
    uint32_t src __attribute__((unused));
    uint32_t dst __attribute__((unused));
    uint8_t *p __attribute__((unused));
    int datalen __attribute__((unused));
{
    return;
}


/* TODO */
/* determine the multicast group and src
 *
 * if it does, then determine if a prune was sent
 * upstream.
 * if prune sent upstream, send graft upstream and send
 * ack downstream.
 *
 * if no prune sent upstream, change the forwarding bit
 * for this interface and send ack downstream.
 *
 * if no entry exists for this group send ack downstream.
 */
void
dvmrp_accept_graft(src, dst, p, datalen)
    uint32_t     src __attribute__((unused));
    uint32_t     dst __attribute__((unused));
    uint8_t     *p __attribute__((unused));
    int         datalen __attribute__((unused));
{
    return;
}


/* TODO */
/*
 * find out which group is involved first of all
 * then determine if a graft was sent.
 * if no graft sent, ignore the message
 * if graft was sent and the ack is from the right
 * source, remove the graft timer so that we don't
 * have send a graft again
 */
void
dvmrp_accept_g_ack(src, dst, p, datalen)
    uint32_t     src __attribute__((unused));
    uint32_t     dst __attribute__((unused));
    uint8_t     *p __attribute__((unused));
    int         datalen __attribute__((unused));
{
    return;
}

/**
 * Local Variables:
 *  version-control: t
 *  indent-tabs-mode: t
 *  c-file-style: "ellemtel"
 *  c-basic-offset: 4
 * End:
 */
