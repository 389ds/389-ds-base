#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.
AC_PREREQ(2.59)
AC_INIT([dirsrv],[1.0],[http://bugzilla.redhat.com/])
# AC_CONFIG_HEADER must be called right after AC_INIT.
AC_CONFIG_HEADERS([config.h])
# include the version information
. $srcdir/VERSION.sh
AC_MSG_NOTICE(This is configure for $PACKAGE_TARNAME $PACKAGE_VERSION)
AC_DEFINE_UNQUOTED([DS_PACKAGE_VERSION], "$PACKAGE_VERSION", [package version])
AC_DEFINE_UNQUOTED([DS_PACKAGE_TARNAME], "$PACKAGE_TARNAME", [package tarball name])
AC_DEFINE_UNQUOTED([DS_PACKAGE_BUGREPORT], "$PACKAGE_BUGREPORT", [package bug report url])
AC_DEFINE_UNQUOTED([DS_PACKAGE_STRING], "$PACKAGE_STRING", [package string])
AM_INIT_AUTOMAKE([1.9 foreign subdir-objects dist-bzip2 no-dist-gzip no-define])
# define these for automake distdir
VERSION=$PACKAGE_VERSION
PACKAGE=$PACKAGE_TARNAME
AC_DEFINE_UNQUOTED([VERSION], "$VERSION", [package version])
AC_DEFINE_UNQUOTED([PACKAGE], "$PACKAGE", [package tar name])
AC_SUBST([CONSOLE_VERSION])
AM_MAINTAINER_MODE
AC_CANONICAL_HOST

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC
AM_PROG_CC_C_O
AM_PROG_AS
# disable static libs by default - we only use a couple
AC_DISABLE_STATIC
AC_PROG_LIBTOOL

# Checks for header files.
AC_HEADER_DIRENT
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([arpa/inet.h fcntl.h malloc.h netdb.h netinet/in.h stdlib.h string.h strings.h sys/file.h sys/socket.h sys/time.h unistd.h inttypes.h mntent.h])

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
AC_ARG_ENABLE(debug, AS_HELP_STRING([--enable-debug], [Enable debug features (default: no)]),
[
  AC_MSG_RESULT(yes)
  debug_defs="-DDEBUG -DMCC_DEBUG"
],
[
  AC_MSG_RESULT(no)
  debug_defs=""
])
AC_SUBST([debug_defs])

# Used for legacy style packaging where we bundle all of the dependencies.
AC_MSG_CHECKING(for --enable-bundle)
AC_ARG_ENABLE(bundle, AS_HELP_STRING([--enable-bundle], [Enable bundled dependencies (default: no)]),
[
  AC_MSG_RESULT(yes)
  bundle="1";
],
[
  AC_MSG_RESULT(no)
  bundle="";
])
AM_CONDITIONAL(BUNDLE,test "$bundle" = "1")

# these enables are for optional or experimental features
if test -z "$enable_pam_passthru" ; then
   enable_pam_passthru=yes # if not set on cmdline, set default
fi
AC_MSG_CHECKING(for --enable-pam-passthru)
AC_ARG_ENABLE(pam-passthru,
        AS_HELP_STRING([--enable-pam-passthru],
                       [enable the PAM passthrough auth plugin (default: yes)]))
if test "$enable_pam_passthru" = yes ; then
  # check for pam header file used by plugins/pass_passthru/pam_ptimpl.c
  AC_CHECK_HEADER([security/pam_appl.h], [], [AC_MSG_ERROR([Missing header file security/pam_appl.h])])
  AC_MSG_RESULT(yes)
  AC_DEFINE([ENABLE_PAM_PASSTHRU], [1], [enable the pam passthru auth plugin])
else
  AC_MSG_RESULT(no)
fi
AM_CONDITIONAL(enable_pam_passthru,test "$enable_pam_passthru" = "yes")

if test -z "$enable_dna" ; then
   enable_dna=yes # if not set on cmdline, set default
fi
AC_MSG_CHECKING(for --enable-dna)
AC_ARG_ENABLE(dna,
        AS_HELP_STRING([--enable-dna],
                       [enable the Distributed Numeric Assignment (DNA) plugin (default: yes)]))
if test "$enable_dna" = yes ; then
  AC_MSG_RESULT(yes)
  AC_DEFINE([ENABLE_DNA], [1], [enable the dna plugin])
else
  AC_MSG_RESULT(no)
fi
AM_CONDITIONAL(enable_dna,test "$enable_dna" = "yes")

if test -z "$enable_ldapi" ; then
   enable_ldapi=yes # if not set on cmdline, set default
fi
AC_MSG_CHECKING(for --enable-ldapi)
AC_ARG_ENABLE(ldapi,
        AS_HELP_STRING([--enable-ldapi],
                       [enable LDAP over unix domain socket (LDAPI) support (default: yes)]))
if test "$enable_ldapi" = yes ; then
  AC_MSG_RESULT(yes)
  AC_DEFINE([ENABLE_LDAPI], [1], [enable ldapi support in the server])
else
  AC_MSG_RESULT(no)
fi
AM_CONDITIONAL(enable_ldapi,test "$enable_ldapi" = "yes")

if test -z "$enable_autobind" ; then
   enable_autobind=no # if not set on cmdline, set default
fi
AC_MSG_CHECKING(for --enable-autobind)
AC_ARG_ENABLE(autobind,
        AS_HELP_STRING([--enable-autobind],
                       [enable auto bind over unix domain socket (LDAPI) support (default: no)]))
if test "$enable_ldapi" = yes -a "$enable_autobind" = yes ; then
  AC_MSG_RESULT(yes)
  AC_DEFINE([ENABLE_AUTOBIND], [1], [enable ldapi auto bind support in the server])
else
  AC_MSG_RESULT(no)
fi
AM_CONDITIONAL(enable_autobind,test "$enable_autobind" = "yes")

if test -z "$enable_auto_dn_suffix" ; then
   enable_auto_dn_suffix=no # if not set on cmdline, set default
fi
AC_MSG_CHECKING(for --enable-auto-dn-suffix)
AC_ARG_ENABLE(autobind,
        AS_HELP_STRING([--enable-auto-dn-suffix],
                       [enable auto bind with auto dn suffix over unix domain socket (LDAPI) support (default: no)]))
if test "$enable_ldapi" = yes -a "$enable_autobind" = yes -a "$enable_auto_dn_suffix" = "yes"; then
  AC_MSG_RESULT(yes)
  AC_DEFINE([ENABLE_AUTO_DN_SUFFIX], [1], [enable ldapi auto bind with auto dn suffix support in the server])
else
  AC_MSG_RESULT(no)
fi
AM_CONDITIONAL(enable_auto_dn_suffix,test "$enable_auto_dn_suffix" = "yes")

if test -z "$enable_bitwise" ; then
   enable_bitwise=yes # if not set on cmdline, set default
fi
AC_MSG_CHECKING(for --enable-bitwise)
AC_ARG_ENABLE(bitwise,
        AS_HELP_STRING([--enable-bitwise],
                       [enable the bitwise matching rule plugin (default: yes)]))
if test "$enable_bitwise" = yes ; then
  AC_MSG_RESULT(yes)
  AC_DEFINE([ENABLE_BITWISE], [1], [enable the bitwise plugin])
else
  AC_MSG_RESULT(no)
fi
AM_CONDITIONAL(enable_bitwise,test "$enable_bitwise" = "yes")

if test -z "$enable_presence" ; then
   enable_presence=no # if not set on cmdline, set default
fi
AC_MSG_CHECKING(for --enable-presence)
AC_ARG_ENABLE(presence,
        AS_HELP_STRING([--enable-presence],
                       [enable the presence plugin (default: no)]))
if test "$enable_presence" = yes ; then
  AC_MSG_RESULT(yes)
  AC_DEFINE([ENABLE_PRESENCE], [1], [enable the presence plugin])
else
  AC_MSG_RESULT(no)
fi
AM_CONDITIONAL(enable_presence,test "$enable_presence" = "yes")

if test -z "$enable_acctpolicy" ; then
   enable_acctpolicy=yes # if not set on cmdline, set default
fi
AC_MSG_CHECKING(for --enable-acctpolicy)
AC_ARG_ENABLE(acctpolicy,
        AS_HELP_STRING([--enable-acctpolicy],
                       [enable the account policy plugin (default: yes)]))
if test "$enable_acctpolicy" = yes ; then
  AC_MSG_RESULT(yes)
  AC_DEFINE([ENABLE_ACCTPOLICY], [1], [enable the account policy plugin])
else
  AC_MSG_RESULT(no)
fi
AM_CONDITIONAL(enable_acctpolicy,test "$enable_acctpolicy" = "yes")

if test -z "$enable_posix_winsync" ; then
   enable_posix_winsync=yes # if not set on cmdline, set default
fi
AC_MSG_CHECKING(for --enable-posix-winsync)
AC_ARG_ENABLE(posix_winsync,
        AS_HELP_STRING([--enable-posix-winsync],
                       [enable support for POSIX user/group attributes in winsync (default: yes)]))
if test "$enable_posix_winsync" = yes ; then
  AC_MSG_RESULT(yes)
  AC_DEFINE([ENABLE_POSIX_WINSYNC], [1], [enable support for POSIX user/group attributes in winsync])
else
  AC_MSG_RESULT(no)
fi
AM_CONDITIONAL(enable_posix_winsync,test "$enable_posix_winsync" = "yes")

# the default prefix - override with --prefix or --with-fhs
AC_PREFIX_DEFAULT([/opt/$PACKAGE_NAME])

m4_include(m4/fhs.m4)

localrundir='/run'

# installation paths - by default, we store everything
# under the prefix.  The with-fhs option will use /usr,
# /etc, and /var.  The with-fhs-opt option will use the
# prefix, but it's sysconfdir and localstatedir will be
# /etc/opt, and /var/opt.
if test "$with_fhs_opt" = "yes"; then
  # Override sysconfdir and localstatedir if FHS optional
  # package was requested.
  sysconfdir='/etc/opt'
  localstatedir='/var/opt'
  localrundir='/var/opt/run'
  # relative to datadir
  sampledatadir=/data
  # relative to datadir
  scripttemplatedir=/script-templates
  # relative to datadir
  updatedir=/updates
  # relative to libdir
  serverdir=
  # relative to includedir
  serverincdir=
  # relative to libdir
  serverplugindir=/plugins
  # relative to datadir
  infdir=/inf
  # relative to datadir
  mibdir=/mibs
  # location of property/resource files, relative to datadir
  propertydir=/properties
  # relative to libdir
  perldir=/perl
else
  if test "$with_fhs" = "yes"; then
    ac_default_prefix=/usr
    prefix=$ac_default_prefix
    exec_prefix=$prefix
    dnl as opposed to the default /usr/etc
    sysconfdir='/etc'
    dnl as opposed to the default /usr/var
    localstatedir='/var'
    localrundir='/run'
  fi
  # relative to datadir
  sampledatadir=/$PACKAGE_NAME/data
  # relative to datadir
  scripttemplatedir=/$PACKAGE_NAME/script-templates
  # relative to datadir
  updatedir=/$PACKAGE_NAME/updates
  # relative to libdir
  serverdir=/$PACKAGE_NAME
  # relative to includedir
  serverincdir=/$PACKAGE_NAME
  # relative to libdir
  serverplugindir=/$PACKAGE_NAME/plugins
  # relative to datadir
  infdir=/$PACKAGE_NAME/inf
  # relative to datadir
  mibdir=/$PACKAGE_NAME/mibs
  # location of property/resource files, relative to datadir
  propertydir=/$PACKAGE_NAME/properties
  # relative to libdir
  perldir=/$PACKAGE_NAME/perl
fi

# if mandir is the default value, override it
# otherwise, the user must have set it - just use it
if test X"$mandir" = X'${prefix}/man' ; then
   mandir='$(datadir)/man'
fi

# Shared paths for all layouts
# relative to sysconfdir
configdir=/$PACKAGE_NAME/config
# relative to sysconfdir
schemadir=/$PACKAGE_NAME/schema

# default user, group
defaultuser=nobody
defaultgroup=nobody

if test -z "$with_tmpfiles_d" ; then
   if test -d $sysconfdir/tmpfiles.d ; then
      with_tmpfiles_d='$(sysconfdir)/tmpfiles.d'
   fi
fi
AC_MSG_CHECKING(for --with-tmpfiles-d)
AC_ARG_WITH(tmpfiles-d,
   AS_HELP_STRING([--with-tmpfiles-d=PATH],
                  [system uses tmpfiles.d to handle temp files/dirs (default: $with_tmpfiles_d)])
)
if test "$with_tmpfiles_d" = yes ; then
  AC_MSG_ERROR([You must specify --with-tmpfiles-d=/full/path/to/tmpfiles.d directory])
elif test "$with_tmpfiles_d" = no ; then
  with_tmpfiles_d=
else
  AC_MSG_RESULT([$with_tmpfiles_d])
fi
AC_SUBST(with_tmpfiles_d)

PKG_PROG_PKG_CONFIG
if test -n "$PKG_CONFIG" ; then
   default_systemdsystemunitdir=`$PKG_CONFIG --variable=systemdsystemunitdir systemd`
   default_systemdsystemconfdir=`$PKG_CONFIG --variable=systemdsystemconfdir systemd`
fi

AC_MSG_CHECKING(for --with-perldir)
AC_ARG_WITH([perldir],
   AS_HELP_STRING([--with-perldir=PATH],
                  [Directory for perl)])
)
if test -n "$with_perldir"; then
  if test  "$with_perldir" = yes ; then
    AC_MSG_ERROR([You must specify --with-perldir=/full/path/to/perl])
  elif test  "$with_perldir" = no ; then
    with_perldir=
  else
    AC_MSG_RESULT([$with_perldir])
  fi
else
  with_perldir=
fi

AC_MSG_CHECKING(for --with-systemdsystemunitdir)
AC_ARG_WITH([systemdsystemunitdir],
   AS_HELP_STRING([--with-systemdsystemunitdir=PATH],
                  [Directory for systemd service files (default: $with_systemdsystemunitdir)])
)
if test "$with_systemdsystemunitdir" = yes ; then
  if test -n "$default_systemdsystemunitdir" ; then
    with_systemdsystemunitdir=$default_systemdsystemunitdir
    AC_MSG_RESULT([$with_systemdsystemunitdir])
  else
    AC_MSG_ERROR([You must specify --with-systemdsystemconfdir=/full/path/to/systemd/system directory])
  fi
elif test "$with_systemdsystemunitdir" = no ; then
  with_systemdsystemunitdir=
else
  AC_MSG_RESULT([$with_systemdsystemunitdir])
fi
AC_SUBST(with_systemdsystemunitdir)

AC_MSG_CHECKING(for --with-systemdsystemconfdir)
AC_ARG_WITH([systemdsystemconfdir],
   AS_HELP_STRING([--with-systemdsystemconfdir=PATH],
                  [Directory for systemd service files (default: $with_systemdsystemconfdir)])
)
if test "$with_systemdsystemconfdir" = yes ; then
  if test -n "$default_systemdsystemconfdir" ; then
    with_systemdsystemconfdir=$default_systemdsystemconfdir
    AC_MSG_RESULT([$with_systemdsystemconfdir])
  else
    AC_MSG_ERROR([You must specify --with-systemdsystemconfdir=/full/path/to/systemd/system directory])
  fi
elif test "$with_systemdsystemconfdir" = no ; then
  with_systemdsystemconfdir=
else
  AC_MSG_RESULT([$with_systemdsystemconfdir])
fi
AC_SUBST(with_systemdsystemconfdir)

if test -n "$with_systemdsystemunitdir" -o -n "$with_systemdsystemconfdir" ; then
   if test -z "$with_systemdgroupname" ; then
      with_systemdgroupname=$PACKAGE_NAME.target
   fi
   AC_MSG_CHECKING(for --with-systemdgroupname)
   AC_ARG_WITH([systemdgroupname],
        AS_HELP_STRING([--with-systemdgroupname=NAME],
                       [Name of group target for all instances (default: $with_systemdgroupname)])
   )
   if test "$with_systemdgroupname" = yes ; then
      AC_MSG_ERROR([You must specify --with-systemdgroupname=name.of.group])
   elif test "$with_systemdgroupname" = no ; then
      AC_MSG_ERROR([You must specify --with-systemdgroupname=name.of.group])
   else
      AC_MSG_RESULT([$with_systemdgroupname])
   fi
   AC_SUBST(with_systemdgroupname)
fi

AC_SUBST(configdir)
AC_SUBST(sampledatadir)
AC_SUBST(propertydir)
AC_SUBST(schemadir)
AC_SUBST(serverdir)
AC_SUBST(serverincdir)
AC_SUBST(serverplugindir)
AC_SUBST(scripttemplatedir)
AC_SUBST(perldir)
AC_SUBST(infdir)
AC_SUBST(mibdir)
AC_SUBST(mandir)
AC_SUBST(updatedir)

AC_SUBST(defaultuser)
AC_SUBST(defaultgroup)

# check for --with-instconfigdir
AC_MSG_CHECKING(for --with-instconfigdir)
AC_ARG_WITH(instconfigdir,
        AS_HELP_STRING([--with-instconfigdir=/path],
                       [Base directory for instance specific writable configuration directories (default $sysconfdir/$PACKAGE_NAME)]),
[
  if test $withval = yes ; then
     AC_ERROR([Please specify a full path with --with-instconfigdir])
  fi
  instconfigdir="$withval"
  AC_MSG_RESULT($withval)
],
[
  dnl this value is expanded out in Makefile.am
  instconfigdir='$(sysconfdir)/$(PACKAGE_NAME)'
  AC_MSG_RESULT(no)
])
AC_SUBST(instconfigdir)

# WINNT should be true if building on Windows system not using
# cygnus, mingw, or the like and using cmd.exe as the shell
AM_CONDITIONAL([WINNT], false)

# Deal with platform dependent defines
# initdir is the location for the SysV init scripts - very heavily platform
# dependent and not specified in fhs or lsb
# and not used if systemd is used
initdir='$(sysconfdir)/rc.d'
AC_MSG_CHECKING(for --with-initddir)
AC_ARG_WITH(initddir,
        AS_HELP_STRING([--with-initddir=/path],
                       [Absolute path (not relative like some of the other options) that should contain the SysV init scripts (default '$(sysconfdir)/rc.d')]),
[
  if test "$withval" = yes -o "$withval" = no  ; then
     AC_ERROR([Please specify a full path with --with-initddir])
  fi
  AC_MSG_RESULT($withval)
],
[
  AC_MSG_RESULT(no)
])
# on most platforms, we will just use perl from PATH
# On some platforms, we cannot.  Why not just use any old
# perl?  Because of perldap.  We use a perldap that is
# compiled to either 32bit or 64bit, so we must use a native
# perl binary compiled with the same bitsize.  On Solaris
# and HP-UX, /usr/bin/perl is 32 bit, so we cannot use
# those with our 64 bit compiled product.
if test -n "$with_perldir"; then
  perlexec="$with_perldir/perl"
else
  perlexec='/usr/bin/env perl'
fi
# we use stty in perl scripts to disable password echo
# this doesn't work unless the full absolute path of the
# stty command is used e.g. system("stty -echo") does not
# work but system("/bin/stty -echo") does work
# since the path of stty may not be the same on all
# platforms, we set the default here to /bin/stty and
# allow that value to be overridden in the platform
# specific section below
sttyexec=/bin/stty
case $host in
  *-*-linux*)
    AC_DEFINE([XP_UNIX], [1], [UNIX])
    AC_DEFINE([Linux], [1], [Linux])
    AC_DEFINE([LINUX], [1], [Linux])
    AC_DEFINE([LINUX2_0], [1], [Linux 2.0])
    AC_DEFINE([LINUX2_2], [1], [Linux 2.2])
    AC_DEFINE([LINUX2_4], [1], [Linux 2.4])
    AC_DEFINE([_GNU_SOURCE], [1], [GNU Source])
    platform="linux"
    initdir='$(sysconfdir)/rc.d/init.d'
    # do arch specific linux stuff here
    case $host in
      i*86-*-linux*)
        AC_DEFINE([CPU_x86], [], [cpu type x86])
        AC_DEFINE([ATOMIC_64BIT_OPERATIONS], [1], [enabling atomic counter])
        ;;
      x86_64-*-linux*)
        AC_DEFINE([CPU_x86_64], [], [cpu type x86_64])
        AC_DEFINE([ATOMIC_64BIT_OPERATIONS], [1], [enabling atomic counter])
        ;;
    esac
    AC_MSG_CHECKING([for GCC provided 64-bit atomic bool cas function ...])
    AC_LINK_IFELSE([AC_LANG_PROGRAM([[]],
                                    [[long long ptrval = 0, val = 0, newval = 1; (void)__sync_bool_compare_and_swap_8(&ptrval, val, newval);]])],
                   [AC_DEFINE([HAVE_64BIT_ATOMIC_CAS_FUNC], [1], [have 64-bit atomic bool compare and swap function provided by gcc])AC_MSG_RESULT([yes])],
                   [AC_MSG_RESULT([no])])
    AC_MSG_CHECKING([for GCC provided 64-bit atomic ops functions ...])
    AC_LINK_IFELSE([AC_LANG_PROGRAM([[]],
                                    [[long long ptrval = 0, val = 0; (void)__sync_add_and_fetch_8(&ptrval, val);]])],
                   [AC_DEFINE([HAVE_64BIT_ATOMIC_OP_FUNCS], [1], [have 64-bit atomic operation functions provided by gcc])AC_MSG_RESULT([yes])],
                   [AC_MSG_RESULT([no])])
    # some programs use the native thread library directly
    THREADLIB=-lpthread
    AC_SUBST([THREADLIB], [$THREADLIB])
    LIBCRYPT=-lcrypt
    AC_SUBST([LIBCRYPT], [$LIBCRYPT])
    AC_DEFINE([USE_POSIX_RWLOCKS], [1], [POSIX rwlocks])
    ;;
  ia64-hp-hpux*)
    AC_DEFINE([XP_UNIX], [1], [UNIX])
    AC_DEFINE([hpux], [1], [HP-UX])
    AC_DEFINE([HPUX], [1], [HP-UX])
    AC_DEFINE([HPUX11], [1], [HP-UX 11])
    AC_DEFINE([HPUX11_23], [1], [HP-UX 11.23])
    AC_DEFINE([CPU_ia64], [], [cpu type ia64])
    AC_DEFINE([OS_hpux], [1], [OS HP-UX])
    AC_DEFINE([_POSIX_C_SOURCE], [199506L], [POSIX revision])
    AC_DEFINE([_HPUX_SOURCE], [1], [Source namespace])
    AC_DEFINE([_INCLUDE_STDC__SOURCE_199901], [1], [to pick up all of the printf format macros in inttypes.h])
    # assume 64 bit
    perlexec='/opt/perl_64/bin/perl'
    platform="hpux"
    initconfigdir="/$PACKAGE_NAME/config"
    # HPUX doesn't use /etc for this
    initdir=/init.d
    ;;
  hppa*-hp-hpux*)
    AC_DEFINE([XP_UNIX], [1], [UNIX])
    AC_DEFINE([hpux], [1], [HP-UX])
    AC_DEFINE([HPUX], [1], [HP-UX])
    AC_DEFINE([HPUX11], [1], [HP-UX 11])
    AC_DEFINE([HPUX11_11], [1], [HP-UX 11.11])
    AC_DEFINE([CPU_hppa], [], [cpu type pa-risc])
    AC_DEFINE([OS_hpux], [1], [OS HP-UX])
    AC_DEFINE([_POSIX_C_SOURCE], [199506L], [POSIX revision])
    AC_DEFINE([_HPUX_SOURCE], [1], [Source namespace])
    AC_DEFINE([_INCLUDE_STDC__SOURCE_199901], [1], [to pick up all of the printf format macros in inttypes.h])
    AC_DEFINE([ATOMIC_64BIT_OPERATIONS], [1], [enabling atomic counter])
    # assume 64 bit
    initconfigdir="/$PACKAGE_NAME/config"
    perlexec='/opt/perl_64/bin/perl'
    platform="hpux"
    # HPUX doesn't use /etc for this
    initdir=/init.d
    ;;
  *-*-solaris*)
    AC_DEFINE([XP_UNIX], [1], [UNIX])
    AC_DEFINE([SVR4], [1], [SVR4])
    AC_DEFINE([__svr4], [1], [SVR4])
    AC_DEFINE([__svr4__], [1], [SVR4])
    AC_DEFINE([_SVID_GETTOD], [1], [SVID_GETTOD])
    AC_DEFINE([SOLARIS], [1], [SOLARIS])
    AC_DEFINE([OS_solaris], [1], [OS SOLARIS])
    AC_DEFINE([sunos5], [1], [SunOS5])
    AC_DEFINE([OSVERSION], [509], [OS version])
    AC_DEFINE([_REENTRANT], [1], [_REENTRANT])
    AC_DEFINE([NO_DOMAINNAME], [1], [no getdomainname])
dnl socket nsl and dl are required to link several programs and libdb
    LIBSOCKET=-lsocket
    AC_SUBST([LIBSOCKET], [$LIBSOCKET])
    LIBNSL=-lnsl
    AC_SUBST([LIBNSL], [$LIBNSL])
    LIBDL=-ldl
    AC_SUBST([LIBDL], [$LIBDL])
dnl Cstd and Crun are required to link any C++ related code
    LIBCSTD=-lCstd
    AC_SUBST([LIBCSTD], [$LIBCSTD])
    LIBCRUN=-lCrun
    AC_SUBST([LIBCRUN], [$LIBCRUN])
    platform="solaris"
    initdir='$(sysconfdir)/init.d'
    case $host in
      i?86-*-solaris2.1[[0-9]]*)
dnl I dont know why i386 need this explicit
        AC_DEFINE([HAVE_GETPEERUCRED], [1], [have getpeerucred])
        ;;
      sparc-*-solaris*)
dnl includes some assembler stuff in counter.o
        AC_DEFINE([ATOMIC_64BIT_OPERATIONS], [1], [enabling atomic counter])
        AC_DEFINE([CPU_sparc], [], [cpu type sparc])
        TARGET='SPARC'
        ;;
    esac
    ;;
  *)
    platform=""
    ;;
esac

# cmd line overrides default setting above
if test -n "$with_initddir" ; then
   initdir="$with_initddir"
fi
# sysv init scripts not used when systemd is used
AC_SUBST(initdir)
AC_SUBST(perlexec)
AC_SUBST(sttyexec)

# set default initconfigdir if not already set
# value will be set so as to be relative to $(sysconfdir)
if test -z "$initconfigdir" ; then
   if test -d /etc/sysconfig ; then
      initconfigdir=/sysconfig
   elif test -d /etc/default ; then
      initconfigdir=/default
   else
      initconfigdir="/$PACKAGE_NAME/config"
   fi
fi
AC_SUBST(initconfigdir)

AM_CONDITIONAL([HPUX],[test "$platform" = "hpux"])
AM_CONDITIONAL([SOLARIS],[test "$platform" = "solaris"])
AM_CONDITIONAL([SYSTEMD],[test -n "$with_systemdsystemunitdir" -o -n "$with_systemdsystemconfdir"])

# Check for library dependencies
m4_include(m4/nspr.m4)
m4_include(m4/nss.m4)
m4_include(m4/openldap.m4)
m4_include(m4/mozldap.m4)
m4_include(m4/db.m4)
m4_include(m4/sasl.m4)
m4_include(m4/svrcore.m4)
m4_include(m4/icu.m4)
m4_include(m4/netsnmp.m4)
m4_include(m4/kerberos.m4)
m4_include(m4/pcre.m4)
m4_include(m4/selinux.m4)

PACKAGE_BASE_VERSION=`echo $PACKAGE_VERSION | awk -F\. '{print $1"."$2}'`
AC_SUBST(PACKAGE_BASE_VERSION)

# set sasl_path for wrapper scripts
if test -z "$sasl_libdir" ; then
  sasl_path="$libdir/sasl2"
else
  sasl_path="$sasl_libdir/sasl2"
fi

AM_CONDITIONAL(SELINUX,test "$with_selinux" = "yes")
AM_CONDITIONAL(OPENLDAP,test "$with_openldap" = "yes")
AM_CONDITIONAL(SOLARIS,test "$platform" = "solaris")
AM_CONDITIONAL(SPARC,test "x$TARGET" = xSPARC)

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
AC_SUBST(openldap_inc)
AC_SUBST(openldap_lib)
AC_SUBST(openldap_libdir)
AC_SUBST(openldap_bindir)
AC_SUBST(ldaptool_bindir)
AC_SUBST(ldaptool_opts)
AC_SUBST(plainldif_opts)
AC_SUBST(ol_libver)
AC_SUBST(db_inc)
AC_SUBST(db_incdir)
AC_SUBST(db_lib)
AC_SUBST(db_libdir)
AC_SUBST(db_bindir)
AC_SUBST(db_libver)
AC_SUBST(sasl_inc)
AC_SUBST(sasl_lib)
AC_SUBST(sasl_libdir)
AC_SUBST(sasl_path)
AC_SUBST(svrcore_inc)
AC_SUBST(svrcore_lib)
AC_SUBST(icu_lib)
AC_SUBST(icu_inc)
AC_SUBST(icu_bin)
AC_SUBST(netsnmp_inc)
AC_SUBST(netsnmp_lib)
AC_SUBST(netsnmp_libdir)
AC_SUBST(netsnmp_link)
AC_SUBST(pcre_inc)
AC_SUBST(pcre_lib)
AC_SUBST(pcre_libdir)
AC_SUBST(localrundir)

AC_SUBST(brand)
AC_SUBST(capbrand)
AC_SUBST(vendor)

# AC_DEFINE([USE_OLD_UNHASHED], [], [Use old unhashed code])
AC_DEFINE([LDAP_DEBUG], [1], [LDAP debug flag])
AC_DEFINE([LDAP_DONT_USE_SMARTHEAP], [1], [Don't use smartheap])

# libtool on fedora/rhel contains some gcc-isms which cause problems
# if not using gcc (e.g. Forte on Solaris, aCC on HP-UX)
# we remove them here
if test "$GCC" != yes ; then
   AC_MSG_NOTICE([Not using gcc - fixing libtool to remove gcc-isms . . .])
   cp -p libtool libtool.orig
   cp -p libtool libtool.tmp
   # dnl note the special chars @<:@ and @:>@ - since m4 treats [ and ] specially,
   # we have to use the quadrigraph @<:@ for [ and @:>@ for ] - and you thought
   # perl produced write-only code . . .
   sed -e '/^gcc_dir/ d' \
       -e '/^gcc_ver/ d' \
       -e 's/^predep_objects=.*echo \("@<:@^"@:>@*"\).*$/predep_objects=\1/' \
       -e 's/^postdep_objects=.*echo \("@<:@^"@:>@*"\).*$/postdep_objects=\1/' \
       -e 's/^compiler_lib_search_path=.*echo \("@<:@^"@:>@*"\).*$/compiler_lib_search_path=\1/' \
       -e 's/^sys_lib_search_path_spec=.*echo \("@<:@^"@:>@*"\).*$/sys_lib_search_path_spec=\1/' \
       libtool > libtool.tmp
   cp -p libtool.tmp libtool
   rm -f libtool.tmp
fi

AC_CONFIG_FILES([Makefile])

AC_OUTPUT
