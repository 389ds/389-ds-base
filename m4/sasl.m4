# -*- tab-width: 4; -*-
# Configure paths for SASL
# Public domain - Nathan Kinder <nkinder@redhat.com> 2006-06-26
# Based upon svrcore.m4 (also PD) by Rich Megginson <richm@stanfordalumni.org>

dnl ========================================================
dnl = sasl is used to support various authentication mechanisms
dnl = such as DIGEST-MD5 and GSSAPI.
dnl ========================================================
dnl ========================================================
dnl = Use the sasl libraries on the system (assuming it exists)
dnl ========================================================
AC_CHECKING(for sasl)

AC_MSG_CHECKING(for --with-sasl)
AC_ARG_WITH(sasl,
    [[  --with-sasl=PATH   Use sasl from supplied path]],
    dnl = Look in the standard system locations
    [
      if test "$withval" = "yes"; then
        AC_MSG_RESULT(yes)

        dnl = Check for sasl.h in the normal locations
        if test -f /usr/include/sasl/sasl.h; then
          sasl_inc="-I/usr/include/sasl"
        elif test -f /usr/include/sasl.h; then
          sasl_inc="-I/usr/include"
        else
          AC_MSG_ERROR(sasl.h not found)
        fi

      dnl = Check the user provided location
      elif test -d "$withval" -a -d "$withval/lib" -a -d "$withval/include" ; then
        AC_MSG_RESULT([using $withval])

        if test -f "$withval/include/sasl/sasl.h"; then
          sasl_inc="-I$withval/include/sasl"
        elif test -f "$withval/include/sasl.h"; then
          sasl_inc="-I$withval/include"
        else
          AC_MSG_ERROR(sasl.h not found)
        fi

        sasl_lib="-L$withval/lib"
        sasl_libdir="$withval/lib"
      else
          AC_MSG_RESULT(yes)
          AC_MSG_ERROR([sasl not found in $withval])
      fi
    ],
    AC_MSG_RESULT(no))

AC_MSG_CHECKING(for --with-sasl-inc)
AC_ARG_WITH(sasl-inc,
    [[  --with-sasl-inc=PATH   SASL include file directory]],
    [
      if test -f "$withval"/sasl.h; then
        AC_MSG_RESULT([using $withval])
        sasl_inc="-I$withval"
      else
        echo
        AC_MSG_ERROR([$withval/sasl.h not found])
      fi
    ],
    AC_MSG_RESULT(no))

AC_MSG_CHECKING(for --with-sasl-lib)
AC_ARG_WITH(sasl-lib,
    [[  --with-sasl-lib=PATH   SASL library directory]],
    [
      if test -d "$withval"; then
        AC_MSG_RESULT([using $withval])
        sasl_lib="-L$withval"
        sasl_libdir="$withval"
      else
        echo
        AC_MSG_ERROR([$withval not found])
      fi
    ],
    AC_MSG_RESULT(no))

if test -z "$sasl_inc"; then
  AC_MSG_CHECKING(for sasl.h)
  dnl - Check for sasl in standard system locations
  if test -f /usr/include/sasl/sasl.h; then
    AC_MSG_RESULT([using /usr/include/sasl/sasl.h])
    sasl_inc="-I/usr/include/sasl"
  elif test -f /usr/include/sasl.h; then
    AC_MSG_RESULT([using /usr/include/sasl.h])
    sasl_inc="-I/usr/include"
  else
    AC_MSG_RESULT(no)
    AC_MSG_ERROR([sasl not found, specify with --with-sasl.])
  fi
fi
