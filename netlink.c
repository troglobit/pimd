/*
 * Fred Griffoul <griffoul@ccrle.nec.de> sent me this file to use
 * when compiling pimd under Linux.
 *
 * There was no copyright message or author name, so I assume he was the
 * author, and deserves the copyright/credit for it: 
 *
 * COPYRIGHT/AUTHORSHIP by Fred Griffoul <griffoul@ccrle.nec.de>
 * (until proven otherwise).
 */

#include <sys/param.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdlib.h>
#include <strings.h>
#include <paths.h>
#include "defs.h"

#include <linux/rtnetlink.h>

int routing_socket = -1;
static uint32_t pid; /* pid_t, but /usr/include/linux/netlink.h says __u32 ... */
static uint32_t seq;

static int getmsg(struct rtmsg *rtm, int msglen, struct rpfctl *rpf);

static int addattr32(struct nlmsghdr *n, size_t maxlen, int type, uint32_t data)
{
    int len = RTA_LENGTH(4);
    struct rtattr *rta;

    if (NLMSG_ALIGN(n->nlmsg_len) + len > maxlen)
	return -1;

    rta = (struct rtattr *)(((char *)n) + NLMSG_ALIGN(n->nlmsg_len));
    rta->rta_type = type;
    rta->rta_len = len;
    memcpy(RTA_DATA(rta), &data, 4);
    n->nlmsg_len = NLMSG_ALIGN(n->nlmsg_len) + len;

    return 0;
}

static int parse_rtattr(struct rtattr *tb[], int max, struct rtattr *rta, int len)
{
    while (RTA_OK(rta, len)) {
	if (rta->rta_type <= max)
	    tb[rta->rta_type] = rta;
	rta = RTA_NEXT(rta, len);
    }

    if (len)
	logit(LOG_WARNING, 0, "NETLINK: Deficit in rtattr %d", len);

    return 0;
}

/* open and initialize the routing socket */
int init_routesock(void)
{
    socklen_t addr_len;
    struct sockaddr_nl local;

    routing_socket = socket(PF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (routing_socket < 0) {
	logit(LOG_ERR, errno, "Failed creating netlink socket");
	return -1;
    }

    memset(&local, 0, sizeof(local));
    local.nl_family = AF_NETLINK;
    local.nl_groups = 0;
    if (bind(routing_socket, (struct sockaddr *)&local, sizeof(local)) < 0) {
	logit(LOG_ERR, errno, "Failed binding to netlink socket");
	return -1;
    }

    addr_len = sizeof(local);
    if (getsockname(routing_socket, (struct sockaddr *)&local, &addr_len) < 0) {
	logit(LOG_ERR, errno, "Failed netlink getsockname");
	return -1;
    }

    if (addr_len != sizeof(local)) {
	logit(LOG_ERR, 0, "Invalid netlink addr len.");
	return -1;
    }

    if (local.nl_family != AF_NETLINK) {
	logit(LOG_ERR, 0, "Invalid netlink addr family.");
	return -1;
    }

    pid = local.nl_pid;
    seq = time(NULL);

    return 0;
}

/* get the rpf neighbor info */
int k_req_incoming(uint32_t source, struct rpfctl *rpf)
{
    int l, rlen;
    char buf[512];
    struct nlmsghdr *n = (struct nlmsghdr *)buf;
    struct rtmsg *r = NLMSG_DATA(n);
    struct sockaddr_nl addr;
    
    rpf->source.s_addr = source;
    rpf->iif = ALL_VIFS;
    rpf->rpfneighbor.s_addr = 0;
    
    n->nlmsg_type = RTM_GETROUTE;
    n->nlmsg_flags = NLM_F_REQUEST;
    n->nlmsg_len = NLMSG_LENGTH(sizeof(*r));
    n->nlmsg_pid = pid;
    n->nlmsg_seq = ++seq;
    
    memset(r, 0, sizeof(*r));
    r->rtm_family = AF_INET;
    r->rtm_dst_len = 32;
    addattr32(n, sizeof(buf), RTA_DST, rpf->source.s_addr);
#ifdef CONFIG_RTNL_OLD_IFINFO
    r->rtm_optlen = n->nlmsg_len - NLMSG_LENGTH(sizeof(*r));
#endif
    addr.nl_family = AF_NETLINK;
    addr.nl_groups = 0;
    addr.nl_pid = 0;
    
    if (!IN_LINK_LOCAL_RANGE(rpf->source.s_addr)) {
	IF_DEBUG(DEBUG_RPF)
	    logit(LOG_DEBUG, 0, "NETLINK: ask path to %s", inet_fmt(rpf->source.s_addr, s1, sizeof(s1)));
    }

    while ((rlen = sendto(routing_socket, buf, n->nlmsg_len, 0, (struct sockaddr *)&addr, sizeof(addr))) < 0) {
	if (errno == EINTR)
	    continue;		/* Received signal, retry syscall. */

	logit(LOG_WARNING, errno, "Error writing to routing socket");
	return FALSE;
    }

    do {
	socklen_t alen = sizeof(addr);

	l = recvfrom(routing_socket, buf, sizeof(buf), 0, (struct sockaddr *)&addr, &alen);
	if (l < 0) {
	    if (errno == EINTR)
		continue;		/* Received signal, retry syscall. */

	    logit(LOG_WARNING, errno, "Error writing to routing socket");
	    return FALSE;
	}
    } while (n->nlmsg_seq != seq || n->nlmsg_pid != pid);
    
    if (n->nlmsg_type != RTM_NEWROUTE) {
	if (!IN_LINK_LOCAL_RANGE(rpf->source.s_addr)) {
	    if (n->nlmsg_type != NLMSG_ERROR)
		logit(LOG_WARNING, 0, "netlink: wrong answer type %d", n->nlmsg_type);
	    else
		logit(LOG_WARNING, -(*(int*)NLMSG_DATA(n)), "netlink get_route");
	}

	return FALSE;
    }

    return getmsg(NLMSG_DATA(n), l - sizeof(*n), rpf);
}

static int getmsg(struct rtmsg *rtm, int msglen, struct rpfctl *rpf)
{
    int ifindex;
    vifi_t vifi;
    struct uvif *v;
    struct rtattr *rta[RTA_MAX + 1];
    
    if (!rpf) {
	logit(LOG_WARNING, 0, "Missing rpf pointer to netlink.c:getmsg()!");
	return FALSE;
    }

    rpf->iif = NO_VIF;
    rpf->rpfneighbor.s_addr = INADDR_ANY;

    if (rtm->rtm_type == RTN_LOCAL) {
	IF_DEBUG(DEBUG_RPF)
	    logit(LOG_DEBUG, 0, "NETLINK: local address");

	if ((rpf->iif = local_address(rpf->source.s_addr)) != MAXVIFS) {
	    rpf->rpfneighbor.s_addr = rpf->source.s_addr;

	    return TRUE;
	}

	return FALSE;
    }
    
    if (rtm->rtm_type != RTN_UNICAST) {
	IF_DEBUG(DEBUG_RPF)
	    logit(LOG_DEBUG, 0, "NETLINK: route type is %d", rtm->rtm_type);
	return FALSE;
    }
    
    memset(rta, 0, sizeof(rta));
    parse_rtattr(rta, RTA_MAX, RTM_RTA(rtm), msglen - sizeof(*rtm));
    
    if (!rta[RTA_OIF]) {
	logit(LOG_WARNING, 0, "NETLINK: no outbound interface");
	return FALSE;
    }

    /* Get ifindex of outbound interface */
    ifindex = *(int *)RTA_DATA(rta[RTA_OIF]);

    for (vifi = 0, v = uvifs; vifi < numvifs; ++vifi, ++v) {
	if (v->uv_ifindex == ifindex)
	    break;
    }

    if (vifi >= numvifs) {
	logit(LOG_WARNING, 0, "NETLINK: ifindex=%d, but no vif", ifindex);
	return FALSE;
    }

    /* Found inbound interface in vifi */
    rpf->iif = vifi;

    IF_DEBUG(DEBUG_RPF)
	logit(LOG_DEBUG, 0, "NETLINK: vif %d, ifindex=%d", vifi, ifindex);

    if (rta[RTA_GATEWAY]) {
	uint32_t gw = *(uint32_t *)RTA_DATA(rta[RTA_GATEWAY]);

	IF_DEBUG(DEBUG_RPF)
	    logit(LOG_DEBUG, 0, "NETLINK: gateway is %s", inet_fmt(gw, s1, sizeof(s1)));
	rpf->rpfneighbor.s_addr = gw;
    } else {
	rpf->rpfneighbor.s_addr = rpf->source.s_addr;
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
