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
##############################################################################
#
# FILE: repl-monitor.pl
#
# SYNOPSIS:
#    repl-monitor.pl -f configuration-file [-h host] [-p port] [-r]
#                    [-u refresh-url] [-t refresh-interval]
#
#    repl-monitor.pl -v
#
# DESCRIPTION:
#    Given an LDAP replication "supplier" server, crawl over all the ldap
#    servers via direct or indirect replication agreements.
#    For each master replica discovered, display the maxcsn of the master
#    and the replication status of all its lower level replicas.
#    All output is in HTML.
#
# OPTIONS:
#    -f configuration-file
#       The configuration file contains the sections for the connection
#       parameters, the server alias, and the thresholds for different colors
#       when display the time lags between consumers and master.
#       If the Admin Server is running on Windows, the configuration-file
#       name may have format "D:/opt/replmon.conf".
#
#   The connection parameter section consists of the section name
#   followed by one of more connection parameter entries:
#
#       [connection]
#       host:port:binddn:bindpwd:bindcert
#       host:port=shadowport:binddn:bindpwd:bindcert
#       ...
#
#   where host:port default (*:*) to that in a replication agreement,
#   binddn default (*) to "cn=Directory Manager", and bindcert is the
#   pathname of cert db if you want the script to connect to the server
#   via SSL.  If bindcert is omitted, the connection will be simple
#   bind.
#   "port=shadowport" means to use shadowport instead of port if port
#   is specified in the replication agreement. This is useful when
#   for example, ssl port is specified in a replication agreement,
#   but you can't access the cert db from the machine where this
#   script is running. So you could let the script to map the ssl
#   port to a non-ssl port and use the simple bind.
#
#   A server may have a dedicated or a share entry in the connection
#   section. The script will find out the most matched entry for a given
#   server. For example, if all the ldap servers except host1 share the
#   same binddn and bindpassword, the connection section then just need
#   two entries:
#
#       [connection]
#        *:*:binddn:bindpassword:
#        host1:*:binddn:bindpassword:
#
#   If a host:port is assigned an alias, then the alias instead of
#   host:port will be displayed in The output file. Each host:port
#   can have only one alias. But each alias may be used by more than
#   one host:port.
#
#       [alias]
#       alias = host:port
#       ...
#
#   CSN time lags between masters and consumers might be displayed in
#   different colors based on their range. The thresholds for different
#   colors may be specified in color section:
#
#       [color]
#       lowmark (in minutes) = color
#       ...
# 
#   If the color section or color entry is missing, the default color
#   set is: green for [0-5) minutes lag, yellow [5-60), and red 60 and more.
#
#    -h host
#       Initial replication supplier's host. Default to the current host.
#
#    -p port
#       Initial replication supplier's port. Default to 389.
#
#    -r If specified, -r causes the routine to be entered without printing
#       HTML header information.  This is suitable when making multiple calls
#       to this routine (e.g. when specifying multiple, different, "unrelated"
#       supplier servers) and expecting a single HTML output. 
#
#    -t refresh-interval
#       Specify the refresh interval in seconds. This option has to be
#       jointly used with option -u.
#
#    -u refresh-url
#       The output HTML file may invoke a CGI program periodically. If
#       this CGI program in turn calls this script, the effect is that
#       the output HTML file would automatically refresh itself. This
#       is useful for continuing monitoring. See also option -t.
#
#    -v Print out the version of this script
# 
# DIAGNOSTICS:
#    There are several ways to invoke this script if you got error
#    "Can't locate Mozilla/LDAP/Conn.pm in @INC", or
#    "usage: Undefined variable":
#
#    0. Prerequisite: NSPR, NSS, Mozilla LDAP C SDK, PerLDAP
#
#    1. Run this perl script via repl-monitor, which sets up LD_LIBRARY_PATH
#       $ repl-monitor
#
#    2. If 1 does not work, try invoking this script as follows.
#       Assuming <MYPERLDIR> contains Mozilla/LDAP:
#       perl -I <MYPERLDIR> repl-monitor.pl
#
#############################################################################
# enable the use of our bundled perldap with our bundled ldapsdk libraries
# all of this nonsense can be omitted if the mozldapsdk and perldap are
# installed in the operating system locations (e.g. /usr/lib /usr/lib/perl5)
# this script is always invoked by repl-monitor-cgi.pl, which sets all of these
# If using this script standalone, be sure to set the shared lib path and
# the path to the perldap modules.

$usage = "\nusage: $0 -f configuration-file [-h host] [-p port] [-r] [-u refresh-url] [-t refresh-interval]\n\nor   : $0 -v\n"; 

use Getopt::Std;		# parse command line arguments
use Mozilla::LDAP::Conn;	# LDAP module for Perl
use Mozilla::LDAP::Utils qw(normalizeDN);	# LULU, utilities.
use Mozilla::LDAP::API qw(:api :ssl :apiv3 :constant); # Direct access to C API
use Time::Local; # to convert GMT Z strings to localtime

#
# Global variables
#
$product = "Directory Server Replication Monitor";
$version = "Version 1.0";
#
# ldap servers given or discovered from the replication agreements:
# @servers		= (host:port=shadowport:binddn:password:cert_db)
#
# entries read from the connection section of the configuration file:
# @allconnections	= (host:port=shadowport:binddn:password:cert_db)
#
# aliases of ldap servers read from the configuration file:
# %allaliases{$host:$port}= (alias)
#
# replicas discovered on all ldap servers
# @allreplicas		= (server#:replicaroot:replicatype:serverid:replicadn)
#
# ruvs retrieved from all replicas
# @allruvs{replica#:masterid} = (rawcsn:decimalcsn;mon/day/year hh:mi:ss)
#
# agreements discovered on all ldap supplier servers:
# @allagreements	= (supplier_replica#:consumer#:conntype:schedule:status)
# the array may take another format after the consumer replicas are located:
# @allagreements	= (supplier_replica#:consumer_replica#:conntype:schedule:status)
#

#main
{
	# turn off buffered I/O
	$| = 1;

	# Check for legal options
	if (!getopts('h:p:f:ru:t:v')) {
		print $usage;
		exit -1;
  	}

	if ($opt_v) {
		print "$product - $version\n";
		exit;
	}

	$interval = $opt_t;
	$interval = 300 if ( !$interval || $interval <= 0 );

	# Get current date/time
	$nowraw = localtime();
	($wday, $mm, $dd, $tt, $yy) = split(/ /, $nowraw);
	$now = "$wday $mm $dd $yy $tt";

	# if no -r (Reenter and skip html header), print html header
	if (!$opt_r) {
		# print the HTML header
		&print_html_header;
	} else {
		# print separator for new replication set
		print "<hr width=90% size=3><br>\n";
	}

	exit -1 if &validateArgs < 0;
	exit if &read_cfg_file ($opt_f) < 0;

	# Start with the given host and port
	# The index names in %ld are defined in Mozilla::LDAP::Utils::ldapArgs()
	&add_server ("$ld{host}:$ld{port}:$ld{bind}:$ld{pswd}:$ld{cert}");

	$serveridx = 0;
	while ($serveridx <= $#servers) {
		if (&get_replicas ($serveridx) != 0 && $serveridx == 0) {
			my ($host, $port, $binddn) = split (/:/, $servers[0]);
			print("Login to $host:$port as \"$binddn\" failed\n");
			exit;
		}
		$serveridx++;
	} 

	&find_consumer_replicas;
	&process_suppliers;

	# All done! - well, for the current invokation only
	# print "</body></html>\n";
	exit;  
} 

sub validateArgs
{
	my ($rc) = 0;

	%ld = Mozilla::LDAP::Utils::ldapArgs();

	if (!$opt_v && !$opt_f) {
		print "<p>Error: Missing configuration file.\n";
		print "<p>If you need help on the configuration file, Please go back and click the Help button.\n";
		#print $usage;	# Don't show usage in CGI
		$rc = -1;
	}
	elsif (!$opt_h) {
		chop ($ld{"host"} = `hostname`);
	}

	return $rc;
}

sub read_cfg_file
{
	my ($fn) = @_;
	unless (open(CFGFILEHANDLE, $fn)) {
		print "<p>Error: Can't open \"$fn\": $!.\n";
		print "<p>If you need help on the configuration file, Please go back and click the Help button.\n";
		return -1;
	}
	$section = 0;
	while (<CFGFILEHANDLE>) {
		next if (/^\s*\#/ || /^\s*$/);
		chop ($_);
		if (m/^\[(.*)\]/) {
			$section = $1;
		}
		else {
			if ( $section =~ /conn/i ) {
				push (@allconnections, $_);
			}
			elsif ( $section =~ /alias/i ) {
				m/^\s*(\S.*)\s*=\s*(\S+)/;
				$allaliases {$2} = $1;
			}
			elsif ( $section =~ /color/i ) {
				m/^\s*(-?\d+)\s*=\s*(\S+)/;
				$allcolors {$1} = $2;
			}
		}
	}
	if ( ! keys (%allcolors) ) {
		$allcolors {0} = "#ccffcc";	#apple green
		$allcolors {5} = "#ffffcc";	#cream yellow
		$allcolors {60} = "#ffcccc";	#pale pink
	}
	@colorkeys = sort (keys (%allcolors));
	close (CFGFILEHANDLE);
	return 0;
}

sub get_replicas
{
	my ($serveridx) = @_;
	my ($conn, $host, $port, $shadowport, $binddn, $bindpwd, $bindcert);
	my ($others);
	my ($replica, $replicadn);
	my ($ruv, $replicaroot, $replicatype, $serverid, $masterid, $maxcsn);
	my ($type, $flag, $i);
	my ($myridx, $ridx, $cidx);

	#
	# Bind to the server
	#
	($host, $port, $binddn, $bindpwd, $bindcert) = split (/:/, "$servers[$serveridx]", 5);

	($port, $shadowport) = split (/=/, $port);
	$shadowport = $port if !$shadowport;

	$conn = new Mozilla::LDAP::Conn ($host, $shadowport, "$binddn", $bindpwd, $bindcert);

	return -1 if (!$conn);

	#
	# Get all the replica on the server
	#
	$myridx = $#allreplicas + 1;
	$replica = $conn->search ("cn=mapping tree,cn=config",
				"sub",
				"(objectClass=nsDS5Replica)", 0,
				qw(nsDS5ReplicaRoot nsDS5ReplicaType nsDS5Flags nsDS5ReplicaId));
	while ($replica) {
		$replicadn = $replica->getDN;
		$replicaroot = normalizeDN ($replica->{nsDS5ReplicaRoot}[0]);
		$type      = $replica->{nsDS5ReplicaType}[0];
		$flag      = $replica->{nsDS5Flags}[0];
		$serverid  = $replica->{nsDS5ReplicaId}[0];

		# flag = 0: change log is not created
		# type = 2: read only replica
		# type = 3: updatable replica
		$replicatype = $flag == 0 ? "consumer" : ($type == 2 ? "hub" : "master");

		push (@allreplicas, "$serveridx:$replicaroot:$replicatype:$serverid:$replicadn");

		$replica = $conn->nextEntry ();
	}

	#
	# Get ruv for each replica
	#
	for ($ridx = $myridx; $ridx <= $#allreplicas; $ridx++) {

		$replicaroot = $1 if ($allreplicas[$ridx] =~ /^\d+:([^:]*)/);
		# do a one level search with nsuniqueid in the filter - this will force the use of the
		# nsuniqueid index instead of the entry dn index, which seems to be unreliable in
		# heavily loaded servers
		$ruv = $conn->search($replicaroot, "one",
							 "(&(nsuniqueid=ffffffff-ffffffff-ffffffff-ffffffff)(objectClass=nsTombstone))",
							 0, qw(nsds50ruv nsruvReplicaLastModified));
		next if !$ruv; # this should be an error case . . .

		for ($ruv->getValues('nsds50ruv')) {
			if (m/\{replica\s+(\d+).+?\}\s*\S+\s*(\S+)/i) {
				$masterid = $1;
				$maxcsn = &to_decimal_csn ($2);
				$allruvs {"$ridx:$masterid"} = "$2:$maxcsn";
			}
		}

		for ($ruv->getValues('nsruvReplicaLastModified')) {
			if (m/\{replica\s+(\d+).+?\}\s*(\S+)/i) {
				$masterid = $1;
				$lastmodifiedat = hex($2);
				my ($sec, $min, $hour, $mday, $mon, $year) = localtime ($lastmodifiedat);
				$mon++;
				$year += 1900;
				$hour = "0".$hour if ($hour < 10);
				$min = "0".$min if ($min < 10);
				$sec = "0".$sec if ($sec < 10);
				$allruvs {"$ridx:$masterid"} .= ";$mon/$mday/$year $hour:$min:$sec";
			}
		}
	}

	#
	# Get all agreements for each supplier replica
	#
	for ($ridx = $myridx; $ridx <= $#allreplicas; $ridx++) {
		$_ = $allreplicas[$ridx];

		# Skip consumers
		next if m/:consumer:/i;

		m/:([^:]*)$/;
		$replicadn = $1;
		my @attrlist = qw(cn nsds5BeginReplicaRefresh nsds5replicaUpdateInProgress
						  nsds5ReplicaLastInitStatus nsds5ReplicaLastInitStart
						  nsds5ReplicaLastInitEnd nsds5replicaReapActive
						  nsds5replicaLastUpdateStart nsds5replicaLastUpdateEnd
						  nsds5replicaChangesSentSinceStartup nsds5replicaLastUpdateStatus
						  nsds5ReplicaHost
						  nsds5ReplicaPort nsDS5ReplicaBindMethod nsds5ReplicaUpdateSchedule);
		$agreement = $conn->search("$replicadn", "sub", "(objectClass=nsDS5ReplicationAgreement)",
								   0, @attrlist);
		while ($agreement) {

			my %agmt = ();
			# Push consumer to server stack if we have not already
			$host = ($agreement->getValues('nsDS5ReplicaHost'))[0];
			$port = ($agreement->getValues('nsDS5ReplicaPort'))[0];
			$cidx = &add_server ("$host:$port");

			for (@attrlist) {
			  $agmt{$_} = ($agreement->getValues($_))[0];
			}
			if ($agmt{nsDS5ReplicaBindMethod} =~ /simple/i) {
			  $agmt{nsDS5ReplicaBindMethod} = 'n';
			}
			if (!$agmt{nsds5ReplicaUpdateSchedule} ||
			    ($agmt{nsds5ReplicaUpdateSchedule} eq '0000-2359 0123456') ||
			    ($agmt{nsds5ReplicaUpdateSchedule} eq '*') ||
			    ($agmt{nsds5ReplicaUpdateSchedule} eq '* *')) {
			  $agmt{nsds5ReplicaUpdateSchedule} = 'always in sync';
			}

			$agmt{ridx} = $ridx;
			$agmt{cidx} = $cidx;
			push @allagreements, \%agmt;

			$agreement = $conn->nextEntry ();
		}
	}

	$conn->close;

	return 0;
}

#
# Initially, the agreements have consumer host:port info instead of
# replica info. This routine will find the consumer replica info
#
sub find_consumer_replicas
{
	my ($m_ridx);		# index of master's replica
	my ($s_ridx);		# index of supplier's replica
	my ($c_ridx);		# index of consumer's replica
	my ($c_sidx);		# index of consumer server
	my ($remainder);	#
	my ($s_replicaroot);	# supplier replica root
	my ($c_replicaroot);	# consumer replica root
	my ($j, $val);

	#
	# Loop through every agreement defined on the current supplier replica
	#
	foreach (@allagreements) {
		$s_ridx = $_->{ridx};
		$c_sidx = $_->{cidx};
		$s_replicaroot = $1 if ($allreplicas[$s_ridx] =~ /^\d+:([^:]*)/);
		$c_replicaroot = "";

		# $c_ridx will be assigned to -$c_sidx
		# if the condumer is not accessible
		# $c_sidx will not be zero since it's
		# not the first server.
		$c_ridx = -$c_sidx;	# $c_sidx will not be zero

		# Loop through consumer's replicas and find
		# the counter part for the current supplier
		# replica
		for ($j = 0; $j <= $#allreplicas; $j++) {

			# Get a replica on consumer
	    	# I'm not sure what's going on here, but possibly could be made
			# much simpler with normalizeDN and/or ldap_explode_dn
			if ($allreplicas[$j] =~ /^$c_sidx:([^:]*)/) {
				$val = $1;

				# We need to find out the consumer
				# replica that matches the supplier
				# replicaroot most.
				if ($s_replicaroot =~ /^.*$val$/i &&
					length ($val) >= length ($c_replicaroot)) {
					$c_ridx = $j;

					# Avoid case-sensitive comparison
					last if (length($s_replicaroot) == length($val));
					$c_replicaroot = $val;
				}
			}
		}
		$_->{ridx} = $s_ridx;
		$_->{cidx} = $c_ridx;
	}
}

sub process_suppliers
{
	my ($ridx, $mid, $maxcsn);

	$mid = "";

	$last_sidx = -1;	# global variable for print html page

	for ($ridx = 0; $ridx <= $#allreplicas; $ridx++) {

		# Skip consumers and hubs
		next if $allreplicas[$ridx] !~ /:master:(\d+):/i;
		$mid = $1;

		# Skip replicas without agreements defined yet
		next if (! grep {$_->{ridx} == $ridx} @allagreements);

		$maxcsn = &print_master_header ($ridx, $mid);
		if ( "$maxcsn" != "none" ) {
			&print_consumer_header ();
			&print_consumers ($ridx, $mid);
		}
		&print_supplier_end;
	}

	if ($mid eq "") {
		print "<p>The server is not a master or it has no replication agreement\n";
	}
}

sub print_master_header
{
	my ($ridx, $mid) = @_;
	my ($myruv) = $allruvs {"$ridx:$mid"};
	my ($maxcsnval) = split ( /;/, "$myruv" );
	my ($maxcsn) = &to_string_csn ($maxcsnval);
	my ($sidx, $replicaroot, $replicatype, $serverid) = split (/:/, $allreplicas[$ridx]);

	# Print the master name
	if ( $last_sidx != $sidx ) {
		my ($ldapurl) = &get_ldap_url ($sidx, $sidx);
		&print_legend if ( $last_sidx < 0);
		print "<p><p><hr><p>\n";
		print "\n<p><center class=page-subtitle><font color=#0099cc>\n";
		print "Master:&nbsp $ldapurl</center>\n";
		$last_sidx = $sidx;
	}

	# Print the current replica info onthe master
	print "\n<p><table border=0 cellspacing=1 cellpadding=6 cols=10 width=100% class=bgColor9>\n";

	print "\n<tr><td colspan=10><center>\n";
	print "<font class=areatitle>Replica ID:&nbsp;</font>";
	print "<font class=text28>$serverid</font>\n";

	print "<font class=areatitle>Replica Root:&nbsp;</font>";
	print "<font class=text28>$replicaroot</font>\n";

	print "<font class=areatitle>Max CSN:&nbsp;</font>";
	print "<font class=text28>$maxcsn</font>\n";

	return $maxcsn;
}

sub print_consumer_header
{
	#Print the header of consumer
	print "\n<tr class=bgColor16>\n";
	print "<th nowrap>Receiver</th>\n";
	print "<th nowrap>Time Lag</th>\n";
	print "<th nowrap>Max CSN</th>\n";
	print "<th nowrap>Last Modify Time</th>\n";
	print "<th nowrap>Supplier</th>\n";
	print "<th nowrap>Sent/Skipped</th>\n";
	print "<th nowrap>Update Status</th>\n";
	print "<th nowrap>Update Started</th>\n";
	print "<th nowrap>Update Ended</th>\n";
	print "<th nowrap colspan=2>Schedule</th>\n";
	print "<th nowrap>SSL?</th>\n";
	print "</tr>\n";
}

sub print_consumers
{
	my ($m_ridx, $mid) = @_;
	my ($ignore, $m_replicaroot) = split (/:/, $allreplicas[$m_ridx]);
	my (@consumers, @ouragreements, @myagreements);
	my ($s_ridx, $c_ridx, $conntype, $schedule, $status);
	my ($c_maxcsn_str, $lag, $markcolor);
	my ($c_replicaroot, $c_replicatype);
	my ($first_entry);
	my ($nrows);
	my ($found);

	undef @ouragreements;

	# Collect all the consumer replicas for the current master replica
	push (@consumers, $m_ridx);
	foreach (@consumers) {
		$s_ridx = $_;
		for (@allagreements) {
			next if ($_->{ridx} != $s_ridx);
			$c_ridx = $_->{cidx};
			next if $c_ridx == $m_ridx;
			push @ouragreements, $_;
			$found = 0;
			foreach (@consumers) {
				if ($_ == $c_ridx) {
					$found = 1;
					last;
				}
			}
			push (@consumers, $c_ridx) if !$found;
		}
	}

	# Print each consumer replica
	my ($myruv) = $allruvs {"$m_ridx:$mid"};
	my ($m_maxcsn) = split ( /;/, "$myruv" );
	foreach (@consumers) {
		$c_ridx = $_;
		next if $c_ridx == $m_ridx;

		if ($c_ridx >= 0) {
			$myruv = $allruvs {"$c_ridx:$mid"};
			($c_maxcsn, $c_lastmodified) = split ( /;/, "$myruv" );
			($c_maxcsn_str, $lag, $markcolor) = &cacl_time_lag ($m_maxcsn, $c_maxcsn);
			$c_maxcsn_str =~ s/ /\<br\>/;
			($c_sidx, $c_replicaroot, $c_replicatype) = split (/:/, $allreplicas[$c_ridx]);
			$c_replicaroot = "same as master" if $m_replicaroot eq $c_replicaroot;
		}
		else {
			# $c_ridx is actually -$c_sidx when c is not available
			$c_sidx = -$c_ridx;
			$c_maxcsn_str = "_";
			$lag = "n/a";
			$markcolor = red;
			$c_replicaroot = "_";
			$c_replicatype = "_";
		}

		$nrows = 0;
		foreach (@ouragreements) {
			next if ($_->{cidx} != $c_ridx);
			$nrows++;
		}

		$first_entry = 1;
		foreach (@ouragreements) {
			next if ($_->{cidx} != $c_ridx);
			$s_ridx = $_->{ridx};
			$conntype = $_->{nsDS5ReplicaBindMethod};
			$status = $_->{nsds5replicaLastUpdateStatus};
			$schedule = $_->{nsds5ReplicaUpdateSchedule};
			$s_sidx = $1 if $allreplicas [$s_ridx] =~ /^(\d+):/;
			$s_ldapurl = &get_ldap_url ($s_sidx, "n/a");

			# Print out the consumer's replica and ruvs
			print "\n<tr class=bgColor13>\n";
			if ($first_entry) {
				$first_entry = 0;
				$c_ldapurl = &get_ldap_url ($c_sidx, $conntype);
				print "<td rowspan=$nrows width=5% class=bgColor5>$c_ldapurl<BR>Type: $c_replicatype</td>\n";
				print "<td rowspan=$nrows width=5% nowrap bgcolor=$markcolor><center>$lag</center></td>\n";
				print "<td rowspan=$nrows width=15% nowrap>$c_maxcsn_str</td>\n";
				print "<td rowspan=$nrows width=15% nowrap>$c_lastmodified</td>\n";
			}
			print "<td width=5% nowrap><center>$s_ldapurl</center></td>\n";
			my $changecount = $_->{nsds5replicaChangesSentSinceStartup};
			if ( $changecount =~ /^$mid:(\d+)\/(\d+) / || $changecount =~ / $mid:(\d+)\/(\d+) / ) {
				$changecount = "$1 / $2";
			}
			elsif ( $changecount =~ /^(\d+)$/ ) {
				$changecount = $changecount . " / " . "$_->{nsds5replicaChangesSkippedSinceStartup}";
			}
			else {
				$changecount = "0 / 0";
			}
			print "<td width=3% nowrap>$changecount</td>\n";       
			my $redfontstart = "";
			my $redfontend = "";
			if ($status =~ /error/i) {
			  $redfontstart = "<font color='red'>";
			  $redfontend = "</font>";
			}
			elsif ($status =~ /^(\d+) /) {
				if ( $1 != 0 ) {
					# warning
					$redfontstart = "<font color='#FF7777'>";
					$redfontend = "</font>";
				}
			}
			print "<td width=20% nowrap>$redfontstart$status$redfontend</td>\n";
			print "<td nowrap>", &format_z_time($_->{nsds5replicaLastUpdateStart}), "</td>\n";
			print "<td nowrap>", &format_z_time($_->{nsds5replicaLastUpdateEnd}), "</td>\n";
			if ( $schedule =~ /always/i ) {
				print "<td colspan=2 width=10% nowrap>$schedule</td>\n";
			}
			else {
				my ($ndays, @days);
				$schedule =~ /(\d\d)(\d\d)-(\d\d)(\d\d) (\d+)/;
				print "<td width=10% nowrap>$1:$2-$3:$4</td>\n";
				$ndays = $5;
				$ndays =~ s/(\d)/$1,/g;
				@days = (Sun,Mon,Tue,Wed,Thu,Fri,Sat)[eval $ndays];
				print "<td width=10% nowrap>@days</td>\n";
			}
			print "<td width=3% nowrap class=bgColor5>$conntype</td>\n";
		}
	}
}

sub cacl_time_lag
{
	my ($s_maxcsn, $c_maxcsn) = @_;
	my ($markcolor);
	my ($csn_str);
	my ($s_tm, $c_tm, $lag_tm, $lag_str, $hours, $minute);

	$csn_str = &to_string_csn ($c_maxcsn);

	if ($s_maxcsn && !$c_maxcsn) {
		$lag_str = "- ?:??:??";
		$markcolor = &get_color (36000); # assume consumer has big latency
	}
	elsif (!$s_maxcsn && $c_maxcsn) {
		$lag_str = "+ ?:??:??";
		$markcolor = &get_color (1); # consumer is ahead of supplier
	}
	elsif ($s_maxcsn le $c_maxcsn) {
		$lag_str = "0:00:00";
		$markcolor = &get_color (0);
	}
	else {
		my ($rawcsn, $decimalcsn) = split (/:/, $s_maxcsn);
		($s_tm) = split(/ /, $decimalcsn);

		($rawcsn, $decimalcsn) = split (/:/, $c_maxcsn);
		($c_tm) = split(/ /, $decimalcsn);
		if ($s_tm > $c_tm) {
			$lag_tm = $s_tm - $c_tm;
			$lag_str = "- ";
			$markcolor = &get_color ($lag_tm);
		}
		else {
			$lag_tm = $c_tm - $s_tm;
			$lag_str = "+ ";
			$markcolor = $allcolors{ $colorkeys[0] };	# no delay
		}
		$hours = int ($lag_tm / 3600);
		$lag_str .= "$hours:";

		$lag_tm = $lag_tm % 3600;
		$minutes = int ($lag_tm / 60);
		$minutes = "0".$minutes if ($minutes < 10);
		$lag_str .= "$minutes:";

		$lag_tm = $lag_tm % 60;
		$lag_tm = "0".$lag_tm if ($lag_tm < 10);
		$lag_str .= "$lag_tm";
	}
	return ($csn_str, $lag_str, $markcolor);
}

#
# The subroutine would append a new entry to the end of
# @servers if the host and port are new to @servers.
#
sub add_server
{
	my ($host, $port, $binddn, $bindpwd, $bindcert) = split (/:/, "@_");
	my ($shadowport) = $port;
	my ($domainpattern) = '\.[^:]+';
	my ($i);

	# Remove the domain name from the host name
	my ($hostnode) = $host;
	$hostnode = $1 if $host =~ /^(\w+)\./;

	# new host:port
	if ($binddn eq "" || $bindpwd eq "" && $bindcert eq "") {
		#
		# Look up connection parameter in the order of
		#	host:port
		#	host:*
		#	*:port
		#	*:*
		#
		my (@myconfig, $h, $p, $d, $w, $c);
		(@myconfig = grep (/^$hostnode($domainpattern)*:$port\D/i, @allconnections)) ||
		(@myconfig = grep (/^$hostnode($domainpattern)*:\*:/i, @allconnections)) ||
		(@myconfig = grep (/^\*:$port\D/, @allconnections)) ||
		(@myconfig = grep (/^\*:\*\D/, @allconnections));
		if ($#myconfig >= 0) {
			($h, $p, $d, $w, $c) = split (/:/, $myconfig[0]);
			($p, $shadowport) = split (/=/, $p);
			$p = "" if $p eq "*";
			$c = "" if $c eq "*";
		}
		if ($binddn eq "" || $binddn eq "*") {
			if ($d eq "" || $d eq "*") {
				$binddn = "cn=Directory Manager";
			}
			else {
				$binddn = $d;
			}
		}
		$bindpwd = $w if ($bindpwd eq "" || $bindpwd eq "*");
		$bindcert = $c if ($bindcert eq "" || $bindcert eq "*");
	}

	for ($i = 0; $i <= $#servers; $i++) {
		return $i if ($servers[$i] =~ /$hostnode($domainpattern)*:\d*=$shadowport\D/i);
	}

	if ($shadowport) {
		push (@servers, "$host:$port=$shadowport:$binddn:$bindpwd:$bindcert");
	} else {
		push (@servers, "$host:$port:$binddn:$bindpwd:$bindcert");
	}
	return $i;
}

sub get_ldap_url
{
	my ($sidx, $conntype) = @_;
	my ($host, $port) = split(/:/, $servers[$sidx]);
	my ($shadowport);
	($port, $shadowport) = split (/=/, $port);
	my ($protocol, $ldapurl);

	if ($port eq 636 && $conntype eq "0" || $conntype =~ /SSL/i) {
		$protocol = ldaps;
	}
	else {
		$protocol = ldap;
	}
	my ($instance) = $allaliases { "$host:$port" };
	$instance = "$host:$port" if !$instance;
	if ($conntype eq "n/a") {
		$ldapurl = $instance;
	}
	else {
		$ldapurl = "<a href=\"$protocol://$host:$port/\">$instance</a>";
	}
	return $ldapurl;
}

sub to_decimal_csn
{
	my ($maxcsn) = @_;
	if (!$maxcsn || $maxcsn eq "") {
		return "none";
	}

	my ($tm, $seq, $masterid, $subseq) = unpack("a8 a4 a4 a4", $maxcsn);

	$tm = hex($tm);
	$seq = hex($seq);
	$masterid = hex($masterid);
	$subseq = hex($subseq);

	return "$tm $seq $masterid $subseq";
}

sub to_string_csn
{
	my ($rawcsn, $decimalcsn) = split(/:/, "@_");
	if (!$rawcsn || $rawcsn eq "") {
		return "none";
	}
	my ($tm, $seq, $masterid, $subseq) = split(/ /, $decimalcsn);
	my ($sec, $min, $hour, $mday, $mon, $year) = localtime($tm);
	$mon++;
	$year += 1900;
	foreach ($sec, $min, $hour, $mday, $mon) {
		$_ = "0".$_ if ($_ < 10);
	}
	my ($csnstr) = "$mon/$mday/$year $hour:$min:$sec";
	$csnstr .= " $seq $subseq" if ( $seq != 0 || $subseq != 0 );
	return "$rawcsn ($csnstr)";
}

sub get_color
{
	my ($lag_minute) = @_;
	$lag_minute /= 60;
	my ($color) = $allcolors { $colorkeys[0] };
	foreach (@colorkeys) {
		last if ($lag_minute < $_);
		$color = $allcolors {$_};
	}
	return $color;
}

# subroutine to remove escaped encoding

sub unescape 
{
	#my ($_) = @_;
	tr/+/ /;
	s/%(..)/pack("c",hex($1))/ge;
	$_;
}

sub print_html_header
{
	# print the HTML header

	print "Content-type: text/html\n\n";
	print "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 3.2//EN\"><html>\n";
	print "<head><title>Replication Status</title>\n";
	# print "<link type=text/css rel=stylesheet href=\"master-style.css\">\n";
	print "<style text/css>\n";
	print "Body, p, table, td, ul, li {color: #000000; font-family: Arial, Helvetica, sans-serif; font-size: 12px;}\n";
	print "A {color:blue; text-decoration: none;}\n";
	print "BODY {font-family: arial, helvetica, sans-serif}\n";
	print "P {font-family: arial, helvetica, sans-serif}\n";
	print "TH {font-weight: bold; font-family: arial, helvetica, sans-serif}\n";
	print "TD {font-family: arial, helvetica, sans-serif}\n";
	print ".bgColor1  {background-color: #003366;}\n";
	print ".bgColor4  {background-color: #cccccc;}\n";
	print ".bgColor5  {background-color: #999999;}\n";
	print ".bgColor9  {background-color: #336699;}\n";
	print ".bgColor13 {background-color: #ffffff;}\n";
	print ".bgColor16 {background-color: #6699cc;}\n";
	print ".text8  {color: #0099cc; font-size: 11px; font-weight: bold;}\n";
	print ".text28 {color: #ffcc33; font-size: 12px; font-weight: bold;}\n";
	print ".areatitle {font-weight: bold; color: #ffffff; font-family: arial, helvetica, sans-serif}\n";
	print ".page-title {font-weight: bold; font-size: larger; font-family: arial, helvetica, sans-serif}\n";
	print ".page-subtitle {font-weight: bold; font-family: arial, helvetica, sans-serif}\n";

	print "</style></head>\n<body class=bgColor4>\n";

	if ($opt_u) {
		print "<meta http-equiv=refresh content=$interval; URL=$opt_u>\n";
	}

	print "<table border=0 cellspacing=0 cellpadding=10 width=100% class=bgColor1>\n";
	print "<tr><td><font class=text8>$now</font></td>\n";
	print "<td align=center class=page-title><font color=#0099CC>";
	print "Directory Server Replication Status</font>\n";

	if ($opt_u) {
		print "<br><font class=text8>(This page updates every $interval seconds)</font>\n";
	}

	print "</td><td align=right valign=center width=25%><font class=text8>$version";
	print "</font></td></table>\n";
}

sub print_legend
{
	my ($nlegends) = $#colorkeys + 1;
	print "\n<center><p><font class=page-subtitle color=#0099cc>Time Lag Legend:</font><p>\n";
	print "<table cellpadding=6 cols=$nlegends width=40%>\n<tr>\n";
	my ($i, $j);
	for ($i = 0; $i < $nlegends - 1; $i++) {
		$j = $colorkeys[$i];
		print "\n<td bgcolor=$allcolors{$j}><center>within $colorkeys[$i+1] min</center></td>\n";
	}
	$j = $colorkeys[$i];
	print "\n<td bgcolor=$allcolors{$j}><center>over $colorkeys[$i] min</center></td>\n";
	print "\n<td bgcolor=red><center>server n/a</center></td>\n";
	print "</table></center>\n";
}

sub print_supplier_end
{
	print "</table>\n";
}

# given a string in generalized time format, convert to ascii time
sub format_z_time
{
  my $zstr = shift;
  return "n/a" if (! $zstr);
  my ($year, $mon, $day, $hour, $min, $sec) =
	($zstr =~ /(\d{4})(\d{2})(\d{2})(\d{2})(\d{2})(\d{2})/);
  my $time = timegm($sec, $min, $hour, $day, ($mon-1), $year);
  ($sec, $min, $hour, $day, $mon, $year) = localtime($time);
  $mon++;
  $year += 1900;
  foreach ($sec, $min, $hour, $day, $mon) {
	$_ = "0".$_ if ($_ < 10);
  }

  return "$mon/$day/$year $hour:$min:$sec";
}
