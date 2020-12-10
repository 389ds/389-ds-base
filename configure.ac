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
AM_INIT_AUTOMAKE([1.9 foreign subdir-objects dist-bzip2 no-dist-gzip no-define tar-pax])
# define these for automake distdir
VERSION=$PACKAGE_VERSION
PACKAGE=$PACKAGE_TARNAME
AC_DEFINE_UNQUOTED([VERSION], "$VERSION", [package version])
AC_DEFINE_UNQUOTED([PACKAGE], "$PACKAGE", [package tar name])
AC_SUBST([RPM_VERSION])
AC_SUBST([RPM_RELEASE])
AC_SUBST([VERSION_PREREL])
AC_SUBST([CONSOLE_VERSION])
AM_MAINTAINER_MODE
AC_CANONICAL_HOST

AC_CONFIG_MACRO_DIRS([m4])

# Checks for programs.
: ${CXXFLAGS=""}
AC_PROG_CXX
: ${CFLAGS=""}
AC_PROG_CC
AM_PROG_CC_C_O
AM_PROG_AS
AC_PROG_CC_STDC
PKG_PROG_PKG_CONFIG

# disable static libs by default - we only use a couple
AC_DISABLE_STATIC
AC_PROG_LIBTOOL

# Checks for header files.
AC_HEADER_DIRENT
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([arpa/inet.h errno.h fcntl.h malloc.h netdb.h netinet/in.h stdlib.h string.h strings.h sys/file.h sys/socket.h sys/time.h syslog.h unistd.h mntent.h sys/sysinfo.h sys/endian.h endian.h])

# These are *required* headers without option.
AC_CHECK_HEADERS([inttypes.h], [], AC_MSG_ERROR([unable to locate required header inttypes.h]))
AC_CHECK_HEADERS([crack.h], [], AC_MSG_ERROR([unable to locate required header crack.h]))


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
AC_CHECK_FUNCS([endpwent ftruncate getcwd getaddrinfo inet_pton inet_ntop localtime_r memmove memset mkdir munmap putenv rmdir setrlimit socket strcasecmp strchr strcspn strdup strerror strncasecmp strpbrk strrchr strstr strtol tzset])

# These functions are *required* without option.
AC_CHECK_FUNCS([clock_gettime], [], AC_MSG_ERROR([unable to locate required symbol clock_gettime]))

# This will detect if we need to add the LIBADD_DL value for us.
LT_LIB_DLLOAD

# Optional rust component support.
AC_MSG_CHECKING(for --enable-rust-offline)
AC_ARG_ENABLE(rust_offline, AS_HELP_STRING([--enable-rust-offline], [Enable rust building offline. you MUST have run vendor! (default: no)]),
              [], [ enable_rust_offline=no ])
AC_MSG_RESULT($enable_rust_offline)
AM_CONDITIONAL([RUST_ENABLE_OFFLINE],[test "$enable_rust_offline" = yes])

AS_IF([test "$enable_rust_offline" = yes],
    [rust_vendor_sources="replace-with = \"vendored-sources\""],
    [rust_vendor_sources=""])
AC_SUBST([rust_vendor_sources])

AC_MSG_CHECKING(for --enable-rust)
AC_ARG_ENABLE(rust, AS_HELP_STRING([--enable-rust], [Enable rust language features (default: no)]),
              [], [ enable_rust=no ])
AC_MSG_RESULT($enable_rust)
if test "$enable_rust" = yes -o "$enable_rust_offline" = yes; then
    AC_CHECK_PROG(CARGO, [cargo], [yes], [no])
    AC_CHECK_PROG(RUSTC, [rustc], [yes], [no])
    # Since fernet uses the openssl lib.
    PKG_CHECK_MODULES([OPENSSL], [openssl])

    AS_IF([test "$CARGO" != "yes" -o "$RUSTC" != "yes"], [
      AC_MSG_FAILURE("Rust based plugins cannot be built cargo=$CARGO rustc=$RUSTC")
    ])
fi
AC_SUBST([enable_rust])
AM_CONDITIONAL([RUST_ENABLE],[test "$enable_rust" = yes -o "$enable_rust_offline" = yes])

# Optional cockpit support (enabled by default)
AC_MSG_CHECKING(for --enable-cockpit)
AC_ARG_ENABLE(cockpit, AS_HELP_STRING([--enable-cockpit], [Enable cockpit plugin (default: yes)]),
              [], [ enable_cockpit=yes ])
AC_MSG_RESULT($enable_cockpit)
AC_SUBST([enable_cockpit])
AC_SUBST(ENABLE_COCKPIT)
AM_CONDITIONAL([ENABLE_COCKPIT],[test "$enable_cockpit" = yes])


AC_MSG_CHECKING(for --enable-debug)
AC_ARG_ENABLE(debug, AS_HELP_STRING([--enable-debug], [Enable debug features (default: no)]),
              [], [ enable_debug=no ])
AC_MSG_RESULT($enable_debug)
if test "$enable_debug" = yes ; then
  debug_defs="-DDEBUG -DMCC_DEBUG"
  debug_cflags="-g3 -O0"
  debug_cxxflags="-g3 -O0"
  debug_rust_defs="-C debuginfo=2 -Z macro-backtrace"
  cargo_defs=""
  rust_target_dir="debug"
else
  debug_defs=""
  # set the default safe CFLAGS that would be set by AC_PROG_CC otherwise
  debug_cflags="-g -O2"
  debug_cxxflags="-g -O2"
  debug_rust_defs="-C debuginfo=2"
  cargo_defs="--release"
  rust_target_dir="release"
fi
AC_SUBST([debug_defs])
AC_SUBST([debug_cflags])
AC_SUBST([debug_cxxflags])
AC_SUBST([debug_rust_defs])
AC_SUBST([cargo_defs])
AC_SUBST([rust_target_dir])
AM_CONDITIONAL([DEBUG],[test "$enable_debug" = yes])

AC_MSG_CHECKING(for --enable-asan)
AC_ARG_ENABLE(asan, AS_HELP_STRING([--enable-asan], [Enable gcc/clang address sanitizer options (default: no)]),
              [], [ enable_asan=no ])
AC_MSG_RESULT($enable_asan)
if test "$enable_asan" = yes ; then
  asan_cflags="-fsanitize=address -fno-omit-frame-pointer"
  asan_rust_defs="-Z sanitizer=address"
else
  asan_cflags=""
  asan_rust_defs=""
fi
AC_SUBST([asan_cflags])
AC_SUBST([asan_rust_defs])
AM_CONDITIONAL(enable_asan,[test "$enable_asan" = yes])

AC_MSG_CHECKING(for --enable-msan)
AC_ARG_ENABLE(msan, AS_HELP_STRING([--enable-msan], [Enable gcc/clang memory sanitizer options (default: no)]),
              [], [ enable_msan=no ])
AC_MSG_RESULT($enable_msan)
if test "$enable_msan" = yes ; then
  msan_cflags="-fsanitize=memory -fsanitize-memory-track-origins -fno-omit-frame-pointer"
  msan_rust_defs="-Z sanitizer=memory"
else
  msan_cflags=""
  msan_rust_defs=""
fi
AC_SUBST([msan_cflags])
AC_SUBST([msan_rust_defs])
AM_CONDITIONAL(enable_msan,test "$enable_msan" = "yes")

AC_MSG_CHECKING(for --enable-tsan)
AC_ARG_ENABLE(tsan, AS_HELP_STRING([--enable-tsan], [Enable gcc/clang thread sanitizer options (default: no)]),
              [], [ enable_tsan=no ])
AC_MSG_RESULT($enable_tsan)
if test "$enable_tsan" = yes ; then
  tsan_cflags="-fsanitize=thread -fno-omit-frame-pointer"
  tsan_rust_defs="-Z sanitizer=thread"
else
  tsan_cflags=""
  tsan_rust_defs=""
fi
AC_SUBST([tsan_cflags])
AC_SUBST([tsan_rust_defs])
AM_CONDITIONAL(enable_tsan,test "$enable_tsan" = "yes")

AC_MSG_CHECKING(for --enable-ubsan)
AC_ARG_ENABLE(ubsan, AS_HELP_STRING([--enable-tsan], [Enable gcc/clang undefined behaviour sanitizer options (default: no)]),
              [], [ enable_ubsan=no ])
AC_MSG_RESULT($enable_ubsan)
if test "$enable_ubsan" = yes ; then
  ubsan_cflags="-fsanitize=undefined -fno-omit-frame-pointer"
  ubsan_rust_defs=""
else
  ubsan_cflags=""
  ubsan_rust_defs=""
fi
AC_SUBST([ubsan_cflags])
AC_SUBST([ubsan_rust_defs])
AM_CONDITIONAL(enable_ubsan,test "$enable_ubsan" = "yes")

AM_CONDITIONAL(with_sanitizer,test "$enable_asan" = "yes" -o "$enable_msan" = "yes" -o "$enable_tsan" = "yes" -o "$enable_ubsan" = "yes")

AC_MSG_CHECKING(for --enable-clang)
AC_ARG_ENABLE(clang, AS_HELP_STRING([--enable-clang], [Enable clang (default: no)]),
              [], [ enable_clang=no ])
AC_MSG_RESULT($enable_clang)
AM_CONDITIONAL(CLANG_ENABLE,test "$enable_clang" = "yes")


AM_CONDITIONAL([RPM_HARDEND_CC], [test -f /usr/lib/rpm/redhat/redhat-hardened-cc1])
AC_MSG_CHECKING(for --enable-gcc-security)
AC_ARG_ENABLE(gcc-security, AS_HELP_STRING([--enable-gcc-security], [Enable gcc secure compilation options (default: no)]),
              [], [ enable_gcc_security=no ])
AC_MSG_RESULT($enable_gcc_security)
if test "$enable_gcc_security" = yes ; then
  gccsec_cflags="-Wall -Wp,-D_FORTIFY_SOURCE=2 -fexceptions -fstack-protector-strong --param=ssp-buffer-size=4 -grecord-gcc-switches -Werror=format-security"
else
  # Without this, -fPIC doesn't work on generic fedora builds, --disable-gcc-sec.
  gccsec_cflags=""
fi
AM_COND_IF([RPM_HARDEND_CC],
           [ gccsec_cflags="$gccsec_flags -specs=/usr/lib/rpm/redhat/redhat-hardened-cc1" ],
           [])
AC_SUBST([gccsec_cflags])

# Pull in profiling.
AC_MSG_CHECKING(for --enable-profiling)
AC_ARG_ENABLE(profiling, AS_HELP_STRING([--enable-profiling], [Enable gcov profiling features (default: no)]),
              [], [ enable_profiling=no ])
AC_MSG_RESULT($enable_profiling)
if test "$enable_profiling" = yes ; then
  profiling_defs="-fprofile-arcs -ftest-coverage -g3 -O0"
  profiling_links="-lgcov --coverage"
else
  profiling_defs=""
  profiling_links=""
fi
AC_SUBST([profiling_defs])
AC_SUBST([profiling_links])

AC_MSG_CHECKING(for --enable-systemtap)
AC_ARG_ENABLE(systemtap, AS_HELP_STRING([--enable-systemtap], [Enable systemtap probe features (default: no)]),
              [], [ enable_systemtap=no ])
AC_MSG_RESULT($enable_systemtap)
if test "$enable_systemtap" = yes ; then
  systemtap_defs="-DSYSTEMTAP"
else
  systemtap_defs=""
fi
AC_SUBST([systemtap_defs])


# these enables are for optional or experimental features
AC_MSG_CHECKING(for --enable-pam-passthru)
AC_ARG_ENABLE(pam-passthru,
        AS_HELP_STRING([--enable-pam-passthru],
                       [enable the PAM passthrough auth plugin (default: yes)]),
                       [], [ enable_pam_passthru=yes ])
AC_MSG_RESULT($enable_pam_passthru)
if test "$enable_pam_passthru" = yes ; then
  # check for pam header file used by plugins/pass_passthru/pam_ptimpl.c
  AC_CHECK_HEADER([security/pam_appl.h], [], [AC_MSG_ERROR([Missing header file security/pam_appl.h])])
  AC_DEFINE([ENABLE_PAM_PASSTHRU], [1], [enable the pam passthru auth plugin])
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
   enable_autobind=yes # if not set on cmdline, set default
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
AC_ARG_ENABLE(auto-dn-suffix,
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

# If we have no prefix specified, we need to fix the prefix variable.
# If we don't what happens is $prefixdir ends up as NONE, and then
# later configure changes $prefix to $ac_default_prefix underneath us.
if test "$prefix" = "NONE"; then
    prefix=$ac_default_prefix
fi

m4_include(m4/fhs.m4)

localrundir='/run'
cockpitdir=/389-console

# installation paths - by default, we store everything
# under the prefix.  The with-fhs option will use /usr,
# /etc, and /var.  The with-fhs-opt option will use the
# prefix, but it's sysconfdir and localstatedir will be
# /etc/opt, and /var/opt.
if test "$with_fhs_opt" = "yes"; then
  # Override sysconfdir and localstatedir if FHS optional
  # package was requested.
  prefixdir=$prefix
  sysconfdir='/etc/opt'
  localstatedir='/var/opt'
  localrundir='/var/opt/run'
  # relative to datadir
  sampledatadir=/data
  # relative to datadir
  systemschemadir=/schema
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
  # relative to libdir
  pythondir=/python
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
  prefixdir=$prefix
  # relative to datadir
  sampledatadir=/$PACKAGE_NAME/data
  # relative to datadir
  systemschemadir=/$PACKAGE_NAME/schema
  # relative to datadir
  scripttemplatedir=/$PACKAGE_NAME/script-templates
  # relative to datadir
  updatedir=/$PACKAGE_NAME/updates
  # relative to libdir
  serverdir=$PACKAGE_NAME
  # relative to includedir
  serverincdir=$PACKAGE_NAME
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
  # relative to libdir
  pythondir=/$PACKAGE_NAME/python
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
defaultuser=dirsrv
defaultgroup=dirsrv

AC_MSG_CHECKING(for --with-perldir)
AC_ARG_WITH([perldir],
   AS_HELP_STRING([--with-perldir=PATH],
                  [Directory for perl])
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

AC_MSG_CHECKING(for --with-pythonexec)
AC_ARG_WITH([pythonexec],
   AS_HELP_STRING([--with-pythonexec=PATH],
                  [Path to executable for python])
)
if test -n "$with_pythonexec"; then
  if test  "$with_pythonexec" = yes ; then
    AC_MSG_ERROR([You must specify --with-pythonexec=/full/path/to/python])
  elif test  "$with_pythonexec" = no ; then
    with_pythonexec=/usr/bin/python3
  else
    AC_MSG_RESULT([$with_pythonexec])
  fi
else
  with_pythonexec=/usr/bin/python3
fi

AC_SUBST(prefixdir)
AC_SUBST(configdir)
AC_SUBST(sampledatadir)
AC_SUBST(systemschemadir)
AC_SUBST(propertydir)
AC_SUBST(schemadir)
AC_SUBST(serverdir)
AC_SUBST(serverincdir)
AC_SUBST(serverplugindir)
AC_SUBST(scripttemplatedir)
AC_SUBST(perldir)
AC_SUBST(pythondir)
AC_SUBST(infdir)
AC_SUBST(mibdir)
AC_SUBST(mandir)
AC_SUBST(updatedir)
AC_SUBST(defaultuser)
AC_SUBST(defaultgroup)
AC_SUBST(cockpitdir)

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
  AC_MSG_RESULT($withval)
],
[
  AC_MSG_RESULT(no)
])

AM_CONDITIONAL([INITDDIR], [test -n "$with_initddir" -a "$with_initddir" != "no"])
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

# This will let us change over the python version easier in the future.
if test -n "$with_pythonexec"; then
  pythonexec="$with_pythonexec"
else
  pythonexec='/usr/bin/python3'
fi

# Default to no atomic queue operations.
with_atomic_queue="no"

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
    AC_DEFINE([LINUX], [1], [Linux])
    AC_DEFINE([_GNU_SOURCE], [1], [GNU Source])
    platform="linux"
    initdir='$(sysconfdir)/rc.d/init.d'
    # do arch specific linux stuff here
    case $host in
      i*86-*-linux*)
        AC_DEFINE([CPU_x86], [], [cpu type x86])
        ;;
      x86_64-*-linux*)
        AC_DEFINE([CPU_x86_64], [1], [cpu type x86_64])

        ;;
      aarch64-*-linux*)
        AC_DEFINE([CPU_arm], [], [cpu type arm])
        ;;
      arm-*-linux*)
        AC_DEFINE([CPU_arm], [], [cpu type arm])
        ;;
      ppc64le-*-linux*)
        ;;
      ppc64-*-linux*)
        ;;
      ppc-*-linux*)
        ;;
      s390-*-linux*)
        ;;
      s390x-*-linux*)
        ;;
    esac
    # some programs use the native thread library directly
    THREADLIB=-lpthread
    AC_SUBST([THREADLIB], [$THREADLIB])
    LIBCRYPT=-lcrypt
    AC_SUBST([LIBCRYPT], [$LIBCRYPT])
    AC_DEFINE([USE_POSIX_RWLOCKS], [1], [POSIX rwlocks])
    ;;
  *-*-freebsd*)
    AC_DEFINE([FREEBSD], [1], [FreeBSD])
    platform="freebsd"
    initdir='$(sysconfdir)/rc.d'
    THREADLIB=-lthr
    AC_SUBST([THREADLIB], [$THREADLIB])
    AC_DEFINE([USE_POSIX_RWLOCKS], [1], [POSIX rwlocks])
    LIBDL=
    ;;
  ia64-hp-hpux*)
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
    AC_DEFINE([hpux], [1], [HP-UX])
    AC_DEFINE([HPUX], [1], [HP-UX])
    AC_DEFINE([HPUX11], [1], [HP-UX 11])
    AC_DEFINE([HPUX11_11], [1], [HP-UX 11.11])
    AC_DEFINE([CPU_hppa], [], [cpu type pa-risc])
    AC_DEFINE([OS_hpux], [1], [OS HP-UX])
    AC_DEFINE([_POSIX_C_SOURCE], [199506L], [POSIX revision])
    AC_DEFINE([_HPUX_SOURCE], [1], [Source namespace])
    AC_DEFINE([_INCLUDE_STDC__SOURCE_199901], [1], [to pick up all of the printf format macros in inttypes.h])
    # assume 64 bit
    initconfigdir="/$PACKAGE_NAME/config"
    perlexec='/opt/perl_64/bin/perl'
    platform="hpux"
    # HPUX doesn't use /etc for this
    initdir=/init.d
    ;;
  *-*-solaris*)
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
        AC_DEFINE([CPU_sparc], [], [cpu type sparc])
        TARGET='SPARC'
        ;;
    esac
    ;;
  *)
    platform=""
    ;;
esac

### TO CHECK FOR SSE4.2!!!
# gcc -march=native -dM -E - < /dev/null | grep SSE
# We can just use the define in GCC instead!

AC_MSG_CHECKING([for GCC provided 64-bit atomic operations])
AC_LINK_IFELSE([AC_LANG_PROGRAM([[
    #include <inttypes.h>
    ]],
    [[
    uint64_t t_counter = 0;
    uint64_t t_oldval = 0;
    uint64_t t_newval = 1;

    __atomic_compare_exchange_8(&t_counter, &t_oldval, t_newval, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    __atomic_add_fetch_8(&t_counter, t_newval, __ATOMIC_SEQ_CST);
    __atomic_sub_fetch_8(&t_counter, t_newval, __ATOMIC_SEQ_CST);
    __atomic_load(&t_counter, &t_oldval, __ATOMIC_SEQ_CST);
    return 0;
    ]])],
    [
        AC_DEFINE([ATOMIC_64BIT_OPERATIONS], [1], [have 64-bit atomic operation functions provided by gcc])
        AC_MSG_RESULT([yes])
    ],
    [
        AC_MSG_RESULT([no])
    ]
)

# cmd line overrides default setting above
if test -n "$with_initddir" ; then
   initdir="$with_initddir"
fi

# sysv init scripts not used when systemd is used
AC_SUBST(initdir)
AC_SUBST(perlexec)
AC_SUBST(pythonexec)
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

# Conditionals for makefile.am
AM_CONDITIONAL([HPUX],[test "$platform" = "hpux"])
AM_CONDITIONAL([SOLARIS],[test "$platform" = "solaris"])
AM_CONDITIONAL([FREEBSD],[test "$platform" = "freebsd"])
AM_CONDITIONAL([SPARC],[test "x$TARGET" = xSPARC])

# Check for library dependencies
PKG_CHECK_MODULES([EVENT], [libevent])

if $PKG_CONFIG --exists nspr; then
    PKG_CHECK_MODULES([NSPR], [nspr])
else
    PKG_CHECK_MODULES([NSPR], [dirsec-nspr])
fi

if $PKG_CONFIG --exists nss; then
    PKG_CHECK_MODULES([NSS], [nss])
    nss_libdir=`$PKG_CONFIG --libs-only-L nss | sed -e s/-L// | sed -e s/\ .*$//`
else
    PKG_CHECK_MODULES([NSS], [dirsec-nss])
    nss_libdir=`$PKG_CONFIG --libs-only-L dirsec-nss | sed -e s/-L// | sed -e s/\ .*$//`
fi
AC_SUBST(nss_libdir)

m4_include(m4/openldap.m4)
m4_include(m4/db.m4)

PKG_CHECK_MODULES([SASL], [libsasl2])

PKG_CHECK_MODULES([ICU], [icu-i18n >= 60.2])

m4_include(m4/netsnmp.m4)

PKG_CHECK_MODULES([KERBEROS], [krb5])
krb5_vendor=`$PKG_CONFIG --variable=vendor krb5`
if test "$krb5_vendor" = "MIT"; then
    AC_DEFINE(HAVE_KRB5, 1, [Define if you have Kerberos V])
    save_LIBS="$LIBS"
    LIBS="$KERBEROS_LIBS"
    AC_CHECK_FUNCS([krb5_cc_new_unique])
    LIBS="$save_LIBS"
elif test "$krb5_vendor" = "Heimdal"; then
    AC_DEFINE(HAVE_HEIMDAL_KERBEROS, 1, [Define if you have Heimdal Kerberos])
fi

if $PKG_CONFIG --exists pcre; then
    PKG_CHECK_MODULES([PCRE], [pcre])
    pcre_libdir=`$PKG_CONFIG --libs-only-L pcre | sed -e s/-L// | sed -e s/\ .*$//`
else
    PKG_CHECK_MODULES([PCRE], [libpcre])
    pcre_libdir=`$PKG_CONFIG --libs-only-L libpcre | sed -e s/-L// | sed -e s/\ .*$//`
fi
AC_SUBST(pcre_libdir)

m4_include(m4/selinux.m4)
m4_include(m4/systemd.m4)

AC_MSG_CHECKING(whether to enable cmocka unit tests)
AC_ARG_ENABLE(cmocka, AS_HELP_STRING([--enable-cmocka], [Enable cmocka unit tests (default: no)]))
if test "x$enable_cmocka" = "xyes"; then
    AC_MSG_RESULT(yes)
    PKG_CHECK_MODULES([CMOCKA], [cmocka])
    AC_DEFINE([ENABLE_CMOCKA], [1], [Enable cmocka unit tests])
else
    AC_MSG_RESULT(no)
fi
AM_CONDITIONAL([ENABLE_CMOCKA], [test "x$enable_cmocka" = "xyes"])

m4_include(m4/doxygen.m4)

PACKAGE_BASE_VERSION=`echo $PACKAGE_VERSION | awk -F\. '{print $1"."$2}'`
AC_SUBST(PACKAGE_BASE_VERSION)

AM_CONDITIONAL(OPENLDAP,test "$with_openldap" = "yes")

# write out paths for binary components
AC_SUBST(ldaplib)
AC_SUBST(ldaplib_defs)
AC_SUBST(ldaptool_bindir)
AC_SUBST(ldaptool_opts)
AC_SUBST(plainldif_opts)
AC_SUBST(localrundir)

AC_SUBST(brand)
AC_SUBST(capbrand)
AC_SUBST(vendor)

# AC_DEFINE([USE_OLD_UNHASHED], [], [Use old unhashed code])

# Internally we use a macro function slapi_log_err() to call slapi_log_error()
# which gives us the option to do performance testing without the presence of
# logging.  To remove the presence of error logging undefine LDAP_ERROR_LOGGING.
AC_DEFINE([LDAP_ERROR_LOGGING], [1], [LDAP error logging flag])

# Build our pkgconfig files
# This currently conflicts with %.in: rule in Makefile.am, which should be removed eventually.

# AC_CONFIG_FILES([ldap/admin/src/defaults.inf])

AC_CONFIG_FILES([src/pkgconfig/dirsrv.pc src/pkgconfig/libsds.pc src/pkgconfig/svrcore.pc])

AC_CONFIG_FILES([Makefile rpm/389-ds-base.spec  ])

AC_CONFIG_FILES([.cargo/config])

AC_OUTPUT

