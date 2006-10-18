#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.
AC_PREREQ(2.59)
AC_INIT([fedora-ds], [1.1], [http://bugzilla.redhat.com/])
AM_INIT_AUTOMAKE([1.9 foreign subdir-objects])
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

# Deal with platform dependent defines
case $host in
  *-*-linux*)
    platform_defs="-DXP_UNIX -DLinux -DLINUX -DLINUX2_0 -DLINUX2_2 -DLINUX2_4"
    platform="linux"
    ;;
  ia64-hp-hpux*)
    platform_defs="-DXP_UNIX -Dhpux -DHPUX -DHPUX11 -DHPUX11_11 -DCPU_ia64 -DOS_hpux -D_PR_NTHREAD"
    platform="hpux"
    ;;
  hppa*-hp-hpux*)
    platform_defs="-DXP_UNIX -Dhpux -DHPUX -DHPUX11 -DHPUX11_11 -DCPU_hppa -DOS_hpux -D_PR_NTHREAD"
    platform="hpux"
    ;;
  sparc-sun-solaris*)
    platform_defs="-DXP_UNIX -DSVR4 -D__svr4 -D__svr4__ -D_SVID_GETTOD -DSOLARIS -DCPU_sparc -DOS_solaris -Dsunos5 -DOSVERSION=509 -D_REENTRANT"
    platform="solaris"
    ;;
  *)
    platform_defs=""
    platform=""
    ;;
esac
AC_SUBST([platform_defs])
AM_CONDITIONAL(SOLARIS,test $platform = solaris)

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

BRAND=fedora
AC_MSG_CHECKING(for --with-brand)
AC_ARG_WITH(brand, [  --with-brand=PATH        Specify branding (fedora or redhat)],
[
  if test "$withval"
  then
    AC_MSG_RESULT([using $withval])
    BRAND="$withval"
  fi
],
AC_MSG_RESULT(no))
PKGNAME=$BRAND-ds
AC_PREFIX_DEFAULT("/opt/\$(PACKAGE)")

# Check for library dependencies
m4_include(m4/nspr.m4)
m4_include(m4/nss.m4)
m4_include(m4/mozldap.m4)
m4_include(m4/db.m4)
m4_include(m4/sasl.m4)
m4_include(m4/svrcore.m4)
m4_include(m4/icu.m4)
m4_include(m4/netsnmp.m4)

# write out paths for binary components
AC_SUBST(nspr_inc)
AC_SUBST(nspr_lib)
AC_SUBST(nss_inc)
AC_SUBST(nss_lib)
AC_SUBST(ldapsdk_inc)
AC_SUBST(ldapsdk_lib)
AC_SUBST(db_inc)
AC_SUBST(db_incdir)
AC_SUBST(db_lib)
AC_SUBST(sasl_inc)
AC_SUBST(sasl_lib)
AC_SUBST(svrcore_inc)
AC_SUBST(svrcore_lib)
AC_SUBST(icu_lib)
AC_SUBST(icu_inc)
AC_SUBST(icu_bin)
AC_SUBST(netsnmp_inc)
AC_SUBST(netsnmp_lib)
AC_SUBST(netsnmp_link)

# WINNT should be true if building on Windows system not using
# cygnus, mingw, or the like and using cmd.exe as the shell
AM_CONDITIONAL([WINNT], false)

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
