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

AC_CHECKING(for NSPR)

# check for --with-nspr
AC_MSG_CHECKING(for --with-nspr)
AC_ARG_WITH(nspr, [  --with-nspr=PATH        Netscape Portable Runtime (NSPR) directory],
[
  if test -e "$withval"/include/nspr.h -a -d "$withval"/lib
  then
    AC_MSG_RESULT([using $withval])
    NSPRDIR=$withval
    nspr_inc="-I$NSPRDIR/include"
    nspr_lib="-L$NSPRDIR/lib"
  else
    echo
    AC_MSG_ERROR([$withval not found])
  fi
],
AC_MSG_RESULT(no))

# check for --with-nspr-inc
AC_MSG_CHECKING(for --with-nspr-inc)
AC_ARG_WITH(nspr-inc, [  --with-nspr-inc=PATH        Netscape Portable Runtime (NSPR) include file directory],
[
  if test -e "$withval"/nspr.h
  then
    AC_MSG_RESULT([using $withval])
    nspr_inc="-I$withval"
  else
    echo
    AC_MSG_ERROR([$withval not found])
  fi
],
AC_MSG_RESULT(no))

# check for --with-nspr-lib
AC_MSG_CHECKING(for --with-nspr-lib)
AC_ARG_WITH(nspr-lib, [  --with-nspr-lib=PATH        Netscape Portable Runtime (NSPR) library directory],
[
  if test -d "$withval"
  then
    AC_MSG_RESULT([using $withval])
    nspr_lib="-L$withval"
  else
    echo
    AC_MSG_ERROR([$withval not found])
  fi
],
AC_MSG_RESULT(no))

# if NSPR is not found yet, try pkg-config

# last resort
if test -z "$nspr_inc" -o -z "$nspr_lib"; then
  AC_MSG_CHECKING(for nspr with pkg-config)
  AC_PATH_PROG(PKG_CONFIG, pkg-config)
  if test -n "$PKG_CONFIG"; then
    if $PKG_CONFIG --exists nspr; then
      nspr_inc=`$PKG_CONFIG --cflags-only-I nspr`
      nspr_lib=`$PKG_CONFIG --libs-only-L nspr`
      AC_MSG_RESULT([using system NSPR])
    else
      AC_MSG_ERROR([NSPR not found, specify with --with-nspr.])
    fi
  fi
fi
