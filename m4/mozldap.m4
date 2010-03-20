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

AC_CHECKING(for Mozilla LDAPSDK)

# check for --with-ldapsdk
AC_MSG_CHECKING(for --with-ldapsdk)
AC_ARG_WITH(ldapsdk, AS_HELP_STRING([--with-ldapsdk@<:@=PATH@:>@],[Mozilla LDAP SDK directory]),
[
  if test "$withval" = yes
  then
    AC_MSG_RESULT(yes)
  elif test "$withval" = no
  then
    AC_MSG_RESULT(no)
  elif test -e "$withval"/include/ldap.h -a -d "$withval"/lib
  then
    AC_MSG_RESULT([using $withval])
    LDAPSDKDIR=$withval
    ldapsdk_inc="-I$LDAPSDKDIR/include"
    ldapsdk_lib="-L$LDAPSDKDIR/lib"
    ldapsdk_libdir="$LDAPSDKDIR/lib"
    ldapsdk_bindir="$LDAPSDKDIR/bin"
    with_ldapsdk=yes
  else
    echo
    AC_MSG_ERROR([$withval not found])
  fi

  if test "$with_ldapsdk" = yes -a "$with_openldap" = yes
  then
    AC_MSG_ERROR([Cannot use both LDAPSDK and OpenLDAP.])
  fi
  if test "$with_ldapsdk" != yes -a "$with_openldap" != yes
  then
    AC_MSG_ERROR([Either LDAPSDK or OpenLDAP must be used.])
  fi
],
[
  if test "$with_openldap" = yes
  then
    AC_MSG_RESULT(no)
  else
    AC_MSG_RESULT(yes)
    with_ldapsdk=yes
  fi
])

# check for --with-ldapsdk-inc
AC_MSG_CHECKING(for --with-ldapsdk-inc)
AC_ARG_WITH(ldapsdk-inc, AS_HELP_STRING([--with-ldapsdk-inc=PATH],[Mozilla LDAP SDK include directory]),
[
  if test -e "$withval"/ldap.h
  then
    AC_MSG_RESULT([using $withval])
    ldapsdk_inc="-I$withval"
    with_ldapsdk=yes
  else
    echo
    AC_MSG_ERROR([$withval not found])
  fi
],
AC_MSG_RESULT(no))

# check for --with-ldapsdk-lib
AC_MSG_CHECKING(for --with-ldapsdk-lib)
AC_ARG_WITH(ldapsdk-lib, AS_HELP_STRING([--with-ldapsdk-lib=PATH],[Mozilla LDAP SDK library directory]),
[
  if test -d "$withval"
  then
    AC_MSG_RESULT([using $withval])
    ldapsdk_lib="-L$withval"
    ldapsdk_libdir="$withval"
    with_ldapsdk=yes
  else
    echo
    AC_MSG_ERROR([$withval not found])
  fi
],
AC_MSG_RESULT(no))

# check for --with-ldapsdk-bin
AC_MSG_CHECKING(for --with-ldapsdk-bin)
AC_ARG_WITH(ldapsdk-bin, AS_HELP_STRING([--with-ldapsdk-bin=PATH],[Mozilla LDAP SDK binary directory]),
[
  if test -d "$withval"
  then
    AC_MSG_RESULT([using $withval])
    ldapsdk_bindir="$withval"
    with_ldapsdk=yes
  else
    echo
    AC_MSG_ERROR([$withval not found])
  fi
],
AC_MSG_RESULT(no))

# if LDAPSDK is not found yet, try pkg-config

# last resort
if test "$with_ldapsdk" = yes ; then
  if test -z "$ldapsdk_inc" -o -z "$ldapsdk_lib" -o -z "$ldapsdk_libdir" -o -z "$ldapsdk_bindir"; then
    AC_PATH_PROG(PKG_CONFIG, pkg-config)
    AC_MSG_CHECKING(for mozldap with pkg-config)
    if test -n "$PKG_CONFIG"; then
      if $PKG_CONFIG --exists mozldap6; then
	    mozldappkg=mozldap6
      elif $PKG_CONFIG --exists mozldap; then
	    mozldappkg=mozldap
      else
        AC_MSG_ERROR([LDAPSDK not found, specify with --with-ldapsdk[-inc|-lib|-bin].])
      fi
      ldapsdk_inc=`$PKG_CONFIG --cflags-only-I $mozldappkg`
      ldapsdk_lib=`$PKG_CONFIG --libs-only-L $mozldappkg`
      ldapsdk_libdir=`$PKG_CONFIG --libs-only-L $mozldappkg | sed -e s/-L// | sed -e s/\ .*$//`
      ldapsdk_bindir=`$PKG_CONFIG --variable=bindir $mozldappkg`
      AC_MSG_RESULT([using system $mozldappkg])
    fi
  fi
fi

if test "$with_ldapsdk" = yes ; then
  if test -z "$ldapsdk_inc" -o -z "$ldapsdk_lib"; then
    AC_MSG_ERROR([LDAPSDK not found, specify with --with-ldapsdk[-inc|-lib|-bin].])
  fi
dnl default path for the ldap c sdk tools (see [210947] for more details)
  if test -z "$ldapsdk_bindir" ; then
    if [ -d $libdir/mozldap6 ] ; then
      ldapsdk_bindir=$libdir/mozldap6
    else
      ldapsdk_bindir=$libdir/mozldap
    fi
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
  AC_DEFINE([USE_MOZLDAP], [1], [If defined, using MozLDAP for LDAP SDK])
  AC_DEFINE([HAVE_LDAP_URL_PARSE_NO_DEFAULTS], [1], [have the function ldap_url_parse_no_defaults])
fi
