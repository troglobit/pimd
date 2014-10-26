/*
 * The mrouted program is covered by the license in the accompanying file
 * named "LICENSE.mrouted". Use of the mrouted program represents acceptance of
 * the terms and conditions listed in that file.
 *
 * The mrouted program is COPYRIGHT 1989 by The Board of Trustees of
 * Leland Stanford Junior University.
 *
 *
 * inet.c,v 3.8.4.1 1997/01/29 19:49:33 fenner Exp
 */
#define C(x)    ((x) & 0xff)

#include "defs.h"


/*
 * Exported variables.
 */
char s1[MAX_INET_BUF_LEN];		/* buffers to hold the string representations  */
char s2[MAX_INET_BUF_LEN];		/* of IP addresses, to be passed to inet_fmt() */
char s3[MAX_INET_BUF_LEN];
char s4[MAX_INET_BUF_LEN];


/*
 * Verify that a given IP address is credible as a host address.
 * (Without a mask, cannot detect addresses of the form {subnet,0} or
 * {subnet,-1}.)
 */
int inet_valid_host(uint32_t naddr)
{
    uint32_t addr;

    addr = ntohl(naddr);

    return !(IN_MULTICAST(addr) ||
	     IN_BADCLASS (addr) ||
	     (addr & 0xff000000) == 0);
}

/*
 * Verify that a given netmask is plausible;
 * make sure that it is a series of 1's followed by
 * a series of 0's with no discontiguous 1's.
 */
int inet_valid_mask(uint32_t mask)
{
    if (~(((mask & -mask) - 1) | mask) != 0) {
        /* Mask is not contiguous */
        return FALSE;
    }

    return TRUE;
}

/*
 * Verify that a given subnet number and mask pair are credible.
 *
 * With CIDR, almost any subnet and mask are credible.  mrouted still
 * can't handle aggregated class A's, so we still check that, but
 * otherwise the only requirements are that the subnet address is
 * within the [ABC] range and that the host bits of the subnet
 * are all 0.
 */
int inet_valid_subnet(uint32_t nsubnet, uint32_t nmask)
{
    uint32_t subnet, mask;

    subnet = ntohl(nsubnet);
    mask   = ntohl(nmask);

    if ((subnet & mask) != subnet)
        return FALSE;

    if (subnet == 0)
        return mask == 0;

    if (IN_CLASSA(subnet)) {
        if (mask < 0xff000000 ||
            (subnet & 0xff000000) == 0x7f000000 ||
            (subnet & 0xff000000) == 0x00000000)
	    return FALSE;
    }
    else if (IN_CLASSD(subnet) || IN_BADCLASS(subnet)) {
        /* Above Class C address space */
        return FALSE;
    }
    if (subnet & ~mask) {
        /* Host bits are set in the subnet */
        return FALSE;
    }
    if (!inet_valid_mask(mask)) {
        /* Netmask is not contiguous */
        return FALSE;
    }

    return TRUE;
}


/*
 * Convert an IP address in uint32_t (network) format into a printable string.
 */
char *inet_fmt(uint32_t addr, char *s, size_t len)
{
    uint8_t *a;

    a = (uint8_t *)&addr;
    snprintf(s, len, "%u.%u.%u.%u", a[0], a[1], a[2], a[3]);

    return s;
}

/*
 * Convert the printable string representation of an IP address into the
 * uint32_t (network) format.  Return 0xffffffff on error.  (To detect the
 * legal address with that value, you must explicitly compare the string
 * with "255.255.255.255".)
 * The return value is in network order.
 */
uint32_t inet_parse(char *s, int n)
{
    uint32_t a = 0;
    u_int a0 = 0, a1 = 0, a2 = 0, a3 = 0;
    int i;
    char c;

    i = sscanf(s, "%u.%u.%u.%u%c", &a0, &a1, &a2, &a3, &c);
    if (i < n || i > 4 || a0 > 255 || a1 > 255 || a2 > 255 || a3 > 255)
        return 0xffffffff;

    ((uint8_t *)&a)[0] = a0;
    ((uint8_t *)&a)[1] = a1;
    ((uint8_t *)&a)[2] = a2;
    ((uint8_t *)&a)[3] = a3;

    return a;
}


/*
 * inet_cksum extracted from:
 *			P I N G . C
 *
 * Author -
 *	Mike Muuss
 *	U. S. Army Ballistic Research Laboratory
 *	December, 1983
 * Modified at Uc Berkeley
 *
 * (ping.c) Status -
 *	Public Domain.  Distribution Unlimited.
 *
 *			I N _ C K S U M
 *
 * Checksum routine for Internet Protocol family headers (C Version)
 *
 */
int inet_cksum(uint16_t *addr, u_int len)
{
        int sum = 0;
        int nleft = (int)len;
        uint16_t *w = addr;
        uint16_t answer = 0;

        /*
         *  Our algorithm is simple, using a 32 bit accumulator (sum),
         *  we add sequential 16 bit words to it, and at the end, fold
         *  back all the carry bits from the top 16 bits into the lower
         *  16 bits.
         */
        while (nleft > 1)  {
                sum += *w++;
                nleft -= 2;
        }

        /* mop up an odd byte, if necessary */
        if (nleft == 1) {
                *(uint8_t *) (&answer) = *(uint8_t *)w ;
                sum += answer;
        }

        /*
         * add back carry outs from top 16 bits to low 16 bits
         */
        sum = (sum >> 16) + (sum & 0xffff);	/* add hi 16 to low 16 */
        sum += (sum >> 16);			/* add carry */
        answer = ~sum;				/* truncate to 16 bits */

        return answer;
}

/*
 * Called by following netname() to create a mask specified network address.
 */
void trimdomain(char *cp)
{
    static char domain[MAXHOSTNAMELEN + 1];
    static int first = 1;
    char *s;

    if (first) {
        first = 0;
        if (gethostname(domain, MAXHOSTNAMELEN) == 0 &&
            (s = strchr(domain, '.')))
           (void) strlcpy(domain, s + 1, sizeof(domain));
        else
            domain[0] = 0;
    }

    if (domain[0]) {
        while ((cp = strchr(cp, '.'))) {
            if (!strcasecmp(cp + 1, domain)) {
                *cp = 0;
                break;
            }
	    cp++;
        }
    }
}

static uint32_t forgemask(uint32_t a)
{
    uint32_t m;

    if (IN_CLASSA(a))
        m = IN_CLASSA_NET;
    else if (IN_CLASSB(a))
        m = IN_CLASSB_NET;
    else
        m = IN_CLASSC_NET;

    return (m);
}

static void domask(char *dst, size_t len, uint32_t addr, uint32_t mask)
{
    int b, i;

    if (!mask || (forgemask(addr) == mask)) {
        *dst = '\0';
        return;
    }

    i = 0;
    for (b = 0; b < 32; b++) {
        if (mask & (1 << b)) {
            int bb;

            i = b;
            for (bb = b+1; bb < 32; bb++) {
                if (!(mask & (1 << bb))) {
                    i = -1; /* noncontig */
                    break;
                }
	    }
            break;
        }
    }

    if (i == -1)
        snprintf(dst, len, "&0x%x", mask);
    else
        snprintf(dst, len, "/%d", 32 - i);
}

/*
 * Return the name of the network whose address is given.
 * The address is assumed to be that of a net or subnet, not a host.
 */
char *netname(uint32_t addr, uint32_t mask)
{
    static char line[MAXHOSTNAMELEN + 4];
    uint32_t omask;
    uint32_t i;

    i = ntohl(addr);
    omask = mask = ntohl(mask);
    if ((i & 0xffffff) == 0)
        snprintf(line, sizeof(line), "%u", C(i >> 24));
    else if ((i & 0xffff) == 0)
        snprintf(line, sizeof(line), "%u.%u", C(i >> 24) , C(i >> 16));
    else if ((i & 0xff) == 0)
        snprintf(line, sizeof(line), "%u.%u.%u", C(i >> 24), C(i >> 16), C(i >> 8));
    else
        snprintf(line, sizeof(line), "%u.%u.%u.%u", C(i >> 24),
                C(i >> 16), C(i >> 8), C(i));
    domask(line + strlen(line), sizeof(line) - strlen(line), i, omask);

    return line;
}

/**
 * Local Variables:
 *  version-control: t
 *  indent-tabs-mode: t
 *  c-file-style: "ellemtel"
 *  c-basic-offset: 4
 * End:
 */
