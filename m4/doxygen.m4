## BEGIN COPYRIGHT BLOCK
## Copyright (c) 2016, William Brown <william at blackhats dot net dot au>
## All rights reserved.
##
## License: License: GPL (version 3 or any later version).
## See LICENSE for details.
## END COPYRIGHT BLOCK

AC_CHECK_PROGS([DOXYGEN], [doxygen])
if test -z "$DOXYGEN";
    then AC_MSG_WARN([Doxygen not found - continuing without Doxygen support])
fi

AC_MSG_RESULT([using system Doxygen])

AM_CONDITIONAL([HAVE_DOXYGEN], [test -n "$DOXYGEN"])AM_COND_IF([HAVE_DOXYGEN], [AC_CONFIG_FILES([docs/slapi.doxy])])


