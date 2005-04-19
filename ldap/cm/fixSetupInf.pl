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
#

$isNT = -d '\\';

if ($isNT) {
  $ServerDir = "/Fedora/Servers";
} else {
  $ServerDir = "/opt/fedora/servers";
}
$ServerDirKey = "DefaultInstallDirectory";

$input = shift;
$output = shift;
die "cannot open input file $input" unless open( FILE, $input );
die "cannot open output file $output" unless open( OUT, ">$output" );
$inGeneralSection = 0;
$addServerDir = 1; # add the server dir if it's not already there
while ( <FILE> ) {
	# if the line begins with Components and does not contain
	# slapd already, add ", slapd" to the end 
	# else, just copy the line to output
	if ( /^Components/ ) {
		chomp;
		if (! /slapd/) {
			$_ .= ", slapd";
			$addedSlapd = 1;
		}
		if (! /nsperl/) {
			$_ .= ", nsperl";
			$addedNSperl = 1;
		}
		if (! /perldap/) {
			$_ .= ", perldap";
			$addedPerLDAP = 1;
		}
		$_ .= "\n";
	}
	if ( $inGeneralSection && /^$ServerDirKey/ ) {
		$addServerDir = 0; # already there, don't add it
	}
	if ( $inGeneralSection && /^\[/ ) {
		if ( $addServerDir ) {
			$_ = "$ServerDirKey = $ServerDir\n$_";
			$addServerDir = 0;
		}
		$inGeneralSection = 0;
	}
	if ( /^\[General\]/ ) {
		$inGeneralSection = 1;
	}
	print OUT $_;
}

close ( FILE );

# now, print the slapd section information
if ($addedSlapd) {
	print OUT "\n[slapd]\n";
	print OUT "ComponentInfoFile = slapd/slapd.inf\n";
}

if ($addedNSperl) {
	print OUT "\n[nsperl]\n";
	print OUT "ComponentInfoFile = nsperl/nsperl.inf\n";
}

if ($addedPerLDAP) {
	print OUT "\n[perldap]\n";
	print OUT "ComponentInfoFile = perldap/perldap.inf\n";
}

close ( OUT ); 

exit(0);
