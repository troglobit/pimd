dist_man8_MANS   = pimd.8
doc_DATA         = README.md README-debug.md README-config.md README.config.jp		\
		   ChangeLog.org LICENSE LICENSE.mrouted pimd.conf
EXTRA_DIST       = README.md README-debug.md README-config.md README.config.jp		\
		   ChangeLog.org LICENSE LICENSE.mrouted pimd.conf autogen.sh		\
		   include/netinet/pim.h include/netinet/pim_var.h
DISTCLEANFILES   = *~ DEADJOE semantic.cache *.gdb *.elf core core.* *.d
SUBDIRS          = src include

if SYSTEMD
systemd_DATA     = pimd.service
endif

