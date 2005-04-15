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
# do so, delete this exception statement from your version. 
# 
# 
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#
use File::Find;

#
# usage: unixstrip.pl [ directory ... ] [ shlibsign ]
#
# if no arguments are passed, strip files under the current directory.
#
# if 1 argument is passed, strip files under the given directory.
#
# if 2 or more arguments are passed,
#   the last argument is considered the path to nss utility shlibsign (NSS3.9~),
#   the preceding args are directories, under which files are to be stripped.
#   And nss libraries libsoftokn3, libfreebl_pure32_3, libfreebl_hybrid_3
#   are to be checksum'ed with shlibsign.

my $SHLIBSIGN = "";
if (@ARGV > 1) {
    $SHLIBSIGN = $ARGV[$#ARGV];
    print STDERR "set $SHLIBSIGN \n";
    for (my $i = 0; $i < $#ARGV; $i++)
    {
        print STDERR "args[$i]: $ARGV[$i]\n";
        find(\&find_cb, $ARGV[$i]);
    }
} elsif (@ARGV == 1) {
    find(\&find_cb, @ARGV);
} else {
    find(\&find_cb, '.');
}

sub find_cb {
    return if (! -f $_);  # only look at plain files
    return if (! -B $_);  # skip text files
    return if (/\.jpg$/); # skip jpg files
    return if (/\.gif$/); # skip gif files
    return if (/\.jar$/); # skip jar files
    return if (/\.zip$/); # skip zip files
    return if (/\.gz$/);  # skip gzip files
    return if (/\.chk$/); # skip chk files
    print STDERR "about to strip $_ .\n";
    system("strip $_");
    print STDERR "strip $_ done.\n";
    if ($SHLIBSIGN ne "" && /libsoftokn3|libfreebl_pure32_3|libfreebl_hybrid_3/)
    {
        print STDERR "$SHLIBSIGN $_\n";
        system("$SHLIBSIGN -v -i $_");
    }
}

exit 0;
