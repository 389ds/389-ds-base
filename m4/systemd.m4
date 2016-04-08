# -*- tab-width: 4; -*-
# Configure paths for systemd functionality
# Public domain - William Brown <wibrown@redhat.com> 2016-03-22

AC_CHECKING(for systemd)

## This is a nice simple check: As we don't need to link to systemd, only be able
## to consume some of it's socket api features, we just need to check for user
## intent with --with-systemd

AC_MSG_CHECKING(for --with-systemd)
AC_ARG_WITH(systemd, AS_HELP_STRING([--with-systemd],[Enable Systemd native integration.]),
[
    if test "$withval" = yes
    then
        AC_MSG_RESULT([using systemd native features])
        with_systemd=yes
    else
        AC_MSG_RESULT(no)
    fi
],
AC_MSG_RESULT(no))

if test "$with_systemd" = yes; then
    SYSTEMD_CFLAGS="-DHAVE_SYSTEMD"
else
    SYSTEMD_CFLAGS=""
fi

