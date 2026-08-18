# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details. 
# END COPYRIGHT BLOCK

AC_MSG_CHECKING(for Systemd)

# check for --with-systemd
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

    AC_MSG_CHECKING(for --with-journald)
    AC_ARG_WITH(journald, AS_HELP_STRING([--with-journald],[Enable Journald native integration. WARNING, this may cause system instability]),
    [
        if test "$withval" = yes
        then
            AC_MSG_RESULT([using journald logging: WARNING, this may cause system instability])
            with_systemd=yes
        else
            AC_MSG_RESULT(no)
        fi
    ],
    AC_MSG_RESULT(no))

    PKG_CHECK_MODULES([SYSTEMD], [libsystemd])

    if test "$with_journald" = yes; then
        systemd_defs="-DWITH_SYSTEMD -DHAVE_JOURNALD"
    else
        systemd_defs="-DWITH_SYSTEMD"
    fi

    # Check for the pkg config provided unit paths
    if test -n "$PKG_CONFIG" ; then
       default_systemdsystemunitdir=`$PKG_CONFIG --variable=systemdsystemunitdir systemd`
       default_systemdsystemconfdir=`$PKG_CONFIG --variable=systemdsystemconfdir systemd`
    fi

    AC_MSG_CHECKING(for --with-systemdsystemunitdir)
    AC_ARG_WITH([systemdsystemunitdir],
       AS_HELP_STRING([--with-systemdsystemunitdir=PATH],
                      [Directory for systemd service files (default: $with_systemdsystemunitdir)])
    )
    if test "$with_systemdsystemunitdir" = yes ; then
      if test -n "$default_systemdsystemunitdir" ; then
        with_systemdsystemunitdir=$default_systemdsystemunitdir
        AC_MSG_RESULT([$with_systemdsystemunitdir])
      else
        AC_MSG_ERROR([You must specify --with-systemdsystemconfdir=/full/path/to/systemd/system directory])
      fi
    elif test "$with_systemdsystemunitdir" = no ; then
      with_systemdsystemunitdir=
    else
      AC_MSG_RESULT([$with_systemdsystemunitdir])
    fi
    AC_SUBST(with_systemdsystemunitdir)

    AC_MSG_CHECKING(for --with-systemdsystemconfdir)
    AC_ARG_WITH([systemdsystemconfdir],
       AS_HELP_STRING([--with-systemdsystemconfdir=PATH],
                      [Directory for systemd service files (default: $with_systemdsystemconfdir)])
    )
    if test "$with_systemdsystemconfdir" = yes ; then
      if test -n "$default_systemdsystemconfdir" ; then
        with_systemdsystemconfdir=$default_systemdsystemconfdir
        AC_MSG_RESULT([$with_systemdsystemconfdir])
      else
        AC_MSG_ERROR([You must specify --with-systemdsystemconfdir=/full/path/to/systemd/system directory])
      fi
    elif test "$with_systemdsystemconfdir" = no ; then
      with_systemdsystemconfdir=
    else
      AC_MSG_RESULT([$with_systemdsystemconfdir])
    fi
    AC_SUBST(with_systemdsystemconfdir)

    if test -z "$with_systemdgroupname" ; then
       with_systemdgroupname=$PACKAGE_NAME.target
    fi
    AC_MSG_CHECKING(for --with-systemdgroupname)
    AC_ARG_WITH([systemdgroupname],
         AS_HELP_STRING([--with-systemdgroupname=NAME],
                        [Name of group target for all instances (default: $with_systemdgroupname)])
    )
    if test "$with_systemdgroupname" = yes ; then
       AC_MSG_ERROR([You must specify --with-systemdgroupname=name.of.group])
    elif test "$with_systemdgroupname" = no ; then
       AC_MSG_ERROR([You must specify --with-systemdgroupname=name.of.group])
    else
       AC_MSG_RESULT([$with_systemdgroupname])
    fi
    AC_SUBST(with_systemdgroupname)

    if test -z "$with_tmpfiles_d" ; then
       if test -d $sysconfdir/tmpfiles.d ; then
          tmpfiles_d='$(sysconfdir)/tmpfiles.d'
       fi
    fi
    AC_MSG_CHECKING(for --with-tmpfiles-d)
    AC_ARG_WITH(tmpfiles-d,
       AS_HELP_STRING([--with-tmpfiles-d=PATH],
                      [system uses tmpfiles.d to handle temp files/dirs (default: $tmpfiles_d)])
    )
    if test "$with_tmpfiles_d" = yes ; then
      AC_MSG_ERROR([You must specify --with-tmpfiles-d=/full/path/to/tmpfiles.d directory])
    elif test "$with_tmpfiles_d" = no ; then
      tmpfiles_d=
    else
      tmpfiles_d=$with_tmpfiles_d
      AC_MSG_RESULT([$tmpfiles_d])
    fi

fi
# End of with_systemd

AM_CONDITIONAL([SYSTEMD],[test -n "$with_systemd"])
AM_CONDITIONAL([with_systemd],[test -n "$with_systemd"])
AM_CONDITIONAL([JOURNALD],[test -n "$with_journald"])
AM_CONDITIONAL([with_systemd_journald],[test -n "$with_journald"])

AC_SUBST(systemd_defs)
AC_SUBST(tmpfiles_d)

