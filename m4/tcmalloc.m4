# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details. 
# END COPYRIGHT BLOCK

AC_CHECKING(for tcmalloc)

# check for --with-tcmalloc
AC_MSG_CHECKING(for --with-tcmalloc)
AC_ARG_WITH(tcmalloc, AS_HELP_STRING([--with-tcmalloc],[Use TCMalloc memory allocator.]),
[
    if test "$withval" = yes
    then
        AC_MSG_RESULT([using tcmalloc memory allocator])
        with_tcmalloc=yes
    else
        AC_MSG_RESULT(no)
    fi
],
AC_MSG_RESULT(no))

if test "$with_tcmalloc" = yes; then
    tcmalloc_link=-ltcmalloc
fi


