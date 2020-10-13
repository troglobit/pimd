# For replacement functions in lib/
AUTOMAKE_OPTIONS = subdir-objects

AM_CPPFLAGS      = -I$(top_srcdir)/include
AM_CPPFLAGS     += -DSYSCONFDIR=\"@sysconfdir@\" -DRUNSTATEDIR=\"@runstatedir@\" -DPRESERVEDIR=\"@localstatedir@\"

sbin_PROGRAMS    = pimd pimctl
pimd_SOURCES     = config.c		debug.c		debug.h			    \
		   defs.h		dvmrp_proto.c	dvmrp.h			    \
		   igmp_proto.c		igmp.c		igmpv2.h	igmpv3.h    \
		   inet.c		ipc.c		ipc.h			    \
		   kern.c		main.c		pathnames.h		    \
		   mrt.c		mrt.h		route.c		rp.c	    \
		   pim_proto.c		pim.c		pimd.h			    \
		   timer.c		trace.c		trace.h	    		    \
		   vif.c		vif.h		queue.h
pimd_CFLAGS      = -W -Wall -Wextra -Wno-unused
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
