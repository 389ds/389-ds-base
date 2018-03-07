#!/bin/sh

# set required versions of tools here
# the version is dotted integers like X.Y.Z where
# X, Y, and Z are integers
# comparisons are done using shell -lt, -gt, etc.
# this works if the numbers are zero filled as well
# so 06 == 6

# autoconf version required
# need 2.69 or later
ac_need_maj=2
ac_need_min=69
# automake version required
# need 1.13.4 or later
am_need_maj=1
am_need_min=13
am_need_rev=4
# libtool version required
# need 2.4.2 or later
lt_need_maj=2
lt_need_min=4
lt_need_rev=2
# should never have to touch anything below this line unless there is a bug
###########################################################################

# input
#  arg1 - version string in the form "X.Y[.Z]" - the .Z is optional
#  args remaining - the needed X, Y, and Z to match
# output
#  return 0 - success - the version string is >= the required X.Y.Z
#  return 1 - failure - the version string is < the required X.Y.Z
# NOTE: All input must be integers, otherwise you will see shell errors
checkvers() {
    vers="$1"; shift
    needmaj="$1"; shift
    needmin="$1"; shift
    needrev="$1"; shift
    verslist=`echo $vers | tr '.' ' '`
    set $verslist
    maj=$1; shift
    min=$1; shift
    rev=$1; shift
    if [ "$maj" -gt "$needmaj" ] ; then return 0; fi
    if [ "$maj" -lt "$needmaj" ] ; then return 1; fi
    # if we got here, maj == needmaj
    if [ -z "$needmin" ] ; then return 0; fi
    if [ "$min" -gt "$needmin" ] ; then return 0; fi
    if [ "$min" -lt "$needmin" ] ; then return 1; fi
    # if we got here, min == needmin
    if [ -z "$needrev" ] ; then return 0; fi
    if [ "$rev" -gt "$needrev" ] ; then return 0; fi
    if [ "$rev" -lt "$needrev" ] ; then return 1; fi
    # if we got here, rev == needrev
    return 0
}

# Check autoconf version
AC_VERSION=`autoconf --version | sed '/^autoconf/ {s/^.* \([1-9][0-9.]*\)$/\1/; q}'`
if checkvers "$AC_VERSION" $ac_need_maj $ac_need_min ; then
    echo Found valid autoconf version $AC_VERSION
else
    echo "You must have autoconf version $ac_need_maj.$ac_need_min or later installed (found version $AC_VERSION)."
    exit 1
fi

# Check automake version
AM_VERSION=`automake --version | sed '/^automake/ {s/^.* \([1-9][0-9.]*\)$/\1/; q}'`
if checkvers "$AM_VERSION" $am_need_maj $am_need_min $am_need_rev ; then
    echo Found valid automake version $AM_VERSION
else
    echo "You must have automake version $am_need_maj.$am_need_min.$am_need_rev or later installed (found version $AM_VERSION)."
    exit 1
fi

# Check libtool version
# NOTE: some libtool versions report a letter at the end e.g. on RHEL6
# the version is 2.2.6b - for comparison purposes, just strip off the
# letter - note that the shell -lt and -gt comparisons will fail with
# test: 6b: integer expression expected if the number to compare
# contains a non-digit
LT_VERSION=`libtool --version | sed '/GNU libtool/ {s/^.* \([1-9][0-9a-zA-Z.]*\)$/\1/; s/[a-zA-Z]//g; q}'`
if checkvers "$LT_VERSION" $lt_need_maj $lt_need_min $lt_need_rev ; then
    echo Found valid libtool version $LT_VERSION
else
    echo "You must have libtool version $lt_need_maj.$lt_need_min.$lt_need_rev or later installed (found version $LT_VERSION)."
    exit 1
fi

# Run autoreconf
echo "Running autoreconf -fvi"
autoreconf -fvi
