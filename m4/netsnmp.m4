# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2006 Red Hat, Inc.
# All rights reserved.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
#
# END COPYRIGHT BLOCK

AC_CHECKING(for Net-SNMP)

dnl - check for --with-netsnmp
AC_MSG_CHECKING(for --with-netsnmp)
AC_ARG_WITH(netsnmp, [  --with-netsnmp=PATH   Net-SNMP directory],
[
  if test -d "$withval" -a -d "$withval/lib" -a -d "$withval/include"; then
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
AC_MSG_RESULT(no))

dnl - check for --with-netsnmp-inc
AC_MSG_CHECKING(for --with-netsnmp-inc)
AC_ARG_WITH(netsnmp-inc, [  --with-netsnmp-inc=PATH     Net-SNMP include directory],
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
AC_ARG_WITH(netsnmp-lib, [  --with-netsnmp-lib=PATH     Net-SNMP library directory],
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
