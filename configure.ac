#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.
AC_PREREQ(2.59)
AC_INIT([fedora-ds], [1.1.0], [http://bugzilla.redhat.com/])
# AC_CONFIG_HEADER must be called right after AC_INIT.
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE([1.9 foreign subdir-objects])
AM_MAINTAINER_MODE
AC_CANONICAL_HOST

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_LIBTOOL

# Checks for header files.
AC_HEADER_DIRENT
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([arpa/inet.h fcntl.h malloc.h netdb.h netinet/in.h stdlib.h string.h strings.h sys/file.h sys/socket.h sys/time.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STAT
AC_C_CONST
AC_HEADER_STDBOOL
AC_TYPE_UID_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_HEADER_TIME
AC_STRUCT_TM

# Checks for library functions.
AC_FUNC_CHOWN
AC_FUNC_CLOSEDIR_VOID
AC_FUNC_ERROR_AT_LINE
AC_FUNC_FORK
AC_FUNC_LSTAT
AC_FUNC_LSTAT_FOLLOWS_SLASHED_SYMLINK
AC_FUNC_MALLOC
AC_FUNC_MEMCMP
AC_FUNC_MMAP
AC_TYPE_SIGNAL
AC_FUNC_STAT
AC_FUNC_STRERROR_R
AC_FUNC_STRFTIME
AC_FUNC_VPRINTF
AC_CHECK_FUNCS([setrlimit endpwent ftruncate getcwd gethostbyname inet_ntoa localtime_r memmove memset mkdir munmap putenv rmdir socket strcasecmp strchr strcspn strdup strerror strncasecmp strpbrk strrchr strstr strtol tzset])

AC_MSG_CHECKING(for --enable-debug)
AC_ARG_ENABLE(debug, [  --enable-debug         Enable debug features],
[
  AC_MSG_RESULT(yes)
  debug_defs="-DDEBUG -DMCC_DEBUG"
],
[
  AC_MSG_RESULT(no)
  debug_defs=""
])
AC_SUBST([debug_defs])

AC_PREFIX_DEFAULT([/opt/$PACKAGE_NAME])

# installation paths - by default, configure will just
# use /usr as the prefix for everything, which means
# /usr/etc and /usr/var.  FHS sez to use /etc and /var.
if test "$with_fhs" = "yes"; then
  ac_default_prefix=/usr
  prefix=$ac_default_prefix
  exec_prefix=$prefix
dnl as opposed to the default /usr/etc
  sysconfdir='/etc'
dnl as opposed to the default /usr/var
  localstatedir='/var'
fi

# relative to sysconfdir
configdir=/$PACKAGE_NAME/config
# relative to datadir
sampledatadir=/$PACKAGE_NAME/data
# relative to sysconfdir
propertydir=/$PACKAGE_NAME/property
# relative to sysconfdir
schemadir=/$PACKAGE_NAME/schema
# relative to libdir
serverdir=/$PACKAGE_NAME
# relative to libdir
serverplugindir=/$PACKAGE_NAME/plugins
# relative to datadir
scripttemplatedir=/$PACKAGE_NAME/script-templates
AC_SUBST(configdir)
AC_SUBST(sampledatadir)
AC_SUBST(propertydir)
AC_SUBST(schemadir)
AC_SUBST(serverdir)
AC_SUBST(serverplugindir)
AC_SUBST(scripttemplatedir)

# WINNT should be true if building on Windows system not using
# cygnus, mingw, or the like and using cmd.exe as the shell
AM_CONDITIONAL([WINNT], false)

# Deal with platform dependent defines
# relative to sysconfdir
initdir=/rc.d
case $host in
  *-*-linux*)
    AC_DEFINE([XP_UNIX], [1], [UNIX])
    AC_DEFINE([Linux], [1], [Linux])
    AC_DEFINE([LINUX], [1], [Linux])
    AC_DEFINE([LINUX2_0], [1], [Linux 2.0])
    AC_DEFINE([LINUX2_2], [1], [Linux 2.2])
    AC_DEFINE([LINUX2_4], [1], [Linux 2.4])
    platform="linux"
    # relative to sysconfdir
    initdir=/rc.d/init.d
    ;;
  ia64-hp-hpux*)
    AC_DEFINE([XP_UNIX], [1], [UNIX])
    AC_DEFINE([hpux], [1], [HP-UX])
    AC_DEFINE([HPUX], [1], [HP-UX])
    AC_DEFINE([HPUX11], [1], [HP-UX 11])
    AC_DEFINE([HPUX11_23], [1], [HP-UX 11.23])
    AC_DEFINE([CPU_ia64], [], [cpu type ia64])
    AC_DEFINE([OS_hpux], [1], [OS HP-UX])
    platform="hpux"
    ;;
  hppa*-hp-hpux*)
    AC_DEFINE([XP_UNIX], [1], [UNIX])
    AC_DEFINE([hpux], [1], [HP-UX])
    AC_DEFINE([HPUX], [1], [HP-UX])
    AC_DEFINE([HPUX11], [1], [HP-UX 11])
    AC_DEFINE([HPUX11_11], [1], [HP-UX 11.11])
    AC_DEFINE([CPU_hppa], [], [cpu type pa-risc])
    AC_DEFINE([OS_hpux], [1], [OS HP-UX])
    platform="hpux"
    ;;
  sparc-sun-solaris*)
    AC_DEFINE([XP_UNIX], [1], [UNIX])
    AC_DEFINE([SVR4], [1], [SVR4])
    AC_DEFINE([__svr4], [1], [SVR4])
    AC_DEFINE([__svr4__], [1], [SVR4])
    AC_DEFINE([_SVID_GETTOD], [1], [SVID_GETTOD])
    AC_DEFINE([SOLARIS], [1], [SOLARIS])
    AC_DEFINE([CPU_sparc], [], [cpu type sparc])
    AC_DEFINE([OS_solaris], [1], [OS SOLARIS])
    AC_DEFINE([sunos5], [1], [SunOS5])
    AC_DEFINE([OSVERSION], [509], [OS version])
    AC_DEFINE([_REENTRANT], [1], [_REENTRANT])
    LIBSOCKET=-lsocket
    AC_SUBST([LIBSOCKET], [$LIBSOCKET])
    LIBNSL=-lnsl
    AC_SUBST([LIBNSL], [$LIBNSL])
    LIBDL=-ldl
    AC_SUBST([LIBDL], [$LIBDL])
    platform="solaris"
    # relative to sysconfdir
    initdir=/init.d
    ;;
  *)
    platform=""
    ;;
esac
AC_SUBST(initdir)

AM_CONDITIONAL(SOLARIS,test "$platform" = "solaris")

# Check for library dependencies
m4_include(m4/nspr.m4)
m4_include(m4/nss.m4)
m4_include(m4/mozldap.m4)
m4_include(m4/db.m4)
m4_include(m4/sasl.m4)
m4_include(m4/svrcore.m4)
m4_include(m4/icu.m4)
m4_include(m4/netsnmp.m4)
m4_include(m4/fhs.m4)

# write out paths for binary components
AC_SUBST(nspr_inc)
AC_SUBST(nspr_lib)
AC_SUBST(nspr_libdir)
AC_SUBST(nss_inc)
AC_SUBST(nss_lib)
AC_SUBST(nss_libdir)
AC_SUBST(ldapsdk_inc)
AC_SUBST(ldapsdk_lib)
AC_SUBST(ldapsdk_libdir)
AC_SUBST(ldapsdk_bindir)
AC_SUBST(db_inc)
AC_SUBST(db_incdir)
AC_SUBST(db_lib)
AC_SUBST(db_libdir)
AC_SUBST(db_bindir)
AC_SUBST(db_libver)
AC_SUBST(sasl_inc)
AC_SUBST(sasl_lib)
AC_SUBST(sasl_libdir)
AC_SUBST(svrcore_inc)
AC_SUBST(svrcore_lib)
AC_SUBST(icu_lib)
AC_SUBST(icu_inc)
AC_SUBST(icu_bin)
AC_SUBST(netsnmp_inc)
AC_SUBST(netsnmp_lib)
AC_SUBST(netsnmp_libdir)
AC_SUBST(netsnmp_link)

AC_DEFINE([LDAP_DEBUG], [1], [LDAP debug flag])
AC_DEFINE([LDAP_DONT_USE_SMARTHEAP], [1], [Don't use smartheap])

AC_CONFIG_FILES([Makefile])

AC_OUTPUT
