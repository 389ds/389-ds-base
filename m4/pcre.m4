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

AC_CHECKING(for pcre)

dnl  - check for --with-pcre
AC_MSG_CHECKING(for --with-pcre)
AC_ARG_WITH(pcre, [  --with-pcre=PATH   Perl Compatible Regular Expression directory],
[
  if test "$withval" = "yes"; then
    AC_MSG_RESULT(yes)
    dnl - check in system locations
    if test -f "/usr/include/pcre/pcre.h"; then
      pcre_incdir="/usr/include/pcre"
      pcre_inc="-I/usr/include/pcre"
      pcre_lib='-L$(libdir)'
      pcre_libdir='$(libdir)'
    elif test -f "/usr/include/pcre.h"; then
      pcre_incdir="/usr/include"
      pcre_inc="-I/usr/include"
      pcre_lib='-L$(libdir)'
      pcre_libdir='$(libdir)'
    else
      AC_MSG_ERROR([pcre.h not found])
    fi
  elif test -d "$withval"/include -a -d "$withval"/lib; then
    AC_MSG_RESULT([using $withval])
    dnl - check the user provided location
    PCREDIR=$withval
    pcre_lib="-L$PCREDIR/lib"
    pcre_libdir="$PCREDIR/lib"
    pcre_incdir="$PCREDIR/include"
    if ! test -e "$pcre_incdir/pcre.h" ; then
      AC_MSG_ERROR([$withval include dir not found])
    fi
    pcre_inc="-I$pcre_incdir"
  else
    echo
    AC_MSG_ERROR([$withval not found])
  fi
],
AC_MSG_RESULT(no))

#
# if PCRE is not found yet, try pkg-config
if test -z "$pcre_inc" -o -z "$pcre_lib" -o -z "$pcre_libdir"; then
  AC_PATH_PROG(PKG_CONFIG, pkg-config)
  AC_MSG_CHECKING(for pcre with pkg-config)
  if test -n "$PKG_CONFIG"; then
    if $PKG_CONFIG --exists pcre; then
      pcre_inc=`$PKG_CONFIG --cflags-only-I pcre`
      pcre_lib=`$PKG_CONFIG --libs-only-L pcre`
      pcre_libdir=`$PKG_CONFIG --libs-only-L pcre | sed -e s/-L// | sed -e s/\ .*$//`
      AC_MSG_RESULT([using system PCRE])
    elif $PKG_CONFIG --exists libpcre; then
      pcre_inc=`$PKG_CONFIG --cflags-only-I libpcre`
      pcre_lib=`$PKG_CONFIG --libs-only-L libpcre`
      pcre_libdir=`$PKG_CONFIG --libs-only-L libpcre | sed -e s/-L// | sed -e s/\ .*$//`
      AC_MSG_RESULT([using system PCRE])
    else
      AC_MSG_ERROR([PCRE not found, specify with --with-pcre.])
    fi
  fi
fi

dnl last resort
dnl - check in system locations
if test -z "$pcre_inc"; then
  AC_MSG_CHECKING(for pcre.h)
  if test -f "/usr/include/pcre.h"; then
    AC_MSG_RESULT([using /usr/include/pcre.h])
    pcre_incdir="/usr/include"
    pcre_inc="-I/usr/include"
    pcre_lib='-L$(libdir)'
    pcre_libdir='$(libdir)'
  else
    AC_MSG_RESULT(no)
    AC_MSG_ERROR([pcre not found, specify with --with-pcre.])
  fi
fi
