#!/usr/bin/env perl
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
# Copyright (C) 2007 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
###################################################################################
#
# FILE: cl-dump.pl
#
# SYNOPSIS:
#  cl-dump.pl [-h host] [-p port] [-D bind-dn] -w bind-password | -P bind-cert
#       [-r replica-roots] [-o output-file] [-c] [-v]
#
#  cl-dump.pl -i changelog-ldif-file-with-base64encoding [-o output-file] [-c]
#
# DESCRIPTION:
#    Dump and decode Directory Server replication change log
#
# OPTIONS:
#    -c Dump and interpret CSN only. This option can be used with or
#       without -i option. 
#
#    -D bind-dn
#       Directory server's bind DN. Default to "cn=Directory Manager" if
#       the option is omitted.
#
#    -h host
#       Directory server's host. Default to the server where the script
#       is running.
#
#    -i changelog-ldif-file-with-base64encoding
#       If you already have a ldif-like changelog, but the changes
#       in that file are encoded, you may use this option to
#       decode that ldif-like changelog.
#
#    -o output-file
#       Path name for the final result. Default to STDOUT if omitted.
#
#    -p port
#       Directory server's port. Default to 389.
#
#    -P bind-cert
#       Pathname of binding certificate DB
#
#    -r replica-roots
#       Specify replica roots whose changelog you want to dump. The replica
#       roots may be seperated by comma. All the replica roots would be
#       dumped if the option is omitted.
#
#    -v Print the version of this script.
#
#    -w bind-password
#       Password for the bind DN
#
# RESTRICTION:
#    If you are not using -i option, the script should be run when the server
#    is running, and from where the server's changelog directory is accessible.
#
# DIAGNOSIS:
#    For environment variable issues, see script repl-monitor.pl under bindir
#
################################################################################
# enable the use of our bundled perldap with our bundled ldapsdk libraries
# all of this nonsense can be omitted if the mozldapsdk and perldap are
# installed in the operating system locations (e.g. /usr/lib /usr/lib/perl5)

$usage="Usage: $0 [-h host] [-p port] [-D bind-dn] [-w bind-password | -P bind-cert] [-r replica-roots] [-o output-file] [-c] [-v]\n\n       $0 -i changelog-ldif-file-with-base64encoding [-o output-file] [-c]\n";

use Getopt::Std;			# Parse command line arguments
use Mozilla::LDAP::Conn;		# LDAP module for Perl
use Mozilla::LDAP::Utils;		# LULU, utilities.
use Mozilla::LDAP::API;			# Used to parse LDAP URL
use MIME::Base64;			# Decode

# Global variables

$version = "Directory Server Changelog Dump - Version 1.0";

#main
{
	# Turn off buffered I/O
	$| = 1;

	# Check for legal options
	if (!getopts('h:p:D:w:P:r:o:cvi:')) {
		print $usage;
		exit -1;
	}

	exit -1 if &validateArgs;

	if ($opt_v) {
		print OUTPUT "$version\n";
		exit;
	}

	if (!$opt_i) {
		&cl_dump_and_decode;
	}
	elsif ($opt_c) {
		&grep_csn ($opt_i);
	}
	else {
		&cl_decode ($opt_i);
	}

	close (OUTPUT);
}

# Validate the parameters
sub validateArgs
{
	my ($rc) = 0;

	%ld = Mozilla::LDAP::Utils::ldapArgs();
	chop ($ld{host} = `hostname`) if !$opt_h;
	$ld{bind} = "cn=Directory Manager" if !$opt_D;
	@allreplicas = ($opt_r) if ($opt_r);
	if ($opt_o && ! open (OUTPUT, ">$opt_o")) {
		print "Can't create output file $opt_o\n";
		$rc = -1;
	} 
	# Open STDOUT if option -o is missing
	open (OUTPUT, ">-") if !$opt_o;

	return $rc;
}

# Dump and decode changelog
# OUTPUT should have been opened before this call
sub cl_dump_and_decode
{
	# Open the connection
	my ($conn) = new Mozilla::LDAP::Conn (\%ld);
	if (!$conn) {
		print OUTPUT qq/Can't connect to $ld{host}:$ld{port} as "$ld{bind}"\n/;
		return -1;
	}

	# Get the changelog dir
	my ($changelogdir);
	my ($entry) = $conn->search ("cn=changelog5,cn=config", "sub", "(objectClass=*)");
	while ($entry) {
		$changelogdir = $entry->{"nsslapd-changelogdir"}[0];
		last if $changelogdir;
		$entry = $conn->nextEntry ();
	}

	# Get all the replicas on the server if -r option is not specified
	if (!$opt_r) {
		$entry = $conn->search ("cn=mapping tree,cn=config", "sub",
								"(objectClass=nsDS5Replica)");
		while ($entry) {
			push (@allreplicas, "$entry->{nsDS5ReplicaRoot}[0]");
			$entry = $conn->nextEntry ();
		}
	}

	# Dump the changelog for the replica
	my (@ldifs);
	my ($replica);
	my ($gotldif);
	my ($ldif);
	foreach (@allreplicas) {
		# Reset the script's start time
		$^T = time;

		$replica = $_;
		$gotldif = 0;

		# Can't move this line before entering the loop:
		# no ldif file generated other than for the first
		# replica.
		$entry = $conn->newEntry();
		$entry->setDN ("cn=replica,cn=\"$_\",cn=mapping tree,cn=config");
		$entry->setValues('nsDS5Task', 'CL2LDIF');
		$conn->update ($entry);

		#Decode the dumped changelog
		@ldifs = <$changelogdir/*.ldif>;
		foreach (@ldifs) {
			# Skip older ldif files
			next if ($#ldifs > 0 && (-M $_ > 0));
			$ldif = $_;
			$gotldif = 1;
			&print_header ($replica, 0);
			if ($opt_c) {
				&grep_csn ($_);
			}
			else {
				&cl_decode ($_);
			}
			# Test op -M doesn't work well so we use rename
			# here to avoid reading the same ldif file more
			# than once.
			rename ($ldif, "$ldif.done");
		}
		&print_header ($replica, "Not Found") if !$gotldif;
	}
	$conn->close;
}

sub print_header
{
	my ($replica, $ldif) = @_;
	print OUTPUT "\n# Replica Root: $replica" if $replica;
	print OUTPUT "\n# LDIF File   : $ldif\n" if $ldif;
}

# Grep and interpret CSNs
# OUTPUT should have been opened before this call
sub grep_csn
{
	open (INPUT, "@_") || return;
	&print_header (0, @_);

	my ($csn, $maxcsn, $modts);
	while (<INPUT>) {
		next if ($_ !~ /(csn:)|(ruv:)/i);
		if (/ruv:\s*{.+}\s+(\w+)\s+(\w+)\s+(\w*)/i) {
			#
			# RUV with two CSNs and an optional lastModifiedTime
			#
			$csn = &csn_to_string($1);
			$maxcsn = &csn_to_string($2);
			$modts = $3;
			if ( $modts =~ /^0+$/ ) {
				$modts = "";
			}
			else {
				$modts = &csn_to_string($modts);
			}
		}
		elsif (/csn:\s*(\w+)\s+/i || /ruv:\s*{.+}\s+(\w+)\s+/i) {
			#
			# Single CSN
			#
			$csn = &csn_to_string($1);
			$maxcsn = "";
			$modts = "";
		}
		else {
			printf OUTPUT;
			next;
		}
		chop;
		printf OUTPUT "$_ ($csn";
		printf OUTPUT "; $maxcsn" if $maxcsn;
		printf OUTPUT "; $modts" if $modts;
		printf OUTPUT ")\n";
	}
}

sub csn_to_string
{
	my ($csn, $tm, $seq, $masterid, $subseq);
	my ($sec, $min, $hour, $mday, $mon, $year);

	$csn = "@_";
	return $csn if !$csn;

	($tm, $seq, $masterid, $subseq) = unpack("a8 a4 a4 a4", $csn);
	$tm = hex($tm);
	$seq = hex($seq);
	$masterid = hex($masterid);
	$subseq = hex($subseq);
	($sec, $min, $hour, $mday, $mon, $year) = localtime ($tm);
	$mon++;
	$year += 1900;
	foreach ($sec, $min, $hour, $mday, $mon) {
		$_ = "0".$_ if ($_ < 10);
	}
	$csn = "$mon/$mday/$year $hour:$min:$sec";
	$csn .= " $seq $subseq" if ( $seq != 0 || $subseq != 0 );

	return $csn;
}

# Decode the changelog
# OUTPUT should have been opened before this call
sub cl_decode
{
	open (INPUT, "@_") || return;
	&print_header (0, @_);

	my ($encoded);
	undef $encoded;
	while (<INPUT>) {
		# Try to accomodate "changes" in 4.X and "change" in 6.X
		if (/^changes?::\s*(\S*)/i) {
			print OUTPUT "change::\n";
			$encoded = $1;
			next;
		}
		if (!defined ($encoded)) {
			print OUTPUT;
			next;
		}
 		if ($_ eq "\n") {
			print OUTPUT MIME::Base64::decode($encoded);
			print OUTPUT "\n";
			undef $encoded;
			next;
		}
		/^\s*(\S+)\s*\n/;
		$encoded .= $1;
	}
}
