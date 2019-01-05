# For replacement functions in lib/
AUTOMAKE_OPTIONS = subdir-objects

# -D_GNU_SOURCE Use GNU extensions, where possible
# -D_BSD_SOURCE Use functions derived from 4.3 BSD Unix rather than POSIX.1
#               In GLIBC >= v2.20 this is replaced with -D_DEFAULT_SOURCE,
#               but to build on older GLIBC systems we now need both ...
AM_CPPFLAGS      = -D_BSD_SOURCE -D_DEFAULT_SOURCE -D_GNU_SOURCE -I$(top_srcdir)/include
AM_CPPFLAGS     += -DSYSCONFDIR=\"@sysconfdir@\" -DLOCALSTATEDIR=\"@localstatedir@\"

sbin_PROGRAMS    = pimd pimctl
pimd_SOURCES     = callout.c		config.c	debug.c		debug.h		\
		   defs.h		dvmrp_proto.c	dvmrp.h				\
		   igmp_proto.c		igmp.c		igmpv2.h	igmpv3.h	\
		   inet.c		kern.c		main.c		pathnames.h	\
		   mrt.c		mrt.h		route.c		rp.c		\
		   pim_proto.c		pim.c		pimd.h		ipc.c		\
		   timer.c		trace.c		trace.h			 	\
		   vif.c		vif.h		queue.h
pimd_CFLAGS      = -W -Wall -Wextra
pimd_LDADD       = $(LIBS) $(LIBOBJS)

if LINUX
AM_CPPFLAGS     += -DRAW_OUTPUT_IS_RAW -DIOCTL_OK_ON_RAW_SOCKET
pimd_SOURCES    += netlink.c
endif

if BSD
pimd_SOURCES    += routesock.c
endif

if RSRR
AM_CPPFLAGS     += -DPIM
pimd_SOURCES    += rsrr.c		rsrr_var.h	rsrr.h
endif

pimctl_SOURCES   = pimctl.c		defs.h
pimctl_LDADD     = $(LIBS) $(LIBOBJS)
