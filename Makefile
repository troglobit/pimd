# -*-Makefile-*-
#
# Note: We use .gz for distribution archives for the sake of
#       OpenBSD not having bzip2 in the base distribution.
#

# VERSION      ?= $(shell git tag -l | tail -1)
VERSION      ?= 2.3.0-dev
EXEC          = pimd
CONFIG        = $(EXEC).conf
PKG           = $(EXEC)-$(VERSION)
ARCHTOOL      = `which git-archive-all`
ARCHIVE       = $(PKG).tar
ARCHIVEZ      = ../$(ARCHIVE).gz

ROOTDIR      ?= $(dir $(shell pwd))
RM           ?= rm -f
CC           ?= $(CROSS)gcc

prefix       ?= /usr/local
sysconfdir   ?= /etc
datadir       = $(prefix)/share/doc/pimd
mandir        = $(prefix)/share/man/man8


IGMP_OBJS     = igmp.o igmp_proto.o trace.o
ROUTER_OBJS   = inet.o kern.o main.o config.o debug.o vers.o callout.o
PIM_OBJS      = route.o vif.o timer.o mrt.o pim.o pim_proto.o rp.o
DVMRP_OBJS    = dvmrp_proto.o

# This magic trick looks like a comment, but works on BSD PMake
#include <config.mk>
include config.mk

## Common
CFLAGS       += $(INCLUDES) $(DEFS) $(USERCOMPILE)
CFLAGS       += -O2 -W -Wall -Werror -fno-strict-aliasing
#CFLAGS       += -O -g
LDLIBS        = $(EXTRA_LIBS)
OBJS          = $(IGMP_OBJS) $(ROUTER_OBJS) $(PIM_OBJS) $(DVMRP_OBJS) $(EXTRA_OBJS)
SRCS          = $(OBJS:.o=.c)
DEPS          = $(addprefix .,$(SRCS:.c=.d))
MANS          = $(addsuffix .8,$(EXEC))
DISTFILES     = README.md README-config.md README.config.jp README-debug.md ChangeLog.org \
		INSTALL LICENSE LICENSE.mrouted TODO.org CREDITS FAQ.md AUTHORS

LINT          = splint
LINTFLAGS     = $(MCAST_INCLUDE) $(filter-out -W -Wall -Werror, $(CFLAGS)) -posix-lib -weak -skipposixheaders

PURIFY        = purify
PURIFYFLAGS   = -cache-dir=/tmp -collector=/import/pkgs/gcc/lib/gcc-lib/sparc-sun-sunos4.1.3_U1/2.7.2.2/ld

all: $(EXEC)

.c.o:
	@printf "  CC      $@\n"
	@$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(EXEC): $(OBJS)
	@printf "  LINK    $@\n"
	@$(CC) $(CFLAGS) $(LDFLAGS) -Wl,-Map,$@.map -o $@ $(OBJS) $(LDLIBS)

purify: $(OBJS)
	@$(PURIFY) $(PURIFYFLAGS) $(CC) $(LDFLAGS) -o $(EXEC) $(CFLAGS) $(OBJS) $(LDLIBS)

vers.c:
	@echo $(VERSION) | sed -e 's/.*/char todaysversion[]="&";/' > vers.c

install: $(EXEC)
	@install -d $(DESTDIR)$(prefix)/sbin
	@install -d $(DESTDIR)$(sysconfdir)
	@install -d $(DESTDIR)$(datadir)
	@install -d $(DESTDIR)$(mandir)
	@install -m 0755 $(EXEC) $(DESTDIR)$(prefix)/sbin/$(EXEC)
	@install -b -m 0644 $(CONFIG) $(DESTDIR)$(sysconfdir)/$(CONFIG)
	@for file in $(DISTFILES); do \
		install -m 0644 $$file $(DESTDIR)$(datadir)/$$file; \
	done
	@for file in $(MANS); do \
		install -m 0644 $$file $(DESTDIR)$(mandir)/$$file; \
	done

uninstall:
	-@$(RM) $(DESTDIR)$(prefix)/sbin/$(EXEC)
	-@$(RM) $(DESTDIR)$(sysconfdir)/$(CONFIG)
	-@$(RM) -r $(DESTDIR)$(datadir)
	@for file in $(DISTFILES); do \
		$(RM) $(DESTDIR)$(datadir)/$$file; \
	done
	-@for file in $(MANS); do \
		$(RM) $(DESTDIR)$(mandir)/$$file; \
	done

clean:
	-@$(RM) $(OBJS) $(EXEC)

distclean:
	-@$(RM) $(OBJS) core $(EXEC) vers.c config.mk tags TAGS *.o *.map .*.d *.out tags

dist:
	@if [ x"$(ARCHTOOL)" = x"" ]; then \
		echo "Missing git-archive-all from https://github.com/Kentzo/git-archive-all"; \
		exit 1; \
	fi
	@if [ -e $(ARCHIVEZ) ]; then \
		echo "Distribution $(ARCHIVEZ) already exists."; \
		exit 1; \
	fi
	@echo "Building xz tarball of $(PKG) in parent dir ..."
	@$(ARCHTOOL) ../$(ARCHIVE)
	@gzip ../$(ARCHIVE)
	@md5sum $(ARCHIVEZ) | tee $(ARCHIVEZ).md5

build-deb:
	@echo "Building .deb if $(PKG)..."
	git-buildpackage --git-ignore-new --git-upstream-branch=master

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

