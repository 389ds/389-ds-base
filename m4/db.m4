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

AC_CHECKING(for db)

dnl  - check for --with-db
AC_MSG_CHECKING(for --with-db)
AC_ARG_WITH(db, AS_HELP_STRING([--with-db@<:@=PATH@:>@],[Berkeley DB directory]),
[
  if test "$withval" = "yes"; then
    AC_MSG_RESULT(yes)
  elif test "$withval" = "no"; then
    AC_MSG_RESULT(no)
    AC_MSG_ERROR([db is required.])
  elif test -d "$withval"/include -a -d "$withval"/lib; then
    AC_MSG_RESULT([using $withval])
    dnl - check the user provided location
    DBDIR=$withval
    db_lib="-L$DBDIR/lib"
    db_libdir="$DBDIR/lib"
    db_incdir="$DBDIR/include"
    if ! test -e "$db_incdir/db.h" ; then
      AC_MSG_ERROR([$withval include dir not found])
    fi
    db_inc="-I$db_incdir"
  else
    echo
    AC_MSG_ERROR([$withval not found])
  fi
],
AC_MSG_RESULT(yes))
dnl default path for the db tools (see [210947] for more details)

# check for --with-db-inc
AC_MSG_CHECKING(for --with-db-inc)
AC_ARG_WITH(db-inc, AS_HELP_STRING([--with-db-inc=PATH],[Berkeley DB include file directory]),
[
  if test -e "$withval"/db.h
  then
    AC_MSG_RESULT([using $withval])
    db_incdir="$withval"
    db_inc="-I$withval"
  else
    echo
    AC_MSG_ERROR([$withval not found])
  fi
],
AC_MSG_RESULT(no))

# check for --with-db-lib
AC_MSG_CHECKING(for --with-db-lib)
AC_ARG_WITH(db-lib, AS_HELP_STRING([--with-db-lib=PATH],[Berkeley DB library directory]),
[
  if test -d "$withval"
  then
    AC_MSG_RESULT([using $withval])
    db_lib="-L$withval"
    db_libdir="$withval"
  else
    echo
    AC_MSG_ERROR([$withval not found])
  fi
],
AC_MSG_RESULT(no))

dnl - check in system locations
if test -z "$db_inc"; then
  AC_MSG_CHECKING(for db.h)
  if test -f "/usr/include/db4/db.h"; then
    AC_MSG_RESULT([using /usr/include/db4/db.h])
    db_incdir="/usr/include/db4"
    db_inc="-I/usr/include/db4"
    db_lib='-L$(libdir)'
    db_libdir='$(libdir)'
  elif test -f "/usr/include/db.h"; then
    AC_MSG_RESULT([using /usr/include/db.h])
    db_incdir="/usr/include"
    db_inc="-I/usr/include"
    db_lib='-L$(libdir)'
    db_libdir='$(libdir)'
  else
    AC_MSG_RESULT(no)
    AC_MSG_ERROR([db not found, specify with --with-db.])
  fi
fi

dnl figure out which version of db we're using from the header file
db_ver_maj=`grep DB_VERSION_MAJOR $db_incdir/db.h | awk '{print $3}'`
db_ver_min=`grep DB_VERSION_MINOR $db_incdir/db.h | awk '{print $3}'`
db_ver_pat=`grep DB_VERSION_PATCH $db_incdir/db.h | awk '{print $3}'`
dnl libname is libdb-maj.min e.g. libdb-4.2
db_libver=${db_ver_maj}.${db_ver_min}
dnl make sure the lib is available
dnl use true so libdb won't be added to LIBS
save_ldflags="$LDFLAGS"
LDFLAGS="$db_lib $LDFLAGS"
AC_CHECK_LIB([db-$db_libver], [db_create], [true],
  [AC_MSG_ERROR([$db_incdir/db.h is version $db_libver but libdb-$db_libver not found])],
  [$LIBNSL])
LDFLAGS="$save_ldflags"

# if DB is not found yet, try pkg-config

# last resort
# Although the other db_* variables are correctly assigned at this point,
# db_bindir needs to be set by pkg-config if possible (e.g., on 64-bit Solaris)
if  test -n "$PKG_CONFIG"; then
  if $PKG_CONFIG --exists db; then
    db_bindir=`$PKG_CONFIG --variable=bindir db`
  else
    db_bindir=/usr/bin
  fi
else
  db_bindir=/usr/bin
fi
