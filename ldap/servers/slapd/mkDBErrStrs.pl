#!/usr/local/bin/perl
#
# BEGIN COPYRIGHT BLOCK
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
# END COPYRIGHT BLOCK
#
#
# Perl script to generate dberrstrs.h, which is used in errormap.c.
#

sub numerically { $a <=> $b; }

$dbdir = "";
$isNT = 0;
$i = 0;
$outdir = "";

while ($i <= $#ARGV) {
	if ("$ARGV[$i]" eq "-nt" || "$ARGV[$i]" eq "-NT") {			# NT
		$isNT = 1;
	} elsif ("$ARGV[$i]" eq "-o" || "$ARGV[$i]" eq "-O") {		# output dir
		$i++;
		$outdir = $ARGV[$i];
	} elsif ("$ARGV[$i]" eq "-i" || "$ARGV[$i]" eq "-I") {		# input db dir
		$i++;
		$dbdir = $ARGV[$i];
	}
	$i++;
}

if ($dbdir eq "") {
	print(STDERR "Usage: $0 [-nt] <db_dir_path>\n");
	exit(1);
}

if ($isNT == 1) {
	$dirsep = "\\";
} else {
	$dirsep = "/";
}

$dbh = sprintf("%s%sdb.h", $dbdir, $dirsep);

open(FOO, $dbh) || die "Cannot open $dbh\n";
@lines = <FOO>;
close(FOO);

$i = 0;
$j = 0;
while ($i < $#lines) {
	chop($lines[$i]);
	if ($lines[$i] =~ /^#define[ 	][_A-Z]*[ 	]*\(-[0-9]*/) {
		($h, $t) = split(/\(/, $lines[$i], 2);
		($num[$j], $tt) = split(/\)/, $t, 2);
		($h, $ttt) = split(/\/\* /, $tt, 2);
		($errstr, $tttt) = split(/ \*\//, $ttt, 2);
		if ($errstr ne "") {
			$errstr =~ s/\"/\\\"/g; # Escape quotes
			$numStrPair{$num[$j]} = $errstr;
		}
		$j++;
	}
	$i++;
}

sort numerically num;

if ($outdir eq "") {
	$myheader = "dberrstrs.h";
} else {
	$myheader = sprintf("%s%sdberrstrs.h", $outdir, $dirsep);
}

open(FOO, "> $myheader") || die "Cannot open $myheader\n";
print( FOO "/* DO NOT EDIT: This is an automatically generated file by $0 */\n" );
$i = 0;
while ($i < $j) {
	print( FOO "{$num[$i],\t\"$numStrPair{$num[$i]}\"},\n" );
	$i++;
}
close(FOO);
