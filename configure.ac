AC_PREREQ(2.61)
AC_INIT(pimd, 3.0-beta1, https://github.com/troglobit/pimd/issues, , http://troglobit.com/pimd.html)
AM_INIT_AUTOMAKE([1.11 foreign])
AM_SILENT_RULES([yes])

AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_HEADER([config.h])
AC_CONFIG_FILES([Makefile src/Makefile include/Makefile man/Makefile pimd.service])

# Check for standard programs, headers, and functions
AC_PROG_CC
AC_PROG_INSTALL

# Required to check for libsystemd-dev
PKG_PROG_PKG_CONFIG

AC_HEADER_STDC
AC_CHECK_HEADERS([fcntl.h ifaddrs.h netinet/pim.h net/if_dl.h sys/time.h linux/netlink.h termios.h])
AC_CHECK_HEADERS([net/if.h], [], [], [
#include <stdio.h>
#ifdef STDC_HEADERS
# include <stdlib.h>
# include <stddef.h>
#else
# ifdef HAVE_STDLIB_H
#  include <stdlib.h>
# endif
#endif
#ifdef HAVE_SYS_SOCKET_H
# include <sys/socket.h>
#endif])

# Check if some func is not in libc
AC_CHECK_LIB([util], [pidfile])

# Check for required functions in libc
AC_CHECK_FUNCS([atexit getifaddrs if_nametoindex])

# Check for usually missing API's, which we can replace
AC_REPLACE_FUNCS([pidfile strlcpy strlcat strtonum utimensat])
AC_CONFIG_LIBOBJ_DIR([lib])

#   Register kernel encapsulation.  Your kernel must support registers
#   kernel encapsulation to be able to use it.
AC_ARG_ENABLE(kernel_encap,
	AS_HELP_STRING([--enable-kernel-encap], [Specialized kernels do PIM Register encapsulation
			by themselves, speeding up processing considerably.
			Not on Linux/*BSD, needs pimkern-PATCH_6]),,
	enable_kernel_encap=no)

#   (*,G) kernel MFC support.  Use it ONLY with (*,G) capable kernel
AC_ARG_ENABLE(kernel_mfc,
	AS_HELP_STRING([--enable-kernel-mfc], [Specialized kernels may have (*,G) MFC support.
			Not on Linux/*BSD, needs pimkern-PATCH_7]),,
	enable_kernel_mfc=no)

#   Saves 4 bytes per unconfigured interface per routing entry.  If set,
#   configuring such interface will restart the daemon and will flush
#   the routing table.
AC_ARG_ENABLE(memory_save,
	AS_HELP_STRING([--enable-memory-save], [Save 4 bytes/unconfigured interface/routing entry.
			Must restart pimd and cause routing table to be flushed when configuring
			new interfaces.]),,
	enable_memory_save=no)

AC_ARG_ENABLE(rsrr,
	AS_HELP_STRING([--enable-rsrr], [Routing Support for Resource Reservation
			currently used by RSVP (EXPERIMENTAL).  For details, see
			http://tools.ietf.org/html/draft-ietf-rsvp-routing-02]),,
	enable_rsrr=no)

AC_ARG_ENABLE(exit_on_error,
        AS_HELP_STRING([--disable-exit-on-error], [Do not exit on error messages (LOG_ERR)]),,
	enable_exit_on_error=yes)

AC_ARG_WITH(max_vifs,
	AS_HELP_STRING([--with-max-vifs=NUM], [Kernel maximum number of allowed VIFs, default: 32.
		This must match the kernel built-in MAXVIFS value!
		Usually, multiple routing tables is what you want.]),
	[max_vifs=$withval], [max_vifs=yes])

AC_ARG_WITH([systemd],
     [AS_HELP_STRING([--with-systemd=DIR], [Directory for systemd service files])],,
     [with_systemd=auto])

# Create config.h from selected features and fallback defautls
AS_IF([test "x$enable_kernel_encap" = "xyes"], [
        AC_DEFINE(PIM_REG_KERNEL_ENCAP, 1, [Use kernel accelerated PIM register encap/decapsulation.])])

AS_IF([test "x$enable_kernel_mfc" = "xyes"], [
        AC_DEFINE(KERNEL_MFC_WC_G, 1, [Use kernel (*,G) MFC support.])])

AS_IF([test "x$enable_memory_save" = "xyes"], [
        AC_DEFINE(SAVE_MEMORY, 1, [Save 4 bytes/unconfigured interface/routing entry.])])

AS_IF([test "x$enable_rsrr" = "xyes"], [
        AC_DEFINE(RSRR, 1, [Routing Support for Resource Reservation.])])

AS_IF([test "x$enable_exit_on_error" = "xno"], [
	AC_DEFINE(CONTINUE_ON_ERROR, 1, [Do not exit on error messages (LOG_ERR).])])

AS_IF([test "x$with_max_vifs" != "xno" -a "x$max_vifs" != "xyes"], [
	AC_DEFINE_UNQUOTED(CUSTOM_MAX_VIFS, $max_vifs, [Custom MAX VIFs in kernel.])], [
	max_vifs=32])

AS_IF([test "x$with_systemd" = "xyes" -o "x$with_systemd" = "xauto"], [
     def_systemd=$($PKG_CONFIG --variable=systemdsystemunitdir systemd)
     AS_IF([test "x$def_systemd" = "x"],
         [AS_IF([test "x$with_systemd" = "xyes"],
	     [AC_MSG_ERROR([systemd support requested but pkg-config unable to query systemd package])])
	     with_systemd=no], [with_systemd="$def_systemd"])]
)
AS_IF([test "x$with_systemd" != "xno"],
     [AC_SUBST([systemddir], [$with_systemd])])

AM_CONDITIONAL(BSD,     [test "x$ac_cv_header_net_if_dl_h"     = "xyes"])
AM_CONDITIONAL(LINUX,   [test "x$ac_cv_header_linux_netlink_h" = "xyes"])
AM_CONDITIONAL(RSRR,    [test "x$enable_rsrr"                  = "xyes"])
AM_CONDITIONAL(SYSTEMD, [test "x$with_systemd"                != "xno"])

# Expand $sbindir early, into $SBINDIR, for systemd unit file
# NOTE: This does *not* take prefix/exec_prefix override at "make
#       install" into account, unfortunately.
test "x$prefix" = xNONE && prefix=$ac_default_prefix
test "x$exec_prefix" = xNONE && exec_prefix='${prefix}'
DOCDIR=`eval echo $docdir`
DOCDIR=`eval echo $DOCDIR`
AC_SUBST(DOCDIR)
SYSCONFDIR=`eval echo $sysconfdir`
SYSCONFDIR=`eval echo $SYSCONFDIR`
AC_SUBST(SYSCONFDIR)
SBINDIR=`eval echo $sbindir`
SBINDIR=`eval echo $SBINDIR`
AC_SUBST(SBINDIR)

# Generate all files
AC_OUTPUT

cat <<EOF

------------------ Summary ------------------
 $PACKAGE_NAME version $PACKAGE_VERSION
  Prefix................: $prefix
  Sysconfdir............: `eval echo $sysconfdir`
  Localstatedir.........: `eval echo $localstatedir`
  C Compiler............: $CC $CFLAGS $CPPFLAGS $LDFLAGS $LIBS

Optional features:
  Kernel register encap.: $enable_kernel_encap
  Kernel (*,G) support..: $enable_kernel_mfc
  Kernel MAX VIFs.......: $max_vifs
  Memory save...........: $enable_memory_save
  RSRR (experimental)...: $enable_rsrr
  Exit on error.........: $enable_exit_on_error

------------- Compiler version --------------
$($CC --version || true)
---------------------------------------------

Check the above options and compile with:
 ${MAKE-make}

EOF
