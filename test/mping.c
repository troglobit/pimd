/*
 * Copyright (c) 2021  Joachim Wiberg <troglobit@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <err.h>
#include <errno.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <signal.h>
#include <stdlib.h>
#include <getopt.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/types.h>

#define MC_GROUP_DEFAULT "225.1.2.3"
#define MC_PORT_DEFAULT  4321
#define MC_TTL_DEFAULT   1

#define RESPONSES_MAX    100

#define MAX_BUF_LEN      1024
#define MAX_HOSTNAME_LEN 256

#define SENDER           's'
#define RECEIVER         'r'

#define INET_ADDRSTR_LEN 64
typedef struct sockaddr_storage inet_addr_t;

struct mping {
	char            version[4];

	unsigned char   type;
	unsigned char   ttl;

	struct in_addr  src_host;
	struct in_addr  dest_host;

	unsigned int    seq_no;
	pid_t           pid;

	struct timeval  tv;
	struct timeval  delay;
} mping;

struct resp {
	struct mping    pkt;
	struct timeval  send_time;
} *responses[RESPONSES_MAX];

int empty_response = 0;

/*#define BANDWIDTH 10000.0 */          /* bw in bytes/sec for mping */
#define BANDWIDTH 100.0                 /* bw in bytes/sec for mping */

unsigned int last_pkt_count = 0;        /* packets heard in last full second */
unsigned int curr_pkt_count = 0;        /* packets heard so far this second */

/* pointer to mping packet buffer */
struct mping *rcvd_pkt;

int   sd;                               /* socket descriptor */
pid_t pid;                              /* our process id */

struct sockaddr_in  mcaddr;
struct ip_mreqn     imr;

struct in_addr      myaddr;

struct timeval      start;              /* start time for sender */

/* counters and statistics variables */
int packets_sent = 0;
int packets_rcvd = 0;

double rtt_total = 0;
double rtt_max   = 0;
double rtt_min   = 999999999.0;

/* default command-line arguments */
char          arg_mcaddr[16] = MC_GROUP_DEFAULT;
int           arg_mcport     = MC_PORT_DEFAULT;
int           arg_count      = -1;
int           arg_timeout    = 5;
int           arg_deadline   = 0;
unsigned char arg_ttl        = MC_TTL_DEFAULT;

int debug = 0;
int quiet = 0;


void init_socket(int ifindex)
{
	int off = 0;
	int on = 1;

	/* create a UDP socket */
	if ((sd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
		err(1, "receive socket() failed");

	/* set reuse port to on to allow multiple binds per host */
	if ((setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))) < 0)
		err(1, "setsockopt() failed");

	if (setsockopt(sd, IPPROTO_IP, IP_MULTICAST_TTL, &arg_ttl, sizeof(arg_ttl)) < 0)
		err(1, "Failed setting IP_MULTICAST_TTL");

	if (setsockopt(sd, IPPROTO_IP, IP_MULTICAST_LOOP, &off, sizeof(off)) < 0)
		err(1, "Failed disabling IP_MULTICAST_LOOP");

	/* construct a multicast address structure */
	mcaddr.sin_family = AF_INET;
	mcaddr.sin_addr.s_addr = inet_addr(arg_mcaddr);
	mcaddr.sin_port = htons(arg_mcport);

	/* bind to multicast address to socket */
	if ((bind(sd, (struct sockaddr *)&mcaddr, sizeof(mcaddr))) < 0)
		err(1, "bind() failed");

	/* construct a IGMP join request structure */
	imr.imr_multiaddr.s_addr = inet_addr(arg_mcaddr);
	imr.imr_ifindex = ifindex;

	/* send an ADD MEMBERSHIP message via setsockopt */
	if ((setsockopt(sd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &imr, sizeof(imr))) < 0)
		err(1, "setsockopt() failed");

        if (setsockopt(sd, IPPROTO_IP, IP_MULTICAST_IF, &imr, sizeof(imr)))
                err(1, "Failed setting IP_MULTICAST_IF %d", ifindex);
}

static size_t strlencpy(char *dst, const char *src, size_t len)
{
	const char *p = src;
	size_t num = len;

	while (num > 0) {
		num--;
		if ((*dst++ = *src++) == 0)
			break;
	}

	if (num == 0 && len > 0)
		*dst = 0;

	return src - p - 1;
}

const char *inet_address(inet_addr_t *ss, char *buf, size_t len)
{
	struct sockaddr_in *sin;

#ifdef AF_INET6
	if (ss->ss_family == AF_INET6) {
		struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)ss;
		return inet_ntop(AF_INET6, &sin6->sin6_addr, buf, len);
	}
#endif

	sin = (struct sockaddr_in *)ss;
	return inet_ntop(AF_INET, &sin->sin_addr, buf, len);
}

/*
 * Find first interface that is not loopback, but is up, has link, and
 * multicast capable.
 */
static char *ifany(char *iface, size_t len)
{
	struct ifaddrs *ifaddr, *ifa;

	if (getifaddrs(&ifaddr) == -1)
		return NULL;

	for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
		int ifindex;

		if (!(ifa->ifa_flags & IFF_UP))
			continue;

		if (!(ifa->ifa_flags & IFF_RUNNING))
			continue;

		if (!(ifa->ifa_flags & IFF_MULTICAST))
			continue;

		ifindex = if_nametoindex(ifa->ifa_name);
                if (debug)
                        printf("Found iface %s, ifindex %d\n", ifa->ifa_name, ifindex);
		strncpy(iface, ifa->ifa_name, len);
		iface[len] = 0;
		break;
	}
	freeifaddrs(ifaddr);

	return iface;
}

/* The BSD's or SVR4 systems like Solaris don't have /proc/net/route */
static char *altdefault(char *iface, size_t len)
{
	char buf[256];
	FILE *fp;

	fp = popen("netstat -rn", "r");
	if (!fp)
		return NULL;

	while (fgets(buf, sizeof(buf), fp)) {
		char *token;

		if (strncmp(buf, "default", 7) && strncmp(buf, "0.0.0.0", 7))
			continue;

		token = strtok(buf, " \t\n");
		while (token) {
			if (if_nametoindex(token)) {
				strncpy(iface, token, len);
				pclose(fp);

				return iface;
			}

			token = strtok(NULL, " \t\n");
		}
	}

	pclose(fp);
	return NULL;
}

/* Find default outbound *LAN* interface, i.e. skipping tunnels */
char *ifdefault(char *iface, size_t len)
{
	uint32_t dest, gw, mask;
	char buf[256], ifname[17];
	char *ptr;
	FILE *fp;
	int rc, flags, cnt, use, metric, mtu, win, irtt;
	int best = 100000, found = 0;

	fp = fopen("/proc/net/route", "r");
	if (!fp) {
		ptr = altdefault(iface, len);
		if (!ptr)
			goto fallback;

		return ptr;
	}

	/* Skip heading */
	ptr = fgets(buf, sizeof(buf), fp);
	if (!ptr)
		goto end;

	while (fgets(buf, sizeof(buf), fp) != NULL) {
		rc = sscanf(buf, "%16s %X %X %X %d %d %d %X %d %d %d\n",
			   ifname, &dest, &gw, &flags, &cnt, &use, &metric,
			   &mask, &mtu, &win, &irtt);

		if (rc < 10 || !(flags & 1)) /* IFF_UP */
			continue;

		if (dest != 0 || mask != 0)
			continue;

		if (!iface[0] || !strncmp(iface, "tun", 3)) {
			if (metric >= best)
				continue;

			strncpy(iface, ifname, len);
			iface[len] = 0;
			best = metric;
			found = 1;

                        if (debug)
                                printf("Found default intefaces %s\n", iface);
		}
	}

end:
	fclose(fp);
	if (found)
		return iface;
fallback:
	return ifany(iface, len);
}

/* Find IP address of default outbound LAN interface */
int ifinfo(char *iface, inet_addr_t *addr, int family)
{
	struct ifaddrs *ifaddr, *ifa;
	char ifname[17] = { 0 };
	char buf[INET_ADDRSTR_LEN] = { 0 };
	int rc = -1;

	if (!iface || !iface[0])
		iface = ifdefault(ifname, sizeof(ifname));
	if (!iface)
		return -2;

	rc = getifaddrs(&ifaddr);
	if (rc == -1)
		return -3;

	rc = -1; /* Return -1 if iface with family is not found */
	for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
		if (!ifa->ifa_addr)
			continue;

		if (!(ifa->ifa_flags & IFF_MULTICAST))
			continue;

		if (family == AF_UNSPEC) {
			if (ifa->ifa_addr->sa_family != AF_INET &&
			    ifa->ifa_addr->sa_family != AF_INET6)
				continue;
		} else if (ifa->ifa_addr->sa_family != family)
			continue;

		if (iface && strcmp(iface, ifa->ifa_name))
			continue;

                if (debug)
                        printf("Found %s addr %s\n", ifa->ifa_name,
                               inet_address((inet_addr_t *)ifa->ifa_addr, buf, sizeof(buf)));
		*addr = *(inet_addr_t *)ifa->ifa_addr;
		rc = if_nametoindex(ifa->ifa_name);
                if (debug)
                        printf("iface %s, ifindex %d, addr %s\n", ifa->ifa_name, rc, buf);
		break;
	}
	freeifaddrs(ifaddr);

	return rc;
}

/* subtract sub from val and leave result in val */
void subtract_timeval(struct timeval *val, const struct timeval *sub)
{
	val->tv_sec  -= sub->tv_sec;
	val->tv_usec -= sub->tv_usec;
	if (val->tv_usec < 0) {
		val->tv_sec--;
		val->tv_usec += 1000000;
	}
}

/* return the timeval converted to a number of milliseconds */
double timeval_to_ms(const struct timeval *val)
{
	return val->tv_sec * 1000.0 + val->tv_usec / 1000.0;
}

static void clean_exit(int signo)
{
        (void)signo;

	if ((setsockopt(sd, IPPROTO_IP, IP_DROP_MEMBERSHIP, &imr, sizeof(imr))) < 0)
		err(1, "setsockopt() failed");

	close(sd);

	printf("\n--- %s mping statistics ---\n", arg_mcaddr);
	printf("%d packets transmitted, %d packets received\n", packets_sent, packets_rcvd);
	if (packets_rcvd == 0)
		printf("round-trip min/avg/max = NA/NA/NA ms\n");
	else
		printf("round-trip min/avg/max = %.3f/%.3f/%.3f ms\n",
		       rtt_min, (rtt_total / packets_rcvd), rtt_max);

        if (arg_count > 0 && arg_count > packets_rcvd)
                exit(1);
	exit(0);
}

void send_packet(struct mping *packet)
{
	int pkt_len = sizeof(struct mping);

	if ((sendto(sd, packet, pkt_len, 0, (struct sockaddr *)&mcaddr, sizeof(mcaddr))) != pkt_len)
		err(1, "sendto() sent incorrect number of bytes");

        packets_sent++;
}

void send_mping(int signo)
{
	static int seqno = 0;
	struct timeval now;

        (void)signo;

        /*
         * Tracks number of sent mpings.  If deadline mode is enabled we
         * ignore this exit and wait for arg_count number of replies or
         * deadline timeout.
         */
        if (arg_deadline) {
		if (arg_count > 0 && packets_rcvd >= arg_count)
                        clean_exit(0);

                gettimeofday(&now, NULL);
                subtract_timeval(&now, &start);
                if (now.tv_sec >= arg_deadline)
                        clean_exit(0);
	} else if (arg_count > 0 && seqno >= arg_count) {
		/* set another alarm call to exit in 5 second */
		signal(SIGALRM, clean_exit);
		alarm(arg_timeout);
		return;
	}

	gettimeofday(&now, NULL);
        strlencpy(mping.version, VERSION, sizeof(mping.version));
	mping.type             = SENDER;
	mping.ttl              = arg_ttl;
	mping.src_host.s_addr  = myaddr.s_addr;
	mping.dest_host.s_addr = inet_addr(arg_mcaddr);
	mping.seq_no           = htonl(seqno);
	mping.pid              = pid;
	mping.tv.tv_sec        = htonl(now.tv_sec);
	mping.tv.tv_usec       = htonl(now.tv_usec);
        mping.delay.tv_sec     = 0;
        mping.delay.tv_usec    = 0;

	send_packet(&mping);
	seqno++;

	/* set another alarm call to send in 1 second */
	signal(SIGALRM, send_mping);
	alarm(1);
}

int process_mping(char *packet, int len, unsigned char type)
{
	if (len < (int)sizeof(struct mping)) {
		if (debug)
			printf("Discarding packet: too small (%zu bytes)\n", strlen(packet));

		return -1;
	}

	rcvd_pkt = (struct mping *)packet;
	rcvd_pkt->seq_no        = ntohl(rcvd_pkt->seq_no);
	rcvd_pkt->tv.tv_sec     = ntohl(rcvd_pkt->tv.tv_sec);
	rcvd_pkt->tv.tv_usec    = ntohl(rcvd_pkt->tv.tv_usec);
	rcvd_pkt->delay.tv_sec  = ntohl(rcvd_pkt->delay.tv_sec);
	rcvd_pkt->delay.tv_usec = ntohl(rcvd_pkt->delay.tv_usec);

	if (strcmp(rcvd_pkt->version, VERSION)) {
		if (debug)
			printf("Discarding packet: version mismatch (%s)\n", rcvd_pkt->version);
		return -1;
	}

	curr_pkt_count++;

	if (rcvd_pkt->type != type) {
		if (debug) {
			switch (rcvd_pkt->type) {
			case SENDER:
				printf("Discarding sender packet\n");
				break;

			case RECEIVER:
				printf("Discarding receiver packet\n");
				break;

			case '?':
				printf("Discarding packet: unknown type(%c)\n", rcvd_pkt->type);
				break;
			}
		}

		return -1;
	}

	if (rcvd_pkt->type == RECEIVER) {
		if (rcvd_pkt->pid != pid) {
			if (debug)
				printf("Discarding packet: pid mismatch (%u/%u)\n", pid, rcvd_pkt->pid);
			return -1;
		}
	}

	packets_rcvd++;

	return 0;
}

void sender_listen_loop()
{
	while (1) {
                char recv_packet[MAX_BUF_LEN + 1] = { 0 };
                int len;

		if ((len = recvfrom(sd, recv_packet, MAX_BUF_LEN, 0, NULL, 0)) < 0) {
			if (errno == EINTR)
				continue; /* interrupt is ok */
                        err(1, "recvfrom() failed");
		}

		if (process_mping(recv_packet, len, RECEIVER) == 0) {
                        struct timeval now;
                        double actual_rtt;	/* rtt - send interval delay */
                        double rtt;		/* round trip time */

                        gettimeofday(&now, NULL);

			/* calculate round trip time in milliseconds */
			subtract_timeval(&now, &rcvd_pkt->tv);
			rtt = timeval_to_ms(&now);

			/* remove the backoff delay to determine actual rtt */
			subtract_timeval(&now, &rcvd_pkt->delay);
			actual_rtt = timeval_to_ms(&now);

			/* keep rtt total, min and max */
			rtt_total += actual_rtt;
			if (actual_rtt > rtt_max)
				rtt_max = actual_rtt;
			if (actual_rtt < rtt_min)
				rtt_min = actual_rtt;

			/* output received packet information */
                        if (!quiet) {
                                printf("%d bytes from %s: seqno=%u ttl=%d ",
                                       len, inet_ntoa(rcvd_pkt->src_host),
                                       rcvd_pkt->seq_no, rcvd_pkt->ttl);
                                printf("etime=%.1f ms atime=%.3f ms\n", rtt, actual_rtt);
                        }
		}
	}
}

void check_send(int i)
{
	struct timeval now;

	gettimeofday(&now, NULL);
	if ((responses[i]->send_time.tv_sec < now.tv_sec) ||
	    ((responses[i]->send_time.tv_sec == now.tv_sec) &&
	     (responses[i]->send_time.tv_usec <= now.tv_usec))) {
                size_t len = sizeof(responses[i]->pkt);

                if (!quiet)
                        printf("Reply to mping from %s bytes=%zu seqno=%u ttl=%d\n",
                               inet_ntoa(responses[i]->pkt.dest_host), len,
                               ntohl(responses[i]->pkt.seq_no), responses[i]->pkt.ttl);

		subtract_timeval(&now, &responses[i]->pkt.delay);
		responses[i]->pkt.delay.tv_sec = htonl(now.tv_sec);
		responses[i]->pkt.delay.tv_usec = htonl(now.tv_usec);

		send_packet(&responses[i]->pkt);
		free(responses[i]);
		responses[i] = NULL;
	}
}

void received_packet_count(int signo)
{
        (void)signo;

	/* update the packet count for the last full second */
	last_pkt_count = curr_pkt_count;
	curr_pkt_count = 0;

	/* check if the packets in the send buffer are ready to send */
	for (int i = empty_response; i < RESPONSES_MAX; i++) {
		if (responses[i] != NULL)
			check_send(i);
	}

	if (empty_response != 0) {
		for (int i = 0; i < empty_response; i++) {
			if (responses[i] != NULL)
				check_send(i);
		}
	}

        if (arg_count > 0 && packets_sent >= arg_count)
                exit(0);

	signal(SIGALRM, received_packet_count);
	alarm(1);
}

void receiver_listen_loop()
{
	printf("Listening on %s:%d\n", arg_mcaddr, arg_mcport);

	while (1) {
                char recv_packet[MAX_BUF_LEN + 1];
                int len;

		if ((len = recvfrom(sd, recv_packet, MAX_BUF_LEN, 0, NULL, 0)) < 0) {
			if (errno == EINTR)
                                continue; /* interrupt is ok */

                        err(1, "recvfrom() failed");
		}

		if (process_mping(recv_packet, len, SENDER) == 0) {
                        int i;

                        if (!quiet)
                                printf("Received mping from %s bytes=%d seqno=%u ttl=%d\n",
                                       inet_ntoa(rcvd_pkt->src_host), len,
                                       rcvd_pkt->seq_no, rcvd_pkt->ttl);

			i = empty_response;
			if (responses[i] != NULL) {
				if (debug)
					printf("Buffer full, packet dropped\n");
				continue;
			}

			responses[i] = (struct resp *)malloc(sizeof(struct resp));
                        if (responses[i] == NULL)
                                err(1, "Failed allocating response %d", i);

			strlencpy(responses[i]->pkt.version, VERSION, sizeof(responses[i]->pkt.version));
			responses[i]->pkt.type             = RECEIVER;
			responses[i]->pkt.ttl              = rcvd_pkt->ttl;
			responses[i]->pkt.src_host.s_addr  = myaddr.s_addr;
			responses[i]->pkt.dest_host.s_addr = rcvd_pkt->src_host.s_addr;
			responses[i]->pkt.seq_no           = htonl(rcvd_pkt->seq_no);
			responses[i]->pkt.pid              = rcvd_pkt->pid;
			responses[i]->pkt.tv.tv_sec        = htonl(rcvd_pkt->tv.tv_sec);
			responses[i]->pkt.tv.tv_usec       = htonl(rcvd_pkt->tv.tv_usec);

			gettimeofday(&responses[i]->send_time, NULL);

			responses[i]->pkt.delay = responses[i]->send_time;

                        /* send reply immediately */
                        received_packet_count(0);

			/* increment response buffer pointer */
			empty_response++;
			if (empty_response >= RESPONSES_MAX)
				empty_response = 0;
		}
	}
}

int usage(void)
{
	fprintf(stderr,
		"Usage:\n"
                "  mping [-dhqrsv] [-c COUNT] [-i IFNAME] [-p PORT] [-t TTL] [-w SEC] [-W SEC] [GROUP]\n"
                "\n"
		"Options:\n"
                "  -c COUNT    Stop after sending/receiving COUNT packets\n"
                "  -d          Debug messages\n"
		"  -h          This help text\n"
		"  -i IFNAME   Interface to use for sending/receiving\n"
		"  -p PORT     Multicast port to listen/send to, default %d\n"
                "  -q          Quiet output, only startup and and summary lines\n"
		"  -r          Receiver mode, default\n"
                "  -s          Sender mode\n"
		"  -t TTL      Multicast time to live to send, default %d\n"
		"  -v          Show program version and contact information\n"
                "  -w DEADLINE Timeout before exiting, waiting for COUNT replies\n"
                "  -W TIMEOUT  Time to wait for a response, in seconds, default 5\n"
                "\n"
                "Defaults to use multicast group %s, UDP dst port %d, outbound\n"
                "interface is chosen by the routing table, unless -i IFNAME\n",
                MC_PORT_DEFAULT, MC_TTL_DEFAULT, MC_GROUP_DEFAULT, MC_PORT_DEFAULT);

	return 0;
}

int main(int argc, char **argv)
{
	char *iface = NULL;
        inet_addr_t addr;
	char ifname[16];
        int mode = 'r';
	int c;

	while ((c = getopt(argc, argv, "c:dh?i:p:qrst:vW:w:")) != -1) {
		switch (c) {
                case 'c':
                        arg_count = atoi(optarg);
                        break;

		case 'd':
			debug = 1;
			break;

		case 'i':
			strlencpy(ifname, optarg, sizeof(ifname));
			iface = ifname;
			break;

		case 'p':
			arg_mcport = atoi(optarg);
			break;

                case 'q':
                        quiet = 1;
                        break;

		case 'r':
                        mode = 'r';
			break;

		case 's':
                        mode = 's';
			break;

		case 't':
			arg_ttl = atoi(optarg);
			break;

		case 'v':
			printf("mping version %s\n"
                               "\n"
                               "Bug report address: https://github.com/troglobit/mping/issues\n"
                               "Project homepage:   https://github.com/troglobit/mping/\n", VERSION);
			return 0;

                case 'W':
                        arg_timeout = atoi(optarg);
                        break;

                case 'w':
                        arg_deadline = atoi(optarg);
                        break;

		case '?':
		case 'h':
		default:
			return usage();
		}
	}

        if (optind < argc)
                strlencpy(arg_mcaddr, argv[optind], sizeof(arg_mcaddr));

	pid = getpid();
	init_socket(ifinfo(iface, &addr, AF_INET));
        if (addr.ss_family == AF_INET) {
                struct sockaddr_in *sin = (struct sockaddr_in *)&addr;
                myaddr = sin->sin_addr;
        }

	if (mode == 's') {
		gettimeofday(&start, NULL);
		printf("MPING %s:%d (ttl %d)\n", arg_mcaddr, arg_mcport, arg_ttl);

		signal(SIGINT, clean_exit);
		signal(SIGALRM, send_mping);
		send_mping(SIGALRM);

		sender_listen_loop();
	} else {
		for (int i = 0; i < RESPONSES_MAX; i++)
			responses[i] = NULL;

		signal(SIGALRM, received_packet_count);
		alarm(1);

		receiver_listen_loop();
	}

	return 0;
}

/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */
