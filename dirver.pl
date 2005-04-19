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

#-----------------------------------------------------------------
# dirver.pl: Generates ascii format #define for FILEVERSION
#   resource identifier used by Windows executable binaries.
#
#   Usage: dirver.pl -v <major.minor.patch> [-d mm/dd/yy] [-o outfile]
#   Example: dirver.pl -v 6.5.2 -d 1/19/2005 -o fileversion.h
#
#   -v <major.minor.patch>    Version number.
#   -d <mm/dd/yy>             Date. (optional)
#   -o <outfile>              Output header file. (optional)
#   -H                        Print this help message
#-----------------------------------------------------------------

use Getopt::Std;
use FileHandle;
                                                                                             
autoflush STDERR 1;
                                                                                             
getopts('v:d:o:H');
                                                                                             
if ($opt_H) {exitHelp();}

# Load arguments
$version = $opt_v || exitHelp();
$date = $opt_d;
$outfile = $opt_o;

# Separate version into components
my @verComponents = split(/\./, $version);

# Set version components to 0 if not defined
if ($verComponents[1] == undef) { $verComponents[1] = "0"; }
if ($verComponents[2] == undef) { $verComponents[2] = "0"; }

# Calculate build version and build date
my $buildVersion = calcVersion(@verComponents);
my $buildDate = calcBuildDate($date);

# Write #defines out to stdout or a file is requested
if ($outfile) {
    open(OUTFILE,">$outfile") || die "Error: Can't create $outfile: $!";
    $outhdl = OUTFILE;
} else {
    $outhdl = STDOUT;
}

print $outhdl "#define VI_PRODUCTVERSION $verComponents[0].$verComponents[1]\n";
print $outhdl "#define PRODUCTTEXT \"$version\"\n";
print $outhdl "#define VI_FILEVERSION $buildVersion, 0, 0, $buildDate\n";
print $outhdl "#define VI_FileVersion \"$version Build $buildDate\\0\"\n";

# Close file if not using STDOUT
if ($outfile) {
    close(OUTFILE);
}

#---------- calcVersion subroutine ----------
sub calcVersion {
    my @ver = shift;
    my $nVersion = 0;

    $nVersion = $ver[0];
    $nVersion <<= 5;
    $nVersion += $ver[1];
    $nVersion <<= 7;
    $nVersion += $ver[2];
    $nVersion &= 0xFFFF;

    return $nVersion;
}

#---------- calcBuildDate subroutine ----------
sub calcBuildDate {
    my $date = shift;
    my @dateComponents = ();
    my $month, $date, $year;
    my $buildDate = "";

    # Use date if passed in, otherwise use system date
    if ($date) {
        # Separate date into month, day, and year
        @dateComponents = split(/\//, $date);

        # Use struct tm range for month
        $dateComponents[0]--;

        # Handle 2 digit years like (20)00
        if ($dateComponents[2] < 70) {
            $dateComponents[2] += 20;
        }

        $month = $dateComponents[0];
        $day = $dateComponents[1];
        $year = $dateComponents[2];
    } else {
        $month = (localtime)[4];
        $day = (localtime)[3];
        $year = (localtime)[5] - 80;
    }

    $buildDate = $year;
    $buildDate <<= 4;
    $buildDate += $month;
    $buildDate <<= 5;
    $buildDate += $day;
    $buildDate &= 0xFFFF;

    return $buildDate;
}

#---------- exitHelp subroutine ----------
sub exitHelp {
    print(STDERR "$0: Generates ascii format #define for FILEVERSION
    \tresource identifier used by Windows executable binaries.

    \tUsage: $0 -v <major.minor.patch> [-d mm/dd/yy] [-o outfile]
    \tExample: $0 -v 6.5.2 -d 1/19/2005 -o fileversion.h

    \t-v <major.minor.patch>    Version number.
    \t-d <mm/dd/yy>             Date. \(optional\)
    \t-o <outfile>              Output header file. \(optional\)
    \t-H                        Print this help message\n");
    exit(0);
}
