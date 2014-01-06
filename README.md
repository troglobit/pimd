README for pimd, the PIM-SM v2 multicast daemon
===============================================
This is pimd, a lightweight, stand-alone implementation of Protocol Independent
Multicast-Sparse Mode that may be freely distributed and/or deployed under the
BSD license.  The project implements the full PIM-SM specification according to
RFC 2362 with a few noted exceptions (see the RELEASE.NOTES for details).

The software should compile and run on most UNIX varieties, including FreeBSD,
BSDi, NetBSD, OpenBSD, SunOS, IRIX, Solaris 2.5, Solaris 2.6, and GNU/Linux.
It has, however, not been thoroughly tested on all these.

If you have any questions, suggestions, bug reports, patches, or pull requests,
please do NOT send them to the PIM IETF Working Group mailing list!  Instead,
contact the current maintainer, see the file AUTHORS, or use the github issue
tracker at:

	http://github.com/troglobit/pimd/issues

The following helpful text is a rip-off from the book Linux Routing.  See
http://linux-networks.net/New.Riders-Linux.Routing/tindex.htm for more.

Configuring pimd
----------------
Since pimd is a single-function daemon, the configuration is really not that
complex -- at least when compared to a monster tool such as gated. The
configuration data is kept in the file /etc/pimd.conf.  While the order of the
statements does not have to strictly follow what is about to be presented,
I'll walk you through the contents you might want to add to this file.

The /etc/pimd.conf file begins with the following statement:

   default_source_preference value

Routers hold elections to determine which gets to be a site's upstream router.
Because pimd is such a focused tool, you generally don't want it to win over
something more general.  Using a value of 101 here is a minimum for making
sure that gated and other routing tools are going to win the election and
leave pimd to do its PIM-SM handling.  The next line is:

   default_source_metric value

This item sets the cost for sending data through this router.  You want only
PIM-SM data to go to this daemon; so once again, a high value is recommended
to prevent accidental usage.  The preferred default value is 1024.

Though you can swap the first two statements around, you must have the next
statement after you set default_source_metric.  This item starts with:

   phyint interface

phyint refers to physical interface.  You fill in interface with a reference to
the ethernet card or other network interface you're telling pimd about, either
with the device's IP address or name (for example, eth0).  If you just want to
activate this interface with default values, you don't need to put anything
else on the line.  However, you do have some additional items you can add.  The
items are, in the order you would need to use them, as follows:

   * _disable_.  Do not send PIM-SM traffic through this interface nor listen
     for PIM-SM traffic from this interface.

   * preference pref.  This interface's value in an election. It will have the
     default_source_preference if not assigned.

   * metric cost.  The cost of sending data through this interface. It will
     have the default_source_metric if not assigned.

Add one phyint line per interface on this router.  If you don't do this, pimd
will simply assume that you want it to utilize all interfaces on the machine
with the default values.  If you set phyint for one or more interfaces but not
for all, the missing ones will be assigned the defaults.  After you have done
this, start the next line with:

   cand_rp

cand_rp refers to Candidate Rendezvous Point (CRP).  This statement specifies
which interface on this machine should be included in RP elections.  Additional
options to choose from are, listed in the order used, as follows:

   * ipadd.  The default is the largest activated IP address. If you don't want
     to utilize that interface, add the IP address of the interface to use as
     the next term.

   * time value.  The number of seconds to wait between advertising this
     CRP. The default value is 60 seconds.

   * priority num. How important this CRP is compared to others. The lower the
     value here, the more important the CRP.

The next line begins with:

   cand_bootstrap_router

Here you give the information for how this machine advertises itself as a
Candidate BootStrap Router (CBSR).  If you need to, add the ipaddr and/or
priority items as defined earlier after cand_bootstrap_router.  What follows
is a series of statements that start with:

   group_prefix

Each group_prefix statement outlines the set of multicast addresses that CRP,
if it wins an election, will advertise to other routers. The two items you
might include here are, listed in order, as follows:

    * groupaddr.  A specific IP or network range this router will handle.
      Remember that a single multicast network is written as a single IP
      address.

    * masklen len.  The number of IP address segments taken up by the netmask.
      Remember that a multicast address is a Class D and has a netmask of
      255.255.255.255, which means its length is 4.  The prefix length can
      also be given as 'groupaddr/len'.

Max group_prefix multicast addresses supported in pimd is 255.

After this comes:

   switch_data_threshold rate rvalue interval ivalue

This statement defines the threshold at which transmission rates trigger the
changeover from the shared tree to the RP tree; starting the line with
switch_register_threshold does the opposite in the same format.  (See Chapter
2, "Multicast Protocols," for more details on the two PIM-SM trees.)
Regardless of which of these you choose, the rvalue stands for the
transmission rate in bits per second, and ivalue sets how often to sample the
rate in seconds -- with a recommended minimum of five seconds.  It's
recommended by the pimd programmers to have ivalue the same in both
statements.

For example, I might end up with the following (these are real IP addresses;
don't use them for actual testing purposes):

   default_source_preference 105

   phyint 199.60.103.90 disable
   phyint 199.60.103.91 preference 1029
   phyint 199.60.103.92 preference 1024
   cand_rp 199.60.103.91
   cand_bootstrap_router 199.60.103.92
   group_prefix 224.0.0.0 masklen 4
   switch_data_threshold rate 60000 interval 10
   switch_register_threshold rate 60000 interval 10

Running pimd
------------
After you've set up the configuration file, you're ready to actually run the
PIM-SM daemon, pimd.  As usual, we tend to recommend that you run this by hand
for testing purposes and then later add the daemon, with any startup flags it
needs, to your system's startup scripts.

The format for running this daemon is:

   pimd -c file -d level1,...,levelN

Both of the flags with their values are optional:

    * -c file.  Utilize the specified configuration file rather than the
       default, /etc/pimd.conf.

    * -d level1,...,levelN.  Specifies the debug level(s) to utilize when
       running this daemon.  Type pimd -h for a full list of these levels.
