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
int do_vifs       = 1;
int no_fallback   = 0;
int retry_forever = 0;
struct rp_hold *g_rp_hold = NULL;
int mrt_table_id = 0;

char *ident       = PACKAGE_NAME;
char *prognm      = NULL;
char *pid_file    = NULL;
char *config_file = NULL;

static int sighandled = 0;
#define GOT_SIGINT      0x01
#define GOT_SIGHUP      0x02
#define GOT_SIGALRM     0x10

#define NHANDLERS       3
static struct ihandler {
    int fd;			/* File descriptor               */
    ihfunc_t func;		/* Function to call with &fd_set */
} ihandlers[NHANDLERS];
static int nhandlers = 0;

/*
 * Forward declarations.
 */
static void            handle_signals(int);
static int             check_signals (void);
static void            timer         (void *);
static struct timeval *timeout       (int);
static void            cleanup       (void);
static void            restart       (int);
static void            resetlogging  (void *);
static void            add_static_rp (void);

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

static int compose_paths(void)
{
    /* Default .conf file path: "/etc" + '/' + "pimd" + ".conf" */
    if (!config_file) {
	size_t len = strlen(SYSCONFDIR) + strlen(ident) + 7;

	config_file = malloc(len);
	if (!config_file) {
	    logit(LOG_ERR, errno, "Failed allocating memory, exiting.");
	    exit(1);
	}

	snprintf(config_file, len, _PATH_PIMD_CONF, ident);
    }

    /* Default is to let pidfile() API construct PID file from ident */
    if (!pid_file)
	pid_file = strdup(ident);

    return 0;
}

static int usage(int code)
{
    char pidfn[80];
    char buf[768];

    compose_paths();
    if (pid_file && pid_file[0] != '/')
	snprintf(pidfn, sizeof(pidfn), "%s/%s.pid", _PATH_PIMD_RUNDIR, pid_file);
    else
	snprintf(pidfn, sizeof(pidfn), "%s", pid_file);

    printf("Usage: %s [-hnrsv] [-f FILE] [-i NAME] [-d [SYS][,SYS...]] [-l LEVEL]\n\n", prognm);
    printf(" -f, --config=FILE   Configuration file, default uses ident NAME: %s\n", config_file);
    printf("     --no-fallback   When started without a config file, skip RP/BSR fallbacks\n");
    printf(" -n, --foreground    Run in foreground, do not detach from calling terminal\n");
    printf(" -d, --debug=SYS     Enable debug for subystem(s) SYS, separate more with comma\n");
    printf(" -l, --loglevel=LVL  Set log level: none, err, notice (default), info, debug\n");
    printf(" -i, --ident=NAME    Identity for config + PID file, and syslog, default: %s\n", ident);
    printf("     --pidfile=FILE  File to store process ID for signaling %s\n"
	   "                     Default uses ident NAME: %s\n", prognm, pidfn);
    printf(" -r                  Retry (forever) if not all configured phyint interfaces are\n"
	   "                     available when starting up, e.g. wait for DHCP lease\n");
    printf("     --disable-vifs  Disable all virtual interfaces (phyint) by default\n");
    printf(" -s, --syslog        Use syslog, default unless running in foreground, -n\n");
    printf(" -t, --table-id=ID   Set multicast routing table ID.  Allowed table ID#:\n"
	   "                      0 .. 999999999.  Default: 0 (use default table)\n");
    printf(" -h, --help          Show this help text\n");
    printf(" -v, --version       Show %s version\n", prognm);
    printf("\n");

    printf("Available subsystems for debug:\n");
    if (!debug_list(DEBUG_ALL, buf, sizeof(buf))) {
	char line[82] = "  ";
	char *ptr;

	ptr = strtok(buf, " ");
	while (ptr) {
	    char *sys = ptr;
	    char buf[20];

	    ptr = strtok(NULL, " ");

	    /* Flush line */
	    if (strlen(line) + strlen(sys) + 3 >= sizeof(line)) {
		puts(line);
		strlcpy(line, "  ", sizeof(line));
	    }

	    if (ptr)
		snprintf(buf, sizeof(buf), "%s ", sys);
	    else
		snprintf(buf, sizeof(buf), "%s", sys);

	    strlcat(line, buf, sizeof(line));
	}

	puts(line);
    }

    printf("\nBug report address: %-40s\n", PACKAGE_BUGREPORT);
#ifdef PACKAGE_URL
    printf("Project homepage: %s\n", PACKAGE_URL);
#endif

    if (config_file)
	free(config_file);

    if (pid_file)
	free(pid_file);

    return code;
}

static char *progname(char *arg0)
{
       char *nm;

       nm = strrchr(arg0, '/');
       if (nm)
	       nm++;
       else
	       nm = arg0;

       return nm;
}

int main(int argc, char *argv[])
{
    int foreground = 0, do_syslog = 1;
    fd_set fds;
    int nfds, fd, n = -1, i, ch, rc;
    struct sigaction sa;
    struct option long_options[] = {
	{ "config",        1, 0, 'f' },
	{ "debug",         1, 0, 'd' },
	{ "no-fallback",   0, 0, 500 },
	{ "disable-vifs",  0, 0, 501 },
	{ "foreground",    0, 0, 'n' },
	{ "help",          0, 0, 'h' },
	{ "ident",         1, 0, 'i' },
	{ "loglevel",      1, 0, 'l' },
	{ "pidfile",       1, 0, 502 },
	{ "syslog",        0, 0, 's' },
	{ "table-id",      1, 0, 't' },
	{ "version",       0, 0, 'v' },
	{ NULL, 0, 0, 0 }
    };

    snprintf(versionstring, sizeof(versionstring), "pimd version %s", PACKAGE_VERSION);

    prognm = ident = progname(argv[0]);
    while ((ch = getopt_long(argc, argv, "d:f:hi:l:nrst:v", long_options, NULL)) != EOF) {
	const char *errstr;

	switch (ch) {
	    case 'd':
		rc = debug_parse(optarg);
		if ((int)DEBUG_PARSE_FAIL == rc)
		    return usage(1);
		debug = rc;
		break;

	    case 'f':
		config_file = strdup(optarg);
		break;

	    case 500:
		no_fallback = 1;
		break;

	    case 'h':
		return usage(0);

	    case 'i':	/* --ident=NAME */
		ident = optarg;
		break;

	    case 'l':
		loglevel = log_str2lvl(optarg);
		if (-1 == loglevel)
		    return usage(1);
		break;

	    case 'n':
		do_syslog--;
		foreground = 1;
		break;

	    case 501:	/* --disable-vifs */
		do_vifs = 0;
		break;

	    case 502:	/* --pidfile=NAME */
		pid_file = strdup(optarg);
		break;

	    case 'r':
		retry_forever++;
		break;

	    case 's':
		do_syslog++;
		break;

	    case 't':
		mrt_table_id = strtonum(optarg, 0, 999999999, &errstr);
		if (errstr) {
		    fprintf(stderr, "Table ID %s!\n", errstr);
		    return usage(1);
		}
		break;

	    case 'v':
		printf("%s\n", versionstring);
		return 0;

	    default:
		return usage(1);
	}
    }

    argc -= optind;
    if (argc > 0)
	return usage(1);

    if (geteuid() != 0)
	errx(1, "Need root privileges to start.");

    compose_paths();
    setlinebuf(stderr);

    if (debug) {
	char buf[350];

	debug_list(debug, buf, sizeof(buf));
	printf("debug level 0x%lx (%s)\n", debug, buf);
    }

    if (!foreground) {
	/* Detach from the terminal */
	if (fork())
	    exit(0);

	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);

	fd = open("/dev/null", O_RDWR, 0);
	if (fd >= 0) {
	    dup2(fd, STDIN_FILENO);
	    dup2(fd, STDOUT_FILENO);
	    dup2(fd, STDERR_FILENO);
	    close(fd);
	}
#ifdef SYSV
	setpgrp();
#else
#ifdef TIOCNOTTY
	fd = open("/dev/tty", 2);
	if (fd >= 0) {
	    (void)ioctl(fd, TIOCNOTTY, (char *)0);
	    close(fd);
	}
#else
	if (setsid() < 0)
	    perror("setsid");
#endif /* TIOCNOTTY */
#endif /* SYSV */
    } /* End of child process code */

    /*
     * Create directory for runtime files
     */
    if (-1 == mkdir(_PATH_PIMD_RUNDIR, 0755) && errno != EEXIST)
	err(1, "Failed creating %s directory for runtime files", _PATH_PIMD_RUNDIR);

    /*
     * Setup logging
     */
    log_init(do_syslog);
    logit(LOG_NOTICE, 0, "%s starting ...", versionstring);

    do_randomize();

    callout_init();
    init_igmp();
    init_pim();
    init_routesock(); /* Both for Linux netlink and BSD routing socket */
    init_pim_mrt();
    init_timers();
    ipc_init();

    /* Start up the log rate-limiter */
    resetlogging(NULL);

    /* TODO: check the kernel DVMRP/MROUTED/PIM support version */

    init_vifs();
    init_rp_and_bsr();   /* Must be after init_vifs() */
    add_static_rp();	 /* Must be after init_vifs() */
#ifdef RSRR
    rsrr_init();
#endif /* RSRR */

    sa.sa_handler = handle_signals;
    sa.sa_flags = 0;	/* Interrupt system calls */
    sigemptyset(&sa.sa_mask);
    sigaction(SIGALRM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);
    sigaction(SIGUSR2, &sa, NULL);

    /* schedule first timer interrupt */
    timer_setTimer(TIMER_INTERVAL, timer, NULL);

    if (pidfile(pid_file))
	warn("Cannot create pidfile");

    /*
     * Main receive loop.
     */
    while (1) {
	if (check_signals())
	    break;

	FD_ZERO(&fds);
	for (i = 0, nfds = 0; i < nhandlers; i++) {
	    FD_SET(ihandlers[i].fd, &fds);
	    if (ihandlers[i].fd >= nfds)
		nfds = ihandlers[i].fd + 1;
	}

	n = select(nfds, &fds, NULL, NULL, timeout(n));
	if (n < 0) {
	    if (errno != EINTR) /* SIGALRM is expected */
		logit(LOG_WARNING, errno, "select failed");
	    continue;
	}

	for (i = 0; n > 0 && i < nhandlers; i++) {
	    if (FD_ISSET(ihandlers[i].fd, &fds))
		ihandlers[i].func(ihandlers[i].fd);
	}
    }

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
 * Handle timeout queue.
 *
 * If select() + packet processing took more than 1 second,
 * or if there is a timeout pending, age the timeout queue.
 *
 * If not, collect usec in difftime to make sure that the
 * time doesn't drift too badly.
 *
 * If the timeout handlers took more than 1 second,
 * age the timeout queue again.  XXX This introduces the
 * potential for infinite loops!
 */
static struct timeval *timeout(int n)
{
    static struct timeval tv, difftime, curtime, lasttime;
    static int init = 1;
    struct timeval *result = NULL;
    int secs;

    secs = timer_nextTimer();
    if (secs != -1) {
	result = &tv;
	tv.tv_sec  = secs;
	tv.tv_usec = 0;
    }

    do {
	/*
	 * If select() timed out, then there's no other
	 * activity to account for and we don't need to
	 * call gettimeofday.
	 */
	if (n == 0) {
	    curtime.tv_sec = lasttime.tv_sec + secs;
	    curtime.tv_usec = lasttime.tv_usec;
	    n = -1; /* don't do this next time through the loop */
	} else {
	    gettimeofday(&curtime, NULL);
	    if (init) {
		init = 0;	/* First time only */
		lasttime = curtime;
	    }
	}

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

    return result;
}

/*
 * Performs all necessary functions to quit gracefully
 */
/* TODO: implement all necessary stuff */
static void cleanup(void)
{
    struct uvif *v;
    cand_rp_t *cand_rp;
    vifi_t vifi;

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

    for (cand_rp = cand_rp_list; cand_rp; cand_rp = cand_rp->next) {
	rp_grp_entry_t *rp_grp;
	mrtentry_t *mrt_rp;

	mrt_rp = cand_rp->rpentry->mrtlink;
	if (mrt_rp)
	    delete_mrtentry(mrt_rp);

	/* Just in case if that (*,*,RP) was deleted */
	mrt_rp = cand_rp->rpentry->mrtlink;

	for (rp_grp = cand_rp->rp_grp_next; rp_grp; rp_grp = rp_grp->rp_grp_next) {
	    grpentry_t *grp;
	    grpentry_t *grp_next;

	    for (grp = rp_grp->grplink; grp; grp = grp_next) {
		mrtentry_t *mrt_srcs_next;
		mrtentry_t *mrt_srcs;
		mrtentry_t *mrt_grp;

		grp_next = grp->rpnext;
		mrt_srcs = grp->mrtlink;

		mrt_grp = grp->grp_route;
		if (mrt_grp)
		    delete_mrtentry(mrt_grp);

		for (; mrt_srcs; mrt_srcs = mrt_srcs_next) {
		    /* routing entry */
		    mrt_srcs_next = mrt_srcs->grpnext;

		    delete_mrtentry(mrt_srcs);
		}
	    }
	}
    }

    delete_rp_list(&cand_rp_list, &grp_mask_list);
    delete_rp_list(&segmented_cand_rp_list, &segmented_grp_mask_list);

    restart(0);

    if (srclist)
	free(srclist);

    if (grplist)
	free(grplist);

    if (cand_rp_adv_message.buffer)
	free(cand_rp_adv_message.buffer);

    if (pim_recv_buf)
	free(pim_recv_buf);

    if (pim_send_buf)
	free(pim_send_buf);

    if (igmp_recv_buf)
	free(igmp_recv_buf);

    if (igmp_send_buf)
	free(igmp_send_buf);

    if (config_file)
	free(config_file);

    if (pid_file)
	free(pid_file);

    ipc_exit();
}


/*
 * Signal handler.  Take note of the fact that the signal arrived
 * so that the main loop can take care of it.
 */
static void handle_signals(int sig)
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
	/* Ignore, don't die, backwards compat. */
	break;
    }
}

static int check_signals(void)
{
    int dummy = SIGALRM;

    if (!sighandled)
	return 0;

    if (sighandled & GOT_SIGINT) {
	sighandled &= ~GOT_SIGINT;
	return 1;
    }

    if (sighandled & GOT_SIGHUP) {
	sighandled &= ~GOT_SIGHUP;
	restart(SIGHUP);
    }

    if (sighandled & GOT_SIGALRM) {
	sighandled &= ~GOT_SIGALRM;
	timer(&dummy);
    }

    return 0;
}

static void add_static_rp(void)
{
    struct rp_hold *rph = g_rp_hold;

    while (rph) {
	add_rp_grp_entry(&cand_rp_list, &grp_mask_list,
			 rph->address, 1, (uint16_t)0xffffff,
			 rph->group, rph->mask,
			 curr_bsr_hash_mask, curr_bsr_fragment_tag);
	rph = rph->next;
    }
}

static void del_static_rp(void)
{
    struct rp_hold *rph, *next;

    rph = g_rp_hold;
    while (rph) {
	next = rph->next;

	delete_rp(&cand_rp_list, &grp_mask_list, rph->address);
	free(rph);

	rph = next;
    }

    g_rp_hold = NULL;
}

/* TODO: not verified */
/*
 * Restart the daemon
 */
static void restart(int signo)
{
    /*
     * reset all the entries
     */
    /* TODO: delete?
       free_all_routes();
    */
    del_static_rp();
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

    /* Exit here if called at cleanup() */
    if (!signo)
	return;

    /*
     * start processing again
     */
    logit(LOG_NOTICE, 0, "%s restarting ...", versionstring);

    init_igmp();
    init_pim();
    init_routesock(); /* Both for Linux netlink and BSD routing socket */
    init_pim_mrt();
    init_vifs();
    add_static_rp();	 /* Must be after init_vifs() */

    /* Touch PID file to acknowledge SIGHUP */
    pidfile(pid_file);

    /* schedule timer interrupts */
    timer_setTimer(TIMER_INTERVAL, timer, NULL);
}

int daemon_restart(void *arg)
{
    (void)arg;
    restart(1);

    return 0;
}

int daemon_kill(void *arg)
{
    (void)arg;
    handle_signals(SIGTERM);

    return 0;
}

static void resetlogging(void *arg)
{
    static int disabled = 0;
    int nxttime = 60;

    (void)arg;

    if (!disabled && log_nmsgs >= LOG_MAX_MSGS) {
	syslog(LOG_WARNING, "logging too fast, shutting up for %d minutes",
	       LOG_SHUT_UP / 60);

	disabled = 1;
	nxttime = LOG_SHUT_UP;
    } else {
	if (disabled) {
	    syslog(LOG_NOTICE, "logging enabled again after rate limiting");
	    disabled = 0;
	}

	log_nmsgs = 0;
    }

    timer_setTimer(nxttime, resetlogging, NULL);
}

/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "ellemtel"
 *  c-basic-offset: 4
 * End:
 */
