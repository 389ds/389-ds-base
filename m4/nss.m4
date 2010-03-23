# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2007 Red Hat, Inc.
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

AC_CHECKING(for NSS)

# check for --with-nss
AC_MSG_CHECKING(for --with-nss)
AC_ARG_WITH(nss, AS_HELP_STRING([--with-nss@<:@=PATH@:>@],[Network Security Services (NSS) directory]),
[
  if test "$withval" = "yes"
  then
    AC_MSG_RESULT(yes)
  elif test "$withval" = "no"
  then
    AC_MSG_RESULT(no)
    AC_MSG_ERROR([NSS is required.])
  elif test -e "$withval"/include/nss.h -a -d "$withval"/lib
  then
    AC_MSG_RESULT([using $withval])
    NSSDIR=$withval
    nss_inc="-I$NSSDIR/include"
    nss_lib="-L$NSSDIR/lib"
    nss_libdir="$NSSDIR/lib"
  else
    echo
    AC_MSG_ERROR([$withval not found])
  fi
],
AC_MSG_RESULT(yes))

# check for --with-nss-inc
AC_MSG_CHECKING(for --with-nss-inc)
AC_ARG_WITH(nss-inc, AS_HELP_STRING([--with-nss-inc=PATH],[Network Security Services (NSS) include directory]),
[
  if test -e "$withval"/nss.h
  then
    AC_MSG_RESULT([using $withval])
    nss_inc="-I$withval"
  else
    echo
    AC_MSG_ERROR([$withval not found])
  fi
],
AC_MSG_RESULT(no))

# check for --with-nss-lib
AC_MSG_CHECKING(for --with-nss-lib)
AC_ARG_WITH(nss-lib, AS_HELP_STRING([--with-nss-lib=PATH],[Network Security Services (NSS) library directory]),
[
  if test -d "$withval"
  then
    AC_MSG_RESULT([using $withval])
    nss_lib="-L$withval"
    nss_libdir="$withval"
  else
    echo
    AC_MSG_ERROR([$withval not found])
  fi
],
AC_MSG_RESULT(no))

# if NSS is not found yet, try pkg-config

# last resort
if test -z "$nss_inc" -o -z "$nss_lib" -o -z "$nss_libdir"; then
  AC_PATH_PROG(PKG_CONFIG, pkg-config)
  AC_MSG_CHECKING(for nss with pkg-config)
  if test -n "$PKG_CONFIG"; then
    if $PKG_CONFIG --exists nss; then
      nss_inc=`$PKG_CONFIG --cflags-only-I nss`
      nss_lib=`$PKG_CONFIG --libs-only-L nss`
      nss_libdir=`$PKG_CONFIG --libs-only-L nss | sed -e s/-L// | sed -e s/\ .*$//`
      AC_MSG_RESULT([using system NSS])
    elif $PKG_CONFIG --exists dirsec-nss; then
      nss_inc=`$PKG_CONFIG --cflags-only-I dirsec-nss`
      nss_lib=`$PKG_CONFIG --libs-only-L dirsec-nss`
      nss_libdir=`$PKG_CONFIG --libs-only-L dirsec-nss | sed -e s/-L// | sed -e s/\ .*$//`
      AC_MSG_RESULT([using system dirsec NSS])
    else
      AC_MSG_ERROR([NSS not found, specify with --with-nss.])
    fi
  fi
fi
