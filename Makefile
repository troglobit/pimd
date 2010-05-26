# -*-Makefile-*-
#
# Protocol Independent Multicast, Sparse-Mode version 2.0
#
# 2010-01-16  Joachim Nilsson <jocke@vmlinux.org>
#       * Cleanup and refactoring for cross building.
#
# 2003-03-27  Antonin Kral <A.Kral@sh.cvut.cz>
# 	* Modified for Debian
#
# 2001-11-13  Pavlin Ivanov Radoslavov (pavlin@catarina.usc.edu)
#       * Original Makefile
#

# VERSION       ?= $(shell git tag -l | tail -1)
VERSION       ?= 2.1.2-rc1
EXEC           = pimd
PKG            = $(EXEC)-$(VERSION)
ARCHIVE        = $(PKG).tar.bz2

ROOTDIR       ?= $(dir $(shell pwd))
CC             = $(CROSS)gcc

prefix        ?= /usr/local
sysconfdir    ?= /etc
datadir        = $(prefix)/share/doc/pimd
mandir         = $(prefix)/share/man/man8

IGMP_OBJS      = igmp.o igmp_proto.o trace.o
ROUTER_OBJS    = inet.o kern.o main.o config.o debug.o netlink.o routesock.o \
		 vers.o callout.o
ifndef HAVE_STRLCPY
ROUTER_OBJS   += strlcpy.o
endif
ifndef HAVE_PIDFILE
ROUTER_OBJS  += pidfile.o
endif
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
		 CHANGES INSTALL LICENSE LICENSE.mrouted \
		 TODO CREDITS FAQ AUTHORS

include rules.mk
include config.mk
include snmp.mk

MCAST_INCLUDE  = -Iinclude
PURIFY         = purify -cache-dir=/tmp -collector=/import/pkgs/gcc/lib/gcc-lib/sparc-sun-sunos4.1.3_U1/2.7.2.2/ld
MISCDEFS       = -D__BSD_SOURCE -D_GNU_SOURCE -DPIM
MISCDEFS      += -W -Wall -Werror -Wextra
COMMON_CFLAGS  = $(MCAST_INCLUDE) $(SNMPDEF) $(RSRRDEF) $(MISCDEFS)
CFLAGS         = $(INCLUDES) $(DEFS) $(COMMON_CFLAGS) $(USERCOMPILE)

LDLIBS         = $(SNMPLIBDIR) $(SNMPLIBS) $(LIB2)

LINT           = splint
LINTFLAGS      = $(MCAST_INCLUDE) $(filter-out -W -Wall -g, $(CFLAGS)) -posix-lib -weak -skipposixheaders

all: $(EXEC)

$(EXEC): $(OBJS) $(CMULIBS)
ifdef Q
	@printf "  LINK    $(subst $(ROOTDIR),,$(shell pwd))/$@\n"
endif
	$(Q)$(CC) $(CFLAGS) $(LDFLAGS) -Wl,-Map,$@.map -o $@ $^ $(LDLIBS$(LDLIBS-$(@)))

purify: $(OBJS)
	@$(PURIFY) $(CC) $(LDFLAGS) -o $(EXEC) $(CFLAGS) $(OBJS) $(LDLIBS)

vers.c:
	@echo $(VERSION) | sed -e 's/.*/char todaysversion[]="&";/' > vers.c

install: $(EXEC)
	$(Q)[ -n "$(DESTDIR)" -a ! -d $(DESTDIR) ] || install -d $(DESTDIR)
	$(Q)install -d $(DESTDIR)$(prefix)/sbin
	$(Q)install -d $(DESTDIR)$(sysconfdir)
	$(Q)install -d $(DESTDIR)$(datadir)
	$(Q)install -d $(DESTDIR)$(mandir)
	$(Q)install -m 0755 $(EXEC) $(DESTDIR)$(prefix)/sbin/$(EXEC)
	$(Q)install --backup=existing -m 0644 $(EXEC).conf $(DESTDIR)$(sysconfdir)/$(EXEC).conf
	$(Q)for file in $(DISTFILES); do \
		install -m 0644 $$file $(DESTDIR)$(datadir)/$$file; \
	done
	$(Q)install -m 0644 $(EXEC).8 $(DESTDIR)$(mandir)/$(EXEC).8

uninstall:
	-$(Q)$(RM) $(DESTDIR)$(prefix)/sbin/$(EXEC)
	-$(Q)$(RM) $(DESTDIR)$(sysconfdir)/$(EXEC).conf
	-$(Q)$(RM) -r $(DESTDIR)$(datadir)
	-$(Q)$(RM) $(DESTDIR)$(mandir)/$(EXEC).8

clean: $(SNMPCLEAN)
	-$(Q)$(RM) $(OBJS) $(EXEC)

distclean:
	-$(Q)$(RM) $(OBJS) core $(EXEC) vers.c tags TAGS *.o *.map .*.d *.out tags TAGS

dist:
	@echo "Building bzip2 tarball of $(PKG) in parent dir..."
	git archive --format=tar --prefix=$(PKG)/ $(VERSION) | bzip2 >../$(ARCHIVE)
	@(cd ..; md5sum $(ARCHIVE))

lint:
	@$(LINT) $(LINTFLAGS) $(SRCS)

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

build-deb:
	git-buildpackage --git-ignore-new --git-upstream-branch=master

ifneq ($(MAKECMDGOALS),clean)
-include $(DEPS)
endif
