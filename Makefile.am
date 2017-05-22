dist_man8_MANS   = pimd.8
doc_DATA         = README.md README-debug.md README-config.md README.config.jp		\
		   ChangeLog.org LICENSE LICENSE.mrouted pimd.conf
EXTRA_DIST       = README.md README-debug.md README-config.md README.config.jp		\
		   ChangeLog.org LICENSE LICENSE.mrouted pimd.conf autogen.sh		\
		   include/netinet/pim.h include/netinet/pim_var.h
DISTCLEANFILES   = *~ DEADJOE semantic.cache *.gdb *.elf core core.* *.d

if SYSTEMD
systemd_DATA     = pimd.service
endif

# -D_GNU_SOURCE Use GNU extensions, where possible
# -D_BSD_SOURCE Use functions derived from 4.3 BSD Unix rather than POSIX.1
#               In GLIBC >= v2.20 this is replaced with -D_DEFAULT_SOURCE,
#               but to build on older GLIBC systems we now need both ...
AM_CPPFLAGS      = -D_BSD_SOURCE -D_DEFAULT_SOURCE -D_GNU_SOURCE
AM_CPPFLAGS     += -DSYSCONFDIR=\"@sysconfdir@\" -DLOCALSTATEDIR=\"@localstatedir@\"

sbin_PROGRAMS    = pimd
pimd_SOURCES     = callout.c		config.c	debug.c		debug.h		\
		   defs.h		dvmrp_proto.c	dvmrp.h				\
		   igmp_proto.c		igmp.c		igmpv2.h	igmpv3.h	\
		   inet.c		kern.c		main.c		pathnames.h	\
		   mrt.c		mrt.h		route.c		rp.c		\
		   pim_proto.c		pim.c		pimd.h				\
		   timer.c		trace.c		trace.h		utimensat.c 	\
		   vif.c		vif.h		queue.h
pimd_LDADD       = @LIBUTIL@

if LINUX
AM_CPPFLAGS     += -I$(srcdir)/include -DRAW_OUTPUT_IS_RAW -DIOCTL_OK_ON_RAW_SOCKET
pimd_SOURCES    += netlink.c
endif

if BSD
pimd_SOURCES    += routesock.c
endif

# -DPIM         Enable PIM extensions in RSRR
if RSRR
AM_CPPFLAGS     += -DPIM
pimd_SOURCES    += rsrr.c		rsrr_var.h	rsrr.h
endif

if NO_PIDFILE
pimd_SOURCES    += pidfile.c
endif

if NO_STRLCPY
pimd_SOURCES    += strlcpy.c
endif

if NO_STRLCAT
pimd_SOURCES    += strlcat.c
endif

if NO_STRTONUM
pimd_SOURCES    += strtonum.c
endif

## BSDI		    -D__bsdi__ is defined by the OS
#AM_CPPFLAGS     += -Iinclude

## SunOS, OSF1, gcc
#AM_CPPFLAGS     += -Iinclude -Iinclude/sunos-gcc
#AM_CPPFLAGS     += -DSunOS=43

## SunOS, OSF1, cc
#AM_CPPFLAGS     += -Iinclude -Iinclude/sunos-cc
#AM_CPPFLAGS     += -DSunOS=43

## IRIX
#AM_CPPFLAGS     += -Iinclude
#AM_CPPFLAGS     += -D_BSD_SIGNALS -DIRIX

## Solaris 2.5
#AM_CPPFLAGS     += -Iinclude
#AM_CPPFLAGS     += -DSYSV -DSunOS=55

## Solaris 2.6
#AM_CPPFLAGS     += -Iinclude
#AM_CPPFLAGS     += -DSYSV -DSunOS=56

## Solaris 2.x
#pimd_LDADD      += -L/usr/ucblib -lucb -L/usr/lib -lsocket -lnsl
