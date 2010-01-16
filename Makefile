#
# Makefile for the Daemon part of PIM-SMv2.0,
# Protocol Independent Multicast, Sparse-Mode version 2.0
#
#
#  Questions concerning this software should be directed to 
#  Pavlin Ivanov Radoslavov (pavlin@catarina.usc.edu)
#
#  $Id: Makefile,v 1.64 2001/11/13 20:51:46 pavlin Exp $
#
# XXX: SEARCH FOR "CONFIGCONFIGCONFIG" (without the quotas) for the lines
# that might need configuration
#
# Modified for Debian by Antonin Kral <A.Kral@sh.cvut.cz>
# 27.03.2003

PROG_NAME=pimd

#CONFIGCONFIGCONFIG
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

MISCDEFS=	# -DPIM_REG_KERNEL_ENCAP #-DKERNEL_MFC_WC_G

#
# Version control stuff. Nothing should be changed
#
VERSION = `cat VERSION`
CVS_VERSION = `cat VERSION | sed 'y/./_/' | sed 'y/-/_/'`
CVS_LAST_VERSION=`cat CVS_LAST_VERSION`
PROG_VERSION =  ${PROG_NAME}-${VERSION}
PROG_CVS_VERSION = ${PROG_NAME}_${CVS_VERSION}
PROG_CVS_LAST_VERSION = ${PROG_NAME}_${CVS_LAST_VERSION}

# TODO: XXX: CURRENTLY SNMP NOT SUPPORTED!!!!
#
# Uncomment the following eight lines if you want to use David Thaler's
# CMU SNMP daemon support.
#
#SNMPDEF=	-DSNMP
#SNMPLIBDIR=	-Lsnmpd -Lsnmplib
#SNMPLIBS=	-lsnmpd -lsnmp
#CMULIBS=	snmpd/libsnmpd.a snmplib/libsnmp.a
#MSTAT=		mstat
#SNMP_SRCS=	snmp.c
#SNMP_OBJS=	snmp.o
#SNMPCLEAN=	snmpclean
# End SNMP support

#CONFIGCONFIGCONFIG
# Uncomment the following line if you want to use RSRR (Routing
# Support for Resource Reservations), currently used by RSVP.
#RSRRDEF=	-DRSRR

CC=		gcc
MCAST_INCLUDE=	-Iinclude
LDFLAGS=
#CONFIGCONFIGCONFIG
PURIFY=		purify -cache-dir=/tmp -collector=/import/pkgs/gcc/lib/gcc-lib/sparc-sun-sunos4.1.3_U1/2.7.2.2/ld

COMMON_CFLAGS= ${MCAST_INCLUDE} ${SNMPDEF} ${RSRRDEF} ${MISCDEFS} -DPIM

#CONFIGCONFIGCONFIG
### Compilation flags for different platforms. Uncomment only one of them

## FreeBSD	-D__FreeBSD__ is defined by the OS
## FreeBSD-3.x, FreeBSD-4.x
#CFLAGS= -Wall	-g	-Iinclude/freebsd ${COMMON_CFLAGS}
## FreeBSD-2.x
#CFLAGS= -Wall	-g	-Iinclude/freebsd2 ${COMMON_CFLAGS}

## NetBSD	-DNetBSD is defined by the OS
#CFLAGS= -Wall	-g	-Iinclude/netbsd ${COMMON_CFLAGS}

## OpenBSD	-DOpenBSD is defined by the OS
#CFLAGS= -Wall	-g	-Iinclude/openbsd ${COMMON_CFLAGS}

## BSDI		-D__bsdi__ is defined by the OS
#CFLAGS=	-g  ${COMMON_CFLAGS}

## SunOS, OSF1, gcc
#CFLAGS= -Wall	-g -Iinclude/sunos-gcc -DSunOS=43 ${COMMON_CFLAGS}
## SunOS, OSF1, cc
#CFLAGS=	-g -Iinclude/sunos-cc  -DSunOS=43 ${COMMON_CFLAGS}

## IRIX
#CFLAGS= -Wall	-g -D_BSD_SIGNALS -DIRIX ${COMMON_CFLAGS}

## Solaris 2.5, gcc
#CFLAGS= -Wall	-g -DSYSV -DSunOS=55 ${COMMON_CFLAGS}
## Solaris 2.5, cc
#CFLAGS= 	-g -DSYSV -DSunOS=55 ${COMMON_CFLAGS}
## Solaris 2.6
#CFLAGS=	-g -DSYSV -DSunOS=56 ${COMMON_CFLAGS}
## Solaris 2.x
#LIB2=		-L/usr/ucblib -lucb -L/usr/lib -lsocket -lnsl

## Linux (there is a good chance the include files won't work ;)
LINUX_INCLUDE	=	-Iinclude/linux
LINUX_DEFS	=	-D__BSD_SOURCE \
			-DRAW_INPUT_IS_RAW \
			-DRAW_OUTPUT_IS_RAW \
			-DIOCTL_OK_ON_RAW_SOCKET \
			-DLinux

## Old Linux (linux-2.1.103 for example)
#CFLAGS= -Wall -g -Dold_Linux ${LINUX_INCLUDE} ${LINUX_DEFS} ${COMMON_CFLAGS}

## Newer Linux (linux-2.1.127 for example)
CFLAGS= -Wall -g ${LINUX_INCLUDE} ${LINUX_DEFS} ${COMMON_CFLAGS}


LIBS=		${SNMPLIBDIR} ${SNMPLIBS} ${LIB2}
LINTFLAGS=	${MCAST_INCLUDE} ${CFLAGS}

IGMP_SRCS=	igmp.c igmp_proto.c trace.c
IGMP_OBJS=	igmp.o igmp_proto.o trace.o
ROUTER_SRCS=	inet.c kern.c main.c config.c debug.c netlink.c routesock.c \
		vers.c callout.c	
ROUTER_OBJS=	inet.o kern.o main.o config.o debug.o netlink.o routesock.o \
		vers.o callout.o
PIM_SRCS=	route.c vif.c timer.c mrt.c pim.c pim_proto.c rp.c
PIM_OBJS=	route.o vif.o timer.o mrt.o pim.o pim_proto.o rp.o
DVMRP_SRCS=	dvmrp_proto.c
DVMRP_OBJS=	dvmrp_proto.o
RSRR_SRCS=	rsrr.c
RSRR_OBJS=	rsrr.o
RSRR_HDRS=      rsrr.h rsrr_var.h
HDRS=		debug.h defs.h dvmrp.h igmpv2.h mrt.h pathnames.h pimd.h \
		trace.h vif.h ${RSRR_HDRS}
SRCS=		${IGMP_SRCS} ${ROUTER_SRCS} ${PIM_SRCS} ${DVMRP_SRCS} \
		${SNMP_SRCS} ${RSRR_SRCS}
OBJS=		${IGMP_OBJS} ${ROUTER_OBJS} ${PIM_OBJS} ${DVMRP_OBJS} \
		${SNMP_OBJS} ${RSRR_OBJS}
DISTFILES=	README README.config README.config.jp README.debug \
		CHANGES ${SRCS} ${HDRS} VERSION LICENSE \
		LICENSE.mrouted Makefile pimd.conf BUGS.TODO include

all: ${PROG_NAME}

${PROG_NAME}: ${OBJS} ${CMULIBS}
	rm -f $@
	${CC} ${LDFLAGS} -o $@ ${CFLAGS} ${OBJS} ${LIBS}

purify: ${OBJS}
	${PURIFY} ${CC} ${LDFLAGS} -o ${PROG_NAME} ${CFLAGS} ${OBJS} ${LIBS}

vers.c:	VERSION
	rm -f $@
	sed -e 's/.*/char todaysversion[]="&";/' < VERSION > vers.c

release: cvs-commit cvs-tag last-dist

re-release: cvs-commit cvs-retag last-dist

cvs-commit:
	cvs commit -m "make cvs-commit   VERSION=${PROG_VERSION}"

cvs-tag:
	cvs tag ${PROG_CVS_VERSION}
	`cat VERSION | sed 'y/./_/' | sed 'y/-/_/' > CVS_LAST_VERSION`

cvs-retag:
	cvs tag -F ${PROG_CVS_VERSION}
	`cat VERSION | sed 'y/./_/' | sed 'y/-/_/' > CVS_LAST_VERSION`

last-dist:
	- mv ${PROG_VERSION} _${PROG_VERSION}.SAVE_
	- rm -rf ${PROG_VERSION}
	- rm -rf ${PROG_VERSION}.tar.gz
	cvs export -r ${PROG_CVS_LAST_VERSION} -d releases/${PROG_VERSION} ${PROG_NAME}
	mv releases/${PROG_VERSION} .
	tar cvf - ${PROG_VERSION} | gzip > ${PROG_VERSION}.tar.gz
	rm -rf ${PROG_VERSION}
	- mv _${PROG_VERSION}.SAVE_ ${PROG_VERSION}

curr-dist:
	cvs commit -m "make curr-dist   VERSION=${PROG_VERSION}"
	- mv ${PROG_NAME}-current _${PROG_NAME}-current.SAVE_
	- rm -rf ${PROG_NAME}-current
	- rm -rf ${PROG_NAME}-current.tar.gz
	cvs checkout -d releases/${PROG_NAME}-current ${PROG_NAME}
	mv releases/${PROG_NAME}-current .
	tar cvf - ${PROG_NAME}-current | gzip > ${PROG_NAME}-current.tar.gz
	rm -rf ${PROG_NAME}-current
	- mv _${PROG_NAME}.SAVE_ ${PROG_NAME}

curr-diff:
	cvs commit -m "make curr-diff   VERSION=${PROG_VERSION}"
	cvs rdiff -kk -u -r ${PROG_CVS_LAST_VERSION} ${PROG_NAME} > ${PROG_NAME}-current.diff

install:	${PROG_NAME}
	# Modified in Debianization
	install -d ${DESTDIR}/usr/sbin
	install -d ${DESTDIR}/etc
	# install -m 0755 -f /usr/local/bin ${PROG_NAME}
	install -m 0755 ${PROG_NAME} ${DESTDIR}/usr/sbin/${PROG_NAME}
	#- mv /etc/pimd.conf /etc/pimd.conf.old
	#cp pimd.conf /etc
	install -m 0644 ${PROG_NAME}.conf ${DESTDIR}/etc/${PROG_NAME}.conf
	#echo "Don't forget to check/edit /etc/pimd.conf!!!"

clean:	FRC ${SNMPCLEAN}
	rm -f ${OBJS} core ${PROG_NAME} tags TAGS *.o

depend:	FRC
	mkdep ${CFLAGS} ${SRCS}

lint:	FRC
	lint ${LINTFLAGS} ${SRCS}

tags:	${SRCS}
	ctags ${SRCS}

cflow:	FRC
	cflow ${MCAST_INCLUDE} ${SRCS} > cflow.out

cflow2:	FRC
	cflow -ix ${MCAST_INCLUDE} ${SRCS} > cflow2.out

rcflow:	FRC
	cflow -r ${MCAST_INCLUDE} ${SRCS} > rcflow.out

rcflow2: FRC
	cflow -r -ix ${MCAST_INCLUDE} ${SRCS} > rcflow2.out

TAGS:	FRC
	etags ${SRCS}

snmpd/libsnmpd.a:
	cd snmpd; make)

snmplib/libsnmp.a:
	(cd snmplib; make)

snmpclean:	FRC
	-(cd snmpd; make clean)
	-(cd snmplib; make clean)

old-dist:	
	- mv $$PROG_NAME} ${PROG_NAME}.bin
	- rm -rf pimd-${VERSION}
	- rm pimd-${VERSION}.tar.gz
	- rm pimd-${VERSION}.tar.gz.formail
	cvs checkout -r pimd_${CVS_VERSION} pimd
	mv pimd pimd-${VERSION}
	tar cvf - pimd-${VERSION} | gzip > pimd-${VERSION}.tar.gz
	uuencode pimd-${VERSION}.tar.gz pimd-${VERSION}.tar.gz > pimd-${VERSION}.tar.gz.formail
	rm -rf pimd-${VERSION}
	- mv pimd.bin pimd

old-diff:
	- rm -rf diff.old
	- mkdir diff.old
	- mv diff-* diff.old
	mkdir diff-${VERSION}-${OLD_VERSION}
	- for i in ${DISTFILES}; do \
		if ! cmp -s ${PIMD_BASE}/$$i $$i ; then \
			diff -u ${PIMD_BASE}/$$i $$i > diff-${VERSION}-${OLD_VERSION}/$$i.diff; \
		fi \
	done
	- rm diff*.tar.gz
	- tar cvf diff-${VERSION}-${OLD_VERSION}.tar diff-${VERSION}-${OLD_VERSION}
	- gzip diff-${VERSION}-${OLD_VERSION}.tar
	- uuencode diff-${VERSION}-${OLD_VERSION}.tar.gz diff-${VERSION}-${OLD_VERSION}.tar.gz > diff.formail

FRC:



# DO NOT DELETE THIS LINE -- mkdep uses it.
