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
 *  $Id: debug.c,v 1.22 2001/11/28 00:13:50 pavlin Exp $
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

#include <stdarg.h>
#include <stdio.h>

int log_nmsgs = 0;
unsigned long debug = 0x00000000;        /* If (long) is smaller than
                                          * 4 bytes, then we are in
                                          * trouble.
                                          */
static char dumpfilename[] = _PATH_PIMD_DUMP;
static char cachefilename[] = _PATH_PIMD_CACHE; /* TODO: notused */


char *packet_kind(u_int proto, u_int type, u_int code)
{
    static char unknown[60];

    switch (proto) {
        case IPPROTO_IGMP:
            switch (type) {
                case IGMP_MEMBERSHIP_QUERY:    return "IGMP Membership Query    ";
                case IGMP_V1_MEMBERSHIP_REPORT:return "IGMP v1 Member Report    ";
                case IGMP_V2_MEMBERSHIP_REPORT:return "IGMP v2 Member Report    ";
                case IGMP_V2_LEAVE_GROUP:      return "IGMP Leave message       ";
                case IGMP_DVMRP:
                    switch (code) {
                        case DVMRP_PROBE:          return "DVMRP Neighbor Probe     ";
                        case DVMRP_REPORT:         return "DVMRP Route Report       ";
                        case DVMRP_ASK_NEIGHBORS:  return "DVMRP Neighbor Request   ";
                        case DVMRP_NEIGHBORS:      return "DVMRP Neighbor List      ";
                        case DVMRP_ASK_NEIGHBORS2: return "DVMRP Neighbor request 2 ";
                        case DVMRP_NEIGHBORS2:     return "DVMRP Neighbor list 2    ";
                        case DVMRP_PRUNE:          return "DVMRP Prune message      ";
                        case DVMRP_GRAFT:          return "DVMRP Graft message      ";
                        case DVMRP_GRAFT_ACK:      return "DVMRP Graft message ack  ";
                        case DVMRP_INFO_REQUEST:   return "DVMRP Info Request       ";
                        case DVMRP_INFO_REPLY:     return "DVMRP Info Reply         ";
                        default:
                            snprintf(unknown, sizeof(unknown), "UNKNOWN DVMRP message code = %3d ", code);
                            return unknown;
                    }
                case IGMP_PIM:
                    /* The old style (PIM v1) encapsulation of PIM messages
                     * inside IGMP messages.
                     */
                    /* PIM v1 is not implemented but we just inform that a message
                     *	has arrived.
                     */
                    switch (code) {
                        case PIM_V1_QUERY:         return "PIM v1 Router-Query      ";
                        case PIM_V1_REGISTER:      return "PIM v1 Register          ";
                        case PIM_V1_REGISTER_STOP: return "PIM v1 Register-Stop     ";
                        case PIM_V1_JOIN_PRUNE:    return "PIM v1 Join/Prune        ";
                        case PIM_V1_RP_REACHABILITY:
                            return "PIM v1 RP-Reachability   ";
                        case PIM_V1_ASSERT:        return "PIM v1 Assert            ";
                        case PIM_V1_GRAFT:         return "PIM v1 Graft             ";
                        case PIM_V1_GRAFT_ACK:     return "PIM v1 Graft_Ack         ";
                        default:
                            snprintf(unknown, sizeof(unknown), "UNKNOWN PIM v1 message type =%3d ", code);
                            return unknown;
                    }
                case IGMP_MTRACE:              return "IGMP trace query         ";
                case IGMP_MTRACE_RESP:         return "IGMP trace reply         ";
                default:
                    snprintf(unknown, sizeof (unknown), "UNKNOWN IGMP message: type = 0x%02x, code = 0x%02x ", type, code);
                    return unknown;
            }
        case IPPROTO_PIM:    /* PIM v2 */
            switch (type) {
                case PIM_V2_HELLO:             return "PIM v2 Hello             ";
                case PIM_V2_REGISTER:          return "PIM v2 Register          ";
                case PIM_V2_REGISTER_STOP:     return "PIM v2 Register_Stop     ";
                case PIM_V2_JOIN_PRUNE:        return "PIM v2 Join/Prune        ";
                case PIM_V2_BOOTSTRAP:         return "PIM v2 Bootstrap         ";
                case PIM_V2_ASSERT:            return "PIM v2 Assert            ";
                case PIM_V2_GRAFT:             return "PIM-DM v2 Graft          ";
                case PIM_V2_GRAFT_ACK:         return "PIM-DM v2 Graft_Ack      ";
                case PIM_V2_CAND_RP_ADV:       return "PIM v2 Cand. RP Adv.     ";
                default:
                    snprintf(unknown, sizeof(unknown), "UNKNOWN PIM v2 message type =%3d ", type);
                    return unknown;
            }
        default:
            snprintf(unknown, sizeof(unknown), "UNKNOWN proto =%3d               ", proto);
            return unknown;
    }
}


/*
 * Used for debugging particular type of messages.
 */
int debug_kind(u_int proto, u_int type, u_int code)
{
    switch (proto) {
        case IPPROTO_IGMP:
            switch (type) {
                case IGMP_MEMBERSHIP_QUERY:        return DEBUG_IGMP;
                case IGMP_V1_MEMBERSHIP_REPORT:    return DEBUG_IGMP;
                case IGMP_V2_MEMBERSHIP_REPORT:    return DEBUG_IGMP;
                case IGMP_V2_LEAVE_GROUP:          return DEBUG_IGMP;
                case IGMP_DVMRP:
                    switch (code) {
                        case DVMRP_PROBE:              return DEBUG_DVMRP_PEER;
                        case DVMRP_REPORT:             return DEBUG_DVMRP_ROUTE;
                        case DVMRP_ASK_NEIGHBORS:      return 0;
                        case DVMRP_NEIGHBORS:          return 0;
                        case DVMRP_ASK_NEIGHBORS2:     return 0;
                        case DVMRP_NEIGHBORS2:         return 0;
                        case DVMRP_PRUNE:              return DEBUG_DVMRP_PRUNE;
                        case DVMRP_GRAFT:              return DEBUG_DVMRP_PRUNE;
                        case DVMRP_GRAFT_ACK:          return DEBUG_DVMRP_PRUNE;
                        case DVMRP_INFO_REQUEST:       return 0;
                        case DVMRP_INFO_REPLY:         return 0;
                        default:                       return 0;
                    }
                case IGMP_PIM:
                    /* PIM v1 is not implemented */
                    switch (code) {
                        case PIM_V1_QUERY:             return DEBUG_PIM;
                        case PIM_V1_REGISTER:          return DEBUG_PIM;
                        case PIM_V1_REGISTER_STOP:     return DEBUG_PIM;
                        case PIM_V1_JOIN_PRUNE:        return DEBUG_PIM;
                        case PIM_V1_RP_REACHABILITY:   return DEBUG_PIM;
                        case PIM_V1_ASSERT:            return DEBUG_PIM;
                        case PIM_V1_GRAFT:             return DEBUG_PIM;
                        case PIM_V1_GRAFT_ACK:         return DEBUG_PIM;
                        default:                       return DEBUG_PIM;
                    }
                case IGMP_MTRACE:                  return DEBUG_TRACE;
                case IGMP_MTRACE_RESP:             return DEBUG_TRACE;
                default:                           return DEBUG_IGMP;
            }
        case IPPROTO_PIM:       /* PIM v2 */
            /* TODO: modify? */
            switch (type) {
                case PIM_V2_HELLO:             return DEBUG_PIM;
                case PIM_V2_REGISTER:          return DEBUG_PIM_REGISTER;
                case PIM_V2_REGISTER_STOP:     return DEBUG_PIM_REGISTER;
                case PIM_V2_JOIN_PRUNE:        return DEBUG_PIM;
                case PIM_V2_BOOTSTRAP:         return DEBUG_PIM_BOOTSTRAP;
                case PIM_V2_ASSERT:            return DEBUG_PIM;
                case PIM_V2_GRAFT:             return DEBUG_PIM;
                case PIM_V2_GRAFT_ACK:         return DEBUG_PIM;
                case PIM_V2_CAND_RP_ADV:       return DEBUG_PIM_CAND_RP;
                default:                       return DEBUG_PIM;
            }
        default:                               return 0;
    }
    return 0;
}


/*
 * Some messages are more important than others.  This routine
 * determines the logging level at which to log a send error (often
 * "No route to host").  This is important when there is asymmetric
 * reachability and someone is trying to, i.e., mrinfo me periodically.
 */
int
log_level(proto, type, code)
    u_int proto, type, code;
{
    switch (proto) {
        case IPPROTO_IGMP:
            switch (type) {
                case IGMP_MTRACE_RESP:
                    return LOG_INFO;

                case IGMP_DVMRP:
                    switch (code) {
                        case DVMRP_NEIGHBORS:
                        case DVMRP_NEIGHBORS2:
                            return LOG_INFO;
                    }
                case IGMP_PIM:
                    /* PIM v1 */
                    switch (code) {
                        default:
                            return LOG_INFO;
                    }
                default:
                    return LOG_WARNING;
            }

        case IPPROTO_PIM:
            /* PIM v2 */
            switch (type) {
                default:
                    return LOG_INFO;
            }
        default:
            return LOG_WARNING;
    }
    return LOG_WARNING;
}


/*
 * Dump internal data structures to stderr.
 */
/* TODO: currently not used
   void
   dump(int i)
   {
   dump_vifs(stderr);
   dump_pim_mrt(stderr);
   }
*/

/*
 * Dump internal data structures to a file.
 */
void fdump(int i __attribute__((unused)))
{
    FILE *fp;

    fp = fopen(dumpfilename, "w");
    if (fp != NULL) {
        dump_vifs(fp);
        dump_pim_mrt(fp);
        (void) fclose(fp);
    }
}

/* TODO: dummy, to be used in the future. */
/*
 * Dump local cache contents to a file.
 */
void cdump(int i __attribute__((unused)))
{
    FILE *fp;

    fp = fopen(cachefilename, "w");
    if (fp != NULL) {
        /* XXX: TODO: implement it:
           dump_cache(fp);
        */
        (void) fclose(fp);
    }
}

void dump_vifs(FILE *fp)
{
    vifi_t vifi;
    register struct uvif *v;
    pim_nbr_entry_t *n;
    int width;
    int i;

    fprintf(fp, "\nVirtual Interface Table\n %-3s  %-15s  %-20s %-8s %-14s %s",
            "Vif", "Local-Address", "Subnet", "Thresh", "Flags",
            "Neighbors\n");

    for (vifi = 0, v = uvifs; vifi < numvifs; ++vifi, ++v) {
        fprintf(fp, " %3u  %-15s  ", vifi, inet_fmt(v->uv_lcl_addr, s1, sizeof(s1)));
        if (v->uv_flags & VIFF_REGISTER)
            fprintf(fp, "%-20s ", v->uv_name);
        else
            fprintf(fp,"%-20.20s ", netname(v->uv_subnet, v->uv_subnetmask));
        fprintf(fp, "%-5u   ", v->uv_threshold);
        /* TODO: XXX: Print VIFF_TUNNEL? */
        width = 0;
        if (v->uv_flags & VIFF_DISABLED)
            fprintf(fp, " DISABLED");
        if (v->uv_flags & VIFF_DOWN)
            fprintf(fp, " DOWN");
        if (v->uv_flags & VIFF_DR) {
            fprintf(fp, " DR");
            width += 3;
        }
        if (v->uv_flags & VIFF_PIM_NBR) {
            fprintf(fp, " PIM");
            width += 4;
        }
        if (v->uv_flags & VIFF_DVMRP_NBR) {
            fprintf(fp, " DVMRP");
            width += 6;
        }
        if (v->uv_flags & VIFF_NONBRS) {
            fprintf(fp, " %-12s", "NO-NBR");
            width += 6;
        }

        if ((n = v->uv_pim_neighbors) != NULL) {
            /* Print the first neighbor on the same line */
            for (i = width; i <= 15; i++)
                fprintf(fp, " ");
            fprintf(fp, "%-15s\n", inet_fmt(n->address, s1, sizeof(s1)));
            for (n = n->next; n != NULL; n = n->next)
                fprintf(fp, "%64s %-15s\n", "", inet_fmt(n->address, s1, sizeof(s1)));

        }
        else
            fprintf(fp, "\n");
    }
    fprintf(fp, "\n");
}


/*
 * Log errors and other messages to the system log daemon and to stderr,
 * according to the severity of the message and the current debug level.
 * For errors of severity LOG_ERR or worse, terminate the program.
 */
void logit(int severity, int syserr, const char *format, ...)
{
    va_list ap;
    static char fmt[211] = "warning - ";
    char *msg;
    struct timeval now;
    struct tm *thyme;
    time_t lt;

    va_start(ap, format);
    vsnprintf(&fmt[10], sizeof(fmt) - 10, format, ap);
    va_end(ap);
    msg = (severity == LOG_WARNING) ? fmt : &fmt[10];

    /*
     * Log to stderr if we haven't forked yet and it's a warning or worse,
     * or if we're debugging.
     */
    if (haveterminal && (debug || severity <= LOG_WARNING)) {
        gettimeofday(&now, NULL);
        lt = now.tv_sec;
	thyme = localtime(&lt);

        if (!debug)
            fprintf(stderr, "%s: ", __progname);
        fprintf(stderr, "%02d:%02d:%02d.%03ld %s", thyme->tm_hour,
                thyme->tm_min, thyme->tm_sec, (long int)(now.tv_usec / 1000), msg);
        if (syserr == 0)
            fprintf(stderr, "\n");
	else
	    fprintf(stderr, ":(error %d): %s\n", syserr, strerror(syserr));
    }

    /*
     * Always log things that are worse than warnings, no matter what
     * the log_nmsgs rate limiter says.
     * Only count things worse than debugging in the rate limiter
     * (since if you put daemon.debug in syslog.conf you probably
     * actually want to log the debugging messages so they shouldn't
     * be rate-limited)
     */
    if ((severity < LOG_WARNING) || (log_nmsgs < LOG_MAX_MSGS)) {
        if (severity < LOG_DEBUG)
            log_nmsgs++;
        if (syserr != 0) {
            errno = syserr;
            syslog(severity, "%s: %s", msg, strerror(syserr));
        } else {
            syslog(severity, "%s", msg);
	}
    }

    if (severity <= LOG_ERR) exit(-1);
}

/* TODO: format the output for better readability */
void dump_pim_mrt(FILE *fp)
{
    grpentry_t *g;
    register mrtentry_t *r;
    register vifi_t vifi;
    u_int number_of_cache_mirrors = 0;
    u_int number_of_groups = 0;
    char oifs[(sizeof(vifbitmap_t)<<3)+1];
    char joined_oifs[(sizeof(vifbitmap_t)<<3)+1];
    char pruned_oifs[(sizeof(vifbitmap_t)<<3)+1];
    char leaves_oifs[(sizeof(vifbitmap_t)<<3)+1];
    char asserted_oifs[(sizeof(vifbitmap_t)<<3)+1];
    char incoming_iif[(sizeof(vifbitmap_t)<<3)+1];
    cand_rp_t *rp;
    kernel_cache_t *kernel_cache;

    fprintf(fp, "Multicast Routing Table\n%s",
            " Source          Group           RP-addr         Flags\n");

    /* TODO: remove the dummy 0.0.0.0 group (first in the chain) */
    for (g = grplist->next; g != (grpentry_t *)NULL; g = g->next) {
        number_of_groups++;
        if ((r = g->grp_route) != (mrtentry_t *)NULL) {
            if (r->flags & MRTF_KERNEL_CACHE) {
                for (kernel_cache = r->kernel_cache;
                     kernel_cache != (kernel_cache_t *)NULL;
                     kernel_cache = kernel_cache->next)
                    number_of_cache_mirrors++;
            }

            /* Print the (*,G) routing info */
            fprintf(fp, "---------------------------(*,G)----------------------------\n");
            fprintf(fp, " %-15s", "INADDR_ANY");
            fprintf(fp, " %-15s", inet_fmt(g->group, s1, sizeof(s1)));
            fprintf(fp, " %-15s",
                    g->active_rp_grp ? inet_fmt(g->rpaddr, s2, sizeof(s2)) : "NULL");

            for (vifi = 0; vifi < numvifs; vifi++) {
                oifs[vifi] =
                    VIFM_ISSET(vifi, r->oifs)          ? 'o' : '.';
                joined_oifs[vifi] =
                    VIFM_ISSET(vifi, r->joined_oifs)   ? 'j' : '.';
                pruned_oifs[vifi] =
                    VIFM_ISSET(vifi, r->pruned_oifs)   ? 'p' : '.';
                leaves_oifs[vifi] =
                    VIFM_ISSET(vifi, r->leaves)        ? 'l' : '.';
                asserted_oifs[vifi] =
                    VIFM_ISSET(vifi, r->asserted_oifs) ? 'a' : '.';
                incoming_iif[vifi] = '.';
            }
            oifs[vifi]          = 0x0;  /* End of string */
            joined_oifs[vifi]   = 0x0;
            pruned_oifs[vifi]   = 0x0;
            leaves_oifs[vifi]   = 0x0;
            asserted_oifs[vifi] = 0x0;
            incoming_iif[vifi]  = 0x0;
            incoming_iif[r->incoming] = 'I';

            /* TODO: don't need some of the flags */
            if (r->flags & MRTF_SPT)           fprintf(fp, " SPT");
            if (r->flags & MRTF_WC)            fprintf(fp, " WC");
            if (r->flags & MRTF_RP)            fprintf(fp, " RP");
            if (r->flags & MRTF_REGISTER)      fprintf(fp, " REG");
            if (r->flags & MRTF_IIF_REGISTER)  fprintf(fp, " IIF_REG");
            if (r->flags & MRTF_NULL_OIF)      fprintf(fp, " NULL_OIF");
            if (r->flags & MRTF_KERNEL_CACHE)  fprintf(fp, " CACHE");
            if (r->flags & MRTF_ASSERTED)      fprintf(fp, " ASSERTED");
            if (r->flags & MRTF_REG_SUPP)      fprintf(fp, " REG_SUPP");
            if (r->flags & MRTF_SG)            fprintf(fp, " SG");
            if (r->flags & MRTF_PMBR)          fprintf(fp, " PMBR");
            fprintf(fp, "\n");

            fprintf(fp, "Joined   oifs: %-20s\n", joined_oifs);
            fprintf(fp, "Pruned   oifs: %-20s\n", pruned_oifs);
            fprintf(fp, "Leaves   oifs: %-20s\n", leaves_oifs);
            fprintf(fp, "Asserted oifs: %-20s\n", asserted_oifs);
            fprintf(fp, "Outgoing oifs: %-20s\n", oifs);
            fprintf(fp, "Incoming     : %-20s\n", incoming_iif);

            fprintf(fp, "\nTIMERS:  Entry   JP   RS Assert VIFS:");
            for (vifi = 0; vifi < numvifs; vifi++)
                fprintf(fp, "  %d", vifi);
            fprintf(fp, "\n           %d     %d    %d    %d        ",
                    r->timer, r->jp_timer, r->rs_timer, r->assert_timer);
            for (vifi = 0; vifi < numvifs; vifi++)
                fprintf(fp, "  %d", r->vif_timers[vifi]);
            fprintf(fp, "\n");
        }

        /* Print all (S,G) routing info */
        for (r = g->mrtlink; r != (mrtentry_t *)NULL; r = r->grpnext) {
            fprintf(fp, "---------------------------(S,G)----------------------------\n");
            if (r->flags & MRTF_KERNEL_CACHE)
                number_of_cache_mirrors++;

            /* Print the routing info */
            fprintf(fp, " %-15s", inet_fmt(r->source->address, s1, sizeof(s1)));
            fprintf(fp, " %-15s", inet_fmt(g->group, s2, sizeof(s2)));
            fprintf(fp, " %-15s",
                    g->active_rp_grp ? inet_fmt(g->rpaddr, s2, sizeof(s2)) : "NULL");

            for (vifi = 0; vifi < numvifs; vifi++) {
                oifs[vifi] =
                    VIFM_ISSET(vifi, r->oifs)          ? 'o' : '.';
                joined_oifs[vifi] =
                    VIFM_ISSET(vifi, r->joined_oifs)   ? 'j' : '.';
                pruned_oifs[vifi] =
                    VIFM_ISSET(vifi, r->pruned_oifs)   ? 'p' : '.';
                leaves_oifs[vifi] =
                    VIFM_ISSET(vifi, r->leaves)        ? 'l' : '.';
                asserted_oifs[vifi] =
                    VIFM_ISSET(vifi, r->asserted_oifs) ? 'a' : '.';
                incoming_iif[vifi] = '.';
            }
            oifs[vifi]          = 0x0;  /* End of string */
            joined_oifs[vifi]   = 0x0;
            pruned_oifs[vifi]   = 0x0;
            leaves_oifs[vifi]   = 0x0;
            asserted_oifs[vifi] = 0x0;
            incoming_iif[vifi]  = 0x0;
            incoming_iif[r->incoming] = 'I';

            /* TODO: don't need some of the flags */
            if (r->flags & MRTF_SPT)           fprintf(fp, " SPT");
            if (r->flags & MRTF_WC)            fprintf(fp, " WC");
            if (r->flags & MRTF_RP)            fprintf(fp, " RP");
            if (r->flags & MRTF_REGISTER)      fprintf(fp, " REG");
            if (r->flags & MRTF_IIF_REGISTER)  fprintf(fp, " IIF_REG");
            if (r->flags & MRTF_NULL_OIF)      fprintf(fp, " NULL_OIF");
            if (r->flags & MRTF_KERNEL_CACHE)  fprintf(fp, " CACHE");
            if (r->flags & MRTF_ASSERTED)      fprintf(fp, " ASSERTED");
            if (r->flags & MRTF_REG_SUPP)      fprintf(fp, " REG_SUPP");
            if (r->flags & MRTF_SG)            fprintf(fp, " SG");
            if (r->flags & MRTF_PMBR)          fprintf(fp, " PMBR");
            fprintf(fp, "\n");

            fprintf(fp, "Joined   oifs: %-20s\n", joined_oifs);
            fprintf(fp, "Pruned   oifs: %-20s\n", pruned_oifs);
            fprintf(fp, "Leaves   oifs: %-20s\n", leaves_oifs);
            fprintf(fp, "Asserted oifs: %-20s\n", asserted_oifs);
            fprintf(fp, "Outgoing oifs: %-20s\n", oifs);
            fprintf(fp, "Incoming     : %-20s\n", incoming_iif);

            fprintf(fp, "\nTIMERS:  Entry   JP   RS Assert VIFS:");
            for (vifi = 0; vifi < numvifs; vifi++)
                fprintf(fp, "  %d", vifi);
            fprintf(fp, "\n           %d    %d    %d    %d        ",
                    r->timer, r->jp_timer, r->rs_timer, r->assert_timer);
            for (vifi = 0; vifi < numvifs; vifi++)
                fprintf(fp, " %d", r->vif_timers[vifi]);
            fprintf(fp, "\n");
        }
    }/* for all groups */

    /* Print the (*,*,R) routing entries */
    fprintf(fp, "--------------------------(*,*,RP)--------------------------\n");
    for (rp = cand_rp_list; rp != (cand_rp_t *)NULL; rp = rp->next) {
        if ((r = rp->rpentry->mrtlink) != (mrtentry_t *)NULL) {
            if (r->flags & MRTF_KERNEL_CACHE) {
                for (kernel_cache = r->kernel_cache;
                     kernel_cache != (kernel_cache_t *)NULL;
                     kernel_cache = kernel_cache->next)
                    number_of_cache_mirrors++;
            }

            /* Print the (*,*,RP) routing info */
            fprintf(fp, " RP = %-15s", inet_fmt(r->source->address, s1, sizeof(s1)));
            fprintf(fp, " %-15s", "INADDR_ANY");

            for (vifi = 0; vifi < numvifs; vifi++) {
                oifs[vifi] =
                    VIFM_ISSET(vifi, r->oifs)          ? 'o' : '.';
                joined_oifs[vifi] =
                    VIFM_ISSET(vifi, r->joined_oifs)   ? 'j' : '.';
                pruned_oifs[vifi] =
                    VIFM_ISSET(vifi, r->pruned_oifs)   ? 'p' : '.';
                leaves_oifs[vifi] =
                    VIFM_ISSET(vifi, r->leaves)        ? 'l' : '.';
                asserted_oifs[vifi] =
                    VIFM_ISSET(vifi, r->asserted_oifs) ? 'a' : '.';
                incoming_iif[vifi]  = '.';
            }
            oifs[vifi]          = 0x0;  /* End of string */
            joined_oifs[vifi]   = 0x0;
            pruned_oifs[vifi]   = 0x0;
            leaves_oifs[vifi]   = 0x0;
            asserted_oifs[vifi] = 0x0;
            incoming_iif[vifi]  = 0x0;
            incoming_iif[r->incoming] = 'I';

            /* TODO: don't need some of the flags */
            if (r->flags & MRTF_SPT)           fprintf(fp, " SPT");
            if (r->flags & MRTF_WC)            fprintf(fp, " WC");
            if (r->flags & MRTF_RP)            fprintf(fp, " RP");
            if (r->flags & MRTF_REGISTER)      fprintf(fp, " REG");
            if (r->flags & MRTF_IIF_REGISTER)  fprintf(fp, " IIF_REG");
            if (r->flags & MRTF_NULL_OIF)      fprintf(fp, " NULL_OIF");
            if (r->flags & MRTF_KERNEL_CACHE)  fprintf(fp, " CACHE");
            if (r->flags & MRTF_ASSERTED)      fprintf(fp, " ASSERTED");
            if (r->flags & MRTF_REG_SUPP)      fprintf(fp, " REG_SUPP");
            if (r->flags & MRTF_SG)            fprintf(fp, " SG");
            if (r->flags & MRTF_PMBR)          fprintf(fp, " PMBR");
            fprintf(fp, "\n");

            fprintf(fp, "Joined   oifs: %-20s\n", joined_oifs);
            fprintf(fp, "Pruned   oifs: %-20s\n", pruned_oifs);
            fprintf(fp, "Leaves   oifs: %-20s\n", leaves_oifs);
            fprintf(fp, "Asserted oifs: %-20s\n", asserted_oifs);
            fprintf(fp, "Outgoing oifs: %-20s\n", oifs);
            fprintf(fp, "Incoming     : %-20s\n", incoming_iif);

            fprintf(fp, "\nTIMERS:  Entry   JP   RS Assert VIFS:");
            for (vifi = 0; vifi < numvifs; vifi++)
                fprintf(fp, "  %d", vifi);
            fprintf(fp, "\n           %d    %d    %d    %d        ",
                    r->timer, r->jp_timer, r->rs_timer, r->assert_timer);
            for (vifi = 0; vifi < numvifs; vifi++)
                fprintf(fp, " %d", r->vif_timers[vifi]);
            fprintf(fp, "\n");
        }
    } /* For all (*,*,RP) */

    fprintf(fp, "Number of Groups: %u\n", number_of_groups);
    fprintf(fp, "Number of Cache MIRRORs: %u\n\n", number_of_cache_mirrors);
}


/* TODO: modify the output for better redability */
/*
 * Dumps the local Cand-RP-set
 */
int dump_rp_set(FILE *fp)
{
    cand_rp_t      *rp;
    rp_grp_entry_t *rp_grp_entry;
    grp_mask_t     *grp_mask;

    fprintf(fp, "---------------------------RP-Set----------------------------\n");
    fprintf(fp, "Current BSR address: %s\n", inet_fmt(curr_bsr_address, s1, sizeof(s1)));
    fprintf(fp, "RP-address      Incoming   Group prefix   Priority   Holdtime \n");

    for (rp = cand_rp_list; rp != (cand_rp_t *)NULL; rp = rp->next) {
        fprintf(fp, "%-15s %-3d        ", inet_fmt(rp->rpentry->address, s1, sizeof(s1)),
                rp->rpentry->incoming);
        if ((rp_grp_entry = rp->rp_grp_next) != (rp_grp_entry_t *)NULL) {
            grp_mask = rp_grp_entry->group;
            fprintf(fp, "%-14.14s %-3u        %-3u\n",
                    netname(grp_mask->group_addr, grp_mask->group_mask),
                    rp_grp_entry->priority, rp_grp_entry->holdtime);

            for (rp_grp_entry = rp_grp_entry->rp_grp_next;
                 rp_grp_entry != (rp_grp_entry_t *)NULL;
                 rp_grp_entry = rp_grp_entry->rp_grp_next) {
                grp_mask = rp_grp_entry->group;
                fprintf(fp, "%-14.14s %-3u        %-3u\n",
                        netname(grp_mask->group_addr, grp_mask->group_mask),
                        rp_grp_entry->priority, rp_grp_entry->holdtime);
            }
        }
    }

    return TRUE;
}

/**
 * Local Variables:
 *  version-control: t
 *  indent-tabs-mode: t
 *  c-file-style: "ellemtel"
 *  c-basic-offset: 4
 * End:
 */
