# -*- tab-width: 4; -*-
# Configure paths for SVRCORE
# Public domain - Nathan Kinder <nkinder@redhat.com> 2006-06-26

AC_MSG_CHECKING(for --with-svrcore)
AC_ARG_WITH(svrcore,
    [[  --with-svrcore[=PATH]   Use system installed svrcore - optional path for svrcore]],
    dnl = Look in the standard system locations
    [
      if test "$withval" = "yes"; then
        AC_MSG_RESULT(yes)

        dnl = Check for svrcore.h in the normal locations
        if test -f /usr/include/svrcore.h; then
          svrcore_inc="-I/usr/include"
        else
          AC_MSG_ERROR(svrcore.h not found)
        fi

      dnl = Check the user provided location
      elif test -d "$withval" -a -d "$withval/lib" -a -d "$withval/include" ; then
        AC_MSG_RESULT([using $withval])

        if test -f "$withval/include/svrcore.h"; then
          svrcore_inc="-I$withval/include"
        else
          AC_MSG_ERROR(svrcore.h not found)
        fi

        svrcore_lib="-L$withval/lib"
      else
        AC_MSG_RESULT(yes)
        AC_MSG_ERROR([svrcore not found in $withval])
      fi
    ],
    AC_MSG_RESULT(no))

AC_MSG_CHECKING(for --with-svrcore-inc)
AC_ARG_WITH(svrcore-inc,
    [[  --with-svrcore-inc=PATH   SVRCORE include file directory]],
    [
      if test -f "$withval"/svrcore.h; then
        AC_MSG_RESULT([using $withval])
        svrcore_inc="-I$withval"
      else
        echo
        AC_MSG_ERROR([$withval/svrcore.h not found])
      fi
    ],
    AC_MSG_RESULT(no))

AC_MSG_CHECKING(for --with-svrcore-lib)
AC_ARG_WITH(svrcore-lib,
    [[  --with-svrcore-lib=PATH   SVRCORE library directory]],
    [
      if test -d "$withval"; then
        AC_MSG_RESULT([using $withval])
        svrcore_lib="-L$withval"
      else
        echo
        AC_MSG_ERROR([$withval not found])
      fi
    ],
    AC_MSG_RESULT(no))

dnl svrcore not given - look for pkg-config
if test -z "$svrcore_inc" -o -z "$svrcore_lib"; then
  AC_MSG_CHECKING(for svrcore with pkg-config)
  AC_PATH_PROG(PKG_CONFIG, pkg-config)
  if test -n "$PKG_CONFIG"; then
    if $PKG_CONFIG --exists svrcore-devel; then
      svrcore_inc=`$PKG_CONFIG --cflags-only-I svrcore-devel`
      svrcore_lib=`$PKG_CONFIG --libs-only-L svrcore-devel`
      AC_MSG_RESULT([using system svrcore])
    fi
  fi
fi

if test -z "$svrcore_inc" -o -z "$svrcore_lib"; then
dnl just see if svrcore is already a system library
  AC_CHECK_LIB([svrcore], [SVRCORE_GetRegisteredPinObj], [havesvrcore=1],
	       [], [$nss_inc $nspr_inc $nss_lib -lnss3 -lsoftokn3 $nspr_lib -lplds4 -lplc4 -lnspr4])
  if test -n "$havesvrcore" ; then
dnl just see if svrcore is already a system header file
    save_cppflags="$CPPFLAGS"
    CPPFLAGS="$nss_inc $nspr_inc"
    AC_CHECK_HEADER([svrcore.h], [havesvrcore=1], [havesvrcore=])
    CPPFLAGS="$save_cppflags"
  fi
dnl for svrcore to be present, both the library and the header must exist
  if test -z "$havesvrcore" ; then
    AC_MSG_ERROR([svrcore not found, specify with --with-svrcore.])
  fi
fi
