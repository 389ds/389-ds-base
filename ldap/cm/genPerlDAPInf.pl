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
# Copyright (C) 2006 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#

# Usage: genPerlDAPInf.pl <full_path_to_perldap.inf> <version> <Vendor>

$vendor = "Red Hat, Inc.";
if ($#ARGV < 1) {
    print "Usage: genPerlDAPInf.pl <full_path_to_perldap.inf> <version> [<Vendora>]\n";
    exit 1;
}
$outfile = $ARGV[0];
$version = $ARGV[1];
if ($#ARGV >= 2) {
    $vendor = $ARGV[2];
}

print "outfile: $outfile, version: $version, vendor: $vendor\n";

($nodot_version = $version) =~ tr/\.//d;
$component = "perldap" . $nodot_version;

open(OUT, ">$outfile") or die "Error: could not write file $outfile: $!";
print OUT "[General]\n";
print OUT "Name=PerLDAP\n";
print OUT "Description=This is mozilla.org PerLDAP.\n";
print OUT "Components=$component\n\n";
print OUT "[$component]\n";
print OUT "Description=The mozilla.org PerLDAP $version\n";
print OUT "NickName=$component\n";
print OUT "Name=PerLDAP $version\n";
print OUT "SourcePath=perldap\n";
print OUT "Vendor=$vendor\n";
print OUT "Security=none\n";
print OUT "Version=$version\n";
print OUT "Compatible=$version\n";
print OUT "Archive=perldap-$version.zip\n";
print OUT "Visible=FALSE\n";
print OUT "Checked=TRUE\n";
close OUT;

exit 0;
