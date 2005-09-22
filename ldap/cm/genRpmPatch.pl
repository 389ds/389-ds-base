#!/usr/bin/perl
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
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
#

sub usage {
    print(STDERR "Usage : $0 -r <releasedir> -o <objdir> -e <extension> -i <identity> -f <inffile>\n");
    print(STDERR "        -r <releasedir>: built/release dir\n");
    print(STDERR "        -o <objdir>: e.g., RHEL4-domestic-full-normal-pth-slapd\n");
    print(STDERR "        -e <extension>: extension for the patch dir\n");
    print(STDERR "        -i <identity>: fedora or redhat\n");
    print(STDERR "        -f <inffile>: file containing the patch info\n");
    print(STDERR "sample <inffile>\n");
    print(STDERR "  ======================================================\n");
    print(STDERR "  base: /share/dev4/fedora-ds/fds71/ships/20050526.1\n");
    print(STDERR "  file: 147585: plugins/slapd/slapi/examples/testpreop.c\n");
    print(STDERR "  file: 164834,165641: bin/slapd/server/ns-slapd\n");
    print(STDERR "  ======================================================\n");
}

$verbose = 0;
$inffile = "";
$builtdirname = "";
$releasedir = "";
$extension = "";
$identity = "";

$i = 0;
while ($i <= $#ARGV) {
    if ("$ARGV[$i]" eq "-o") {
        $i++;
        $builtdirname = $ARGV[$i];
    } elsif ("$ARGV[$i]" eq "-r") {
        $i++;
        $releasedir = $ARGV[$i];
    } elsif ("$ARGV[$i]" eq "-e") {
        $i++;
        $extension = $ARGV[$i];
    } elsif ("$ARGV[$i]" eq "-i") {
        $i++;
        $identity = $ARGV[$i];
    } elsif ("$ARGV[$i]" eq "-f") {
        $i++;
        $inffile = $ARGV[$i];
    } elsif ("$ARGV[$i]" eq "-v") {
        $verbose = 1;
    }
    $i++;
}

if ("$builtdirname" eq "") {
    print(STDERR "ERROR: builtdirname is not given\n");
    &usage; exit(1);
}
if ("$releasedir" eq "") {
    print(STDERR "ERROR: releasedir is not given\n");
    &usage; exit(1);
}
if ("$extension" eq "") {
    print(STDERR "ERROR: extension is not given\n");
    &usage; exit(1);
}
if ("$identity" eq "" || 
    (("$identity" ne "fedora") && ("$identity" ne "redhat"))) {
    print(STDERR "ERROR: $identity is not fedora or redhat\n");
    &usage; exit(1);
}
if ("$inffile" eq "") {
    print(STDERR "ERROR: inffile is not given\n");
    &usage; exit(1);
}
if (!(-d "$releasedir")) {
    print(STDERR "ERROR: $releasedir does not exist\n");
    exit(1);
}

unless (open (INFFILE, $inffile)) {
    die "Error, cannot open info file $inffile\n";
}

$basedir = 0;
@newfiles = ();
while ($l = <INFFILE>) {
    chop($l);
    $pos = length($l);
    if ($l =~ /^base: /) {
        $pos = rindex($l, ":", $pos);
        $pos++;
        $basedir = substr($l, $pos);
        $basedir =~ s/[     ]//g;
    } elsif ($l =~ /^file: /) {
        $pos = rindex($l, ":", $pos);
        $pos++;
        $file = substr($l, $pos);
        $file =~ s/[     ]//g;
        push(@newfiles, ($file));
    }
}
if (1 == $verbose) {
    print "Base: $basedir\n";
    print "New Files:\n";
    foreach $afile (@newfiles) {
        print "    $afile\n";
    }
}

if ($builtdirname !~ /RHEL/) {
    print(STDERR "ERROR: Not RHEL\n");
    exit(1);
}

# Get info from $builtdirname (e.g., RHEL4-domestic-full-normal-pth-slapd\n")
$rhelversion = "";
$rhelversionl = "";
if ($builtdirname =~ /RHEL3/) {
    $rhelversion = "RHEL3";
    $rhelversionl = "rhel3";
} elsif ($builtdirname =~ /RHEL4/) {
    $rhelversion = "RHEL4";
    $rhelversionl = "rhel4";
} else {
    print(STDERR "ERROR: $builtdirname is not supported\n");
    exit(1);
}

$optordbg = "";
if ($builtdirname =~ /full/) {
    $optordbg = "dbg";
} elsif ($builtdirname =~ /optimize/) {
    $optordbg = "opt";
} else {
    print(STDERR "ERROR: $builtdirname has no opt/debug info\n");
    exit(1);
}

# Get fullpath to the RPM file
$fullrpmfile = "";
$iddir = "";
opendir(BASEDIR, $basedir) or die "ERROR: Could not open $basedir\n";
while ( defined ( $subdir = readdir(BASEDIR))) {
    if ($subdir =~ /$rhelversionl/ || $subdir =~ /$rhelversion/) {
        $fullsubdir = $basedir . "/" . $subdir;
        opendir(SUBDIR, $fullsubdir) or die "ERROR: Could not open $fullsubdir\n";
        while ( defined ( $rpmfile = readdir(SUBDIR))) {
            if (($rpmfile =~ /$rhelversionl/ || $rpmfile =~ /$rhelversion/) &&
                $rpmfile =~ /$optordbg/ && $rpmfile =~ /\.rpm$/) {
                $fullrpmfile = $fullsubdir . "/" . $rpmfile;
                ($org, $ds, $rest) = split('-', $rpmfile, 3);
                $iddir = $org . "-" . $ds;
                if ("$org" ne "$identity") {
                    print "ERROR: rpmfile name $rpmfile does not match the given identity $identity\n";
                    exit(1);
                }
                closedir(SUBDIR);
                last;
            }
        }
        closedir(BASEDIR);
        last;
    }
}
if ("$fullrpmfile" eq "") {
    print(STDERR "ERROR: Cannot file an rpm file under $basedir\n");
    exit(1);
}
if (1 == $verbose) {
    print "RPM File: $fullrpmfile\n";
}

# Expand the RPM file to the $releasedir
$workdir = $releasedir . "/slapd/" . $builtdirname . $extension;
mkdir($workdir, 0700);
chdir($workdir);
if (1 == $verbose) {
    print "Work Dir: $workdir\n";
}
open(RPM2CPIO, "rpm2cpio $fullrpmfile | cpio -id | ") or die "Cannot run program: $!\n";
close(RPM2CPIO);

# Copy new files onto the expanded files
foreach $afile (@newfiles) {
    $srcfile = $releasedir . "/slapd/" . $builtdirname . "/" . $afile;
    $destfile = $workdir . "/opt/" . $iddir . "/" . $afile;
    $destdir = substr($destfile, 0, rindex($destfile, "/", length($destfile)));
    if (!(-d $destdir)) {
        print "WARNING: $destdir does not exist.  Skipping ...\n";
        next;
    }
    if (1 == $verbose) {
        print "Copy: $srcfile => $destdir\n";
    }
    open(COPY, "cp $srcfile $destdir | ") or print "Copy $srcfile to $destdir failed: $!\n";
    close(COPY);
}
