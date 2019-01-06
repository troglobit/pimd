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

#define SYSLOG_NAMES
#include "defs.h"

#include <stdarg.h>
#include <stdio.h>

#define MAX_MSG_SIZE 64                  /* Max for dump_frame() */

static struct debugname {
    char	*name;
    uint32_t	 level;
    size_t	 nchars;
} debugnames[] = {
    {   "dvmrp_detail",	    DEBUG_DVMRP_DETAIL,   5	    },
    {   "dvmrp_prunes",	    DEBUG_DVMRP_PRUNE,    8	    },
    {   "dvmrp_pruning",    DEBUG_DVMRP_PRUNE,    8	    },
    {   "dvmrp_routes",	    DEBUG_DVMRP_ROUTE,    7	    },
    {   "dvmrp_routing",    DEBUG_DVMRP_ROUTE,    7	    },
    {	"dvmrp_mrt",	    DEBUG_DVMRP_ROUTE,	  7	    },
    {	"dvmrp_neighbors",  DEBUG_DVMRP_PEER,	  7	    },
    {	"dvmrp_peers",	    DEBUG_DVMRP_PEER,	  8	    },
    {	"dvmrp_hello",	    DEBUG_DVMRP_PEER,	  7	    },
    {	"dvmrp_timers",	    DEBUG_DVMRP_TIMER,	  7	    },
    {	"dvmrp",	    DEBUG_DVMRP,	  1	    },
    {	"igmp_proto",	    DEBUG_IGMP_PROTO,	  6	    },
    {	"igmp_timers",	    DEBUG_IGMP_TIMER,	  6	    },
    {	"igmp_members",	    DEBUG_IGMP_MEMBER,	  6	    },
    {	"groups",	    DEBUG_MEMBER,	  1	    },
    {	"membership",	    DEBUG_MEMBER,	  2	    },
    {	"igmp",		    DEBUG_IGMP,		  1	    },
    {	"trace",	    DEBUG_TRACE,	  2	    },
    {	"mtrace",	    DEBUG_TRACE,	  2	    },
    {	"traceroute",	    DEBUG_TRACE,	  2	    },
    {	"timeout",	    DEBUG_TIMEOUT,	  2	    },
    {	"callout",	    DEBUG_TIMEOUT,	  3	    },
    {	"packets",	    DEBUG_PKT,		  2	    },
    {	"pkt",		    DEBUG_PKT,		  2	    },
    {	"interfaces",	    DEBUG_IF,		  2	    },
    {	"vif",		    DEBUG_IF,		  1	    },
    {	"kernel",	    DEBUG_KERN,		  2	    },
    {	"cache",	    DEBUG_MFC,		  1	    },
    {	"mfc",		    DEBUG_MFC,		  2	    },
    {	"k_cache",	    DEBUG_MFC,		  2	    },
    {	"k_mfc",	    DEBUG_MFC,		  2	    },
    {	"rsrr",		    DEBUG_RSRR,		  2	    },
    {	"pim_detail",	    DEBUG_PIM_DETAIL,	  5	    },
    {	"pim_hello",	    DEBUG_PIM_HELLO,	  5	    },
    {	"pim_neighbors",    DEBUG_PIM_HELLO,	  5	    },
    {	"pim_peers",	    DEBUG_PIM_HELLO,	  5	    },
    {	"pim_register",	    DEBUG_PIM_REGISTER,	  5	    },
    {	"registers",	    DEBUG_PIM_REGISTER,	  2	    },
    {	"pim_join_prune",   DEBUG_PIM_JOIN_PRUNE, 5	    },
    {	"pim_j_p",	    DEBUG_PIM_JOIN_PRUNE, 5	    },
    {	"pim_jp",	    DEBUG_PIM_JOIN_PRUNE, 5	    },
    {	"pim_bootstrap",    DEBUG_PIM_BOOTSTRAP,  5	    },
    {	"pim_bsr",	    DEBUG_PIM_BOOTSTRAP,  5	    },
    {	"bsr",		    DEBUG_PIM_BOOTSTRAP,  1	    },
    {	"bootstrap",	    DEBUG_PIM_BOOTSTRAP,  1	    },
    {	"pim_asserts",	    DEBUG_PIM_ASSERT,	  5	    },
    {	"pim_cand_rp",	    DEBUG_PIM_CAND_RP,	  5	    },
    {	"pim_c_rp",	    DEBUG_PIM_CAND_RP,	  5	    },
    {	"pim_rp",	    DEBUG_PIM_CAND_RP,	  6	    },
    {	"rp",		    DEBUG_PIM_CAND_RP,	  2	    },
    {	"pim_routes",	    DEBUG_PIM_MRT,	  6	    },
    {	"pim_routing",	    DEBUG_PIM_MRT,	  6	    },
    {	"pim_mrt",	    DEBUG_PIM_MRT,	  5	    },
    {	"pim_timers",	    DEBUG_PIM_TIMER,	  5	    },
    {	"pim_rpf",	    DEBUG_PIM_RPF,	  6	    },
    {	"rpf",		    DEBUG_RPF,		  3	    },
    {	"pim",		    DEBUG_PIM,		  1	    },
    {	"routes",	    DEBUG_MRT,		  1	    },
    {	"routing",	    DEBUG_MRT,		  1	    },
    {	"mrt",		    DEBUG_MRT,		  1	    },
    {	"neighbors",	    DEBUG_NEIGHBORS,	  1	    },
    {	"routers",	    DEBUG_NEIGHBORS,	  6	    },
    {	"mrouters",	    DEBUG_NEIGHBORS,	  7	    },
    {	"peers",	    DEBUG_NEIGHBORS,	  1	    },
    {	"timers",	    DEBUG_TIMER,	  1	    },
    {	"asserts",	    DEBUG_ASSERT,	  1	    },
    {	"all",		    DEBUG_ALL,		  2	    },
    {	"3",		    DEBUG_ALL,		  1	    }	 /* compat. */
};

int log_nmsgs = 0;
int loglevel = LOG_NOTICE;
unsigned long debug = 0x00000000;        /* If (long) is smaller than
					  * 4 bytes, then we are in
					  * trouble.
					  */
static int log_syslog = 0;


char *packet_kind(int proto, int type, int code)
{
    static char unknown[60];

    switch (proto) {
	case IPPROTO_IGMP:
	    switch (type) {
		case IGMP_MEMBERSHIP_QUERY:     return "IGMP Membership Query    ";
		case IGMP_V1_MEMBERSHIP_REPORT: return "IGMP v1 Membership Report";
		case IGMP_V2_MEMBERSHIP_REPORT: return "IGMP v2 Membership Report";
		case IGMP_V3_MEMBERSHIP_REPORT: return "IGMP v3 Membership Report";
		case IGMP_V2_LEAVE_GROUP:       return "IGMP Leave message       ";
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
int debug_kind(int proto, int type, int code)
{
    switch (proto) {
	case IPPROTO_IGMP:
	    switch (type) {
		case IGMP_MEMBERSHIP_QUERY:        return DEBUG_IGMP;
		case IGMP_V1_MEMBERSHIP_REPORT:    return DEBUG_IGMP;
		case IGMP_V2_MEMBERSHIP_REPORT:    return DEBUG_IGMP;
		case IGMP_V3_MEMBERSHIP_REPORT:    return DEBUG_IGMP;
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

int debug_list(int mask, char *buf, size_t len)
{
    struct debugname *d;
    size_t i;

    memset(buf, 0, len);
    for (i = 0, d = debugnames; i < ARRAY_LEN(debugnames); i++, d++) {
	if (!(mask & d->level))
	    continue;

	if (mask != (int)DEBUG_ALL)
	    mask &= ~d->level;

	strlcat(buf, d->name, len);

	if (mask && i + 1 < ARRAY_LEN(debugnames))
	    strlcat(buf, ", ", len);
    }

    return 0;
}

int debug_parse(char *arg)
{
    struct debugname *d;
    size_t i, len;
    char *next = NULL;
    int sys = 0;

    if (!arg || !strlen(arg) || strstr(arg, "none"))
	return sys;

    while (arg) {
	next = strchr(arg, ',');
	if (next)
	    *next++ = '\0';

	len = strlen(arg);
	for (i = 0, d = debugnames; i < ARRAY_LEN(debugnames); i++, d++) {
	    if (len >= d->nchars && strncmp(d->name, arg, len) == 0)
		break;
	}

	if (i == ARRAY_LEN(debugnames))
	    return DEBUG_PARSE_FAIL;

	sys |= d->level;
	arg = next;
    }

    return sys;
}

/*
 * Some messages are more important than others.  This routine
 * determines the logging level at which to log a send error (often
 * "No route to host").  This is important when there is asymmetric
 * reachability and someone is trying to, i.e., mrinfo me periodically.
 */
int log_level(int proto, int type, int code)
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
		    return LOG_WARNING;

		case IGMP_PIM:
		    /* PIM v1 */
		    switch (code) {
			default:
			    return LOG_INFO;
		    }
		    return LOG_WARNING;

		default:
		    return LOG_WARNING;
	    }

	case IPPROTO_PIM:
	    /* PIM v2 */
	    switch (type) {
		default:
		    return LOG_INFO;
	    }
	    return LOG_WARNING;

	default:
	    return LOG_WARNING;
    }
    return LOG_WARNING;
}

/*
            1         2         3         4         5         6         7         8
  012345678901234567890123456789012345678901234567890123456789012345678901234567890
  Virtual Interface Table
  Vif  Local-Address    Subnet                Thresh  Flags          Neighbors
  0  10.0.3.1         10.0.3/24             1       DR NO-NBR
  1  172.16.12.254    172.16.12/24          1       DR PIM         172.16.12.2
                                                                   172.16.12.3
  2  192.168.122.147  register_vif0         1
*/
void dump_vifs(FILE *fp)
{
    vifi_t vifi;
    struct uvif *v;
    pim_nbr_entry_t *n;
    int width;
    int i;

    fprintf(fp, "Virtual Interface Table ======================================================\n");
    fprintf(fp, "Vif  Local Address    Subnet              Thresh  Flags      Neighbors\n");
    fprintf(fp, "---  ---------------  ------------------  ------  ---------  -----------------\n");

    for (vifi = 0, v = uvifs; vifi < numvifs; ++vifi, ++v) {
	int down = 0;

	fprintf(fp, "%3u  %-15s  ", vifi, inet_fmt(v->uv_lcl_addr, s1, sizeof(s1)));

	if (v->uv_flags & VIFF_REGISTER)
	    fprintf(fp, "%-18s  ", v->uv_name);
	else
	    fprintf(fp,"%-18.18s  ", netname(v->uv_subnet, v->uv_subnetmask));

	fprintf(fp, "%6u ", v->uv_threshold);

	/* TODO: XXX: Print VIFF_TUNNEL? */
	width = 0;
	if (v->uv_flags & VIFF_DISABLED) {
	    fprintf(fp, " DISABLED");
	    down = 1;
	}
	if (v->uv_flags & VIFF_DOWN) {
	    fprintf(fp, " DOWN");
	    down = 1;
	}

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
	    fprintf(fp, " NO-NBR");
	    width += 6;
	}

	n = v->uv_pim_neighbors;
	if (!down && n) {
	    for (i = width; i <= 11; i++)
		fprintf(fp, " ");
	    fprintf(fp, "%-15s\n", inet_fmt(n->address, s1, sizeof(s1)));
	    for (n = n->next; n; n = n->next)
		fprintf(fp, "%61s%-15s\n", "", inet_fmt(n->address, s1, sizeof(s1)));
	} else {
	    fprintf(fp, "\n");
	}
    }
}

void dump_ssm(FILE *fp)
{
    struct listaddr *group, *source;
    struct uvif *v;
    vifi_t vifi;
    int first = 1;

    /* Dump groups and sources */
    for (vifi = 0, v = uvifs; vifi < numvifs; ++vifi, ++v) {
	for (group = v->uv_groups; group != NULL; group = group->al_next) {
	    if (!IN_PIM_SSM_RANGE(group->al_addr))
		continue;

	    if (first) {
		fprintf(fp, "\n %-3s  %-15s  %-20s\n", "Vif", "SSM Group", "Sources");
		first = 0;
	    }

	    fprintf(fp, " %3u  %-15s ", vifi, inet_fmt(group->al_addr, s1, sizeof(s1)));
	    for (source = group->al_sources; source; source = source->al_next)
		fprintf(fp, "%s ", inet_fmt(source->al_addr, s1, sizeof(s1)));
	    fprintf(fp, "\n");
	}
    }
}

void log_init(int do_syslog)
{
    int log_opts = LOG_PID;

    log_syslog = do_syslog;

#ifdef LOG_DAEMON
    openlog(ident, log_opts, LOG_DAEMON);
    setlogmask(LOG_UPTO(loglevel));
#else
    openlog(ident, log_opts);
#endif
}

int log_str2lvl(char *level)
{
    int i;

    for (i = 0; prioritynames[i].c_name; i++) {
	size_t len = MIN(strlen(prioritynames[i].c_name), strlen(level));

	if (!strncasecmp(prioritynames[i].c_name, level, len))
	    return prioritynames[i].c_val;
    }

    return atoi(level);
}

char *log_lvl2str(int val)
{
    int i;

    for (i = 0; prioritynames[i].c_name; i++) {
	if (prioritynames[i].c_val == val)
	    return prioritynames[i].c_name;
    }

    return "unknown";
}

int log_list(char *buf, size_t len)
{
    int i;

    memset(buf, 0, len);
    for (i = 0; prioritynames[i].c_name; i++) {
	if (i > 0)
	    strlcat(buf, ", ", len);
	strlcat(buf, prioritynames[i].c_name, len);
    }

    return 0;
}

/*
 * Log errors and other messages to the system log daemon and to stderr,
 * according to the severity of the message and the current debug level.
 * For errors of severity LOG_ERR or worse, terminate the program.
 */
void logit(int severity, int syserr, const char *format, ...)
{
    va_list ap;
    char msg[211];
    struct timeval now;
    struct tm *thyme;
    time_t lt;

    va_start(ap, format);
    vsnprintf(msg, sizeof(msg), format, ap);
    va_end(ap);

    /* pimd running in foreground */
    if (!log_syslog) {
	if (!debug && severity > loglevel)
	    return;

	gettimeofday(&now, NULL);
	lt = now.tv_sec;
	thyme = localtime(&lt);

	if (!debug)
	    fprintf(stderr, "%s: ", prognm);

	fprintf(stderr, "%02d:%02d:%02d.%03ld %s", thyme->tm_hour, thyme->tm_min,
		thyme->tm_sec, (long int)(now.tv_usec / 1000), msg);

	if (syserr)
	    fprintf(stderr, ": %s", strerror(syserr));

	fprintf(stderr, "\n");
	return;
    }

    /*
     * Always log things that are worse than warnings, no matter what
     * the log_nmsgs rate limiter says.
     *
     * Only count things at the defined loglevel or worse in the rate limiter
     * and exclude debugging (since if you put daemon.debug in syslog.conf
     * you probably actually want to log the debugging messages so they
     * shouldn't be rate-limited)
     */
    if ((severity < LOG_WARNING) || (log_nmsgs < LOG_MAX_MSGS)) {
	if ((severity <= loglevel) && (severity != LOG_DEBUG))
	    log_nmsgs++;

	if (syserr)
	    syslog(severity, "%s: %s", msg, strerror(syserr));
	else
	    syslog(severity, "%s", msg);
    }

#ifndef CONTINUE_ON_ERROR
    if (severity <= LOG_ERR)
	exit(-1);		/* Exit status: 255 */
#endif
}


/*
 * Hex dump a control frame to the log, MAX 64 bytes length
 */
void dump_frame(char *desc, void *dump, size_t len)
{
    unsigned int length, i = 0;
    unsigned char *data = (unsigned char *)dump;
    char buf[80] = "";
    char tmp[10];

    length = len;
    if (length > MAX_MSG_SIZE)
	length = MAX_MSG_SIZE;

    if (desc)
	logit(LOG_DEBUG, 0, "%s", desc);

    while (i < length) {
	if (!(i % 16))
	    snprintf(buf, sizeof(buf), "%03X: ", i);

	snprintf(tmp, sizeof(tmp), "%02X ", data[i++]);
	strlcat(buf, tmp, sizeof(buf));

	if (i > 0 && !(i % 16))
	    logit(LOG_DEBUG, 0, "%s", buf);
	else if (i > 0 && !(i % 8))
	    strlcat(buf, ":: ", sizeof(buf));
    }
    logit(LOG_DEBUG, 0, "%s", buf);
}


static void dump_route(FILE *fp, mrtentry_t *r)
{
    vifi_t vifi;
    char oifs[MAXVIFS+1];
    char joined_oifs[MAXVIFS+1];
    char pruned_oifs[MAXVIFS+1];
    char leaves_oifs[MAXVIFS+1];
    char asserted_oifs[MAXVIFS+1];
    char incoming_iif[MAXVIFS+1];

    for (vifi = 0; vifi < numvifs; vifi++) {
	oifs[vifi] =
	    PIMD_VIFM_ISSET(vifi, r->oifs) ? 'o' : '.';
	joined_oifs[vifi] =
	    PIMD_VIFM_ISSET(vifi, r->joined_oifs) ? 'j' : '.';
	pruned_oifs[vifi] =
	    PIMD_VIFM_ISSET(vifi, r->pruned_oifs) ? 'p' : '.';
	leaves_oifs[vifi] =
	    PIMD_VIFM_ISSET(vifi, r->leaves) ? 'l' : '.';
	asserted_oifs[vifi] =
	    PIMD_VIFM_ISSET(vifi, r->asserted_oifs) ? 'a' : '.';
	incoming_iif[vifi] = '.';
    }
    oifs[vifi]		= 0x0;	/* End of string */
    joined_oifs[vifi]	= 0x0;
    pruned_oifs[vifi]	= 0x0;
    leaves_oifs[vifi]	= 0x0;
    asserted_oifs[vifi] = 0x0;
    incoming_iif[vifi]	= 0x0;
    incoming_iif[r->incoming] = 'I';

    /* TODO: don't need some of the flags */
    if (r->flags & MRTF_SPT)	       fprintf(fp, " SPT");
    if (r->flags & MRTF_WC)	       fprintf(fp, " WC");
    if (r->flags & MRTF_RP)	       fprintf(fp, " RP");
    if (r->flags & MRTF_REGISTER)      fprintf(fp, " REG");
    if (r->flags & MRTF_IIF_REGISTER)  fprintf(fp, " IIF_REG");
    if (r->flags & MRTF_NULL_OIF)      fprintf(fp, " NULL_OIF");
    if (r->flags & MRTF_KERNEL_CACHE)  fprintf(fp, " CACHE");
    if (r->flags & MRTF_ASSERTED)      fprintf(fp, " ASSERTED");
    if (r->flags & MRTF_REG_SUPP)      fprintf(fp, " REG_SUPP");
    if (r->flags & MRTF_SG)	       fprintf(fp, " SG");
    if (r->flags & MRTF_PMBR)	       fprintf(fp, " PMBR");
    fprintf(fp, "\n");

    fprintf(fp, "Joined   oifs: %-20s\n", joined_oifs);
    fprintf(fp, "Pruned   oifs: %-20s\n", pruned_oifs);
    fprintf(fp, "Leaves   oifs: %-20s\n", leaves_oifs);
    fprintf(fp, "Asserted oifs: %-20s\n", asserted_oifs);
    fprintf(fp, "Outgoing oifs: %-20s\n", oifs);
    fprintf(fp, "Incoming     : %-20s\n", incoming_iif);

    fprintf(fp, "\nTIMERS:  Entry    JP    RS  Assert VIFS:");
    for (vifi = 0; vifi < numvifs; vifi++)
	fprintf(fp, "  %d", vifi);
    fprintf(fp, "\n         %5d  %4d  %4d  %6d      ",
	    r->timer, r->jp_timer, r->rs_timer, r->assert_timer);
    for (vifi = 0; vifi < numvifs; vifi++)
	fprintf(fp, " %2d", r->vif_timers[vifi]);
    fprintf(fp, "\n");
}

void dump_pim_mrt(FILE *fp)
{
    grpentry_t *g;
    mrtentry_t *r;
    u_int number_of_cache_mirrors = 0;
    u_int number_of_groups = 0;
    cand_rp_t *rp;
    kernel_cache_t *kc;

    fprintf(fp, "Multicast Routing Table ======================================================\n");

    /* TODO: remove the dummy 0.0.0.0 group (first in the chain) */
    for (g = grplist->next; g; g = g->next) {
	number_of_groups++;

	r = g->grp_route;
	if (r) {
	    if (r->flags & MRTF_KERNEL_CACHE) {
		for (kc = r->kernel_cache; kc; kc = kc->next)
		    number_of_cache_mirrors++;
	    }

	    /* Print the (*,G) routing info */
	    fprintf(fp, "----------------------------------- (*,G) ------------------------------------\n");
	    fprintf(fp, "Source           Group            RP Address       Flags\n");
	    fprintf(fp, "---------------  ---------------  ---------------  ---------------------------\n");
	    fprintf(fp, "%-15s  ", "INADDR_ANY");
	    fprintf(fp, "%-15s  ", inet_fmt(g->group, s1, sizeof(s1)));
	    fprintf(fp, "%-15s ", IN_PIM_SSM_RANGE(g->group) ? "SSM" :
		    (g->active_rp_grp ? inet_fmt(g->rpaddr, s2, sizeof(s2)) : "NULL"));

	    dump_route(fp, r);
	}

	/* Print all (S,G) routing info */
	fprintf(fp, "----------------------------------- (S,G) ------------------------------------\n");
	for (r = g->mrtlink; r; r = r->grpnext) {
	    if (r->flags & MRTF_KERNEL_CACHE)
		number_of_cache_mirrors++;

	    /* Print the routing info */
	    fprintf(fp, "Source           Group            RP Address       Flags\n");
	    fprintf(fp, "---------------  ---------------  ---------------  ---------------------------\n");
	    fprintf(fp, "%-15s  ", inet_fmt(r->source->address, s1, sizeof(s1)));
	    fprintf(fp, "%-15s  ", inet_fmt(g->group, s2, sizeof(s2)));
	    fprintf(fp, "%-15s ", IN_PIM_SSM_RANGE(g->group) ? "SSM" :
		    (g->active_rp_grp ? inet_fmt(g->rpaddr, s2, sizeof(s2)) : "NULL"));

	    dump_route(fp, r);
	}
    }/* for all groups */

    /* Print the (*,*,R) routing entries */
    fprintf(fp, "--------------------------------- (*,*,G) ------------------------------------\n");
    for (rp = cand_rp_list; rp; rp = rp->next) {
	r = rp->rpentry->mrtlink;
	if (r) {
	    if (r->flags & MRTF_KERNEL_CACHE) {
		for (kc = r->kernel_cache; kc; kc = kc->next)
		    number_of_cache_mirrors++;
	    }

	    /* Print the (*,*,RP) routing info */
	    fprintf(fp, "Source           Group            RP Address       Flags\n");
	    fprintf(fp, "---------------  ---------------  ---------------  ---------------------------\n");
	    fprintf(fp, "%-15s  ", inet_fmt(r->source->address, s1, sizeof(s1)));
	    fprintf(fp, "%-15s  ", "INADDR_ANY");
	    fprintf(fp, "%-15s ", "");

	    dump_route(fp, r);
	}
    } /* For all (*,*,RP) */

    fprintf(fp, "Number of Groups: %u\n", number_of_groups);
    fprintf(fp, "Number of Cache MIRRORs: %u\n", number_of_cache_mirrors);
    fprintf(fp, "------------------------------------------------------------------------------\n\n");
}

static void dump_rpgrp(FILE *fp, rp_grp_entry_t *rpgrp)
{
    grp_mask_t *grp = rpgrp->group;

    fprintf(fp, "                           %-18.18s  %-8u  %-8u\n",
	    netname(grp->group_addr, grp->group_mask),
	    rpgrp->priority, rpgrp->holdtime);
}

/*
 * Dumps the local RP-set
 */
int dump_rp_set(FILE *fp)
{
    cand_rp_t      *rp;
    rp_grp_entry_t *rpgrp;

    fprintf(fp, "Rendezvous-Point Set =========================================================\n");
    fprintf(fp, "RP address       Incoming  Group Prefix        Priority  Holdtime\n");
    fprintf(fp, "---------------  --------  ------------------  --------  ---------------------\n");
    for (rp = cand_rp_list; rp; rp = rp->next) {
	fprintf(fp, "%-15s  %-8d                                %-8u\n",
		inet_fmt(rp->rpentry->address, s1, sizeof(s1)),
		rp->rpentry->incoming,
		rp->rpentry->adv_holdtime);

	for (rpgrp = rp->rp_grp_next; rpgrp; rpgrp = rpgrp->rp_grp_next)
		dump_rpgrp(fp, rpgrp);
    }

    fprintf(fp, "------------------------------------------------------------------------------\n");
    fprintf(fp, "Current BSR address: %s\n\n", inet_fmt(curr_bsr_address, s1, sizeof(s1)));

    return TRUE;
}

/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "ellemtel"
 *  c-basic-offset: 4
 * End:
 */
