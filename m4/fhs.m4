# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details. 
# END COPYRIGHT BLOCK

AC_MSG_CHECKING(for FHS)

# check for --with-fhs
AC_MSG_CHECKING(for --with-fhs)
AC_ARG_WITH(fhs, AS_HELP_STRING([--with-fhs],[Use FHS layout]),
[
  with_fhs=yes
  AC_MSG_RESULT(yes)
],
AC_MSG_RESULT(no))

if test "$with_fhs" = "yes"; then
  AC_DEFINE([IS_FHS], [1], [Use FHS layout])
fi

# check for --with-fhs-opt
AC_MSG_CHECKING(for --with-fhs-opt)
AC_ARG_WITH(fhs-opt, AS_HELP_STRING([--with-fhs-opt],[Use FHS optional layout]),
[
  with_fhs_opt=yes
  AC_MSG_RESULT(yes)
  AC_SUBST(with_fhs_opt)
],
AC_MSG_RESULT(no))

if test "$with_fhs_opt" = "yes"; then
  AC_DEFINE([IS_FHS_OPT], [1], [Use FHS optional layout])
fi

if test "$with_fhs" = "yes" -a "$with_fhs_opt" = "yes"; then
  AC_MSG_ERROR([Can't set both --with-fhs and --with-fhs-opt.  Please only use one of these options.])
fi
