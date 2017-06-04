Installation instruction for pimd
=================================

It is recommended to use a pimd from your distribution, be it from ports
in one of the major BSD's, or your GNU/Linux distribution of choice.

However, if you want to try the latest bleeding edge pimd, download one
of the release tarballs at <https://github.com/troglobit/pimd/releases>

After unpacking the tarball, cd to the new directory, e.g. `pimd-2.3.0/`
followed by:

    ./configure && make
    sudo make install

By default pimd is installed to the `/usr/local` prefix, except for
`pimd.conf` which is installed to `/etc`.  If you want to install to
another directory, e.g. `/opt`, use:

    ./configure --prefix=/opt && make
    sudo make install

This will change both the `--prefix` and the `--sysconfdir` paths.  To
install `pimd.conf` to another path, add `--sysconfdir` *after* the
`--prefix` path.

For distribution packagers and ports maintainers, the pimd `Makefile`
supports the use of `DESTDIR=` to install to a staging directory.  What
you want is probably something like:

    ./configure --prefix=/usr --sysconfdir=/etc && make
    make DESTDIR=/tmp/staging VERSION=2.3.0-1 install

The default `/etc/pimd.conf` should be good enough for most use cases.
But if you edit it, see the man page or the comments in the file for
some help.

NetBSD and FreeBSD users may have to install the kernel modules to get
multicast routing support, including PIM support.  See your respective
documentation, or consult the web for help!


Cross Compiling
---------------

The pimd build system does not use GNU autotools, but it is still
possible to cross-compile.  Simply make sure to give the `configure`
script the correct paths and options, and then set the environment
variable `CROSS` to your cross compiler prefix.  E.g.

    ./configure --prefix=/ --embedded-libc
    make CROSS=arm-linux-gnueabi-

**Note:** some toolchains do not properly setup at `cc` symlink, for
  instance the Debian/Ubuntu ARM toolchains.  Instead they assume that
  projects are using GCC and only provide a `gcc` symlink.


Old INSTALL
-----------

Old install instructions, before PIM kernel support was readily
available in all major operating systems

1. Apply the PIM kernel patches, recompile, reboot

2. Copy pimd.conf to /etc and edit as appropriate.  Disable the
   interfaces you don't need. Note that you need at least 2 physical
   interfaces enabled.

3. Edit Makefile by uncommenting the line(s) corresponding to your platform.

4. Recompile pimd

5. Run pimd as a root. It is highly recommended to run it in debug mode.
   Because there are many debug messages, you can specify only a subset of
   the messages to be printed out:

        usage: pimd [-c configfile] [-d [debug_level][,debug_level]]

   Valid debug levels: `dvmrp_prunes`, `dvmrp_mrt`, `dvmrp_neighbors`,
   `dvmrp_timers`, `igmp_proto`, `igmp_timers`, `igmp_members`, `trace`,
   `timeout`, `pkt`, `interfaces`, `kernel`, `cache`, `rsrr`,
   `pim_hello`, `pim_register`, `pim_join_prune`, `pim_bootstrap`,
   `pim_asserts`, `pim_cand_rp`, `pim_routes`, `pim_timers`, `pim_rpf`

   If you want to see all messages, use `pimd -dall` only.

6. Note that it takes of the order of 30 seconds to 1 minute until the
   Bootstrap router is elected and the RP-set distributed to the PIM
   routers, and without the RP-set in the routers the multicast packets
   cannot be forwarded.

7. There are plenty of bugs, some of them known (check BUGS.TODO), some of
   them unknown, so your bug reports are more than welcome.


