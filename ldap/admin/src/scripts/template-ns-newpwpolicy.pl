#{{PERL-EXEC}}
#
# BEGIN COPYRIGHT BLOCK
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
use Mozilla::LDAP::Utils qw(:all);
use Mozilla::LDAP::API qw(:api :ssl :apiv3 :constant); # Direct access to C API

#############################################################################
# Default values of the variables

$opt_D = "{{ROOT-DN}}";
$opt_p = "{{SERVER-PORT}}";
$opt_h = "{{SERVER-NAME}}";
$opt_v = 0;

# Variables
$ldapsearch="{{DS-ROOT}}{{SEP}}shared{{SEP}}bin{{SEP}}ldapsearch";
$ldapmodify="{{DS-ROOT}}{{SEP}}shared{{SEP}}bin{{SEP}}ldapmodify";

chdir("{{DS-ROOT}}{{SEP}}shared{{SEP}}bin");

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
		print (STDERR "host = $opt_h, port = $opt_p, suffixDN = \"$opt_S\"\n\n") if $opt_v;
		@base=(
			"cn=nsPwPolicyContainer,$opt_S",
			"cn=\"cn=nsPwPolicyEntry,$opt_S\",cn=nsPwPolicyContainer,$opt_S",
			"cn=\"cn=nsPwTemplateEntry,$opt_S\",cn=nsPwPolicyContainer,$opt_S",
			"cn=nsPwPolicy_cos,$opt_S"
		);

		$ldapadd="$ldapmodify -p $opt_p -h $opt_h -D \"$opt_D\" -w \"$opt_w\" -c -a 2>&1";
		$modifyCfg="$ldapmodify -p $opt_p -h $opt_h -D \"$opt_D\" -w \"$opt_w\" -c 2>&1";

		@container=(
			"dn: cn=nsPwPolicyContainer,$opt_S\n",
			"objectclass: top\n",
			"objectclass: nsContainer\n\n" );
		@pwpolicy=(
			"dn: cn=\"cn=nsPwPolicyEntry,$opt_S\",cn=nsPwPolicyContainer,$opt_S\n",
			"objectclass: top\n",
			"objectclass: ldapsubentry\n",
			"objectclass: passwordpolicy\n\n" );
		@template=(
			"dn: cn=\"cn=nsPwTemplateEntry,$opt_S\",cn=nsPwPolicyContainer,$opt_S\n",
			"objectclass: top\n",
			"objectclass: extensibleObject\n",
			"objectclass: costemplate\n",
			"objectclass: ldapsubentry\n",
			"cosPriority: 1\n",
			"pwdpolicysubentry: cn=\"cn=nsPwPolicyEntry,$opt_S\",cn=nsPwPolicyContainer,$opt_S\n\n" );
		@cos=(
			"dn: cn=nsPwPolicy_cos,$opt_S\n",
			"objectclass: top\n",
			"objectclass: LDAPsubentry\n",
			"objectclass: cosSuperDefinition\n",
			"objectclass: cosPointerDefinition\n",
			"cosTemplateDn: cn=\"cn=nsPwTemplateEntry,$opt_S\",cn=nsPwPolicyContainer,$opt_S\n",
			"cosAttribute: pwdpolicysubentry default operational-default\n\n" );

		@all=(\@container, \@pwpolicy, \@template, \@cos);

        $i=0;

        foreach $current (@base)
        {
			open(FD,"| $ldapadd");
			print FD @{$all[$i]};
			close(FD);
			if ( $? != 0 ) {
				$retCode=$?>>8;
				if ( $retCode == 68 ) {
					print( STDERR "Entry \"$current\" already exists. Please ignore the error\n\n");
				}
				else {
					# Probably a more serious problem.
					# Exit with LDAP error
					print(STDERR "Error $retcode while adding \"$current\". Exiting.\n");
					exit $retCode;
				}
			}
			else {
				print( STDERR "Entry \"$current\" created\n\n") if $opt_v;
			}
			$i=$i+1;
		}

		$modConfig = "dn:cn=config\nchangetype: modify\nreplace:nsslapd-pwpolicy-local\nnsslapd-pwpolicy-local: on\n\n";
		open(FD,"| $modifyCfg ");
		print(FD $modConfig);
		close(FD);
		$retcode = $?;
		if ( $retcode != 0 ) {
			print( STDERR "Error $retcode while modifing \"cn=config\". Exiting.\n" );
			exit ($retcode);
		}
		else {
			print( STDERR "Entry \"cn=config\" modified\n\n") if $opt_v;
		}
	} # end of $opt_S

	if ($opt_U) {
		my $norm_opt_U = normalizeDN($opt_U);
		print (STDERR "host = $opt_h, port = $opt_p, userDN = \"$norm_opt_U\"\n\n") if $opt_v;
		$retcode = `$ldapsearch -h $opt_h -p $opt_p -b \"$norm_opt_U\" -s base \"\"`;
		if ($retcode != 0 ) {
			print( STDERR "the user entry $norm_opt_U does not exist. Exiting.\n");
			exit ($retcode);
		}
		
		print( STDERR "the user entry $norm_opt_U found..\n\n") if $opt_v;
		
		# Now, get the parentDN 
		@rdns = ldap_explode_dn($norm_opt_U, 0);
		shift @rdns;
		$parentDN = join(',', @rdns);

		print (STDERR "parentDN is $parentDN\n\n") if $opt_v;

		@base=(
			"cn=nsPwPolicyContainer,$parentDN",
			"cn=\"cn=nsPwPolicyEntry,$norm_opt_U\",cn=nsPwPolicyContainer,$parentDN"
		);

		$ldapadd="$ldapmodify -p $opt_p -h $opt_h -D \"$opt_D\" -w \"$opt_w\" -c -a 2>&1";
		$modifyCfg="$ldapmodify -p $opt_p -h $opt_h -D \"$opt_D\" -w \"$opt_w\" -c 2>&1";

		@container=(
			"dn: cn=nsPwPolicyContainer,$parentDN\n",
			"objectclass: top\n",
			"objectclass: nsContainer\n\n" );
		@pwpolicy=(
			"dn: cn=\"cn=nsPwPolicyEntry,$norm_opt_U\",cn=nsPwPolicyContainer,$parentDN\n",
			"objectclass: top\n",
			"objectclass: ldapsubentry\n",
			"objectclass: passwordpolicy\n\n" );

		@all=(\@container, \@pwpolicy);

        $i=0;

        foreach $current (@base)
        {
			open(FD,"| $ldapadd ");
			print FD @{$all[$i]};
			close(FD);
			if ( $? != 0 ) {
				$retCode=$?>>8;
				if ( $retCode == 68 ) {
					print( STDERR "Entry $current already exists. Please ignore the error\n\n");
				}
				else {
					# Probably a more serious problem.
					# Exit with LDAP error
					print(STDERR "Error $retcode while adding \"$current\". Exiting.\n");
					exit $retCode;
				}
			}
			else {
				print( STDERR "Entry $current created\n\n") if $opt_v;
			}
			$i=$i+1;
		}

		$target = "cn=\"cn=nsPwPolicyEntry,$norm_opt_U\",cn=nsPwPolicyContainer,$parentDN";
		$modConfig = "dn: $norm_opt_U\nchangetype: modify\nreplace:pwdpolicysubentry\npwdpolicysubentry: $target\n\n";
		open(FD,"| $modifyCfg ");
		print(FD $modConfig);
		close(FD);
		$retcode = $?;
		if ( $retcode != 0 ) {
			print( STDERR "Error $retcode while modifing $norm_opt_U. Exiting.\n" );
			exit ($retcode);
		}
		else {
			print( STDERR "Entry \"$norm_opt_U\" modified\n\n") if $opt_v;
		}

		$modConfig = "dn:cn=config\nchangetype: modify\nreplace:nsslapd-pwpolicy-local\nnsslapd-pwpolicy-local: on\n\n";
		open(FD,"| $modifyCfg ");
		print(FD $modConfig);
		close(FD);
		$retcode = $?;
		if ( $retcode != 0 ) {
			print( STDERR "Error $retcode while modifing \"cn=config\". Exiting.\n" );
			exit ($retcode);
		}
		else {
			print( STDERR "Entry \"cn=config\" modified\n\n") if $opt_v;
		}
	} # end of $opt_U
}
