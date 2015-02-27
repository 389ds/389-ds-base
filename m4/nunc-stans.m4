# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2015 Red Hat, Inc.
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

AC_CHECKING(for nunc-stans)

dnl  - check for --with-nunc-stans
AC_MSG_CHECKING(for --with-nunc-stans)
AC_ARG_WITH(nunc-stans, AS_HELP_STRING([--with-nunc-stans@<:@=PATH@:>@],[nunc-stans directory]),
[
  if test "$withval" = "yes"; then
    AC_MSG_RESULT(yes)
  elif test "$withval" = "no"; then
    AC_MSG_RESULT(no)
  elif test -d "$withval"; then
    AC_MSG_RESULT([using $withval])
    dnl - check the user provided location
    nunc_stans_lib="-L$withval/lib"
    nunc_stans_libdir="$withval/lib"
    nunc_stans_incdir="$withval/include"
    if ! test -e "$nunc_stans_incdir/nunc-stans/ns_thrpool.h" ; then
      AC_MSG_ERROR([$withval include dir not found])
    fi
    nunc_stans_inc="-I$nunc_stans_incdir"
  else
    echo
    AC_MSG_ERROR([$withval not found])
  fi
],
AC_MSG_RESULT(no))

# check for --with-nunc-stans-inc
AC_MSG_CHECKING(for --with-nunc-stans-inc)
AC_ARG_WITH(nunc-stans-inc, AS_HELP_STRING([--with-nunc-stans-inc=PATH],[nunc-stans include file directory]),
[
  if test -e "$withval"/nunc-stans/ns_thrpool.h
  then
    AC_MSG_RESULT([using $withval])
    nunc_stans_incdir="$withval"
    nunc_stans_inc="-I$withval"
  else
    echo
    AC_MSG_ERROR([$withval not found])
  fi
],
AC_MSG_RESULT(no))

# check for --with-nunc-stans-lib
AC_MSG_CHECKING(for --with-nunc-stans-lib)
AC_ARG_WITH(nunc-stans-lib, AS_HELP_STRING([--with-nunc-stans-lib=PATH],[nunc-stans library directory]),
[
  if test -d "$withval"
  then
    AC_MSG_RESULT([using $withval])
    nunc_stans_lib="-L$withval"
    nunc_stans_libdir="$withval"
  else
    echo
    AC_MSG_ERROR([$withval not found])
  fi
],
AC_MSG_RESULT(no))
