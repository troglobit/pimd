# -*-Makefile-*-
# Compilation flags for different platforms. Uncomment only one of them

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
