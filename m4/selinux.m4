# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details. 
# END COPYRIGHT BLOCK

AC_MSG_CHECKING(for SELinux)

# check for --with-selinux
AC_MSG_CHECKING(for --with-selinux)
AC_ARG_WITH(selinux, AS_HELP_STRING([--with-selinux],[Support SELinux features]),
[
  if test "$withval" = "no"; then
    AC_MSG_RESULT(no)
  else
    with_selinux=yes
    AC_MSG_RESULT(yes)
    AC_SUBST(with_selinux)
  fi
],
AC_MSG_RESULT(no))

AM_CONDITIONAL(SELINUX,test "$with_selinux" = "yes")

