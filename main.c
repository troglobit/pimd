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
 *  $Id: main.c,v 1.19 2003/02/12 21:56:04 pavlin Exp $
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

struct rp_hold *g_rp_hold=NULL;


#ifdef SNMP
#include "snmp.h"
#endif

char configfilename[256] = _PATH_PIMD_CONF;
char versionstring[100];
int pid = 0;
int disable_all_by_default = 0;

static char pidfilename[]  = _PATH_PIMD_PID;
/* TODO: not used
static char genidfilename[] = _PATH_PIMD_GENID;
*/

int haveterminal = 1;
char *progname;

static int sighandled = 0;
#define GOT_SIGINT      0x01
#define GOT_SIGHUP      0x02
#define GOT_SIGUSR1     0x04
#define GOT_SIGUSR2     0x08
#define GOT_SIGALRM     0x10


#ifdef SNMP
#define NHANDLERS       34
#else
#define NHANDLERS       3
#endif

static struct ihandler {
    int fd;			/* File descriptor               */
    ihfunc_t func;		/* Function to call with &fd_set */
} ihandlers[NHANDLERS];
static int nhandlers = 0;

static struct debugname {
    char *name;
    int	 level;
    u_int nchars;
} debugnames[] = {
    {   "dvmrp_detail",	    DEBUG_DVMRP_DETAIL,   5	    },
    {   "dvmrp_prunes",	    DEBUG_DVMRP_PRUNE,    8	    },
    {   "dvmrp_pruning",    DEBUG_DVMRP_PRUNE,    8	    },
    {   "dvmrp_mrt",        DEBUG_DVMRP_ROUTE,    7	    },
    {   "dvmrp_routes",	    DEBUG_DVMRP_ROUTE,    7	    },
    {   "dvmrp_routing",    DEBUG_DVMRP_ROUTE,    7	    },
    {   "dvmrp_neighbors",  DEBUG_DVMRP_PEER,     7	    },
    {   "dvmrp_peers",	    DEBUG_DVMRP_PEER,     8	    },
    {   "dvmrp_hello",      DEBUG_DVMRP_PEER,     7	    },
    {   "dvmrp_timers",	    DEBUG_DVMRP_TIMER,    7	    },
    {   "dvmrp",	    DEBUG_DVMRP,          1	    },
    {   "igmp_proto",	    DEBUG_IGMP_PROTO,     6	    },
    {   "igmp_timers",	    DEBUG_IGMP_TIMER,     6	    },
    {   "igmp_members",	    DEBUG_IGMP_MEMBER,    6	    },
    {   "groups",	    DEBUG_MEMBER,         1	    },
    {   "membership",       DEBUG_MEMBER,         2	    },
    {   "igmp",	            DEBUG_IGMP, 	  1	    },
    {   "trace",	    DEBUG_TRACE,          2	    },
    {   "mtrace",	    DEBUG_TRACE,          2	    },
    {   "traceroute",       DEBUG_TRACE,          2	    },
    {   "timeout",	    DEBUG_TIMEOUT,        2	    },
    {   "callout",	    DEBUG_TIMEOUT,        3	    },
    {   "pkt",	            DEBUG_PKT,  	  2	    },
    {   "packets",	    DEBUG_PKT,  	  2	    },
    {   "interfaces",       DEBUG_IF,   	  2	    },
    {   "vif",	            DEBUG_IF,   	  1	    },
    {   "kernel",           DEBUG_KERN,           2	    },
    {   "cache",            DEBUG_MFC,   	  1	    },
    {   "mfc",              DEBUG_MFC,  	  2	    },
    {   "k_cache",          DEBUG_MFC,  	  2	    },
    {   "k_mfc",            DEBUG_MFC,  	  2	    },
    {   "rsrr",	            DEBUG_RSRR, 	  2	    },
    {   "pim_detail",       DEBUG_PIM_DETAIL,     5	    },
    {   "pim_hello",        DEBUG_PIM_HELLO,      5	    },
    {   "pim_neighbors",    DEBUG_PIM_HELLO,      5	    },
    {   "pim_register",     DEBUG_PIM_REGISTER,   5	    },
    {   "registers",        DEBUG_PIM_REGISTER,   2	    },
    {   "pim_join_prune",   DEBUG_PIM_JOIN_PRUNE, 5	    },
    {   "pim_j_p",          DEBUG_PIM_JOIN_PRUNE, 5	    },
    {   "pim_jp",           DEBUG_PIM_JOIN_PRUNE, 5	    },
    {   "pim_bootstrap",    DEBUG_PIM_BOOTSTRAP,  5	    },
    {   "pim_bsr",          DEBUG_PIM_BOOTSTRAP,  5	    },
    {   "bsr",	            DEBUG_PIM_BOOTSTRAP,  1	    },
    {   "bootstrap",        DEBUG_PIM_BOOTSTRAP,  1	    },
    {   "pim_asserts",      DEBUG_PIM_ASSERT,     5	    },
    {   "pim_cand_rp",      DEBUG_PIM_CAND_RP,    5	    },
    {   "pim_c_rp",         DEBUG_PIM_CAND_RP,    5	    },
    {   "pim_rp",           DEBUG_PIM_CAND_RP,    6	    },
    {   "rp",	            DEBUG_PIM_CAND_RP,    2	    },
    {   "pim_routes",       DEBUG_PIM_MRT,        6	    },
    {   "pim_routing",      DEBUG_PIM_MRT,        6	    },
    {   "pim_mrt",          DEBUG_PIM_MRT,        5	    },
    {   "pim_timers",       DEBUG_PIM_TIMER,      5	    },
    {   "pim_rpf",          DEBUG_PIM_RPF,        6	    },
    {   "rpf",              DEBUG_RPF,            3	    },
    {   "pim",              DEBUG_PIM,  	  1	    },
    {   "routes",	    DEBUG_MRT,            1	    },
    {   "routing",	    DEBUG_MRT,            1	    },
    {   "mrt",  	    DEBUG_MRT,            1	    },
    {   "routers",          DEBUG_NEIGHBORS,      6	    },
    {   "mrouters",         DEBUG_NEIGHBORS,      7	    },
    {   "neighbors",        DEBUG_NEIGHBORS,      1	    },
    {   "timers",           DEBUG_TIMER,          1	    },
    {   "asserts",          DEBUG_ASSERT,         1	    },
    {   "all",              DEBUG_ALL,            2         },
    {   "3",	            0xffffffff,           1	    }    /* compat. */
};

/*
 * Forward declarations.
 */
static void handler     (int);
static void timer        (void *);
static void cleanup      (void);
static void restart      (int);
static void resetlogging (void *);

int register_input_handler(int fd, ihfunc_t func)
{
    if (nhandlers >= NHANDLERS)
	return -1;
    
    ihandlers[nhandlers].fd = fd;
    ihandlers[nhandlers++].func = func;
    
    return 0;
}

int main(int argc, char *argv[])
{	
    int dummy, dummysigalrm;
    FILE *fp;
    struct timeval tv, difftime, curtime, lasttime, *timeout;
    fd_set rfds, readers;
    int nfds, n, i, secs;
    extern char todaysversion[];
    struct sigaction sa;
    struct debugname *d;
    char c;
    int tmpd;
    time_t boottime;

    setlinebuf(stderr);

    progname = strrchr(argv[0], '/');
    if (progname)
	progname++;
    else
	progname = argv[0];
	
    if (geteuid() != 0) {
	fprintf(stderr, "%s: must be root\n", progname);
	exit(1);
    }
    pid = getpid();
    
    argv++;
    argc--;
    while (argc > 0 && *argv[0] == '-') {
	if (strcmp(*argv, "-d") == 0) {
	    if (argc > 1 && *(argv + 1)[0] != '-') { 
		char *p,*q;
		u_int i, len;
		struct debugname *d;
		
		argv++;
		argc--;
		debug = 0;
		p = *argv; q = NULL;
		while (p) {
		    q = strchr(p, ',');
		    if (q)
			*q++ = '\0';
		    len = strlen(p);
		    for (i = 0, d = debugnames;
			 i < sizeof(debugnames) / sizeof(debugnames[0]);
			 i++, d++)
			if (len >= d->nchars && strncmp(d->name, p, len) == 0)
			    break;
		    if (i == sizeof(debugnames) / sizeof(debugnames[0])) {
			int j = 0xffffffff;
			int k = 0;
			fprintf(stderr, "Valid debug levels: ");
			for (i = 0, d = debugnames;
			     i < sizeof(debugnames) / sizeof(debugnames[0]);
			     i++, d++) {
			    if ((j & d->level) == d->level) {
				if (k++)
				    putc(',', stderr);
				fputs(d->name, stderr);
				j &= ~d->level;
			    }
			}
			putc('\n', stderr);
			goto usage;
		    }
		    debug |= d->level;
		    p = q;
		}
	    }
	    else
		debug = DEBUG_DEFAULT;
	}
	else if (strcmp(*argv, "-c") == 0) {
	    if (argc > 1) {
		argv++; argc--;
		strcpy(configfilename, *argv);
	    }
	    else
		goto usage;
/* TODO: not implemented */
#ifdef SNMP
	}
	else if (strcmp(*argv, "-P") == 0) {
	    if (argc > 1 && isdigit(*(argv + 1)[0])) {
		argv++, argc--;
		dest_port = atoi(*argv);
	    }
	    else
		dest_port = DEFAULT_PORT;
#endif
	}
	else if (strcmp(*argv, "-N") == 0) {
		disable_all_by_default = 1;
	}
	else
	    goto usage;
	argv++; argc--;
    }

    if (argc > 0) {
    usage:
	tmpd = 0xffffffff;
	fprintf(stderr, "usage: pimd [-c configfile] [-N] [-d [debug_level][,debug_level]]\n");
	
	fprintf(stderr, "debug levels: ");
    c = '(';
    for (d = debugnames; d < debugnames +
	     sizeof(debugnames) / sizeof(debugnames[0]); d++) {
	if ((tmpd & d->level) == d->level) {
	    tmpd &= ~d->level;
	    fprintf(stderr, "%c%s", c, d->name);
	    c = ',';
	}
    }
    fprintf(stderr, ")\n");
    fprintf(stderr, "-N: causes all interfaces found in kernel to be disabled by default\n");
    exit(1);
    }	
    
    if (debug != 0) {
	tmpd = debug;
	fprintf(stderr, "debug level 0x%lx ", debug);
	c = '(';
	for (d = debugnames; d < debugnames +
		 sizeof(debugnames) / sizeof(debugnames[0]); d++) {
	    if ((tmpd & d->level) == d->level) {
		tmpd &= ~d->level;
		fprintf(stderr, "%c%s", c, d->name);
		c = ',';
	    }
	}
	fprintf(stderr, ")\n");
    }
    
#ifdef LOG_DAEMON
    (void)openlog("pimd", LOG_PID, LOG_DAEMON);
    (void)setlogmask(LOG_UPTO(LOG_NOTICE));
#else
    (void)openlog("pimd", LOG_PID);
#endif /* LOG_DAEMON */
    sprintf(versionstring, "pimd version %s", todaysversion);
    
    logit(LOG_DEBUG, 0, "%s starting", versionstring);
    
/* TODO: XXX: use a combination of time and hostid to initialize the random
 * generator.
 */
#ifdef SYSV
    srand48(time(NULL));
#else
    srandom(gethostid());
#endif

    time(&boottime);
    
    /* Start up the log rate-limiter */
    resetlogging(NULL);

    callout_init();
    init_igmp();
    init_pim();
#ifdef HAVE_ROUTING_SOCKETS
    init_routesock();
#endif /* HAVE_ROUTING_SOCKETS */
    init_pim_mrt();
    init_timers();
    
    /* TODO: check the kernel DVMRP/MROUTED/PIM support version */
    
#ifdef SNMP
    if (i = snmp_init())
	return i;
#endif /* SNMP */
    init_vifs();
    init_rp_and_bsr();   /* Must be after init_vifs() */
    
#ifdef RSRR
    rsrr_init();
#endif /* RSRR */
    
    sa.sa_handler = handler;
    sa.sa_flags = 0;	/* Interrupt system calls */
    sigemptyset(&sa.sa_mask);
    sigaction(SIGALRM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);
    sigaction(SIGUSR2, &sa, NULL);
    
    FD_ZERO(&readers);
    FD_SET(igmp_socket, &readers);
    nfds = igmp_socket + 1;
    for (i = 0; i < nhandlers; i++) {
	FD_SET(ihandlers[i].fd, &readers);
	if (ihandlers[i].fd >= nfds)
	    nfds = ihandlers[i].fd + 1;
    }
    
    IF_DEBUG(DEBUG_IF)
	dump_vifs(stderr);
    IF_DEBUG(DEBUG_PIM_MRT)
	dump_pim_mrt(stderr);
    
    /* schedule first timer interrupt */
    timer_setTimer(TIMER_INTERVAL, timer, NULL);
    
    if (debug == 0) {
	/* Detach from the terminal */
	haveterminal = 0;
	if (fork())
	    exit(0);
	(void)close(0);
	(void)close(1);
	(void)close(2);
	(void)open("/", 0);
	(void)dup2(0, 1);
	(void)dup2(0, 2);
#if defined(SYSV) || defined(__USE_SVID)
	(void)setpgrp();
#else 
#ifdef TIOCNOTTY
	n = open("/dev/tty", 2);
	if (n >= 0) {
	    (void)ioctl(n, TIOCNOTTY, (char *)0);
	    (void)close(n);
	}
#else
	if (setsid() < 0)
	    perror("setsid");
#endif /* TIOCNOTTY */
#endif /* SYSV */
    } /* End of child process code */
    pid = getpid();	/* XXX: if a child fork, the pid may change */
    
    fp = fopen(pidfilename, "w");
    if (fp != NULL) {
	fprintf(fp, "%d\n", (int)getpid());
	(void) fclose(fp);
    }
    
    /*
     * Main receive loop.
     */
    dummy = 0;
    dummysigalrm = SIGALRM;
    difftime.tv_usec = 0;
    gettimeofday(&curtime, NULL);
    lasttime = curtime;
    for(;;) {
	bcopy((char *)&readers, (char *)&rfds, sizeof(rfds));
	secs = timer_nextTimer();
	if (secs == -1)
	    timeout = NULL;
	else {
	    timeout = &tv;
	    timeout->tv_sec = secs;
	   timeout->tv_usec = 0;
        }

        if (boottime) {
           time_t n;

           time(&n);
           if (n > boottime + 15) {
              struct rp_hold *rph = g_rp_hold;

              while(rph) {
                 add_rp_grp_entry(&cand_rp_list, &grp_mask_list,
                                  rph->address, 1, (u_int16)0xffffff, rph->group, rph->mask,
                                  curr_bsr_hash_mask, curr_bsr_fragment_tag);
                 rph = rph->next;
              }
              boottime = 0;
           }
        }
	
	if (sighandled) {
	    if (sighandled & GOT_SIGINT) {
		sighandled &= ~GOT_SIGINT;
		break;
	    }
	    if (sighandled & GOT_SIGHUP) {
		sighandled &= ~GOT_SIGHUP;
		restart(SIGHUP);
		
		/* reconstruct readers and nfds */
		FD_ZERO(&readers);
		FD_SET(igmp_socket, &readers);
		nfds = igmp_socket + 1;
		for (i = 0; i < nhandlers; i++) {
		    FD_SET(ihandlers[i].fd, &readers);
		    if (ihandlers[i].fd >= nfds)
			nfds = ihandlers[i].fd + 1;
		}
		bcopy((char *)&readers, (char *)&rfds, sizeof(rfds));
	    }
	    if (sighandled & GOT_SIGUSR1) {
		sighandled &= ~GOT_SIGUSR1;
		fdump(SIGUSR1);
	    }
	    if (sighandled & GOT_SIGUSR2) {
		sighandled &= ~GOT_SIGUSR2;
		cdump(SIGUSR2);
	    }
	    if (sighandled & GOT_SIGALRM) {
		sighandled &= ~GOT_SIGALRM;
		timer(&dummysigalrm);
	    }
	}
	if ((n = select(nfds, &rfds, NULL, NULL, timeout)) < 0) {
	    if (errno != EINTR) /* SIGALRM is expected */
		logit(LOG_WARNING, errno, "select failed");
	    continue;
	}
	if (n > 0) {
	    /* TODO: shall check first igmp_socket for better performance? */
	    for (i = 0; i < nhandlers; i++) {
		if (FD_ISSET(ihandlers[i].fd, &rfds)) {
		    (*ihandlers[i].func)(ihandlers[i].fd, &rfds);
		}
	    }
	}
    
	/*
	 * Handle timeout queue.
	 *
	 * If select + packet processing took more than 1 second,
	 * or if there is a timeout pending, age the timeout queue.
	 *
	 * If not, collect usec in difftime to make sure that the
	 * time doesn't drift too badly.
	 *
	 * If the timeout handlers took more than 1 second,
	 * age the timeout queue again.  XXX This introduces the
	 * potential for infinite loops!
	 */
	do {
	    /*
	     * If the select timed out, then there's no other
	     * activity to account for and we don't need to
	     * call gettimeofday.
	     */
	    if (n == 0) {
		curtime.tv_sec = lasttime.tv_sec + secs;
		curtime.tv_usec = lasttime.tv_usec;
		n = -1;	/* don't do this next time through the loop */
	    } else
		gettimeofday(&curtime, NULL);
	    difftime.tv_sec = curtime.tv_sec - lasttime.tv_sec;
	    difftime.tv_usec += curtime.tv_usec - lasttime.tv_usec;
	    while (difftime.tv_usec >= 1000000) {
		difftime.tv_sec++;
		difftime.tv_usec -= 1000000;
	    }
	    if (difftime.tv_usec < 0) {
		difftime.tv_sec--;
		difftime.tv_usec += 1000000;
	    }
	    lasttime = curtime;
	    if (secs == 0 || difftime.tv_sec > 0)
		age_callout_queue(difftime.tv_sec);
	    secs = -1;
	} while (difftime.tv_sec > 0);
    } /* Main loop */

    logit(LOG_NOTICE, 0, "%s exiting", versionstring);
    cleanup();
    exit(0);
}

/*
 * The 'virtual_time' variable is initialized to a value that will cause the
 * first invocation of timer() to send a probe or route report to all vifs
 * and send group membership queries to all subnets for which this router is
 * querier.  This first invocation occurs approximately TIMER_INTERVAL seconds
 * after the router starts up.   Note that probes for neighbors and queries
 * for group memberships are also sent at start-up time, as part of initial-
 * ization.  This repetition after a short interval is desirable for quickly
 * building up topology and membership information in the presence of possible
 * packet loss.
 *
 * 'virtual_time' advances at a rate that is only a crude approximation of
 * real time, because it does not take into account any time spent processing,
 * and because the timer intervals are sometimes shrunk by a random amount to
 * avoid unwanted synchronization with other routers.
 */

u_long virtual_time = 0;

/*
 * Timer routine. Performs all perodic functions:
 * aging interfaces, quering neighbors and members, etc... The granularity
 * is equal to TIMER_INTERVAL.
 */
static void timer(void *i __attribute__((unused)))
{
    age_vifs();	        /* Timeout neighbors and groups         */
    age_routes();  	/* Timeout routing entries              */
    age_misc();         /* Timeout the rest (Cand-RP list, etc) */
    
    virtual_time += TIMER_INTERVAL;
    timer_setTimer(TIMER_INTERVAL, timer, NULL);
}	

/*
 * Performs all necessary functions to quit gracefully
 */
/* TODO: implement all necessary stuff */
static void cleanup(void)
{
    vifi_t vifi;
    struct uvif *v;
    
    /* inform all neighbors that I'm going to die */
    for (vifi = 0, v = uvifs; vifi < numvifs; ++vifi, ++v) {
	if ((v->uv_flags &
	     (VIFF_DOWN | VIFF_DISABLED | VIFF_REGISTER | VIFF_TUNNEL)) == 0)
	    send_pim_hello(v, 0);
    }
    
#ifdef RSRR
    rsrr_clean();
#endif /* RSRR */
    
    /* TODO: XXX (not in the spec): if I am the BSR, somehow inform the
     * other routers I am going down and need to elect another BSR?
     * (probably by sending a the Cand-RP-set with my_priority=LOWEST?)
     */
    
    k_stop_pim(igmp_socket);
}


/*
 * Signal handler.  Take note of the fact that the signal arrived
 * so that the main loop can take care of it.
 */
static void handler(int sig)
{
    switch (sig) {
    case SIGALRM:
	sighandled |= GOT_SIGALRM;
    case SIGINT:
    case SIGTERM:
	sighandled |= GOT_SIGINT;
	break;
	
    case SIGHUP:
	sighandled |= GOT_SIGHUP;
	break;
	
    case SIGUSR1:
	sighandled |= GOT_SIGUSR1;
	break;
	
    case SIGUSR2:
	sighandled |= GOT_SIGUSR2;
	break;
    }
}


/* TODO: not verified */
/*
 * Restart the daemon
 */
static void restart(int i __attribute__((unused)))
{
#ifdef SNMP
    int s;
#endif /* SNMP */
    
    logit(LOG_NOTICE, 0, "%s % restart", versionstring);
    
    /*
     * reset all the entries
     */
    /* TODO: delete?
       free_all_routes();
       */
    free_all_callouts();
    stop_all_vifs();
    k_stop_pim(igmp_socket);
    nhandlers = 0;
    close(igmp_socket);
    close(pim_socket);
    /*
     * When IOCTL_OK_ON_RAW_SOCKET is defined, 'udp_socket' is equal
     * 'to igmp_socket'. Therefore, 'udp_socket' should be closed only
     * if they are different.
     */
#ifndef IOCTL_OK_ON_RAW_SOCKET
    close(udp_socket);
#endif
#ifdef HAVE_ROUTING_SOCKETS
    close(routing_socket);
#endif /* HAVE_ROUTING_SOCKETS */
    
    /*
     * start processing again
     */
    
    init_igmp();
    init_pim();
#ifdef HAVE_ROUTING_SOCKETS
    init_routesock();
#endif /* HAVE_ROUTING_SOCKETS */
    init_pim_mrt();
#ifdef SNMP
    if ( s = snmp_init())
	exit(s);
#endif /* SNMP */
    init_vifs();
#ifdef RSRR
    rsrr_init();
#endif /* RSRR */

    /* schedule timer interrupts */
    timer_setTimer(TIMER_INTERVAL, timer, NULL);
}


static void resetlogging(void *arg)
{
    int nxttime = 60;
    void *narg = NULL;
    
    if (arg == NULL && log_nmsgs > LOG_MAX_MSGS) {
	nxttime = LOG_SHUT_UP;
	narg = (void *)&log_nmsgs;	/* just need some valid void * */
	syslog(LOG_WARNING, "logging too fast, shutting up for %d minutes",
	       LOG_SHUT_UP / 60);
    } else {
	log_nmsgs = 0;
    }
    
    timer_setTimer(nxttime, resetlogging, narg);
}
