AC_PREREQ(2.61)
AC_INIT(pimd, 2.4.0-beta1, https://github.com/troglobit/pimd/issues, , http://troglobit.com/pimd.html)
AM_INIT_AUTOMAKE([1.11 foreign])
AM_SILENT_RULES([yes])

AC_CONFIG_SRCDIR([main.c])
AC_CONFIG_HEADER([config.h])
AC_CONFIG_FILES([Makefile])

# Check for standard programs, headers, and functions
AC_PROG_CC
AC_PROG_INSTALL

AC_HEADER_STDC
AC_CHECK_HEADERS([netinet/pim.h net/if_dl.h])

AC_CHECK_FUNC([pidfile],  has_pidfile=yes, has_pidfile=no)
AC_CHECK_FUNC([strlcpy],  has_strlcpy=yes, has_strlcpy=no)
AC_CHECK_FUNC([strlcat],  has_strlcat=yes, has_strlcat=no)
AC_CHECK_FUNC([strtonum], has_strtonum=yes, has_strtonum=no)

AS_IF([test "x$has_strlcpy" = xno],
	AC_SEARCH_LIBS([strlcpy], [util],
		has_strlcpy=yes
		has_strlcat=yes
		has_strtonum=yes
		libutill=yes))

AM_CONDITIONAL(NO_PIDFILE,  [test "x$has_pidfile"  = "xno"])
AM_CONDITIONAL(NO_STRLCPY,  [test "x$has_strlcpy"  = "xno"])
AM_CONDITIONAL(NO_STRLCAT,  [test "x$has_strlcat"  = "xno"])
AM_CONDITIONAL(NO_STRTONUM, [test "x$has_strtonum" = "xno"])

#   If your RP is buggy cisco PIM-SMv2 implementation that computes
#   the PIM-Register checksum over the whole pkt instead only over
#   the header, you need to define this. Otherwise, all your PIM-Register
#   may be dropped by the cisco-RP.
AC_ARG_ENABLE(broken_crc,
	AS_HELP_STRING([--enable-broken-crc], [Interop with older broken Cisco firmware that did
			checksum in PIM Register messages slightly wrong.
			Should not be needed anymore.]))

#   Register kernel encapsulation.  Your kernel must support registers
#   kernel encapsulation to be able to use it.
AC_ARG_ENABLE(kernel_encap,
	AS_HELP_STRING([--enable-kernel-encap], [Specialized kernels do PIM Register encapsulation
			by themselves, speeding up processing considerably.
			Not on Linux/*BSD, needs pimkern-PATCH_6]))

#   (*,G) kernel MFC support.  Use it ONLY with (*,G) capable kernel
AC_ARG_ENABLE(kernel_mfc,
	AS_HELP_STRING([--enable-kernel-mfc], [Specialized kernels may have (*,G) MFC support.
			Not on Linux/*BSD, needs pimkern-PATCH_7]))

#   Saves 4 bytes per unconfigured interface per routing entry.  If set,
#   configuring such interface will restart the daemon and will flush
#   the routing table.
AC_ARG_ENABLE(memory_save,
	AS_HELP_STRING([--enable-memory-save], [Save 4 bytes/unconfigured interface/routing entry.
			Must restart pimd and cause routing table to be flushed when configuring
			new interfaces.]))

#   Scoped access control list support in pimd.conf.  If you want to
#   install NUL OIF for the "scoped groups", use the following syntax:
#   "phyint IFNAME [scoped <MCAST_ADDR> masklen <PREFIX_LEN>]", e.g.
#      phyint fxp0 scoped "addr" masklen "len"
#   Support contributed by Marian Stagarescu <marian@cidera.com>
AC_ARG_ENABLE(scoped_acls,
	AS_HELP_STRING([--enable-scoped-acls], [Scoped access control list support in pimd.conf:
			phyint IFNAME [scoped <MCGROUP> masklen <LEN>]
			phyint fxp0 scoped 239.0.0.0 masklen 8]))

AC_ARG_ENABLE(rsrr,
	AS_HELP_STRING([--enable-rsrr], [Routing Support for Resource Reservation
			currently used by RSVP (EXPERIMENTAL).  For details, see
			http://tools.ietf.org/html/draft-ietf-rsvp-routing-02]))

AC_ARG_ENABLE(pim_genid,
        AS_HELP_STRING([--disable-pim-genid], [Disable Generation ID in PIM Hello, RFC3973]))

AC_ARG_ENABLE(exit_on_error,
        AS_HELP_STRING([--disable-exit-on-error], [Do not exit on error messages (LOG_ERR)]))

AC_ARG_ENABLE(masklen_check,
        AS_HELP_STRING([--disable-masklen-check], [Allow virtual tunctl VIFs with prefix len 32]))


AC_ARG_WITH(max_vifs,
	AS_HELP_STRING([--with-max-vifs=NUM], [Kernel maximum number of allowed VIFs, default: 32.
		This must match the kernel built-in MAXVIFS value!
		Usually, multiple routing tables is what you want.]),
	[max_vifs=$withval], [max_vifs=yes])

# Create config.h from selected features and fallback defautls
AS_IF([test "x$enable_broken_crc" = "xyes"], [
        AC_DEFINE(BROKEN_CISCO_CHECKSUM, 1, [Interop with older broken Cisco firmware.])])

AS_IF([test "x$enable_kernel_encap" = "xyes"], [
        AC_DEFINE(PIM_REG_KERNEL_ENCAP, 1, [Use kernel accelerated PIM register encap/decapsulation.])])

AS_IF([test "x$enable_kernel_mfc" = "xyes"], [
        AC_DEFINE(KERNEL_MFC_WC_G, 1, [Use kernel (*,G) MFC support.])])

AS_IF([test "x$enable_memory_save" = "xyes"], [
        AC_DEFINE(SAVE_MEMORY, 1, [Save 4 bytes/unconfigured interface/routing entry.])])

AS_IF([test "x$enable_scoped_acls" = "xyes"], [
        AC_DEFINE(SCOPED_ACL, 1, [Scoped access control list support in pimd.conf.])])

AS_IF([test "x$enable_rsrr" = "xyes"], [
        AC_DEFINE(RSRR, 1, [Routing Support for Resource Reservation.])])

AS_IF([test "x$enable_pim_genid" != "xno"], [
	AC_DEFINE(ENABLE_PIM_HELLO_GENID, 1, [Enable Generation ID in PIM Hello.])])

AS_IF([test "x$enable_exit_on_error" = "xno"], [
	AC_DEFINE(CONTINUE_ON_ERROR, 1, [Do not exit on error messages (LOG_ERR).])])

AS_IF([test "x$enable_masklen_check" = "xno"], [
	AC_DEFINE(DISABLE_MASKLEN_CHECK, 1, [Allow virtual tunctl VIFs with prefix len 32.])])

AS_IF([test "x$with_max_vifs" != "xno" -a "x$max_vifs" != "xyes"], [
	AC_DEFINE_UNQUOTED(CUSTOM_MAX_VIFS, $max_vifs, [Custom MAX VIFs in kernel.])])

AM_CONDITIONAL(BSD,   [test "x$ac_cv_header_net_if_dl_h"    = "xyes"])
AM_CONDITIONAL(LINUX, [test "x$ac_cv_header_netinet_pim_h" != "xyes"])
AM_CONDITIONAL(RSRR,  [test "x$enable_rsrr"                 = "xyes"])

LIBUTIL=
AS_IF([test "x$libutil" = "yes"], [LIBUTIL=-lutil])
AC_SUBST(LIBUTIL)

# Generate all files
AC_OUTPUT
