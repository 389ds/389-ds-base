# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details. 
# END COPYRIGHT BLOCK

AC_CHECKING(for --enable-tcmalloc)

AC_ARG_ENABLE(tcmalloc, AS_HELP_STRING([--enable-tcmalloc], [Enable tcmalloc based tests (default: no)]),
[
    AC_MSG_RESULT(yes)
    AC_DEFINE([WITH_TCMALLOC], [1], [With tcmalloc])
    with_tcmalloc="yes"

    if test "$enable_asan" = "yes" ; then
        AC_MSG_ERROR([CRITICAL: You may not enable ASAN and TCMALLOC simultaneously])
    fi

    case $host in
      s390-*-linux*)
        AC_MSG_ERROR([tcmalloc not support on s390])
        ;;
      s390x-*-linux*)
        AC_MSG_ERROR([tcmalloc not support on s390x])
        ;;
      *)
        AC_MSG_CHECKING(for tcmalloc)
        if $PKG_CONFIG --exists libtcmalloc; then
            tcmalloc_inc=`$PKG_CONFIG --cflags libtcmalloc`
            tcmalloc_lib=`$PKG_CONFIG --libs libtcmalloc`
            AC_MSG_RESULT([using system tcmalloc])
        else
            AC_MSG_ERROR([pkg-config could not find tcmalloc!])
        fi
    esac

],
[
    AC_MSG_RESULT(no)
    with_tcmalloc="0"
])

AM_CONDITIONAL([WITH_TCMALLOC], [test "$with_tcmalloc" = "yes"])
AC_SUBST(tcmalloc_inc)
AC_SUBST(tcmalloc_lib)


