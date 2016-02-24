# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details. 
# END COPYRIGHT BLOCK

AC_CHECKING(for Systemd)

# check for --with-systemd
AC_MSG_CHECKING(for --with-systemd)
AC_ARG_WITH(systemd, AS_HELP_STRING([--with-systemd],[Enable Systemd native integration.]),
[
    if test "$withval" = yes
    then
        AC_MSG_RESULT([using systemd native features])
        with_systemd=yes
    else
        AC_MSG_RESULT(no)
    fi
],
AC_MSG_RESULT(no))

if test "$with_systemd" = yes; then
    AC_PATH_PROG(PKG_CONFIG, pkg-config)
    AC_MSG_CHECKING(for Systemd with pkg-config)
    if test -n "$PKG_CONFIG" && $PKG_CONFIG --exists systemd libsystemd-journal libsystemd-daemon ; then
        systemd_inc=`$PKG_CONFIG --cflags-only-I systemd libsystemd-journal libsystemd-daemon`
        systemd_lib=`$PKG_CONFIG --libs-only-l systemd libsystemd-journal libsystemd-daemon`
        systemd_defs="-DWITH_SYSTEMD"
    else
        AC_MSG_ERROR([no Systemd / Journald pkg-config files])
    fi

    # Check for the pkg config provided unit paths
    if test -n "$PKG_CONFIG" ; then
       default_systemdsystemunitdir=`$PKG_CONFIG --variable=systemdsystemunitdir systemd`
       default_systemdsystemconfdir=`$PKG_CONFIG --variable=systemdsystemconfdir systemd`
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


fi
# End of with_systemd

AM_CONDITIONAL([SYSTEMD],[test -n "$with_systemd"])

