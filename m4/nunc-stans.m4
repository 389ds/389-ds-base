# BEGIN COPYRIGHT BLOCK
# License: GPL (version 3 or any later version).
# See LICENSE for details. 
# END COPYRIGHT BLOCK

AC_CHECKING(for nunc-stans)

dnl  - check for --with-nunc-stans
AC_MSG_CHECKING(for --with-nunc-stans)
AC_ARG_WITH(nunc-stans, AS_HELP_STRING([--with-nunc-stans@<:@=PATH@:>@],[nunc-stans directory]),
[
  if test "$withval" = "yes"; then
    AC_MSG_RESULT(yes)
  elif test "$withval" = "no"; then
    AC_MSG_RESULT(no)
  elif test -d "$withval"; then
    AC_MSG_RESULT([using $withval])
    dnl - check the user provided location
    nunc_stans_lib="-L$withval/lib"
    nunc_stans_libdir="$withval/lib"
    nunc_stans_incdir="$withval/include"
    if ! test -e "$nunc_stans_incdir/nunc-stans/nunc-stans.h" ; then
      AC_MSG_ERROR([$withval include dir not found])
    fi
    nunc_stans_inc="-I$nunc_stans_incdir"
  else
    echo
    AC_MSG_ERROR([$withval not found])
  fi
],
AC_MSG_RESULT(no))

# check for --with-nunc-stans-inc
AC_MSG_CHECKING(for --with-nunc-stans-inc)
AC_ARG_WITH(nunc-stans-inc, AS_HELP_STRING([--with-nunc-stans-inc=PATH],[nunc-stans include file directory]),
[
  if test -e "$withval"/nunc-stans/nunc-stans.h
  then
    AC_MSG_RESULT([using $withval])
    nunc_stans_incdir="$withval"
    nunc_stans_inc="-I$withval"
  else
    echo
    AC_MSG_ERROR([$withval not found])
  fi
],
AC_MSG_RESULT(no))

# check for --with-nunc-stans-lib
AC_MSG_CHECKING(for --with-nunc-stans-lib)
AC_ARG_WITH(nunc-stans-lib, AS_HELP_STRING([--with-nunc-stans-lib=PATH],[nunc-stans library directory]),
[
  if test -d "$withval"
  then
    AC_MSG_RESULT([using $withval])
    nunc_stans_lib="-L$withval"
    nunc_stans_libdir="$withval"
  else
    echo
    AC_MSG_ERROR([$withval not found])
  fi
],
AC_MSG_RESULT(no))
