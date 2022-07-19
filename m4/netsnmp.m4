# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details. 
# END COPYRIGHT BLOCK

AC_MSG_CHECKING(for Net-SNMP)

dnl - check for --with-netsnmp
AC_MSG_CHECKING(for --with-netsnmp)
AC_ARG_WITH(netsnmp, AS_HELP_STRING([--with-netsnmp@<:@=PATH@:>@],[Net-SNMP directory]),
[
  if test "$withval" = "yes"; then
    AC_MSG_RESULT(yes)
  elif test "$withval" = "no"; then
    AC_MSG_RESULT(no)
    AC_MSG_ERROR([Net-SNMP is required.])
  elif test -d "$withval" -a -d "$withval/lib" -a -d "$withval/include"; then
    AC_MSG_RESULT([using $withval])
    NETSNMPDIR=$withval

    if test -f "$withval/include/net-snmp/net-snmp-includes.h"; then
      netsnmp_inc="-I$withval/include"
    else
      AC_MSG_ERROR(net-snmp-config.h not found)
    fi

    netsnmp_lib="-L$withval/lib"
    netsnmp_libdir="$withval/lib"
  else
    AC_MSG_RESULT(yes)
    AC_MSG_ERROR([$withval not found])
  fi
],
AC_MSG_RESULT(yes))

dnl - check for --with-netsnmp-inc
AC_MSG_CHECKING(for --with-netsnmp-inc)
AC_ARG_WITH(netsnmp-inc, AS_HELP_STRING([--with-netsnmp-inc=PATH],[Net-SNMP include directory]),
[
  if test -f "$withval/net-snmp/net-snmp-includes.h"; then
    AC_MSG_RESULT([using $withval])
    netsnmp_inc="-I$withval"
  else
    echo
    AC_MSG_ERROR([$withval/net-snmp/net-snmp-includes.h not found])
  fi
],
AC_MSG_RESULT(no))

dnl -  check for --with-netsnmp-lib
AC_MSG_CHECKING(for --with-netsnmp-lib)
AC_ARG_WITH(netsnmp-lib, AS_HELP_STRING([--with-netsnmp-lib=PATH],[Net-SNMP library directory]),
[
  if test -d "$withval"
  then
    AC_MSG_RESULT([using $withval])
    netsnmp_lib="-L$withval"
    netsnmp_libdir="$withval"
  else
    echo
    AC_MSG_ERROR([$withval not found])
  fi
],
AC_MSG_RESULT(no))

dnl - look in standard system locations
if test -z "$netsnmp_inc" -o -z "$netsnmp_lib"; then
  AC_MSG_CHECKING(for net-snmp-includes.h)
  if test -f /usr/include/net-snmp/net-snmp-includes.h; then
    AC_MSG_RESULT([using /usr/include/net-snmp/net-snmp-includes.h])
    netsnmp_inc="-I/usr/include"
  else
    AC_MSG_RESULT(no)
    AC_MSG_ERROR([net-snmp not found, specify with --with-netsnmp.])
  fi
fi

dnl -  find dependent libs with net-snmp-config
if test -n "$netsnmp_inc"; then
  if test -x "$NETSNMPDIR/bin/net-snmp-config"; then
    NETSNMP_CONFIG=$NETSNMPDIR/bin/net-snmp-config
  else
    AC_PATH_PROG(NETSNMP_CONFIG, net-snmp-config)
  fi

  if test -n "$NETSNMP_CONFIG"; then
    netsnmp_link=`$NETSNMP_CONFIG --agent-libs`
  else
    AC_MSG_ERROR([net-snmp-config not found, specify with --with-netsnmp.])
  fi
else
  AC_MSG_ERROR([Net-SNMP not found, specify with --with-netsnmp.])
fi

AC_SUBST(netsnmp_inc)
AC_SUBST(netsnmp_lib)
AC_SUBST(netsnmp_libdir)
AC_SUBST(netsnmp_link)

