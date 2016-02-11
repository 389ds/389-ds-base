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
        AC_MSG_ERROR([no Systemd pkg-config files])
    fi

fi
