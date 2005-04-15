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

# give several files on the command line
# from the first file will be extracted the following mappings:
#   directive #define name to "real" name and vice versa
#   attr #define name to "real" name and vice versa
#   directive name to attr name and vice versa
# this file will typically be slap.h
#
# from the second file will be extracted
#   the list of config var members of the slapd frontend config structure
# this file will also usually be slap.h
#
# from the third file will be extracted
#   the mapping of directive #define name to the function which sets its value
# this file will typically be config.c

%DIRECTIVEDEF2NAME = ();
%DIRECTIVENAME2DEF = ();
%ATTRDEF2NAME = ();
%ATTRNAME2DEF = ();
%DIRECTIVE2ATTR = ();
%ATTR2DIRECTIVE = ();
%SETFUNC2VAR = ();

# these are the ldbm specific attributes
%LDBMATTRS = ();

$filename = 'slap.h';
open(F, $filename) or die "Error: could not open $filename: $!";
while (<F>) {
	if (/(CONFIG_.+?_ATTRIBUTE)\s+[\"](.+?)[\"]/) {
		# "
		$ATTRDEF2NAME{$1} = $2;
		$ATTRNAME2DEF{$2} = $1;
	}
}
close F;

$filename = 'libglobs.c';
open(F, $filename) or die "Error: could not open $filename: $!";
while (<F>) {
	if (/^\s*\{(CONFIG_.+?_ATTRIBUTE),/) {
		$attrdef = $1;
		$def = $_;
		do {
			$_ = <F>;
			$def .= $_;
		} until ($def =~ /\}[,]?\s*$/);
		($ignore, $setfunc, $logsetfunc, $whichlog, $varname, $type, $getfunc) =
			split(/\s*\,\s*/, $def);
#		print "attrdef = $attrdef\n";
#		print "attrname = $ATTRDEF2NAME{$attrdef}\n";
#		print "type = $type\n";
#		print "setfunc = $setfunc\n";
		print "$ATTRDEF2NAME{$attrdef} $type";
		if ((($setfunc =~ /0/) || ($setfunc =~ /NULL/)) &&
			(($logsetfunc =~ /0/) || ($logsetfunc =~ /NULL/))) {
			print " is read only";
		}
		print "\n";
	}
}
print "\nTypes:\n";
print "\tCONFIG_INT\t\tan integer\n";
print "\tCONFIG_LONG\t\tan integer\n";
print "\tCONFIG_STRING\t\ta string\n";
print "\tCONFIG_CHARRAY\t\ta list of strings\n";
print "\tCONFIG_ON_OFF\t\tthe string \"on\" or \"off\"\n";
print "\tCONFIG_STRING_OR_OFF\ta string or \"off\" if not applicable\n";
print "\tCONFIG_STRING_OR_UNKNOWN\ta string or \"unknown\" if not applicable\n";
print "\tCONFIG_CONSTANT_INT\tan integer\n";
print "\tCONFIG_CONSTANT_STRING\ta string\n";
print "\tCONFIG_SPECIAL_REFERRALLIST\ta list of strings\n";
print "\tCONFIG_SPECIAL_STORESTATEINFO\tan integer\n";
print "\tCONFIG_SPECIAL_SSLCLIENTAUTH\t\"off\" or \"allowed\" or \"required\"\n";
print "\tCONFIG_SPECIAL_ERRORLOGLEVEL\tan integer\n";

# get a list of ldbm attributes and directives
$filename = 'back-ldbm/backldbm_configdse.c';
open(F, $filename) or die "Error: could not open $filename: $!";
while (<F>) {
	if (/attr_replace[^"]+["]([^"]+)["]/) {
		$LDBMATTRS{$1} = "\n";
	}
	if (/sprintf[^"]+["](\w+)\\t/) {
		$LDBMDIRECTIVES{$1} = "\n";
	}
}
close F;

$filename = 'back-ldbm/dblayer.c';
open(F, $filename) or die "Error: could not open $filename: $!";
while (<F>) {
	if (/dblayer_config_type_[^"]+["]([^"]+)["]/) {
		$LDBMDIRECTIVES{$1} = "\n";
	}
}
close F;

