#!/usr/bin/perl -w
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
# do so, delete this exception statement from your version. 
# 
# 
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# --- END COPYRIGHT BLOCK ---

# take a solaris8 patch list and a solaris9 patch list and merge them
# together, removing duplicates
# we are looking for patches that have the same major revision
# number and release OS.  We only want to keep the one with the highest
# minor revision number

# key is the major patch number
# the value is a hash ref which has two keys 'iminor' and 'val'
# the value of key 'iminor' is the minor patch number
# the system keeps track of all revisions (minor number) for each patch (major number)
# we only want to list the highest revision, since on Solaris higher revisions include
# and supersede lower revisions
# the value of key 'val' is the string to print out
%patches = ();
@lines = ();

for $file (@ARGV) {
  open IN, $file or die "Error: could not open $file: $!";
  while (<IN>) {
	if (/^\s*\{(\d+),(\d+),\d,(\d+),/) {
	  $major = $1;
	  $minor = $2;
	  $rel = $3;
	  my $h = { 'val' => $_ };
	  $patches{$major}{$rel}{$minor} = $h;
	  if (! $patches{$major}{$rel}{highestminor}) {
		$patches{$major}{$rel}{highestminor} = $minor;
	  } elsif ($patches{$major}{$rel}{highestminor} <= $minor) { # highest minor rev is lt or eq new minor
		my $oldminor = $patches{$major}{$rel}{highestminor};
		$patches{$major}{$rel}{$oldminor}->{skip} = 1;
		$patches{$major}{$rel}{highestminor} = $minor;
	  } elsif ($patches{$major}{$rel}{highestminor} > $minor) {
		# skip the new one
		$h->{skip} = 1;
	  }
	  push @lines, $h; # put a hash ref into lines
	} else {
	  push @lines, $_; # put the scalar value into lines
	}
  }
  close IN;
}

for (@lines) {
  if (ref($_)) {
	if ($_->{skip}) {
	  chomp $_->{val};
	  print "/* duplicate or superseded ", $_->{val}, " */\n";
	} else {
	  print $_->{val};
	}
  } else {
	print;
  }
}
