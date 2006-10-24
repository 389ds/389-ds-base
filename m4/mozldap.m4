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

AC_CHECKING(for LDAPSDK)

# check for --with-ldapsdk
AC_MSG_CHECKING(for --with-ldapsdk)
AC_ARG_WITH(ldapsdk, [  --with-ldapsdk=PATH     Mozilla LDAP SDK directory],
[
  if test -e "$withval"/include/ldap.h -a -d "$withval"/lib
  then
    AC_MSG_RESULT([using $withval])
    LDAPSDKDIR=$withval
    ldapsdk_inc="-I$LDAPSDKDIR/include"
    ldapsdk_lib="-L$LDAPSDKDIR/lib"
    ldapsdk_libdir="$LDAPSDKDIR/lib"
  else
    echo
    AC_MSG_ERROR([$withval not found])
  fi
],
AC_MSG_RESULT(no))

# check for --with-ldapsdk-inc
AC_MSG_CHECKING(for --with-ldapsdk-inc)
AC_ARG_WITH(ldapsdk-inc, [  --with-ldapsdk-inc=PATH     Mozilla LDAP SDK include directory],
[
  if test -e "$withval"/ldap.h
  then
    AC_MSG_RESULT([using $withval])
    ldapsdk_inc="-I$withval"
  else
    echo
    AC_MSG_ERROR([$withval not found])
  fi
],
AC_MSG_RESULT(no))

# check for --with-ldapsdk-lib
AC_MSG_CHECKING(for --with-ldapsdk-lib)
AC_ARG_WITH(ldapsdk-lib, [  --with-ldapsdk-lib=PATH     Mozilla LDAP SDK library directory],
[
  if test -d "$withval"
  then
    AC_MSG_RESULT([using $withval])
    ldapsdk_lib="-L$withval"
    ldapsdk_libdir="$withval"
  else
    echo
    AC_MSG_ERROR([$withval not found])
  fi
],
AC_MSG_RESULT(no))

# if LDAPSDK is not found yet, try pkg-config

# last resort
if test -z "$ldapsdk_inc" -o -z "$ldapsdk_lib" -o -z "$ldapsdk_libdir"; then
  AC_MSG_CHECKING(for mozldap with pkg-config)
  AC_PATH_PROG(PKG_CONFIG, pkg-config)
  if test -n "$PKG_CONFIG"; then
    if $PKG_CONFIG --exists mozldap6; then
      ldapsdk_inc=`$PKG_CONFIG --cflags-only-I mozldap6`
      ldapsdk_lib=`$PKG_CONFIG --libs-only-L mozldap6`
      ldapsdk_libdir=`$PKG_CONFIG --libs-only-L mozldap6 | sed -e s/-L// | sed -e s/\ *$//`
      AC_MSG_RESULT([using system mozldap6])
    else
      AC_MSG_ERROR([LDAPSDK not found, specify with --with-ldapsdk[-inc|-lib].])
    fi
  fi
fi
if test -z "$ldapsdk_inc" -o -z "$ldapsdk_lib"; then
  AC_MSG_ERROR([LDAPSDK not found, specify with --with-ldapsdk[-inc|-lib].])
fi
dnl make sure the ldap sdk version is 6 or greater - we do not support
dnl the old 5.x or prior versions - the ldap server code expects the new
dnl ber types and other code used with version 6
save_cppflags="$CPPFLAGS"
CPPFLAGS="$ldapsdk_inc $nss_inc $nspr_inc"
AC_CHECK_HEADER([ldap.h], [isversion6=1], [isversion6=],
[#include <ldap-standard.h>
#if LDAP_VENDOR_VERSION < 600
#error The LDAP C SDK version is not supported
#endif
])
CPPFLAGS="$save_cppflags"

if test -z "$isversion6" ; then
  AC_MSG_ERROR([The LDAPSDK version in $ldapsdk_inc/ldap-standard.h is not supported])
fi
