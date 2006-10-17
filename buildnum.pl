#!/usr/bin/perl
# --- BEGIN COPYRIGHT BLOCK ---
# This Program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; version 2 of the License.
# 
# This Program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License along with
# this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
# Place, Suite 330, Boston, MA 02111-1307 USA.
# 
# In addition, as a special exception, Red Hat, Inc. gives You the additional
# right to link the code of this Program with code not covered under the GNU
# General Public License ("Non-GPL Code") and to distribute linked combinations
# including the two, subject to the limitations in this paragraph. Non-GPL Code
# permitted under this exception must only link to the code of this Program
# through those well defined interfaces identified in the file named EXCEPTION
# found in the source code files (the "Approved Interfaces"). The files of
# Non-GPL Code may instantiate templates or use macros or inline functions from
# the Approved Interfaces without causing the resulting work to be covered by
# the GNU General Public License. Only Red Hat, Inc. may make changes or
# additions to the list of Approved Interfaces. You must obey the GNU General
# Public License in all respects for all of the Program code and other code used
# in conjunction with the Program except the Non-GPL Code covered by this
# exception. If you modify this file, you may extend this exception to your
# version of the file, but you are not obligated to do so. If you do not wish to
# provide this exception without modification, you must delete this exception
# statement from your version and license this file solely under the GPL without
# exception. 
# 
# 
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
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
