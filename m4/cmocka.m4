## BEGIN COPYRIGHT BLOCK
## Copyright (c) 2016, William Brown <william at blackhats dot net dot au>
## All rights reserved.
##
## License: License: GPL (version 3 or any later version).
## See LICENSE for details.
## END COPYRIGHT BLOCK

AC_MSG_CHECKING(for --enable-cmocka)
AC_ARG_ENABLE(cmocka, AS_HELP_STRING([--enable-cmocka], [Enable cmocka based tests (default: no)]),
[
    AC_MSG_RESULT(yes)
    AC_DEFINE([WITH_CMOCKA], [1], [With cmocka unit tests])
    with_cmocka="yes"
    AC_MSG_CHECKING(for cmocka)
    if $PKG_CONFIG --exists cmocka; then
        cmocka_inc=`$PKG_CONFIG --cflags cmocka`
        cmocka_lib=`$PKG_CONFIG --libs cmocka`
        AC_MSG_RESULT([using system cmocka])
    else
        AC_MSG_ERROR([pkg-config could not find cmocka!])
    fi
],
[
    AC_MSG_RESULT(no)
    with_cmocka="0"
])

AM_CONDITIONAL([WITH_CMOCKA], [test "$with_cmocka" = "yes"])
AC_SUBST(cmocka_inc)
AC_SUBST(cmocka_lib)


