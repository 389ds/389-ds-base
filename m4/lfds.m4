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

AC_CHECKING(for lfds)

dnl  - check for --with-lfds
AC_MSG_CHECKING(for --with-lfds)
AC_ARG_WITH(lfds, AS_HELP_STRING([--with-lfds@<:@=PATH@:>@],[LFDS directory]),
[
  if test "$withval" = "yes"; then
    AC_MSG_RESULT(yes)
  elif test "$withval" = "no"; then
    AC_MSG_RESULT(no)
  elif test -d "$withval"/inc -a -d "$withval"/bin; then
    AC_MSG_RESULT([using $withval])
    dnl - check the user provided location
    lfds_lib="-L$withval/bin"
    lfds_libdir="$withval/lib"
    lfds_incdir="$withval/inc"
    if ! test -e "$lfds_incdir/liblfds.h" ; then
      AC_MSG_ERROR([$withval include dir not found])
    fi
    lfds_inc="-I$lfds_incdir"
  else
    echo
    AC_MSG_ERROR([$withval not found])
  fi
],
AC_MSG_RESULT(no))

# check for --with-lfds-inc
AC_MSG_CHECKING(for --with-lfds-inc)
AC_ARG_WITH(lfds-inc, AS_HELP_STRING([--with-lfds-inc=PATH],[LFDS include file directory]),
[
  if test -e "$withval"/liblfds.h
  then
    AC_MSG_RESULT([using $withval])
    lfds_incdir="$withval"
    lfds_inc="-I$withval"
  else
    echo
    AC_MSG_ERROR([$withval not found])
  fi
],
AC_MSG_RESULT(no))

# check for --with-lfds-lib
AC_MSG_CHECKING(for --with-lfds-lib)
AC_ARG_WITH(lfds-lib, AS_HELP_STRING([--with-lfds-lib=PATH],[LFDS library directory]),
[
  if test -d "$withval"
  then
    AC_MSG_RESULT([using $withval])
    lfds_lib="-L$withval"
    lfds_libdir="$withval"
  else
    echo
    AC_MSG_ERROR([$withval not found])
  fi
],
AC_MSG_RESULT(no))
