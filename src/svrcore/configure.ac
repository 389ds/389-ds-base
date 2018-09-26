#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT(svrcore, 4.1.3, [wibrown@redhat.com, rmeggins@redhat.com], svrcore)
AM_INIT_AUTOMAKE([gnu check-news dist-bzip2])
AM_MAINTAINER_MODE
AC_CONFIG_SRCDIR([src/svrcore.h])
AC_CONFIG_HEADER([config.h])

AC_PROG_CC
AC_PROG_LIBTOOL

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADER([string.h],,[AC_ERROR([We need string.h in order to build])])
AC_CHECK_HEADER([termios.h],,[AC_ERROR([We need termios.h in order to build])])
AC_CHECK_HEADER([unistd.h],,[AC_ERROR([We need unistd.h in order to build])])
AC_PROG_CC_STDC

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST

# Checks for library functions.
AC_FUNC_MALLOC
AC_CHECK_FUNCS([memset strchr strdup])

# Check for library dependencies
m4_include(m4/nss.m4)
m4_include(m4/nspr.m4)
m4_include(m4/systemd.m4)

AC_MSG_CHECKING(for --enable-debug)
AC_ARG_ENABLE(debug, AS_HELP_STRING([--enable-debug], [Enable debug features (default: no)]),
[
  AC_MSG_RESULT(yes)
  debug_defs="-g3 -DDEBUG"
],
[
  AC_MSG_RESULT(no)
  debug_defs=""
])
AC_SUBST([debug_defs])

AC_MSG_CHECKING(for --enable-asan)
AC_ARG_ENABLE(asan, AS_HELP_STRING([--enable-asan], [Enable gcc address sanitizer options (default: no)]),
[
  AC_MSG_RESULT(yes)
  asan_cflags="-fsanitize=address -fno-omit-frame-pointer -lasan"
],
[
  AC_MSG_RESULT(no)
  asan_cflags=""
])
AC_SUBST([asan_cflags])

AC_SUBST([NSPR_CFLAGS])
AC_SUBST([NSPR_LIBS])
AC_SUBST([NSPR_MIN_VER])
AC_SUBST([NSPR_NAME])
AC_SUBST([NSS_CFLAGS])
AC_SUBST([NSS_LIBS])
AC_SUBST([NSS_MIN_VER])
AC_SUBST([NSS_NAME])
AC_SUBST([SYSTEMD_CFLAGS])
AC_SUBST([PACKAGE_VERSION])

AC_CONFIG_FILES([src/Makefile Makefile svrcore.pc])
AC_OUTPUT
