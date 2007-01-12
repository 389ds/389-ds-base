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

AC_CHECKING(for db)

dnl  - check for --with-db
AC_MSG_CHECKING(for --with-db)
AC_ARG_WITH(db, [  --with-db=PATH   Berkeley DB directory],
[
  if test "$withval" = "yes"; then
    AC_MSG_RESULT(yes)
    dnl - check in system locations
    if test -f "/usr/include/db.h"; then
      db_incdir="/usr/include"
      db_inc="-I/usr/include"
    else
      AC_MSG_ERROR([db.h not found])
    fi
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
AC_MSG_RESULT(no))
dnl default path for the db tools (see [210947] for more details)
db_bindir=/usr/bin

dnl - check in system locations
if test -z "$db_inc"; then
  AC_MSG_CHECKING(for db.h)
  if test -f "/usr/include/db.h"; then
    AC_MSG_RESULT([using /usr/include/db.h])
    db_incdir="/usr/include"
    db_inc="-I/usr/include"
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
AC_CHECK_LIB([db-$db_libver], [db_create], [],
  [AC_MSG_ERROR([$db_incdir/db.h is version $db_libver but libdb-$db_libver not found])])

