# -*-Makefile-*-
# Misc. definitions
# -DBROKEN_CISCO_CHECKSUM :
#   If your RP is buggy cisco PIM-SMv2 implementation that computes
#   the PIM-Register checksum over the whole pkt instead only over
#   the header, you need to define this. Otherwise, all your PIM-Register
#   may be dropped by the cisco-RP.
# -DPIM_OLD_KERNEL : older PIM kernels don't prepare the inner encapsulated
#   pkt, and the daemon had to take care of the details
#   (prior to pimd-2.1.0-alpha29). Newer kernels will prepare everything,
#   and then the daemon should not touch anything. Unfortunately, both
#   kernels are not compatible. If you still have one of those old kernels
#   around and want to use it, then define PIM_OLD_KERNEL here.
# -DPIM_REG_KERNEL_ENCAP : Register kernel encapsulation. Your kernel must
#   support registers kernel encapsulation to be able to use it.
# -DKERNEL_MFC_WC_G : (*,G) kernel MFC support. Use it ONLY with (*,G)
#   capable kernel
# -DSAVE_MEMORY : saves 4 bytes per unconfigured interface
#   per routing entry. If set, configuring such interface will restart the
#   daemon and will flush the routing table.
#
#  marian stagarescu: marian@cidera.com
#
# -DSCOPED_ACL :
#   use "scoped [mcast_addr] masklen [l]" statements in pimd.conf: e.g.
#
#   phyint fxp0 scoped "addr" masklen "len" 
#
#   if you want to install NUL OIF for the "scoped groups"
#

# -DPIM_REG_KERNEL_ENCAP #-DKERNEL_MFC_WC_G
MISCDEFS       =

### Compilation flags for different platforms. 
### Uncomment only one of them. Default: Linux

## FreeBSD	-D__FreeBSD__ is defined by the OS
## FreeBSD-3.x, FreeBSD-4.x
#INCLUDES       = -Iinclude/freebsd
#DEFS           = -Wall -g
## FreeBSD-2.x
#INCLUDES       = -Iinclude/freebsd2
#DEFS           = -Wall -g

## NetBSD	-DNetBSD is defined by the OS
#INCLUDES       = -Iinclude/netbsd
#DEFS           = -Wall -g

## OpenBSD	-DOpenBSD is defined by the OS
#INCLUDES       = -Iinclude/openbsd
#DEFS           = -Wall -g

## BSDI		-D__bsdi__ is defined by the OS
#INCLUDES       =
#DEFS           = -g

## SunOS, OSF1, gcc
#INCLUDES       = -Iinclude/sunos-gcc
#DEFS           = -Wall -g -DSunOS=43

## SunOS, OSF1, cc
#INCLUDES       = -Iinclude/sunos-cc
#DEFS           = -g -DSunOS=43

## IRIX
#INCLUDES       =
#DEFS           = -Wall -g -D_BSD_SIGNALS -DIRIX

## Solaris 2.5, gcc
#INCLUDES       =
#DEFS           = -Wall -g -DSYSV -DSunOS=55
## Solaris 2.5, cc
#INCLUDES       =
#DEFS           = -g -DSYSV -DSunOS=55
## Solaris 2.6
#INCLUDES       =
#DEFS           = -g -DSYSV -DSunOS=56
## Solaris 2.x
#LIB2           = -L/usr/ucblib -lucb -L/usr/lib -lsocket -lnsl

## Linux v2.2, or later
INCLUDES       = -Iinclude/linux
DEFS           = -W -Wall -g -D__BSD_SOURCE -DLinux \
		 -DRAW_INPUT_IS_RAW -DRAW_OUTPUT_IS_RAW \
		 -DIOCTL_OK_ON_RAW_SOCKET \
