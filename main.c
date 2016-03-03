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
#include <err.h>
#include <getopt.h>
#include <sys/stat.h>

char versionstring[100];
int disable_all_by_default = 0;
int haveterminal = 1;
struct rp_hold *g_rp_hold = NULL;
int mrt_table_id = 0;

char *config_file = _PATH_PIMD_CONF;

extern int loglevel;
extern char todaysversion[];

static int sighandled = 0;
#define GOT_SIGINT      0x01
#define GOT_SIGHUP      0x02
#define GOT_SIGUSR1     0x04
#define GOT_SIGUSR2     0x08
#define GOT_SIGALRM     0x10

#define NHANDLERS       3
static struct ihandler {
    int fd;			/* File descriptor               */
    ihfunc_t func;		/* Function to call with &fd_set */
} ihandlers[NHANDLERS];
static int nhandlers = 0;

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
    {	"3",		    0xffffffff,		  1	    }	 /* compat. */
};


/*
 * Forward declarations.
 */
static void handler      (int);
static void timer	 (void *);
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

static void do_randomize(void)
{
#define rol32(data,shift) ((data) >> (shift)) | ((data) << (32 - (shift)))
   int fd;
   unsigned int seed;

   /* Setup a fallback seed based on quasi random. */
#ifdef SYSV
   seed = time(NULL);
#else
   seed = time(NULL) ^ gethostid();
#endif
   seed = rol32(seed, seed);

   fd = open("/dev/urandom", O_RDONLY);
   if (fd >= 0) {
       if (-1 == read(fd, &seed, sizeof(seed)))
	   warn("Failed reading entropy from /dev/urandom");
       close(fd);
  }

#ifdef SYSV
   srand48(seed);
#else
   srandom(seed);
#endif
}

/* Figure out the PID of a running daemon. */
static pid_t daemon_pid(void)
{
    int result;
    char *path = NULL;
    FILE *fp;
    pid_t pid = -1;

    result = asprintf(&path, "%s%s.pid", _PATH_VARRUN, __progname);
    if (result == -1 || path == NULL)
	return -1;

    fp = fopen(path, "r");
    if (!fp) {
	free(path);
	return -1;
    }

    result = fscanf(fp, "%d", &pid);
    fclose(fp);
    free(path);

    return pid;
}

/* Send signal to running daemon and the show resulting file. */
static int killshow(int signo, char *file)
{
    pid_t pid = daemon_pid();
    char buf[100];

    if (pid > 0) {
	if (file && -1 == remove(file) && errno != ENOENT)
	    warn("Failed removing %s, may be showing stale information", file);

	kill(pid, signo);
	if (file) {
	    usleep(200);
	    snprintf(buf, sizeof(buf), "cat %s", file);
	    if (-1 == system(buf)) {
		warnx("Failed listing file %s\n", file);
	    }
	}
    }

    return 0;
}

static int usage(int code)
{
    size_t i;
    char line[76] = "  ";
    struct debugname *d;

    printf("\nUsage: %s [-fhlNqrv] [-c FILE] [-d [SYS][,SYS...]] [-s LEVEL]\n\n", __progname);
    printf(" -c, --config=FILE   Configuration file to use, default %s\n", _PATH_PIMD_CONF);
    printf(" -d, --debug[=SYS]   Debug subsystem, see below for valid systems, default all\n");
    printf(" -f, --foreground    Run in foreground, do not detach from calling terminal\n");
    printf(" -h, --help          Show this help text\n");
    /* printf("  -i, --show-cache     Show internal cache tables\n"); */
    printf(" -l, --reload-config Tell a running pimd to reload its configuration\n");
    printf(" -N, --disable-vifs  Disable all virtual interfaces (phyint) by default\n");
    /* printf("  -p,--show-debug     Show debug dump, only if debug is enabled\n"); */
    printf(" -q, --quit-daemon   Send SIGTERM to a running pimd\n");
    printf(" -r, --show-routes   Show state of VIFs and multicast routing tables\n");
    printf(" -t, --table-id=ID   Set multicast routing table ID.  Allowed table ID#:\n"
	   "                      0 .. 999999999.  Default: 0 (use default table)\n");
    printf(" -s, --loglevel=LVL  Set log level: none, err, info, notice (default), debug\n");
    printf(" -v, --version       Show %s version\n", __progname);
    printf("\n");

    /* From pimd v2.3.0 we show *all* the debug levels again */
    printf("Available subsystems for debug:\n");
    for (i = 0, d = debugnames; i < ARRAY_LEN(debugnames); i++, d++) {
	if (strlen(line) + strlen(d->name) + 3 >= sizeof(line)) {
	    /* Finish this line and send to console */
	    strlcat(line, "\n", sizeof(line));
	    printf("%s", line);

	    /* Prepare for next line */
	    strlcpy(line, "  ", sizeof(line));
	}

	strlcat(line, d->name, sizeof(line));

	if (i + 1 < ARRAY_LEN(debugnames))
	    strlcat(line, ", ", sizeof(line));
    }
    /* Flush remaining line. */
    strlcat(line, "\n", sizeof(line));
    printf("%s", line);

    printf("\nBug report address: %-40s\n\n", PACKAGE_BUGREPORT);

    return code;
}

int main(int argc, char *argv[])
{
    int dummysigalrm, foreground = 0;
    struct timeval tv, difftime, curtime, lasttime, *timeout;
    fd_set rfds, readers;
    int nfds, n, i, secs, ch;
    struct sigaction sa;
    time_t boottime;
    struct option long_options[] = {
	{"config", 1, 0, 'c'},
	{"debug", 2, 0, 'd'},
	{"foreground", 0, 0, 'f'},
	{"disable-vifs", 0, 0, 'N'},
	{"help", 0, 0, 'h'},
	{"version", 0, 0, 'v'},
	{"quit-daemon", 0, 0, 'q'},
	{"reload-config", 0, 0, 'l'},
	{"show-routes", 0, 0, 'r'},
	{"table-id", 1, 0, 't'},
	{"syslog-level", 1, 0, 's'},   /* Compat */
	{"loglevel", 1, 0, 's'},
	/* {"show-cache", 0, 0, 'i'}, */
	/* {"show-debug", 0, 0, 'p'}, */
	{0, 0, 0, 0}
    };

    snprintf(versionstring, sizeof (versionstring), "pimd version %s", todaysversion);

    while ((ch = getopt_long(argc, argv, "c:d::fhlNvqrt:s:", long_options, NULL)) != EOF) {
	const char *errstr;

	switch (ch) {
	    case 'c':
		if (optarg)
		    config_file = optarg;
		break;

	    case 'd':
		if (!optarg) {
		    debug = DEBUG_DEFAULT;
		} else {
		    char *p,*q;
		    size_t i, len;
		    struct debugname *d;

		    debug = 0;
		    p = optarg; q = NULL;
		    while (p) {
			q = strchr(p, ',');
			if (q)
			    *q++ = '\0';
			len = strlen(p);
			for (i = 0, d = debugnames; i < ARRAY_LEN(debugnames); i++, d++) {
			    if (len >= d->nchars && strncmp(d->name, p, len) == 0)
				break;
			}

			if (i == ARRAY_LEN(debugnames))
			    return usage(1);

			debug |= d->level;
			p = q;
		    }
		}
		break;

	    case 'f':
		foreground = 1;
		break;

	    case 'h':
		return usage(0);

	    case 'l':
		return killshow(SIGHUP, NULL);

	    case 'N':
		disable_all_by_default = 1;
		break;

	    case 'v':
		printf("%s\n", versionstring);
		return 0;

	    case 'q':
		return killshow(SIGTERM, NULL);

	    case 'r':
		return killshow(SIGUSR1, _PATH_PIMD_DUMP);

	    case 's':
		if (!optarg) {
			fprintf(stderr, "Missing loglevel argument!\n");
			return usage(1);
		}

		loglevel = loglvl(optarg);
		if (-1 == loglevel)
		    return usage(1);
		break;

	    case 't':
		if (!optarg) {
			fprintf(stderr, "Missing Table ID argument!\n");
			return usage(1);
		}

		mrt_table_id = strtonum(optarg, 0, 999999999, &errstr);
		if (errstr) {
		    fprintf(stderr, "Table ID %s!\n", errstr);
		    return usage(1);
		}
		break;

#if 0 /* XXX: TODO */
	    case 'i':
		return killshow(SIGUSR2, _PATH_PIMD_CACHE);

	    case 'p':
		return killshow(SIGQUIT, NULL);
#endif
	    default:
		return usage(1);
	}
    }

    argc -= optind;
    if (argc > 0)
	return usage(1);

    if (geteuid() != 0)
	errx(1, "Need root privileges to start.");

    setlinebuf(stderr);

    if (debug != 0) {
	struct debugname *d;
	char c;
	int tmpd = debug;

	fprintf(stderr, "debug level 0x%lx ", debug);
	c = '(';
	for (d = debugnames; d < debugnames + ARRAY_LEN(debugnames); d++) {
	    if ((tmpd & d->level) == d->level) {
		tmpd &= ~d->level;
		fprintf(stderr, "%c%s", c, d->name);
		c = ',';
	    }
	}
	fprintf(stderr, ")\n");
    }

    /*
     * Create directory for runtime files
     */
    if (-1 == mkdir(_PATH_PIMD_RUNDIR, 0755) && errno != EEXIST)
	err(1, "Failed creating %s directory for runtime files", _PATH_PIMD_RUNDIR);

    /*
     * Setup logging
     */
#ifdef LOG_DAEMON
    openlog("pimd", LOG_PID, LOG_DAEMON);
    setlogmask(LOG_UPTO(loglevel));
#else
    openlog("pimd", LOG_PID);
#endif /* LOG_DAEMON */

    logit(LOG_NOTICE, 0, "%s starting ...", versionstring);

    do_randomize();
    time(&boottime);

    callout_init();
    init_igmp();
    init_pim();
    init_routesock(); /* Both for Linux netlink and BSD routing socket */
    init_pim_mrt();
    init_timers();

    /* Start up the log rate-limiter */
    resetlogging(NULL);

    /* TODO: check the kernel DVMRP/MROUTED/PIM support version */

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

    if (!debug && !foreground) {
	/* Detach from the terminal */
	haveterminal = 0;
	if (fork())
	    exit(0);

	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);

	n = open("/dev/null", O_RDWR, 0);
	if (n >= 0) {
	    dup2(n, STDIN_FILENO);
	    dup2(n, STDOUT_FILENO);
	    dup2(n, STDERR_FILENO);
	}
#ifdef SYSV
	setpgrp();
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

    if (pidfile(NULL))
	warn("Cannot create pidfile");

    /*
     * Main receive loop.
     */
    dummysigalrm = SIGALRM;
    difftime.tv_usec = 0;
    gettimeofday(&curtime, NULL);
    lasttime = curtime;
    while (1) {
	memcpy(&rfds, &readers, sizeof(rfds));
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
				     rph->address, 1, (uint16_t)0xffffff,
				     rph->group, rph->mask,
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
		memcpy(&rfds, &readers, sizeof(rfds));
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

    logit(LOG_NOTICE, 0, "%s exiting.", versionstring);
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

uint32_t virtual_time = 0;

/*
 * Timer routine. Performs all perodic functions:
 * aging interfaces, quering neighbors and members, etc... The granularity
 * is equal to TIMER_INTERVAL.
 */
static void timer(void *i __attribute__((unused)))
{
    age_vifs();		/* Timeout neighbors and groups         */
    age_routes();	/* Timeout routing entries              */
    age_misc();		/* Timeout the rest (Cand-RP list, etc) */

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
	break;

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
    logit(LOG_NOTICE, 0, "%s restarting ...", versionstring);

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

    /* Both for Linux netlink and BSD routing socket */
    close(routing_socket);

    /*
     * start processing again
     */

    init_igmp();
    init_pim();
    init_routesock(); /* Both for Linux netlink and BSD routing socket */
    init_pim_mrt();
    init_vifs();

    /* schedule timer interrupts */
    timer_setTimer(TIMER_INTERVAL, timer, NULL);
}


static void resetlogging(void *arg)
{
    int nxttime = 60;
    void *narg = NULL;

    if (arg == NULL && log_nmsgs >= LOG_MAX_MSGS) {
	nxttime = LOG_SHUT_UP;
	narg = (void *)&log_nmsgs;	/* just need some valid void * */
	syslog(LOG_WARNING, "logging too fast, shutting up for %d minutes",
	       LOG_SHUT_UP / 60);
    } else {
	if (arg != NULL) {
	    syslog(LOG_NOTICE, "logging enabled again after rate limiting");
	}
	log_nmsgs = 0;
    }

    timer_setTimer(nxttime, resetlogging, narg);
}

/**
 * Local Variables:
 *  version-control: t
 *  indent-tabs-mode: t
 *  c-file-style: "ellemtel"
 *  c-basic-offset: 4
 * End:
 */
