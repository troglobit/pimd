doc_DATA       = README.md ChangeLog.md LICENSE doc/LICENSE.mrouted pimd.conf
EXTRA_DIST     = README.md ChangeLog.md LICENSE doc/LICENSE.mrouted pimd.conf
DISTCLEANFILES = *~ DEADJOE semantic.cache *.gdb *.elf core core.* *.d
SUBDIRS        = src include man
TAG            = $(top_srcdir)/.git/refs/tags/$(PACKAGE_VERSION)

if ENABLE_TEST
SUBDIRS       += test
endif

if SYSTEMD
systemd_DATA   = pimd.service
endif

## Generate .deb package
package build-deb:
	@debuild -uc -us -B --lintian-opts --profile debian -i -I

## Check if tagged in git
$(TAG):
	@printf "\e[1m\e[41mPlease create the \"$(PACKAGE_VERSION)\" tag first\e[0m\n"
	@exit 1

## Target to run when building a release
release: $(TAG) distcheck
	@for file in $(DIST_ARCHIVES); do		\
		md5sum    $$file > ../$$file.md5;	\
		sha256sum $$file > ../$$file.sha256;	\
	done
	@mv $(DIST_ARCHIVES) ../
	@echo
	@echo "Resulting release files ======================================================================="
	@for file in $(DIST_ARCHIVES); do						\
		printf "%-30s Distribution tarball\n" $$file;				\
		printf "%-30s " $$file.md5;    cat ../$$file.md5    | cut -f1 -d' ';	\
		printf "%-30s " $$file.sha256; cat ../$$file.sha256 | cut -f1 -d' ';	\
	done

DISTCHECK_CONFIGURE_FLAGS = --with-systemd=$$dc_install_base/$(systemd)
