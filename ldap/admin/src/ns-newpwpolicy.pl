# perl script
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

# Add new password policy specific entries

#############################################################################
# enable the use of Perldap functions
require DynaLoader;

use Getopt::Std;
use Mozilla::LDAP::Conn;
use Mozilla::LDAP::Entry;
use Mozilla::LDAP::Utils qw(:all);
use Mozilla::LDAP::API qw(:api :ssl :apiv3 :constant); # Direct access to C API

#############################################################################
# Default values of the variables

$opt_D = "cn=directory manager";
$opt_p = 389;
$opt_h = "localhost";
$opt_v = 0;

#############################################################################

sub usage {
	print (STDERR "ns-newpwpolicy.pl [-v] [-D rootdn] { -w password | -j filename } \n");
	print (STDERR "                  [-p port] [-h host] -U UserDN -S SuffixDN\n\n");

	print (STDERR "Arguments:\n");
	print (STDERR "	-?		- help\n");
	print (STDERR "	-v		- verbose output\n");
	print (STDERR "	-D rootdn	- Directory Manager DN. Default= '$opt_D'\n");
	print (STDERR "	-w rootpw	- password for the Directory Manager DN\n");
	print (STDERR "	-j filename	- Read the Directory Manager's password from file\n");
	print (STDERR "	-p port		- port. Default= $opt_p\n");
	print (STDERR "	-h host		- host name. Default= '$opt_h'\n");
	print (STDERR "	-U userDN	- User entry DN\n");
	print (STDERR "	-S suffixDN	- Suffix entry DN\n");
	exit 100;
}

# Process the command line arguments
{
	usage() if (!getopts('vD:w:j:p:h:U:S:'));

	if ($opt_j ne ""){
		die "Error, cannot open password file $opt_j\n" unless (open (RPASS, $opt_j));
		$opt_w = <RPASS>;
		chomp($opt_w);
		close(RPASS);
	} 

	usage() if( $opt_w eq "" );
	if ($opt_U eq "" && $opt_S eq "") {
		print (STDERR "Please provide at least -S or -U option.\n\n");
	}

	# Now, check if the user/group exists

	if ($opt_S) {
		my $norm_opt_S = normalizeDN($opt_S);
		print (STDERR "host = $opt_h, port = $opt_p, suffixDN = $norm_opt_S\n\n") if $opt_v;
		%ld = Mozilla::LDAP::Utils::ldapArgs();
		$ld->{"host"} = $opt_h;
		$ld->{"port"} = $opt_p;
		$ld->{"bind"} = $opt_D;
		$ld->{"pswd"} = $opt_w;
		$conn = new Mozilla::LDAP::Conn(\%ld); die "No LDAP connection" unless $conn;
	
		$entry_1 = new Mozilla::LDAP::Entry;
		$dn1 = "cn=nsPwPolicyContainer, " . $norm_opt_S;
		print (STDERR "adding $dn1\n\n") if $opt_v;
		$entry_1->setDN("$dn1");
		$entry_1->setValues("objectclass", "top", "nsContainer");
		$conn->add($entry_1);
		$error = $conn->getErrorCode();
		if ( ( $error ne 0 ) && ( $error ne 68 ) ) {
			$conn->printError();
			exit (-1);
		}

		$entry_2 = new Mozilla::LDAP::Entry;
		$dn2 = "cn=\"cn=nsPwPolicyEntry,$norm_opt_S\",cn=nsPwPolicyContainer," . $norm_opt_S;
		print (STDERR "adding $dn2\n\n") if $opt_v;
		$entry_2->setDN("$dn2");
		$entry_2->setValues("objectclass", "top", "ldapsubentry", "passwordpolicy");
		$conn->add($entry_2);
		$conn->printError() if $conn->getErrorCode();

		$entry_3 = new Mozilla::LDAP::Entry;
		$dn3 = "cn=\"cn=nsPwTemplateEntry,$norm_opt_S\",cn=nsPwPolicyContainer, " . $norm_opt_S;
		print (STDERR "adding $dn3\n\n") if $opt_v;
		$entry_3->setDN("$dn3");
		$entry_3->setValues("objectclass", "top", "extensibleObject", "costemplate", "ldapsubentry");
		$entry_3->setValues("cospriority", "1");
		$entry_3->setValues("pwdpolicysubentry", "$dn2");
		$conn->add($entry_3);
		$conn->printError() if $conn->getErrorCode();

		$entry_4 = new Mozilla::LDAP::Entry;
		$dn4 = "cn=nsPwPolicy_cos, " . $norm_opt_S;
		print (STDERR "adding $dn4\n\n") if $opt_v;
		$entry_4->setDN("$dn4");
		$entry_4->setValues("objectclass", "top", "cosSuperDefinition", "cosPointerDefinition", "ldapsubentry");
		$entry_4->setValues("cosTemplateDn", "$dn3");
		$entry_4->setValues("cosAttribute", "pwdpolicysubentry default operational-default");
		$conn->add($entry_4);	
		$conn->printError() if $conn->getErrorCode();

		$cfg_entry = $conn->search("cn=config", "base", "(objectclass=*)");
		$conn->printError() if $conn->getErrorCode();
		print (STDERR "modifying cn=config\n\n") if $opt_v;
		$cfg_entry->setValues("nsslapd-pwpolicy-local", "on");
		$conn->update($cfg_entry);
		$conn->printError() if $conn->getErrorCode();
		
		$conn->close if $conn;
	
	} # end of $opt_S

	if ($opt_U) {
		my $norm_opt_U = normalizeDN($opt_U);
		print (STDERR "host = $opt_h, port = $opt_p, userDN = $norm_opt_U\n\n") if $opt_v;
		%ld = Mozilla::LDAP::Utils::ldapArgs();
		$ld->{"host"} = $opt_h;
		$ld->{"port"} = $opt_p;
		$ld->{"bind"} = $opt_D;
		$ld->{"pswd"} = $opt_w;
		$conn = new Mozilla::LDAP::Conn(\%ld); die "No LDAP connection" unless $conn;

		$user_entry = $conn->search($norm_opt_U, "base", "(objectclass=*)");
		$conn->printError() if $conn->getErrorCode();
		if (! $user_entry) {
			print (STDERR "The user entry $norm_opt_U does not exist. Exiting.\n");
			exit (-1);
		}

		print (STDERR "the user entry $norm_opt_U found..\n\n") if $opt_v;
	
		# Now, get the parentDN 
		@rdns = ldap_explode_dn($norm_opt_U, 0);
		shift @rdns;
		$parentDN = join(',', @rdns);

		print (STDERR "parentDN is $parentDN\n\n") if $opt_v;

		# Now, check if the nsContainer entry exists at the parent level
		$dn1 = "cn=nsPwPolicyContainer, " . $parentDN;
		$entry = $conn->search($dn1, "base", "(objectclass=*)");
		my $error = $conn->getErrorCode();
		$conn->printError() 
			if (( $error ne 0 ) && ( $error ne 32 ) && ( $error ne 68 ));

		if (! $entry) { 
			print (STDERR "nsContainer doesn't exist. Creating one now..\n\n") if $opt_v;
	  
			$entry_1 = new Mozilla::LDAP::Entry;
		
			print (STDERR "adding $dn1\n\n") if $opt_v;
			$entry_1->setDN("$dn1");
			$entry_1->setValues("objectclass", "top", "nsContainer");
			$conn->add($entry_1);
			$conn->printError() if $conn->getErrorCode();
		} else {
			print (STDERR "nsContainer exists..\n\n") if $opt_v;
		}

		$entry_2 = new Mozilla::LDAP::Entry;
		$dn2 = "cn=\"cn=nsPwPolicyEntry,$norm_opt_U\",cn=nsPwPolicyContainer," . $parentDN;
		print (STDERR "adding $dn2\n\n") if $opt_v;
		$entry_2->setDN("$dn2");
		$entry_2->setValues("objectclass", "top", "ldapsubentry", "passwordpolicy");
		$conn->add($entry_2);
		$conn->printError() if $conn->getErrorCode();

		print (STDERR "modifying $norm_opt_U\n\n") if $opt_v;
		$user_entry->setValues("pwdpolicysubentry", "$dn2");
		$conn->update($user_entry);
		$conn->printError() if $conn->getErrorCode();

		$cfg_entry = $conn->search("cn=config", "base", "(objectclass=*)");
		$conn->printError() if $conn->getErrorCode();
		print (STDERR "modifying cn=config\n\n") if $opt_v;
		$cfg_entry->setValues("nsslapd-pwpolicy-local", "on");
		$conn->update($cfg_entry);
		$conn->printError() if $conn->getErrorCode();

		$conn->close if $conn;
		
	} # end of $opt_U
}
