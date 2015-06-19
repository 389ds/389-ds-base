#!/usr/bin/perl
# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details. 
# --- END COPYRIGHT BLOCK ---

#--------------------------------------------
# buildnum.pl
#
# Generates a dated build number and writes
# out a buildnum.dat file in a user specified
# subdirectory.
#
# Usage: buildnum.pl -p <platform dir>
#--------------------------------------------

use Getopt::Std;
use FileHandle;
                                                                                             
autoflush STDERR 1;
                                                                                             
getopts('p:H');
                                                                                             
if ($opt_H) {exitHelp();}

# Load arguments
$platdir = $opt_p;

# Get current time
@now = gmtime;

# Format buildnum as YYYY.DDD.HHMM
$year = $now[5] + 1900;
$doy = $now[7] + 1;
if ($doy < 100) { $doy = 0 . $doy; }
$tod = $now[2] . $now[1];
$buildnum = "$year.$doy.$tod";

if ($platdir) {
    # Write buildnum.dat
    $buildnum_file = "./$platdir/buildnum.dat";
    open(BUILDNUM,">$buildnum_file") || die "Error: Can't create $buildnum_file: $!\n";
    print BUILDNUM "\\\"$buildnum\\\"";
    close(BUILDNUM);
} else {
    print "\\\"$buildnum\\\"";
}

#---------- exitHelp subroutine ----------
sub exitHelp {
    print(STDERR "$0: Generates a dated build number.

    \tUsage: $0 -p <platform>

    \t-p <platform>             Platform subdirectory.
    \t-H                        Print this help message\n");
    exit(0);
}
