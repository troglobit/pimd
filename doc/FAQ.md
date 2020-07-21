Mini FAQ
========

* Q: My RP is a Cisco router, but it doesn't work with pimd?

  If your Cisco is running PIM-SMv1, it won't work with pimd which
  implements only PIM-SMv2.  You need to upgrade/configure your Cisco to
  run PIM-SMv2.
	
  If your Cisco is indeed running PIM-SMv2, and it is the RP, you need
  to run the pimd `configure` script with `--enable-broken-crc` defined.
  See the beginning of the configure script, or the output from the
  command `configure --help`.  Note that this will then likely cause the
  PIM Register messages to *not* be accepted by some other vendors, but
  pimd-to-pimd should still be OK.

  **Note:** This is a *very* old FAQ and this issue is exteremly likely
  to be rather reversed in 2015 ...

* Q: Do I need to re-configure my Linux kernel to run pimd?

  Maybe, most major GNU/Linux distributions today ship with multicast
  capable Linux kernels.  However, do make a habit of verifying that
  you have at least the following:

	    CONFIG_IP_MULTICAST
		CONFIG_IP_PIMSM_V2
		CONFIG_IP_MROUTE

  You *may* enable `CONFIG_IP_PIMSM_V1` as well, but it is likely not
  required to interop with any active equipment anymore.  What may cause
  you to have to recompile Linux in 2015 is the lack of multiple
  multicast routing tables.  My Ubuntu 15.04 lists the following in its
  `/boot/config-3.19.0-23-generic`:

		# CONFIG_IP_MROUTE_MULTIPLE_TABLES is not set

  Also, make sure to check that NETLINK related settings are enabled,
  because that is the interface pimd uses on Linux, not routing sockets
  anymore.  Again, very likely to be default in 2015.

  Make sure that those options are set to "y" to include the relevant
  code in the kernel; if you enable them as modules, then you may have
  to load that module after you boot with the new kernel.  One way to
  find-out if multicast routing is not working, is to use command `cat
  /proc/sys/net/ipv4/conf/eth0/mc_forwarding` after you have started
  pimd (you may use other interface name instead of `eth0`).  If it
  returns zero, multicast forwarding on that interface is not working.

* Q: I tried pimd on Linux, but I get the following error message:

		netlink socket: Address family not supported by protocol

  You need to enable the NETLINK related stuff in the kernel and
  recompile it.

* Q: pimd compiled and is running on a single machines, but when I run
  it on 2+ machines, the multicast packets do not reach the receivers.

  Without detailed debug information I cannot answer this question.
  Please send to the pimd maintainer a scheme (topology map) of your
  network, and the debug output from each router (`pimd -dall`), that
  may help.

* Q: How do I debug my multicast routing?

  Check [README-debug.md][debug] for some hints.

* Q: How do I use pimd with GRE tunnels?

  See the file [README-config.md][config] for examples.

* Q: How do I run pimd but without configuring it as a Cand-RP and/or a
  Cand-BSR?

  See the file [README-config.md][config] for details.

* Q: I have set the `phyint dr-priority` to 10, but another router is
  still elected as DR, why?

  This happens when not all routers on a LAN advertise the *DR Priority*
  option in PIM Hello messages.  Check with tcpdump or wireshark to find
  the culprit.  Versions of pimd older than v2.3.0 did not support the
  *DR Priority* option.

* Q: How do I configure pimd to do FOO?

  See file [README-config.md][config].  If the answer is not there, send
  an email to the current pimd maintainer, or file a bug report at the
  [GitHub issue tracker][tracker].


[debug]:   https://github.com/troglobit/pimd/blob/dev/README-debug.md
[config]:  https://github.com/troglobit/pimd/blob/dev/README-config.md
[tracker]: https://github.com/troglobit/pimd/issues 

<!--
  -- Local Variables:
  -- mode: markdown
  -- End:
  -->
