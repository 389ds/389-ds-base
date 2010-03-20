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

AC_CHECKING(for LIBICU)

# check for --with-icu
AC_MSG_CHECKING(for --with-icu)
AC_ARG_WITH(icu, AS_HELP_STRING([--with-icu@<:@=PATH@:>@],[ICU directory]),
[
  if test "$withval" = "yes"
  then
    AC_MSG_RESULT(yes)
  elif test "$withval" = "no"
  then
    AC_MSG_RESULT(no)
    AC_MSG_ERROR([ICU is required.])
  elif test -d "$withval"/lib
  then
    AC_MSG_RESULT([using $withval])
    ICUDIR=$withval
    icu_lib="-L$ICUDIR/lib"
    icu_inc="-I$withval/include"
    icu_bin="$withval/bin"
  else
    echo
    AC_MSG_ERROR([$withval not found])
  fi
],
AC_MSG_RESULT(yes))

# check for --with-icu-inc
AC_MSG_CHECKING(for --with-icu-inc)
AC_ARG_WITH(icu-inc, AS_HELP_STRING([--with-icu-inc=PATH],[ICU include directory]),
[
  if test -d "$withval"
  then
    AC_MSG_RESULT([using $withval])
    icu_inc="-I$withval"
  else
    echo
    AC_MSG_ERROR([$withval not found])
  fi
],
AC_MSG_RESULT(no))

# check for --with-icu-lib
AC_MSG_CHECKING(for --with-icu-lib)
AC_ARG_WITH(icu-lib, AS_HELP_STRING([--with-icu-lib=PATH],[ICU library directory]),
[
  if test -d "$withval"
  then
    AC_MSG_RESULT([using $withval])
    icu_lib="-L$withval"
  else
    echo
    AC_MSG_ERROR([$withval not found])
  fi
],
AC_MSG_RESULT(no))

# check for --with-icu-bin
AC_MSG_CHECKING(for --with-icu-bin)
AC_ARG_WITH(icu-bin, AS_HELP_STRING([--with-icu-bin=PATH],[ICU binary directory]),
[
  if test -d "$withval"
  then
    AC_MSG_RESULT([using $withval])
    icu_bin="$withval"
  else
    echo
    AC_MSG_ERROR([$withval not found])
  fi
],
AC_MSG_RESULT(no))
# if ICU is not found yet, try pkg-config

# last resort
if test -z "$icu_lib"; then
  AC_PATH_PROG(ICU_CONFIG, icu-config)
  AC_MSG_CHECKING(for icu with icu-config)
  if test -n "$ICU_CONFIG"; then
    icu_lib=`$ICU_CONFIG --ldflags-searchpath`
    icu_inc=`$ICU_CONFIG --cppflags-searchpath`
    icu_bin=`$ICU_CONFIG --bindir`
    AC_MSG_RESULT([using system ICU])
  else
    AC_MSG_ERROR([ICU not found, specify with --with-icu.])
  fi
fi
