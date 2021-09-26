PIM-SM/SSM Multicast Routing for UNIX
=====================================
[![License Badge][]][License] [![GitHub Status][]][GitHub] [![Coverity Status][]][Coverity Scan]

Table of Contents
-----------------

* [Introduction](#introduction)
* [Configuration](#configuration)
* [Running pimd](#running-pimd)
* [Troubleshooting Checklist](#troubleshooting-checklist)
* [Monitoring](#monitoring)
* [Large Setups](#large-setups)
* [Build & Install](#build--install)
* [Building from GIT](#building-from-git)
* [Contributing](#contributing)
* [Origin & References](#origin--references)


Introduction
------------

pimd is a lightweight, stand-alone PIM-SM/SSM multicast routing daemon
available under the free [3-clause BSD license][License].  This is the
restored original version from University of Southern California, by
Ahmed Helmy, Rusty Eddy and Pavlin Ivanov Radoslavov.

Today pimd is maintained at [GitHub][].  This is the preferred way to
download releases, access the GIT sources, report bugs, and send patches
or pull requests.  Official release tarballs at the [homepage][] and at
the GitHub project's release directory.

pimd is developed on Linux and should work as-is out of the box on all
major distributions.  Other UNIX variants; NetBSD, FreeBSD, and Illumos,
may also work, but do not receive the same amount of testing.

pimd ships with a useful `pimctl` tool, compatible with all PIM daemons
from the same family: pimd, pimd-dense, pim6sd. It can be a very helpful
little tool when debugging and learning PIM setups.  The pimctl API is
documented in the file `src/ipc.c`, in case you want to use `socat` to
talk to pimd over its UNIX domain socket:

    echo "help" |socat - UNIX-CONNECT:/var/run/pimd.sock

For a summary of changes for each release, see the [ChangeLog][].


Configuration
-------------

The configuration is kept in the file `/etc/pimd.conf`, the order of
the statements are in some cases important.

PIM-SM is a designed to be a *protocol independent* multicast routing
protocol.  As such it relies on unicast protocols like, e.g, OSPF, RIP,
or static routing entries to figure out the reverse path to multicast
sources.  This information is necessary in setups with more than one
route between a multicast sender and a receiver to figure out which PIM
router should be the active forwarder.

However, pimd currently cannot retrieve the unicast routing distance
(preference) and metric of routes from the system, not from the kernel
nor a route manager like zebra.  Hence, pimd currently needs to be setup
statically on each router using the desired distance and metric for each
active interface.  If either the distance and/or the metric is missing
in an interface configuration, the following two defaults will be used:

    default-route-distance   <1-255>     default: 101
    default-route-metric     <1-1024>    default: 1024

By default pimd starts up on all interfaces it can find, using the above
defaults.  To configure individual interfaces use:

    phyint <address | ifname> ...

You can reference the interface via either its local IPv4 address or
its name, e.g., eth0.  Some common interface settings are:

   * `disable`: Disable pimd on this interface, i.e., do not send or
     listen for PIM-SM traffic

   * `dr-priority <1-4294967294>`: The DR Priority option, sent in all
     all PIM Hello messages.  Used instead of the IP address in all DR
     elections, if all PIM routers in LAN advertise it.  The higher, the
     better, default 1.

   * `distance <1-255>`: The interface's admin distance value (also
     confusingly referred to as *metric preference* in the RFC) in PIM
     Assert messages.  Used with `metric` to elect the active multicast
     forwarding router.  Defaults to `default-route-distance`

   * `metric <1-1024>`: The cost for traversing this router.  Used with
     the `preference` value above. Defaults to `default-route-metric`

More interface settings are available, see the pimd(8) manual page for
the full details.

The most notable feature of PIM-SM is that multicast is distributed from
so called Rendezvous Points (RP).  Each RP handles distribution of one
or more multicast groups, pimd can be configured to advertise itself as
a candidate RP `rp-candidate`, and request to be static RP `rp-address`
for one or more multicast groups.

    rp-address <address> [<group>[/<LENGTH> | masklen <LENGTH]

The `rp-address` setting is the same as the Cisco `ip pim rp-address`
setting to configure static Rendezvous Points.  The first argument can
be an IPv4 address or a multicast group address.  The default group and
prefix length is 224.0.0.0/16.  Static RP's always have priority 1.

    rp-candidate [address | ifname] [interval <10-16383>] [priority <0-255>]

The Rendezvous Point candidate, or CRP, setting is the same as the Cisco
`ip pim rp-candidate` setting.  Use it to control which interface that
should be used in RP elections.

   * `address | ifname`: Optional local IPv4 address, or interface name
     to acquire address from.  The default is to use the highest active
     IP address.

   * `interval <10-16383>`: The CRP advertisement interval, in seconds.
     Default: 60 seconds

   * `priority <0-255>`: How important this CRP is compared to others.
     The lower the value here, the more important the CRP.  Like Cisco,
     pimd defaults to priority 0 when this is left out

In the CRP messages sent out by pimd, one or more multicast groups can
be advertised using the following syntax.

    group-prefix <group>[</LENGTH> | masklen <LENGTH>]

Each `group-prefix` setting defines one multicast group and an optional
mask length, which defaults to 16 if left out.  A maximum of 255
multicast group prefix records is possible for the CRP.

To keep track of all Rendezvous Points in a PIM-SM domain there exists a
feature called *Bootstrap Router*.  The elected BSR in a PIM-SM domain
periodically announces the RP set in Bootstrap messages.  For details on
PIM BSR operation, see [RFC 5059](http://tools.ietf.org/search/rfc5059).

    bsr-candidate [address | ifname] [priority <0-255>] [interval <10-26214>]

The configuration of a Candidate BootStrap Router (CBSR) is very similar
to that of CRP.  If either the address or the interface name is left out
`pimd` uses the highest active IP address.  If the priority is omitted,
`pimd` (like Cisco) defaults to priority 0.  If the interval is omitted,
it defaults to the RFC value of 60 seconds.

In a PIM-SM domain there can be two, or more, paths from a designated
router (DR) for a multicast sender to reach a receiver.  When receivers
begin joining multicast groups all data is received via the *shared
tree* (RPT) from each Rendezvous Point (RP).  This is often not an
optimal route, so when the volume starts exceeding a configurable
threshold, on either the last-hop router or the RP itself, the router
will attempt to switch to the *shortest path tree* (SPT) from the
multicast source to the receiver.

In versions of pimd prior to 2.2.0 this threshold was confusingly split
in two different settings, one for the DR and one for the RP.  These
settings are still supported, for compatibility reasons and documented
in the man-page, but it is strongly recommended to change to the new
syntax instead:

    spt-threshold [rate <KBPS> | packets <NUM> | infinity] [interval <5-60>]

Only slightly different from the Cisco `ip pim spt-threshold` setting,
pimd can trigger a switch to SPT on a rate or number of packets and you
can also tweak the poll interval.  It's recommended to keep the interval
in the tens of seconds, the default is 100 sec.  The default threshold
is set to zero packets, which will cause a switch over to the SPT after
the first multicast packet is received.


### Example

    # Interface eth0 is disabled, i.e., pimd will not run there.
    phyint eth0 disable

    # On this LAN we have a lower numeric IP than other PIM routers
    # but we want to take care of forwarding all PIM messages.
    phyint eth1 dr-priority 10

    # Partake in BSR elections on eth1
    bsr-candidate eth1

    # Offer to be an RP for all of 224.0.0.0/4
    rp-candidate eth1
    group-prefix 224.0.0.0 masklen 4

    # This is the built-in defaults, switch to SPT on first packet
    spt-threshold packets 0 interval 100


Running pimd
------------

Having set up the configuration file, you are ready to run `pimd`.  As
usual, it is recommended that you start it manually first, to make sure
everything works as expected, before adding it to your system's startup
scripts, with any startup flags it might need.

    pimd [-hnrsv] [-f file] [-d subsys1[,...,subsysN]] [-l level]

* `-n`: Run in foreground, with logs to stdout (for systemd and finit)
* `-s`: Use syslog, default unless `-n`
* `-c file`: Utilize the specified configuration file rather than the
   default, `/etc/pimd.conf`
* `-d [subsys1,...,subsysN]`: Subsystems to enable debug for when
  running the daemon.  Optional argument, if left out, all subsystems
  are enabled.  Type `pimd -h` for a full list of subsystems
* `-l level`: Log level, one of `none`, `error`, `warning`, `notice`,
   `info`, or `debug`.  Default is `notice`

**Example:**

    pimd -f /cfg/pimd.conf

When running multiple instances of pimd, make sure to use the `-I ident`
argument, otherwise the PID and IPC socket files will be overwritten and
the syslog will also be hard to follow.  Note, `-I` changes the default
`.conf` filename pimd looks for as well, a complete identity change.


### Enabling Debug

Remember to set the correct log level when enabling debug messages,
usually you need `-l debug`, and `-s` to force messages to syslog
when running in the foreground (`-n`).

    pimd -d igmp_proto,pim_jp,kernel,pim_register -l debug -n -s


## Troubleshooting Checklist

1. Check the TTL of incoming multicast.  Remember, the TTL of the
   multicast stream must be >1 to be routed.  Or rather, `>` than then
   `ttl-threshold` of the inbound `phyint`

2. If you see `Permission denied` in your logs, you are most likely
   having firewall, or SELinux, problems

3. For PIM-SM, make sure you have a Rendezvous-Point (RP) in your
   network.  Check `rp-candidate` (CRP) and `bsr-candidate` (CBSR)
   settings in your `pimd.conf`, or `rp-address` if you prefer the
   static RP approach

4. Check the Linux `rp_filter` setting.  Many Linux systems have the
   "strict" setting enabled, "loose" can work but may cause problems in
   some setups.  We recommend disabling it entirely

5. PIM is protocol *independent* so you must have unicast routeing in
   place already for `pimd` to work.  Use `ping` to verify connectivity
   between multicast sender and receiver


Monitoring
----------

To see the virtual interface table, including neighboring PIM routers,
and the multicast routing table:

    pimctl show interfaces
    pimctl show neighbor
    pimctl show mrt
    ...

The default command is `pimctl show pim`.  To watch it continually
(notice the `-c` flag to watch(1) to tell it to interpret the ANSI
escape sequences):

    watch -cd pimctl

See the `pimctl help` usage text for more commands (available only when
a running PIM daemon is available), or the pimctl(8) man page.

Also worth mentioning, `pimd` logs important events to the system log,
in particular at startup when it parses the `pimd.conf` configuration
file.


Large Setups
------------

pimd is limited to the number of `MAXVIFS` interfaces listed in the
kernel headers.  In Linux see `/usr/include/linux/mroute.h`. 

To overcome this limitation, adjust the kernel `#define` to, e.g., 1280,
and configure pimd `--with-max-vifs=1280`.  Please note, this has only
been tested with Linux and will likely not work with other kernels!

With this many interfaces the kernel may run out of memory to let pimd
to enable IGMP on all interfaces.  In Linux, use sysctl to tweak the
following settings:

    sysctl -w net.core.optmem_max=327680
    sysctl -w net.ipv4.igmp_max_memberships=5120


Build & Install
---------------

The configure script and Makefile supports de facto standard settings
and environment variables such as `--prefix=PATH` and `DESTDIR=` for the
install process.  E.g., to install pimd to `/usr` instead of the default
`/usr/local`, but redirect install to a package directory in `/tmp`:

    ./configure --prefix=/usr --sysconfdir=/etc --localstatedir=/var
	make
    make DESTDIR=/tmp/pimd-2.3.2-1 install-strip


Building from GIT
-----------------

If you want to contribute, or simply just try out the latest but
unreleased features, then you need to know a few things about the
[GNU build system][buildsystem]:

- `configure.ac` and a per-directory `Makefile.am` are key files
- `configure` and `Makefile.in` are generated from `autogen.sh`
- `Makefile` is generated by `configure` script

To build from GIT you first need to clone the repository and run the
`autogen.sh` script.  This requires `automake` and `autoconf` to be
installed on your system.

    git clone https://github.com/troglobit/pimd.git
    cd pimd/
    ./autogen.sh
    ./configure && make

GIT sources are a moving target and are not recommended for production
systems, unless you know what you are doing!


Contributing
------------

pimd is maintained by [Joachim Wiberg][] at [GitHub][].  If you find
bugs, have feature requests, or want to contribute fixes or features,
check out the code from GitHub:

	git clone https://github.com/troglobit/pimd
	cd pimd

See the file [CONTRIBUTING.md][contrib] for further details.


Origin & References
-------------------

Part of this program has been derived from mrouted.  The mrouted program
is covered by the 3-clause BSD license in the accompanying file named
[LICENSE.mrouted](doc/LICENSE.mrouted).

The mrouted program is COPYRIGHT 2002 by The Board of Trustees of Leland
Stanford Junior University.


[License]:         https://en.wikipedia.org/wiki/BSD_licenses
[License Badge]:   https://img.shields.io/badge/License-BSD%203--Clause-blue.svg
[github]:          https://github.com/troglobit/pimd
[homepage]:        https://troglobit.com/pimd.html
[ChangeLog]:       https://github.com/troglobit/pimd/blob/master/ChangeLog.org
[releases page]:   https://github.com/troglobit/pimd/releases
[buildsystem]:     https://autotools.io/
[contrib]:         https://github.com/troglobit/pimd/blob/master/.github/CONTRIBUTING.md
[Joachim Wiberg]:  https://troglobit.com
[GitHub]:          https://github.com/troglobit/pimd/actions/workflows/build.yml/
[GitHub Status]:   https://github.com/troglobit/pimd/actions/workflows/build.yml/badge.svg
[Coverity Scan]:   https://scan.coverity.com/projects/3319
[Coverity Status]: https://scan.coverity.com/projects/3319/badge.svg
