#!/usr/local/bin/perl
#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
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
