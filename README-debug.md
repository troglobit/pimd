> $Id: README.debug,v 1.4 2002/11/17 20:01:31 pavlin Exp $

This file contains some hints how to debug your multicast routing.

**NOTE:** currently, it is very incomplete.  If something is missing
	      and/or unclear, email to the current maintainer of pimd or
	      file an issue in the GitHub issue tracker.

1. Make sure that the TTL of the sender is large enough to reach the
   receiver. E.g., if the sender and the receiver are separated by a
   two routers in the middle, the TTL of the data packets transmitted
   by the sender must be at least 3.

2. Make sure the receiver sends IGMP join (membership report) for the
   group(s) it wants to receive.  Use tcpdump on the router closest to
   the receiver to make sure.  Sometimes buggy IGMP snooping switches,
   or cloud provider networks, filter out multicast in general, or all
   control traffic (IGMP/PIM) in particular.

3. Before you start the multicast routing daemon, verify the kernel
   config, the following settings should be activated:

   - On Linux:

                CONFIG_IP_MROUTE=y
                CONFIG_IP_PIMSM_V1=y
                CONFIG_IP_PIMSM_V2=y
                CONFIG_IP_MROUTE_MULTIPLE_TABLES=y    # Optional

     Check the list of multicast capable interfaces:

                cat /proc/net/dev_mcast

   - On *BSD:

                options    MROUTING         # Multicast routing
                options    PIM              # Enable for pimd

   - Start the multicast routing daemon in debug mode.  E.g., `pimd -dall`
	 or if you just want to see some subystems: `pimd -drpf,mrt -s7`

4. After you start the multicast routing daemon

   - Are the multicast vifs correctly installed in the kernel:

	 - On Linux:

                cat /proc/net/ip_mr_vif

	 - On *BSD:

                netstat -gn

 - Is multicast forwarding enabled on those vifs:

	 - On Linux:

                sysctl net.ipv4.conf.eth0.mc_forwarding

	   For each of the enabled interfaces.  If it returns zero, the
	   multicast forwarding on that interface is not working.

	 - On *BSD:

                sysctl net.inet.ip.forwarding
                sysctl net.inet.ip.mforwarding      # Only OpenBSD

 - Is the PIM multicast routing daemon exchanging `PIM_HELLO` messages
   with its neighbors?  Look into the debug messages output; if
   necessary, use `tcpdump` as well.

 - Are the Bootpstrap messages received by all PIM-SM daemons?

 - If a Bootstrap message is received, is it accepted, or is it
   rejected because of a wrong iif?

 - After a while, does the Cand-RP set contain the set of RPs?

 - After the first multicast packets are received, is there `CACHE_MISS`
   signal from the kernel to the user-level daemon?

 - After the `CACHE_MISS` signal, are the MFC (Multicast Forwarding Cache)
   entries installed in the kernel?
 
	 - On Linux:

                cat /proc/net/ip_mr_cache

	 - On *BSD:

                netstat -gn

<!--
  -- Local Variables:
  -- mode: markdown
  -- End:
  -->
