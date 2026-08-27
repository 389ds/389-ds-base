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

    # Detect systemd directories from pkg-config.
    # Each --with-* flag is an optional override.

    # systemd unit dir
    default_systemdsystemunitdir=`$PKG_CONFIG --variable=systemdsystemunitdir systemd 2>/dev/null`
    if test -z "$default_systemdsystemunitdir" ; then
       default_systemdsystemunitdir='$(prefixdir)/lib/systemd/system'
    fi
    AC_MSG_CHECKING(for --with-systemdsystemunitdir)
    AC_ARG_WITH([systemdsystemunitdir],
       AS_HELP_STRING([--with-systemdsystemunitdir=PATH],
                      [Directory for systemd service files (default: auto-detected from pkg-config)])
    )
    if test "$with_systemdsystemunitdir" = yes ; then
      with_systemdsystemunitdir=$default_systemdsystemunitdir
    elif test "$with_systemdsystemunitdir" = no ; then
      with_systemdsystemunitdir=
    else
      if test -n "$with_systemdsystemunitdir" ; then
        : # user-provided value, keep it
      else
        with_systemdsystemunitdir=$default_systemdsystemunitdir
      fi
    fi
    AC_MSG_RESULT([$with_systemdsystemunitdir])
    AC_SUBST(with_systemdsystemunitdir)

    # systemd system conf dir
    default_systemdsystemconfdir=`$PKG_CONFIG --variable=systemdsystemconfdir systemd 2>/dev/null`
    if test -z "$default_systemdsystemconfdir" ; then
       default_systemdsystemconfdir='$(sysconfdir)/systemd/system'
    fi
    AC_MSG_CHECKING(for --with-systemdsystemconfdir)
    AC_ARG_WITH([systemdsystemconfdir],
       AS_HELP_STRING([--with-systemdsystemconfdir=PATH],
                      [Directory for systemd system configuration (default: auto-detected from pkg-config)])
    )
    if test "$with_systemdsystemconfdir" = yes ; then
      with_systemdsystemconfdir=$default_systemdsystemconfdir
    elif test "$with_systemdsystemconfdir" = no ; then
      with_systemdsystemconfdir=
    else
      if test -n "$with_systemdsystemconfdir" ; then
        : # user-provided value, keep it
      else
        with_systemdsystemconfdir=$default_systemdsystemconfdir
      fi
    fi
    AC_MSG_RESULT([$with_systemdsystemconfdir])
    AC_SUBST(with_systemdsystemconfdir)

    # systemd group name
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

    # tmpfiles.d dir
    tmpfiles_d=`$PKG_CONFIG --variable=tmpfilesdir systemd 2>/dev/null`
    if test -z "$tmpfiles_d" ; then
       tmpfiles_d='$(prefixdir)/lib/tmpfiles.d'
    fi
    AC_MSG_CHECKING(for --with-tmpfiles-d)
    AC_ARG_WITH(tmpfiles-d,
       AS_HELP_STRING([--with-tmpfiles-d=PATH],
                      [Directory for systemd tmpfiles.d config files (default: auto-detected from pkg-config)])
    )
    if test "$with_tmpfiles_d" = yes ; then
      : # keep auto-detected default
    elif test "$with_tmpfiles_d" = no ; then
      tmpfiles_d=
    else
      if test -n "$with_tmpfiles_d" ; then
        tmpfiles_d=$with_tmpfiles_d
      fi
    fi
    AC_MSG_RESULT([$tmpfiles_d])

    # sysusers.d dir
    sysusers_d=`$PKG_CONFIG --variable=sysusersdir systemd 2>/dev/null`
    if test -z "$sysusers_d" ; then
       sysusers_d='$(prefixdir)/lib/sysusers.d'
    fi
    AC_MSG_CHECKING(for --with-sysusers-d)
    AC_ARG_WITH(sysusers-d,
       AS_HELP_STRING([--with-sysusers-d=PATH],
                      [Directory for systemd sysusers.d config files (default: auto-detected from pkg-config)])
    )
    if test "$with_sysusers_d" = yes ; then
      : # keep auto-detected default
    elif test "$with_sysusers_d" = no ; then
      sysusers_d=
    else
      if test -n "$with_sysusers_d" ; then
        sysusers_d=$with_sysusers_d
      fi
    fi
    AC_MSG_RESULT([$sysusers_d])

    # sysctl.d dir
    sysctl_d=`$PKG_CONFIG --variable=sysctldir systemd 2>/dev/null`
    if test -z "$sysctl_d" ; then
       sysctl_d='$(prefixdir)/lib/sysctl.d'
    fi
    AC_MSG_CHECKING(for --with-sysctl-d)
    AC_ARG_WITH(sysctl-d,
       AS_HELP_STRING([--with-sysctl-d=PATH],
                      [Directory for sysctl.d config files (default: auto-detected from pkg-config)])
    )
    if test "$with_sysctl_d" = yes ; then
      : # keep auto-detected default
    elif test "$with_sysctl_d" = no ; then
      sysctl_d=
    else
      if test -n "$with_sysctl_d" ; then
        sysctl_d=$with_sysctl_d
      fi
    fi
    AC_MSG_RESULT([$sysctl_d])

fi
# End of with_systemd

AM_CONDITIONAL([SYSTEMD],[test -n "$with_systemd"])
AM_CONDITIONAL([with_systemd],[test -n "$with_systemd"])
AM_CONDITIONAL([JOURNALD],[test -n "$with_journald"])
AM_CONDITIONAL([with_systemd_journald],[test -n "$with_journald"])
AM_CONDITIONAL([INSTALL_TMPFILES],[test -n "$tmpfiles_d"])
AM_CONDITIONAL([INSTALL_SYSUSERS],[test -n "$sysusers_d"])
AM_CONDITIONAL([INSTALL_SYSCTL],[test -n "$sysctl_d"])

AC_SUBST(systemd_defs)
AC_SUBST(tmpfiles_d)
AC_SUBST(sysusers_d)
AC_SUBST(sysctl_d)

