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
# used to add the perl preamble to all perl scripts used by the directory
# server run time; the problem is that the INC paths are compiled into
# the executable and cannot be overridden at run time, so we have to make
# sure we fix them

# the first argument is the source path for the INC path
# the second argument is the token to replace with the server root
# at install time
# the third argument is the source perl script
# the fourth argument is the destination perl script
# 

($sourceLibPath, $serverRootToken, $sourceScript, $destScript) = @ARGV;
open SRC, $sourceScript or die "Error: could not open $sourceScript: $!";
open DEST, ">$destScript" or die "Error: could not write $destScript: $!";

$isNT = -d '\\';

print DEST<<EOF1;
#!perl
# This preamble must be at the beginning of every perl script so that we can
# find packages and dynamically load code at run time; SERVER_ROOT must
# be replaced with the absolute path to the server root at installation
# time; it is assumed that the perl library will be installed as
# server root/install/perl
BEGIN {
EOF1

# if on NT, just assume we are using activeState, which looks like this
# server root/install/lib
#                    /bin
#                    /site/lib
# there is no arch subdir on NT
if ($isNT) {
	print DEST "\t\@INC = qw( $serverRootToken/install/lib $serverRootToken/install/site/lib . );\n";
	print DEST "}\n";
} else {
	print DEST<<EOF2;
	\$BUILD_PERL_PATH = \"$sourceLibPath\";
	\$RUN_PERL_PATH = \"$serverRootToken/install\";
	# make sure we use the unix path conventions
	grep { s#\$BUILD_PERL_PATH#\$RUN_PERL_PATH#g } \@INC;
}
EOF2
}

# copy the rest of the file
while (<SRC>) {
	print DEST;
}

close DEST;
close SRC;
