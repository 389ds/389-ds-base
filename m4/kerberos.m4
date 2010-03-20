# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2008 Red Hat, Inc.
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
# -*- tab-width: 4; -*-
# Configure paths for Kerberos

dnl ========================================================
dnl = Kerberos is used directly for server to server SASL/GSSAPI
dnl = authentication (replication, chaining, etc.)
dnl = This allows us to authenticate using a keytab without
dnl = having to call kinit outside the process
dnl ========================================================
AC_CHECKING(for Kerberos)

if test -z "$with_kerberos" ; then
   with_kerberos=yes # if not set on cmdline, set default
fi

AC_MSG_CHECKING(for --with-kerberos)
AC_ARG_WITH(kerberos,
    AS_HELP_STRING([--with-kerberos@<:@=PATH@:>@], [Use the kerberos API in the server directly - allows the server to authenticate directly with a keytab - otherwise, SASL/GSSAPI auth depends on underlying SASL libraries and external kinit with a keytab - if PATH is not specified, look for kerberos in the system locations.  This will attempt to use krb5-config from the PATH to find the libs and include dirs - you can specify KRB5_CONFIG_BIN to specify a different filename or absolute path.  If krb5-config does not work, this will attempt to look in various system directories]),
    [
        if test "x$withval" = "xyes"; then
            AC_MSG_RESULT(yes)
        elif test "x$withval" = "xno"; then
            AC_MSG_RESULT(no)
            with_kerberos=
        elif test -d "$withval" -a -d "$withval/lib" -a -d "$withval/include" ; then
            AC_MSG_RESULT([using $withval])
            kerberos_incdir="$withval/include"
            kerberos_libdir="$withval/lib"
        else
            AC_MSG_RESULT(yes)
            AC_MSG_ERROR([kerberos not found in $withval])
        fi
    ],
    [
        AC_MSG_RESULT(no)
        with_kerberos=
    ]
)

AC_MSG_CHECKING(for --with-kerberos-inc)
AC_ARG_WITH(kerberos-inc,
    AS_HELP_STRING([--with-kerberos-inc=PATH], [Allows you to explicitly set the directory containing the kerberos include files - implies use of kerberos]),
    [
      if test -f "$withval"/krb5.h; then
        AC_MSG_RESULT([using $withval])
        kerberos_incdir="$withval"
        with_kerberos=yes # implies use of kerberos
      else
        echo
        AC_MSG_ERROR([$withval/krb5.h not found])
      fi
    ],
    AC_MSG_RESULT(no)
)

AC_MSG_CHECKING(for --with-kerberos-lib)
AC_ARG_WITH(kerberos-lib,
    AS_HELP_STRING([--with-kerberos-lib=PATH], [Allows you to explicitly set the directory containing the kerberos libraries - implies use of kerberos]),
    [
      if test -d "$withval"; then
        AC_MSG_RESULT([using $withval])
        kerberos_libdir="$withval"
        with_kerberos=yes # implies use of kerberos
      else
        echo
        AC_MSG_ERROR([$withval not found])
      fi
    ],
    AC_MSG_RESULT(no)
)

if test -n "$with_kerberos" ; then
    if test -z "$kerberos_incdir" -o -z "$kerberos_libdir" ; then
        dnl look for these using the krb5-config script
        dnl user can define KRB5_CONFIG_BIN to the full path
        dnl and filename of the script if it cannot or will not
        dnl be found in PATH
        if test -z "$KRB5_CONFIG_BIN" ; then
            AC_PATH_PROG(KRB5_CONFIG_BIN, krb5-config)
        fi
        if test -n "$KRB5_CONFIG_BIN" ; then
            AC_MSG_CHECKING(for kerberos with $KRB5_CONFIG_BIN)
            if test -z "$kerberos_libdir" ; then
                kerberos_lib=`$KRB5_CONFIG_BIN --libs krb5`
            fi
            if test -z "$kerberos_incdir" ; then
                kerberos_inc=`$KRB5_CONFIG_BIN --cflags krb5`
            fi
            dnl if using system includes, inc will be empty - ok
            if test -n "$kerberos_lib" ; then
                AC_MSG_RESULT([using kerberos found with $KRB5_CONFIG_BIN])
                have_krb5=yes
            fi
        fi
    fi
fi

if test -n "$with_kerberos" -a -z "$kerberos_lib" ; then
    # save these in order to set them to use the check macros below
    # like AC_CHECK_HEADERS, AC_CHECK_LIB, and AC_CHECK_FUNCS
    save_CPPFLAGS="$CPPFLAGS"
    if test -n "$kerberos_incdir" ; then
        CPPFLAGS="-I$kerberos_incdir $CPPFLAGS"
    fi
    save_LDFLAGS="$LDFLAGS"
    if test -n "$kerberos_libdir" ; then
        LDFLAGS="-L$kerberos_libdir $LDFLAGS"
    fi
    krb5_impl=mit

    dnl check for Heimdal Kerberos
    AC_CHECK_HEADERS(heim_err.h)
    if test $ac_cv_header_heim_err_h = yes ; then
        krb5_impl=heimdal
    fi

    if test "x$krb5_impl" = "xmit"; then
        AC_CHECK_LIB(k5crypto, main,
            [krb5crypto=k5crypto],
            [krb5crypto=crypto])

        AC_CHECK_LIB(krb5, main,
            [have_krb5=yes
            kerberos_lib="-lkrb5 -l$krb5crypto -lcom_err"],
            [have_krb5=no],
            [-l$krb5crypto -lcom_err])

    elif test "x$krb5_impl" = "xheimdal"; then
        AC_CHECK_LIB(des, main,
            [krb5crypto=des],
            [krb5crypto=crypto])

        AC_CHECK_LIB(krb5, main,
            [have_krb5=yes
            kerberos_lib="-lkrb5 -l$krb5crypto -lasn1 -lroken -lcom_err"],
            [have_krb5=no],
            [-l$krb5crypto -lasn1 -lroken -lcom_err])

        AC_DEFINE(HAVE_HEIMDAL_KERBEROS, 1,
            [define if you have HEIMDAL Kerberos])

    else
        have_krb5=no
        AC_MSG_WARN([Unrecognized Kerberos5 Implementation])
    fi

    # reset to original values
    CPPFLAGS="$save_CPPFLAGS"
    LDFLAGS="$save_LDFLAGS"
    if test -n "$kerberos_incdir" ; then
        kerberos_inc="-I$kerberos_incdir"
    fi
    if test -n "$kerberos_libdir" ; then
        kerberos_lib="-L$kerberos_libdir $kerberos_lib"
    fi
fi

dnl at this point kerberos_lib and kerberos_inc should be set

if test -n "$with_kerberos" ; then
    if test "x$have_krb5" = "xyes" ; then
        AC_DEFINE(HAVE_KRB5, 1,
            [define if you have Kerberos V])
    else
        AC_MSG_ERROR([Required Kerberos 5 support not available])
    fi

    dnl look for the wonderfully time saving function krb5_cc_new_unique
    save_LIBS="$LIBS"
    LIBS="$kerberos_lib"
    save_CPPFLAGS="$CPPFLAGS"
    CPPFLAGS="$kerberos_inc $CPPFLAGS"
    AC_CHECK_FUNCS([krb5_cc_new_unique])
    LIBS="$save_LIBS"
    CPPFLAGS="$save_CPPFLAGS"
fi

AC_SUBST(kerberos_inc)
AC_SUBST(kerberos_lib)
AC_SUBST(kerberos_libdir)
