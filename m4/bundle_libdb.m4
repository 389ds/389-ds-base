# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2024 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# END COPYRIGHT BLOCK

AC_MSG_CHECKING(Handling bundle_libdb)

db_lib="-L${with_bundle_libdb}/.libs -R${prefix}/lib64/dirsrv"
db_incdir=$with_bundle_libdb
db_inc="-I $db_incdir"
db_libver="5.3-389ds"

dnl figure out which version of db we're using from the header file
db_ver_maj=`grep DB_VERSION_MAJOR $db_incdir/db.h | awk '{print $3}'`
db_ver_min=`grep DB_VERSION_MINOR $db_incdir/db.h | awk '{print $3}'`
db_ver_pat=`grep DB_VERSION_PATCH $db_incdir/db.h | awk '{print $3}'`

dnl Ensure that we have libdb at least 4.7, older versions aren't supported
if test ${db_ver_maj} -lt 4; then
  AC_MSG_ERROR([Found db ${db_ver_maj}.${db_ver_min} is too old, update to version 4.7 at least])
elif test ${db_ver_maj} -eq 4 -a ${db_ver_min} -lt 7; then
  AC_MSG_ERROR([Found db ${db_ver_maj}.${db_ver_min} is too old, update to version 4.7 at least])
else
  AC_MSG_RESULT([libdb-${db_ver_maj}.${db_ver_min}-389ds.so])
fi


AC_SUBST(db_inc)
AC_SUBST(db_lib)
AC_SUBST(db_libver)

