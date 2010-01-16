# -*-Makefile-*-
#
# Protocol Independent Multicast, Sparse-Mode version 2.0
#
# NOTE: Search for "CONFIGCONFIGCONFIG", without the quotes, for sections
# that might need configuration by you.
#
# 2010-01-16  Joachim Nilsson <jocke@vmlinux.org>
#             * Cleanup and refactoring for cross building.
#
# 2003-03-27  Antonin Kral <A.Kral@sh.cvut.cz>
#             * Modified for Debian
#
# 2001-11-13  Pavlin Ivanov Radoslavov (pavlin@catarina.usc.edu)
#             * Original Makefile
#

# $(shell git tag -l | tail -1)
VERSION       ?= 2.1.0
EXEC           = pimd
PKG            = $(EXEC)-$(VERSION)
ARCHIVE        = $(PKG).tar.bz2
ROOTDIR        = `pwd`
CC             = $(CROSS)gcc
IGMP_OBJS      = igmp.o igmp_proto.o trace.o
ROUTER_OBJS    = inet.o kern.o main.o config.o debug.o netlink.o routesock.o \
		 vers.o callout.o
PIM_OBJS       = route.o vif.o timer.o mrt.o pim.o pim_proto.o rp.o
DVMRP_OBJS     = dvmrp_proto.o
RSRR_OBJS      = rsrr.o
RSRR_HDRS      = rsrr.h rsrr_var.h
HDRS           = debug.h defs.h dvmrp.h igmpv2.h mrt.h pathnames.h pimd.h \
		 trace.h vif.h $(RSRR_HDRS)
OBJS           = $(IGMP_OBJS) $(ROUTER_OBJS) $(PIM_OBJS) $(DVMRP_OBJS) \
		 $(SNMP_OBJS) $(RSRR_OBJS)
SRCS           = $(OBJS:.o=.c)
DEPS           = $(addprefix .,$(SRCS:.c=.d))
DISTFILES      = README README.config README.config.jp README.debug \
		 CHANGES $(SRCS) $(HDRS) LICENSE LICENSE.mrouted \
		 Makefile pimd.conf BUGS.TODO include

include rules.mk
include Makefile.inc

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

# -DPIM_REG_KERNEL_ENCAP #-DKERNEL_MFC_WC_G
MISCDEFS       =

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

MCAST_INCLUDE  = -Iinclude
#CONFIGCONFIGCONFIG
PURIFY         = purify -cache-dir=/tmp -collector=/import/pkgs/gcc/lib/gcc-lib/sparc-sun-sunos4.1.3_U1/2.7.2.2/ld
COMMON_CFLAGS  = $(MCAST_INCLUDE) $(SNMPDEF) $(RSRRDEF) $(MISCDEFS) -DPIM
CFLAGS         = $(INCLUDES) $(DEFS) $(COMMON_CFLAGS) $(USERCOMPILE)

LDLIBS         = $(SNMPLIBDIR) $(SNMPLIBS) $(LIB2)
LINTFLAGS      = $(MCAST_INCLUDE) $(CFLAGS)

all: $(EXEC)

$(EXEC): $(OBJS) $(CMULIBS)
ifdef Q
	@printf "  LINK    $(subst $(ROOTDIR)/,,$(shell pwd))/$@\n"
endif
	$(Q)$(CC) $(CFLAGS) $(LDFLAGS) -Wl,-Map,$@.map -o $@ $^ $(LDLIBS$(LDLIBS-$(@)))

purify: $(OBJS)
	@$(PURIFY) $(CC) $(LDFLAGS) -o $(EXEC) $(CFLAGS) $(OBJS) $(LDLIBS)

vers.c:
	@echo $(VERSION) | sed -e 's/.*/char todaysversion[]="&";/' > vers.c

install: $(EXEC)
	# Modified in Debianization
	@install -d $(DESTDIR)/usr/sbin
	@install -d $(DESTDIR)/etc
	# install -m 0755 -f /usr/local/bin $(EXEC)
	@install -m 0755 $(EXEC) $(DESTDIR)/usr/sbin/$(EXEC)
	#- mv /etc/pimd.conf /etc/pimd.conf.old
	#cp pimd.conf /etc
	@install -m 0644 $(EXEC).conf $(DESTDIR)/etc/$(EXEC).conf
	#echo "Don't forget to check/edit /etc/pimd.conf!!!"

clean: $(SNMPCLEAN)
	-$(Q)$(RM) $(OBJS) $(EXEC)

distclean:
	-$(Q)$(RM) $(OBJS) core $(EXEC) vers.c tags TAGS *.o *.map .*.d

lint:
	@lint $(LINTFLAGS) $(SRCS)

tags: $(SRCS)
	@ctags $(SRCS)

cflow:
	@cflow $(MCAST_INCLUDE) $(SRCS) > cflow.out

cflow2:
	@cflow -ix $(MCAST_INCLUDE) $(SRCS) > cflow2.out

rcflow:
	@cflow -r $(MCAST_INCLUDE) $(SRCS) > rcflow.out

rcflow2:
	@cflow -r -ix $(MCAST_INCLUDE) $(SRCS) > rcflow2.out

TAGS:
	@etags $(SRCS)

snmpd/libsnmpd.a:
	@make -C snmpd

snmplib/libsnmp.a:
	@make -C snmplib

snmpclean:
	@for dir in snmpd snmplib; do \
		make -C $$dir clean;  \
	done

dist:
	@echo "Building bzip2 tarball of $(PKG) in parent dir..."
	git archive --format=tar --prefix=$(PKG)/ $(VERSION) | bzip2 >../$(ARCHIVE)
	@(cd ..; md5sum $(ARCHIVE))

ifneq ($(MAKECMDGOALS),clean)
-include $(DEPS)
endif
