> $Id: README.config,v 1.4 2002/06/13 17:39:19 pavlin Exp $

This is file contains help for configuring and using pimd, the
PIM-SM/SSM multicast daemon.  For the latest pimd version, see
<https://github.com/troglobit/pimd>

There is an older Japanese version of this file, it could need
some updating help, in the meantime, see [README.config.jp][jp]

**NOTE:** currently, this file is very incomplete.  If something is
          missing and/or unclear, email the current maintainer of pimd
          or file an issue in the GitHub issue tracker.

## Using GRE Tunnels for Multicast Routing

Based on information contributed by Hiroyuki Komatsu
<mailto:komatsu@taiyaki.org>

If you are configuring the particular gre interfaces for the first time,
ignore the errors after `ip link set gre1 down` and `ip tunnel del gre1`

On Linux (Debian) try the following:

### GRE Tunnel Between Two Machines

This sets up a GRE tunnel between hosts 11.11.11.11 and 33.33.33.33.

	Physical interfaces:    [11.11.11.11]         [33.33.33.33]
	GRE tunnel:              22.22.22.11 <-------> 22.22.22.33
	
	==== host 11.11.11.11 (GRE interface 22.22.22.11)
	echo 1 > /proc/sys/net/ipv4/ip_forward
	echo 0 > /proc/sys/net/ipv4/conf/all/rp_filter
	ip link set gre1 down
	ip tunnel del gre1
	ip tunnel add gre1 mode gre remote 33.33.33.33 local 11.11.11.11 ttl 127
	ip addr add 22.22.22.11/24 peer 22.22.22.33/24 dev gre1
	ip link set gre1 up multicast on
	
	==== host 33.33.33.33 (GRE interface 22.22.22.33)
	echo 1 > /proc/sys/net/ipv4/ip_forward
	echo 0 > /proc/sys/net/ipv4/conf/all/rp_filter
	ip link set gre1 down
	ip tunnel del gre1
	ip tunnel add gre1 mode gre remote 11.11.11.11 local 33.33.33.33 ttl 127
	ip addr add 22.22.22.33/24 peer 22.22.22.11/24 dev gre1
	ip link set gre1 up multicast on


### GRE Tunnels with Three Machines

> STOP!!!STOP!!!STOP!!!STOP!!!STOP!!!STOP!!!STOP!!!STOP!!!STOP!!!STOP!!!
> 
> IF YOU ADD MORE THAN TWO GRE TUNNELS IN A CHAIN, IT IS VERY EASY TO CREATE
> UNICAST ROUTING LOOPS, AND THIS MAY LEAD TO MULTICAST ROUTING LOOPS.
> MULTICAST ROUTING LOOP IS A DISASTER THAT MAY BRING YOUR WHOLE NETWORK DOWN.
> BEFORE ATTEMPTING THIS CONFIGURATION, MAKE SURE YOU UNDERSTAND VERY WELL
> WHAT YOU ARE DOING, AND WHAT MAY HAPPEN.
> IF YOU ARE READING THIS, THE CHANCES ARE THAT YOU DON'T KNOW, SO THINK AGAIN!!
> 
> THINK!!!THINK!!!THINK!!!THINK!!!THINK!!!THINK!!!THINK!!!THINK!!!THINK!!!

	Physical interfaces: [11.11.11.11]    [33.33.33.33]    [55.55.55.55]
	GRE tunnels:          22.22.22.11 <--> 22.22.22.33
                                           44.44.44.33 <--> 44.44.44.55
	
	==== host 33.33.33.33 (GRE interfaces 22.22.22.33 and 44.44.44.33)
	echo 1 > /proc/sys/net/ipv4/ip_forward
	echo 0 > /proc/sys/net/ipv4/conf/all/rp_filter
	ip tunnel add gre1 mode gre remote 11.11.11.11 local 33.33.33.33 ttl 127
	ip addr add 22.22.22.33/24 peer 22.22.22.11/24 dev gre1
	ip link set gre1 up multicast on
	ip tunnel add gre2 mode gre remote 55.55.55.55 local 33.33.33.33 ttl 127
	ip addr add 44.44.44.33/24 peer 44.44.44.55/24 dev gre2
	ip link set gre2 up multicast on
	
	==== host 55.55.55.55 (GRE interface 44.44.44.55)
	echo 1 > /proc/sys/net/ipv4/ip_forward
	echo 0 > /proc/sys/net/ipv4/conf/all/rp_filter
	ip tunnel add gre1 mode gre remote 33.33.33.33 local 55.55.55.55 ttl 127
	ip addr add 44.44.44.55/24 peer 44.44.44.33/24 dev gre1
	ip link set gre1 up multicast on
	route add -net 22.22.22.0 netmask 255.255.255.0 gw 44.44.44.33 gre1
	
	==== host 11.11.11.11 (GRE interface 22.22.22.11)
	echo 1 > /proc/sys/net/ipv4/ip_forward
	echo 0 > /proc/sys/net/ipv4/conf/all/rp_filter
	ip tunnel add gre1 mode gre remote 33.33.33.33 local 11.11.11.11 ttl 127
	ip addr add 22.22.22.11/24 peer 22.22.22.33/24 dev gre1
	ip link set gre1 up multicast on
	route add -net 44.44.44.0 netmask 255.255.255.0 gw 22.22.22.33 gre1


## The pimd.conf FAQ

For a complete list of all available options, see `pimd.conf` and the man page.

1. How to disable pimd being Cand-RP?

   Comment-out the `rp-candidate` and `group-prefix` lines in `pimd.conf`

2. How to disable pimd being Cand-BSR?

   Comment-out the `bsr-candidate` line in `pimd.conf`

3. How to prevent a prefix of multicast addresses being routed through
   my multicast router?

   If you want to scope, say, prefixes 238.0.0.0/8 and 239.0.0.0/8, add
   the following lines to `pimd.conf`:

        phyint eth1 scoped 238.0.0.0 masklen 8
        phyint eth1 scoped 239.0.0.0 masklen 8

4. How to create a scope zone and stop multicast packets for some multicast
   prefix being propagated beyond the boundary of my network?

   Add scoping filters on your border routers for each prefix you want
   to scope. E.g.:

        phyint eth1 scoped 239.0.0.0 masklen 8

[jp]: https://github.com/troglobit/pimd/blob/master/README.config.jp

<!--
  -- Local Variables:
  -- mode: markdown
  -- End:
  -->
