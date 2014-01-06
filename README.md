README
======

pimd is a lightweight, stand-alone implementation of RFC 2362, available
under the 3-clause BSD license.  This is the restored original version
from University of Southern California, by Ahmed Helmy, Rusty Eddy and
Pavlin Ivanov Radoslavov.

pimd is maintained at GitHub.  Use its facilities to access the source,
report bugs, feature requests, send patches and for GIT pull requests:

  http://github.com/troglobit/pimd

pimd also has a homepage, mainly to distribute releases:

  http://troglobit.com/pimd.html

pimd is primarily developed on Linux and should work as-is out of the
box on all major distributions.  Other UNIX variants should also work,
but are not as thoroughly tested.  See the file `config.mk` for details
on support and various tricks needed.


Configuration
-------------

The configuration is kept in the file `/etc/pimd.conf`, the order of
the statements are in some cases important.

PIM-SM is a designed to be a _protocol independent_ multicast routing
protocol.  As such it relies on protocols like, e.g, OSPF, RIP, or
static routing entries, to figure out the path to all multicast capable
neighboring routers.  This information is necessary in setups with more
than one route between a multicast sender and a receiver to figure out
which PIM router should be the active forwarder.

However, pimd currently cannot retrieve the unicast routing distance
(preference) and metric of routes from the system, not from the kernel
nor a route manager like zebra.  Hence, pimd currently needs to be setup
statically on each router using the desired distance and metric for each
active interface.  If either the distance and/or the metric is missing
in an interface configuration, the following two defaults will be used:

    default_source_preference <1-255>     default: 101   (distance)
    default_source_metric     <1-1024>    default: 1024

By default pimd starts up on all interfaces it can find, using the above
defaults.  To configure individual interfaces use:

    phyint <address | ifname> ...

You can reference the interface via either its local IPv4 address or
its name, e.g., eth0.  Some common interface settings are:

   * `disable`: Disable pimd on this interface, i.e., do not send or
     listen for PIM-SM traffic

   * `preference <1-255>`: The interface's distance value (also
     confusingly referred to as metric preference) in PIM Assert
     messages.  Used with `metric` to elect the active multicast
     forwarding router.  Defaults to `default_source_preference`

   * `metric <1-1024>`: The cost for traversing this router.  Used with
     the `preference` value above. Defaults to `default_source_metric`

More interface settings are available, see the pimd(8) manual page for
the full details.

The most notable feature of PIM-SM is that multicast is distributed from
so called Rendezvous Points (RP).  Each RP handles distribution of one
or more multicast groups, pimd can be configured to advertise itself as
a candidate RP `cand_rp`, and request to be static RP `rp_address` for
one or more multicast groups.

    rp_address <address> [<group>[/<LENGTH> | masklen <LENGTH]

The `rp_address` setting is the same as the Cisco `ip pim rp-address`
setting to configure static Rendezvous Points.  The first argument can
be an IPv4 address or a multicast group address.  The default group and
prefix length is 224.0.0.0/16.  Static RP's always have priority 1.

    cand_rp [address | ifname] [time <10-16383>] [priority <0-255>]

The Rendezvous Point candidate, or CRP, setting is the same as the Cisco
`ip pim rp-candidate` setting.  Use it to control which interface that
should be used in RP elections.

   * `address | ifname`: Optional local IPv4 address, or interface name
     to acquire address from.  The default is to use the highest active
     IP address.

   * `time <10-16383>`: The interval, in seconds, between advertising
     this CRP. Default: 60 seconds

   * `priority <0-255>`: How important this CRP is compared to others.
     The lower the value here, the more important the CRP.  Like Cisco,
     pimd defaults to priority 0 when this is left out

In the CRP messages sent out by pimd, one or more multicast groups can
be advertised using the following syntax.

    group_prefix <group>[</LENGTH> | masklen <LENGTH>]

Each `group_prefix` setting defines one multicast group and an optional
mask length, which defaults to 16 if left out.  A maximum of 255
multicast group prefix records is possible for the CRP.

To keep track of all Rendezvous Points in a PIM-SM domain there exists a
feature called *Bootstrap Router*.  The elected BSR in a PIM-SM domain
periodically announces the RP set in Bootstrap messages.  For details on
PIM BSR operation, see [RFC 5059](http://tools.ietf.org/search/rfc5059).

    cand_bootstrap_router [address | ifname] [priority <0-255>]

The configuration of a Candidate BootStrap Router (CBSR) is very similar
to that of CRP, except for the interval time.  If either the address or
the interface name is left out pimd uses the highest active IP address.
If the priority is left out, pimd (like Cisco) defaults to priority 0.



After this comes:

    switch_data_threshold rate rvalue interval ivalue

This statement defines the threshold at which transmission rates trigger
the changeover from the shared tree to the RP tree; starting the line
with `switch_register_threshold` does the opposite in the same format.
Regardless of which of these you choose, the `rvalue` stands for the
transmission rate in bits per second, and `ivalue` sets how often to
sample the rate in seconds -- with a recommended minimum of five
seconds.  It's recommended by the pimd developers to have `ivalue` the
same in both statements.

For example, I might end up with the following (these are real IP
addresses; don't use them for actual testing purposes):

    default_source_preference 105
    
    phyint 199.60.103.90 disable
    phyint 199.60.103.91 preference 1029
    phyint 199.60.103.92 preference 1024
    cand_rp 199.60.103.91
    cand_bootstrap_router 199.60.103.92
    group_prefix 224.0.0.0 masklen 4
    switch_data_threshold rate 60000 interval 10
    switch_register_threshold rate 60000 interval 10


Running
-------

After you've set up the configuration file, you're ready to actually run
the PIM-SM daemon.  As usual, we recommend that you run this by hand for
testing purposes and then later add the daemon, with any startup flags
it needs, to your system's startup scripts.

The format for running this daemon is:

    pimd [-c file] [-d [level1,...,levelN]]

Both of the flags with their values are optional:

   * `-c file`: Utilize the specified configuration file rather than the
      default, `/etc/pimd.conf`

   * `-d [level1,...,levelN]`: Specifies the debug level(s) to utilize
      when running the daemon.  Type `pimd -h` for a full list of levels


Monitoring
----------

    pimd -r

or

    watch pimd -r


