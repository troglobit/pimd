# config.mk                                                     -*-Makefile-*-
# This is the pimd build configuration file.  See the below description for details
# on each build option.

# -D_GNU_SOURCE : Use GNU extensions, where possible
# -D_BSD_SOURCE : Use functions derived from 4.3 BSD Unix rather than POSIX.1
# -DPIM : Enable PIM extensions in RSRR
DEFS       = -D__BSD_SOURCE -D_GNU_SOURCE -DPIM

# EXTRA_OBJS= For locally provided objects missing on some platforms, e.g., strlcpy.o
# EXTRA_LIBS= For platform specific libraries, e.g., -lutil

# Misc. definitions
# -DBROKEN_CISCO_CHECKSUM :
#   If your RP is buggy cisco PIM-SMv2 implementation that computes
#   the PIM-Register checksum over the whole pkt instead only over
#   the header, you need to define this. Otherwise, all your PIM-Register
#   may be dropped by the cisco-RP.
#DEFS += -DBROKEN_CISCO_CHECKSUM
#
# -DPIM_OLD_KERNEL : older PIM kernels don't prepare the inner encapsulated
#   pkt, and the daemon had to take care of the details
#   (prior to pimd-2.1.0-alpha29). Newer kernels will prepare everything,
#   and then the daemon should not touch anything. Unfortunately, both
#   kernels are not compatible. If you still have one of those old kernels
#   around and want to use it, then define PIM_OLD_KERNEL here.
#DEFS += -DPIM_OLD_KERNEL
#
# -DPIM_REG_KERNEL_ENCAP : Register kernel encapsulation. Your kernel must
#   support registers kernel encapsulation to be able to use it.
#DEFS += -DPIM_REG_KERNEL_ENCAP
#
# -DKERNEL_MFC_WC_G : (*,G) kernel MFC support. Use it ONLY with (*,G)
#   capable kernel
#DEFS += -DKERNEL_MFC_WC_G
#
# -DSAVE_MEMORY : saves 4 bytes per unconfigured interface
#   per routing entry. If set, configuring such interface will restart the
#   daemon and will flush the routing table.
#DEFS += -DSAVE_MEMORY
#
# -DSCOPED_ACL :
#   Scoped access control list support in pimd.conf, by Marian Stagarescu <marian@cidera.com>
#   If you want to install NUL OIF for the "scoped groups", use the following syntax:
#   "phyint IFNAME [scoped <MCAST_ADDR> masklen <PREFIX_LEN>]", e.g.
#      phyint fxp0 scoped "addr" masklen "len"
#DEFS += -DSCOPED_ACL
#

##
# Compilation flags for different platforms.
# Uncomment only one of them. Default: Linux

# If the multicast header files are not in the standard place on your system,
# define MCAST_INCLUDE to be an appropriate `-I' options for the C compiler.
#MCAST_INCLUDE=	-I/sys

## FreeBSD	-D__FreeBSD__ is defined by the OS
## FreeBSD-3.x, FreeBSD-4.x
# Seems to already have ip_mroute.h and pim.h, add -Iinclude/freebsd if
# ip_mroute.h or in.h is missing on your system and -Iinclude for pim.h
#INCLUDES     =
#DEFS        += -DHAVE_STRTONUM -DHAVE_STRLCPY
#EXTRA_OBJS   = pidfile.o
#EXTRA_LIBS   =
## FreeBSD-2.x
#INCLUDES     = -Iinclude -Iinclude/freebsd2
#DEFS        +=
#EXTRA_OBJS   = strlcpy.o pidfile.o

## NetBSD	-DNetBSD is defined by the OS
# Seems to already have ip_mroute.h and pim.h, add -Iinclude/netbsd if
# ip_mroute.h or in.h is missing on your system and -Iinclude for pim.h
#INCLUDES     =
#DEFS        += -DHAVE_STRTONUM -DHAVE_STRLCPY -DHAVE_PIDFILE
#EXTRA_OBJS   =
#EXTRA_LIBS   = -lutil

## OpenBSD	-DOpenBSD is defined by the OS
# Seems to already have ip_mroute.h and pim.h, add -Iinclude/openbsd if
# ip_mroute.h or in.h is missing on your system and -Iinclude for pim.h
#INCLUDES     =
#DEFS        += -DHAVE_STRTONUM -DHAVE_STRLCPY -DHAVE_PIDFILE
#EXTRA_OBJS   =
#EXTRA_LIBS   = -lutil

## BSDI		-D__bsdi__ is defined by the OS
#INCLUDES     = -Iinclude
#DEFS        +=
#EXTRA_OBJS   = strlcpy.o pidfile.o

## SunOS, OSF1, gcc
#INCLUDES     = -Iinclude -Iinclude/sunos-gcc
#DEFS        += -DSunOS=43
#EXTRA_OBJS   = strlcpy.o pidfile.o

## SunOS, OSF1, cc
#INCLUDES     = -Iinclude -Iinclude/sunos-cc
#DEFS        += -DSunOS=43
#EXTRA_OBJS   = strlcpy.o pidfile.o

## IRIX
#INCLUDES     = -Iinclude
#DEFS        += -D_BSD_SIGNALS -DIRIX
#EXTRA_OBJS   = strlcpy.o pidfile.o

## Solaris 2.5, gcc
#INCLUDES     = -Iinclude
#DEFS        += -DSYSV -DSunOS=55
## Solaris 2.5, cc
#INCLUDES     = -Iinclude
#DEFS        += -DSYSV -DSunOS=55
## Solaris 2.6
#INCLUDES     = -Iinclude
#DEFS        += -DSYSV -DSunOS=56
## Solaris 2.x
#EXTRA_OBJS   = strlcpy.o pidfile.o
#EXTRA_LIBS   = -L/usr/ucblib -lucb -L/usr/lib -lsocket -lnsl

## Linux	-D__linux__ is defined by the OS
# GNU/Linux systems do not seem to ship pim.h and pim_var.h,
# use local include/netinet
# For uClibc based Linux systems, add -DHAVE_STRLCPY to DEFS
INCLUDES      = -Iinclude
DEFS         += -DRAW_INPUT_IS_RAW -DRAW_OUTPUT_IS_RAW -DIOCTL_OK_ON_RAW_SOCKET
EXTRA_OBJS    = strlcpy.o pidfile.o

