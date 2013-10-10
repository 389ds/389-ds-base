#!/usr/bin/env perl

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
# Copyright (C) 2013 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#

#
# Check for usage
#
use strict;
use warnings;
use warnings 'untie';
use Time::Local;
use IO::File;
use Getopt::Long;
use DB_File;
use sigtrap qw(die normal-signals);

Getopt::Long::Configure ("bundling");
Getopt::Long::Configure ("permute");

if ($#ARGV < 0){;
&displayUsage;
}

#######################################
#                                     #
# parse commandline switches          #
#                                     #
#######################################

my $file_count = 0;
my $arg_count = 0;
my $logversion = "8.0";
my $sizeCount = "20";
my $startFlag = 0;
my $startTime = 0;
my $endFlag = 0;
my $endTime = 0;
my $minEtime = 0;
my $reportStats = "";
my $dataLocation = "/tmp";
my $startTLSoid = "1.3.6.1.4.1.1466.20037";
my @statnames=qw(last last_str results srch add mod modrdn moddn cmp del abandon
                 conns sslconns bind anonbind unbind notesA notesU etime);
my $s_stats = new_stats_block( );
my $m_stats = new_stats_block( );
my $verb = "no";
my @excludeIP;
my $xi = 0;
my $bindReportDN;
my $usage = "";
my @latency;
# key is conn number - val is IP address
my %openConnection;
my @errorCode;
my @errtext;
my @errornum;
my @errornum2;
my $ds6x = "false";
my $connCodeCount = 0;
my %connList;
my %bindReport;
my @vlvconn;
my @vlvop;
my @fds;
my $fdds = 0;
my $reportBinds = "no";
my $rootDN = "";
my $needCleanup = 0;
my @scopeTxt = ("0 (base)", "1 (one)", "2 (subtree)");

GetOptions(
	'd|rootDN=s' => \$rootDN,
	'v|version' => sub { print "Access Log Analyzer v$logversion\n"; exit (0); },
	'V|verbose' => sub { $verb = "yes"; },
	'D|data=s' => \$dataLocation,
	'X|excludeIP=s' => \$excludeIP[$xi++],
	's|sizeLimit=s' => \$sizeCount,
	'S|startTime=s' => \$startTime,
	'E|endTime=s' => \$endTime,
	'T|minEtime=s' => \$minEtime,
	'B|bind=s' => sub { $reportBinds = "yes"; $bindReportDN=($_[1]) },
	'm|reportFileSecs=s' => sub { my ($opt,$value) = @_; $s_stats = new_stats_block($value); $reportStats = "-m";},
	'M|reportFileMins=s' =>  sub { my ($opt,$value) = @_; $m_stats = new_stats_block($value); $reportStats = "-M";},
	'h|help' => sub { displayUsage() },
	# usage options '-efcibaltnxgjuiryp'
	'e' => sub { $usage = $usage . "e"; },
	'f' => sub { $usage = $usage . "f"; },
	'c' => sub { $usage = $usage . "c"; },
	'i' => sub { $usage = $usage . "i"; },
	'b' => sub { $usage = $usage . "b"; },
	'a' => sub { $usage = $usage . "a"; },
	'l' => sub { $usage = $usage . "l"; },
	't' => sub { $usage = $usage . "t"; },
	'n' => sub { $usage = $usage . "n"; },
	'x' => sub { $usage = $usage . "x"; },
	'g' => sub { $usage = $usage . "g"; },
	'j' => sub { $usage = $usage . "j"; },
	'u' => sub { $usage = $usage . "u"; },
	'r' => sub { $usage = $usage . "r"; },
	'y' => sub { $usage = $usage . "y"; },
	'p' => sub { $usage = $usage . "p"; },
	'U' => sub { $usage = $usage . "U"; }
);

#
# setup the report Bind DN if any
#
if($reportBinds eq "yes"){
	$bindReportDN =~ tr/A-Z/a-z/;
	if($bindReportDN eq "all"){
		$bindReportDN = "";
	}
	if($bindReportDN eq "anonymous"){
		$bindReportDN = "Anonymous";
	}
}

#
# set the default root DN
#
if($rootDN eq ""){
	$rootDN = "cn=directory manager";
}

#
#  get the logs
#
my @files = ();
while($arg_count <= $#ARGV){
	$files[$file_count] = $ARGV[$arg_count];
	$file_count++;
	$arg_count++;
}

if($file_count == 0){
	if($reportStats){
		print "Usage error for option $reportStats, either the output file or access log is missing!\n\n";
	} else {
		print "There are no access logs specified!\n\n";
	}
	exit 1;
}

if ($sizeCount eq "all"){$sizeCount = "100000";}

#######################################
#                                     #
# Initialize Hashes and variables     #
#                                     #
#######################################

print "\nAccess Log Analyzer $logversion\n";
print "\nCommand: logconv.pl @ARGV\n\n";

my $rootDNBindCount = 0;
my $anonymousBindCount = 0;
my $unindexedSrchCountNotesA = 0;
my $unindexedSrchCountNotesU = 0;
my $vlvNotesACount= 0;
my $vlvNotesUCount= 0;
my $srchCount = 0;
my $fdTaken = 0;
my $fdReturned = 0;
my $highestFdTaken = 0;
my $unbindCount = 0;
my $cmpCount = 0;
my $modCount = 0;
my $delCount = 0;
my $addCount = 0;
my $modrdnCount = 0;
my $abandonCount = 0;
my $extopCount = 0;
my $vlvCount = 0;
my $errorCount = 0;
my $proxiedAuthCount = 0;
my $serverRestartCount = 0;
my $resourceUnavailCount = 0;
my $brokenPipeCount = 0;
my $v2BindCount = 0;
my $v3BindCount = 0;
my $vlvSortCount = 0;
my $connResetByPeerCount = 0;
my $isVlvNotes = 0;
my $successCount = 0;
my $sslCount = 0;
my $sslClientBindCount = 0;
my $sslClientFailedCount = 0;
my $objectclassTopCount= 0;
my $pagedSearchCount = 0;
my $bindCount = 0;
my $filterCount = 0;
my $baseCount = 0;
my $scopeCount = 0;
my $allOps = 0;
my $allResults = 0;
my $badPwdCount = 0;
my $saslBindCount = 0;
my $internalOpCount = 0;
my $entryOpCount = 0;
my $referralCount = 0;
my $anyAttrs = 0;
my $persistentSrchCount = 0;
my $maxBerSizeCount = 0;
my $connectionCount = 0;
my $timerange = 0;
my $simConnection = 0;
my $maxsimConnection = 0;
my $firstFile = 1;
my $elapsedDays = 0;
my $logCount = 0;
my $startTLSCount = 0;
my $ldapiCount = 0;
my $autobindCount = 0;
my $limit = 25000; # number of lines processed to trigger output

my @removefiles = ();

my @conncodes = qw(A1 B1 B4 T1 T2 B2 B3 R1 P1 P2 U1);
my %conn = ();
map {$conn{$_} = $_} @conncodes;

# hash db-backed hashes
my @hashnames = qw(attr rc src rsrc excount conn_hash ip_hash conncount nentries
                   filter base ds6xbadpwd saslmech bindlist etime oid
                   start_time_of_connection end_time_of_connection
                   notesa_conn_op notesu_conn_op etime_conn_op nentries_conn_op
                   optype_conn_op time_conn_op srch_conn_op del_conn_op mod_conn_op
                   mdn_conn_op cmp_conn_op bind_conn_op unbind_conn_op ext_conn_op
                   abandon_conn_op badpwd_conn_op);
# need per connection code ip address counts - so use a hash table
# for each connection code - key is ip, val is count
push @hashnames, @conncodes;
my $hashes = openHashFiles($dataLocation, @hashnames);

$needCleanup = 1;

my @err;
$err[0] = "Successful Operations\n";
$err[1] = "Operations Error(s)\n";
$err[2] = "Protocal Errors\n";
$err[3] = "Time Limit Exceeded\n";
$err[4] = "Size Limit Exceeded\n";
$err[5] = "Compare False\n";
$err[6] = "Compare True\n";
$err[7] = "Strong Authentication Not Supported\n";
$err[8] = "Strong Authentication Required\n";
$err[9] = "Partial Results\n";
$err[10] = "Referral Received\n";
$err[11] = "Administrative Limit Exceeded (Look Through Limit)\n";
$err[12] = "Unavailable Critical Extension\n";
$err[13] = "Confidentiality Required\n";
$err[14] = "SASL Bind in Progress\n";
$err[16] = "No Such Attribute\n";
$err[17] = "Undefined Type\n";
$err[18] = "Inappropriate Matching\n";
$err[19] = "Constraint Violation\n";
$err[20] = "Type or Value Exists\n";
$err[21] = "Invalid Syntax\n";
$err[32] = "No Such Object\n";
$err[33] = "Alias Problem\n";
$err[34] = "Invalid DN Syntax\n";
$err[35] = "Is Leaf\n";
$err[36] = "Alias Deref Problem\n";
$err[48] = "Inappropriate Authentication (No password presented, etc)\n";
$err[49] = "Invalid Credentials (Bad Password)\n";
$err[50] = "Insufficent (write) Privledges\n";
$err[51] = "Busy\n";
$err[52] = "Unavailable\n";
$err[53] = "Unwilling To Perform\n";
$err[54] = "Loop Detected\n";
$err[60] = "Sort Control Missing\n";
$err[61] = "Index Range Error\n";
$err[64] = "Naming Violation\n";
$err[65] = "Objectclass Violation\n";
$err[66] = "Not Allowed on Non Leaf\n";
$err[67] = "Not Allowed on RDN\n";
$err[68] = "Already Exists\n";
$err[69] = "No Objectclass Mods\n";
$err[70] = "Results Too Large\n";
$err[71] = "Effect Multiple DSA's\n";
$err[80] = "Other :-)\n";
$err[81] = "Server Down\n";
$err[82] = "Local Error\n";
$err[83] = "Encoding Error\n";
$err[84] = "Decoding Error\n";
$err[85] = "Timeout\n";
$err[86] = "Authentication Unknown\n";
$err[87] = "Filter Error\n";
$err[88] = "User Canceled\n";
$err[89] = "Parameter Error\n";
$err[90] = "No Memory\n";
$err[91] = "Connect Error\n";
$err[92] = "Not Supported\n";
$err[93] = "Control Not Found\n";
$err[94] = "No Results Returned\n";
$err[95] = "More Results To Return\n";
$err[96] = "Client Loop\n";
$err[97] = "Referral Limit Exceeded\n";

my %connmsg;
$connmsg{"A1"} = "Client Aborted Connections";
$connmsg{"B1"} = "Bad Ber Tag Encountered";
$connmsg{"B4"} = "Server failed to flush data (response) back to Client";
$connmsg{"T1"} = "Idle Timeout Exceeded";
$connmsg{"T2"} = "IO Block Timeout Exceeded or NTSSL Timeout";
$connmsg{"B2"} = "Ber Too Big";
$connmsg{"B3"} = "Ber Peek";
$connmsg{"R1"} = "Revents";
$connmsg{"P1"} = "Plugin";
$connmsg{"P2"} = "Poll";
$connmsg{"U1"} = "Cleanly Closed Connections";

my %monthname = (
	"Jan" => 0,
	"Feb" => 1,
	"Mar" => 2,
	"Apr" => 3,
	"May" => 4,
	"Jun" => 5,
	"Jul" => 6,
	"Aug" => 7,
	"Sep" => 8,
	"Oct" => 9,
	"Nov" => 10,
	"Dec" => 11,

);

my $linesProcessed;
my $lineBlockCount;
my $cursize = 0;
sub statusreport {
	if ($lineBlockCount > $limit) {
		my $curpos = tell(LOG);
		my $percent = $curpos/$cursize*100.0;
		print sprintf "%10d Lines Processed     %12d of %12d bytes (%.3f%%)\n",--$linesProcessed,$curpos,$cursize,$percent;
		$lineBlockCount = 0;
	}
}

##########################################
#                                        #
#         Parse Access Logs              #
#                                        # 
##########################################

if ($files[$#files] =~ m/access.rotationinfo/) {  $file_count--; }

print "Processing $file_count Access Log(s)...\n\n";

#print "Filename\t\t\t   Total Lines\n";
#print "--------------------------------------------------\n";

my $skipFirstFile = 0;
if ($file_count > 1 && $files[0] =~ /\/access$/){
        $files[$file_count] = $files[0];
        $file_count++;
        $skipFirstFile = 1;
}
$logCount = $file_count;

my $logline;
my $totalLineCount = 0;

for (my $count=0; $count < $file_count; $count++){
	# we moved access to the end of the list, so if its the first file skip it
        if($file_count > 1 && $count == 0 && $skipFirstFile == 1){
                next;
        }
        $linesProcessed = 0; $lineBlockCount = 0;
	$logCount--;
		my $logCountStr;
	if($logCount < 10 ){ 
		# add a zero for formatting purposes
		$logCountStr = "0" . $logCount;
	} else {
		$logCountStr = $logCount;
	}
		my ($dev,$ino,$mode,$nlink,$uid,$gid,$rdev,$atime,$mtime,$ctime,$blksize,$blocks);
		($dev,$ino,$mode,$nlink,$uid,$gid,$rdev,$cursize,
		 $atime,$mtime,$ctime,$blksize,$blocks) = stat($files[$count]);
		print sprintf "[%s] %-30s\tsize (bytes): %12s\n",$logCountStr, $files[$count], $cursize;
	
	open(LOG,"$files[$count]") or do { openFailed($!, $files[$count]) };
	my $firstline = "yes";
	while(<LOG>){
		unless ($endFlag) {
			if ($firstline eq "yes"){
				if (/^\[/) {
                        		$logline = $_;
                        		$firstline = "no";
				}
				$linesProcessed++;$lineBlockCount++;
                	} elsif (/^\[/ && $firstline eq "no"){
                         	&parseLine();
                         	$logline = $_;
                	} else {
                        	$logline = $logline . $_;
                        	$logline =~ s/\n//;
                	}
		}
	}
	&parseLine();
	close (LOG);
	print_stats_block( $s_stats );
	print_stats_block( $m_stats );
	$totalLineCount = $totalLineCount + $linesProcessed;
		statusreport();
}

print "\n\nTotal Log Lines Analysed:  " . ($totalLineCount - 1) . "\n";

$allOps = $srchCount + $modCount + $addCount + $cmpCount + $delCount + $modrdnCount + $bindCount + $extopCount + $abandonCount + $vlvCount;

##################################################################
#                                                                #
#  Calculate the total elapsed time of the processed access logs #
#                                                                #
##################################################################

# if we are using startTime & endTime then we need to clean it up for our processing

my $start;
if($startTime){
	if ($start =~ / *([0-9a-z:\/]+)/i){$start=$1;}
}
my $end;
if($endTime){
	if ($end =~ / *([0-9a-z:\/]+)/i){$end =$1;}
}

#
# Get the start time in seconds
#  

my $logStart = $start;
my $logDate;
my @dateComps;
my ($timeMonth, $timeDay, $timeYear, $dateTotal);
if ($logStart =~ / *([0-9A-Z\/]+)/i ){
        $logDate = $1;
        @dateComps = split /\//, $logDate;

        $timeMonth = 1 +$monthname{$dateComps[1]};
        $timeMonth = $timeMonth * 3600 *24 * 30; 
        $timeDay= $dateComps[0] * 3600 *24;
        $timeYear = $dateComps[2] *365 * 3600 * 24;
        $dateTotal = $timeMonth + $timeDay + $timeYear;
}

my $logTime;
my @timeComps;
my ($timeHour, $timeMinute, $timeSecond, $timeTotal);
if ($logStart =~ / *(:[0-9:]+)/i ){
        $logTime = $1;
        @timeComps = split /:/, $logTime;

        $timeHour = $timeComps[1] * 3600;
        $timeMinute = $timeComps[2] *60;
        $timeSecond = $timeComps[3];
        $timeTotal = $timeHour + $timeMinute + $timeSecond;
}

my $startTotal = $timeTotal + $dateTotal;

#
# Get the end time in seconds
#

my $logEnd = $end;
my ($endDay, $endMonth, $endYear, $endTotal);
if ($logEnd =~ / *([0-9A-Z\/]+)/i ){
        $logDate = $1;
        @dateComps = split /\//, $logDate;

        $endDay = $dateComps[0] *3600 * 24;
        $endMonth = 1 + $monthname{$dateComps[1]};
        $endMonth = $endMonth * 3600 * 24 * 30;
        $endYear = $dateComps[2] *365 * 3600 * 24 ;
        $dateTotal = $endDay + $endMonth + $endYear;
}

my ($endHour, $endMinute, $endSecond);
if ($logEnd =~ / *(:[0-9:]+)/i ){
        $logTime = $1;
        @timeComps = split /:/, $logTime;

        $endHour = $timeComps[1] * 3600;
        $endMinute = $timeComps[2] *60;
        $endSecond = $timeComps[3];
        $timeTotal = $endHour + $endMinute + $endSecond;	
}

$endTotal = $timeTotal +  $dateTotal;

#
# Tally the numbers
#
my $totalTimeInSecs = $endTotal - $startTotal;
my $remainingTimeInSecs = $totalTimeInSecs;

#
# Calculate the elapsed time
#

# days
while(($remainingTimeInSecs - 86400) > 0){
	$elapsedDays++;
	$remainingTimeInSecs =  $remainingTimeInSecs - 86400;

}

# hours
my $elapsedHours = 0;
while(($remainingTimeInSecs - 3600) > 0){
	$elapsedHours++;
	$remainingTimeInSecs = $remainingTimeInSecs - 3600;
}

# minutes
my $elapsedMinutes = 0;
while($remainingTimeInSecs - 60 > 0){
	$elapsedMinutes++;
	$remainingTimeInSecs = $remainingTimeInSecs - 60;
}

# seconds
my $elapsedSeconds = $remainingTimeInSecs;

#####################################
#                                   #
#     Display Basic Results         #
#                                   #
#####################################


print "\n\n----------- Access Log Output ------------\n";
print "\nStart of Logs:    $start\n";
print "End of Logs:      $end\n\n";

if($elapsedDays eq "0"){
        print "Processed Log Time:  $elapsedHours Hours, $elapsedMinutes Minutes, $elapsedSeconds Seconds\n\n";
} else {
        print "Processed Log Time:  $elapsedDays Days, $elapsedHours Hours, $elapsedMinutes Minutes, $elapsedSeconds Seconds\n\n";
}

#
#  Check here if we are producing any unqiue reports
#

if($reportBinds eq "yes"){
	&displayBindReport();
}

#
# Continue with standard report
#

print "Restarts:                     $serverRestartCount\n";
print "Total Connections:            $connectionCount\n";
print " - StartTLS Connections:      $startTLSCount\n";
print " - LDAPS Connections:         $sslCount\n";
print " - LDAPI Connections:         $ldapiCount\n";
print "Peak Concurrent Connections:  $maxsimConnection\n";
print "Total Operations:             $allOps\n";
print "Total Results:                $allResults\n";
my ($perf, $tmp);
if ($allOps ne "0"){
 print sprintf "Overall Performance:          %.1f%%\n\n" , ($perf = ($tmp = ($allResults / $allOps)*100) > 100 ? 100.0 : $tmp) ;
 }
else {
 print "Overall Performance:          No Operations to evaluate\n\n";
}

my $searchStat = sprintf "(%.2f/sec)  (%.2f/min)\n",($srchCount / $totalTimeInSecs), $srchCount / ($totalTimeInSecs/60);
my $modStat = sprintf "(%.2f/sec)  (%.2f/min)\n",$modCount / $totalTimeInSecs, $modCount/($totalTimeInSecs/60);
my $addStat = sprintf "(%.2f/sec)  (%.2f/min)\n",$addCount/$totalTimeInSecs, $addCount/($totalTimeInSecs/60);
my $deleteStat = sprintf "(%.2f/sec)  (%.2f/min)\n",$delCount/$totalTimeInSecs, $delCount/($totalTimeInSecs/60);
my $modrdnStat = sprintf "(%.2f/sec)  (%.2f/min)\n",$modrdnCount/$totalTimeInSecs, $modrdnCount/($totalTimeInSecs/60);
my $compareStat = sprintf "(%.2f/sec)  (%.2f/min)\n",$cmpCount/$totalTimeInSecs, $cmpCount/($totalTimeInSecs/60);
my $bindCountStat = sprintf "(%.2f/sec)  (%.2f/min)\n",$bindCount/$totalTimeInSecs, $bindCount/($totalTimeInSecs/60);

format STDOUT =
Searches:                     @<<<<<<<<<<<< @<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
                              $srchCount,   $searchStat,
Modifications:                @<<<<<<<<<<<< @<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
                              $modCount,    $modStat,
Adds:                         @<<<<<<<<<<<< @<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
                              $addCount,    $addStat,
Deletes:                      @<<<<<<<<<<<< @<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
                              $delCount,    $deleteStat,
Mod RDNs:                     @<<<<<<<<<<<< @<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
                              $modrdnCount, $modrdnStat,
Compares:                     @<<<<<<<<<<<< @<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
                              $cmpCount,    $compareStat,
Binds:                        @<<<<<<<<<<<< @<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
                              $bindCount,   $bindCountStat,
.
write STDOUT;

# format/write messes up emacs autoformatting - inserting this dummy function
# fixes that
sub dummy {
	my $bar = shift;
}

print "\n";
print "Proxied Auth Operations:      $proxiedAuthCount\n";
print "Persistent Searches:          $persistentSrchCount\n";
print "Internal Operations:          $internalOpCount\n";
print "Entry Operations:             $entryOpCount\n";
print "Extended Operations:          $extopCount\n";
print "Abandoned Requests:           $abandonCount\n";
print "Smart Referrals Received:     $referralCount\n";
print "\n";
print "VLV Operations:               $vlvCount\n";
print "VLV Unindexed Searches:       $vlvNotesACount\n";
print "VLV Unindexed Components:     $vlvNotesUCount\n";
print "SORT Operations:              $vlvSortCount\n";
print "\n";
print "Entire Search Base Queries:   $objectclassTopCount\n";
print "Paged Searches:               $pagedSearchCount\n";
print "Unindexed Searches:           $unindexedSrchCountNotesA\n";
print "Unindexed Components:         $unindexedSrchCountNotesU\n";

if ($verb eq "yes" || $usage =~ /u/ || $usage =~ /U/){
	if ($unindexedSrchCountNotesA > 0){
		my $conn_hash = $hashes->{conn_hash};
		my $notesa_conn_op = $hashes->{notesa_conn_op};
		my $time_conn_op = $hashes->{time_conn_op};
		my $etime_conn_op = $hashes->{etime_conn_op};
		my $nentries_conn_op = $hashes->{nentries_conn_op};
		my $base_conn_op = $hashes->{base_conn_op};
		my $scope_conn_op = $hashes->{scope_conn_op};
		my $filter_conn_op = $hashes->{filter_conn_op};

		my $notesCount = 1;
		my $unindexedIp;
		my %uniqFilt = (); # hash of unique filters
		while (my ($srcnt_conn_op, $count) = each %{$notesa_conn_op}) {
			if ($verb eq "yes" || $usage =~ /u/) {
				my ($srvRstCnt, $conn, $op) = split(",", $srcnt_conn_op);
				my $unindexedIp = getIPfromConn($conn, $srvRstCnt);
				print "\n  Unindexed Search #".$notesCount." (notes=A)\n";
				print "  -  Date/Time:             $time_conn_op->{$srcnt_conn_op}\n";
				print "  -  Connection Number:     $conn\n";
				print "  -  Operation Number:      $op\n";
				print "  -  Etime:                 $etime_conn_op->{$srcnt_conn_op}\n";
				print "  -  Nentries:              $nentries_conn_op->{$srcnt_conn_op}\n";
				print "  -  IP Address:            $unindexedIp\n";
				if (exists($base_conn_op->{$srcnt_conn_op}) && defined($base_conn_op->{$srcnt_conn_op})) {
					print "  -  Search Base:           $base_conn_op->{$srcnt_conn_op}\n";
				}
				if (exists($scope_conn_op->{$srcnt_conn_op}) && defined($scope_conn_op->{$srcnt_conn_op})) {
					print "  -  Search Scope:          $scope_conn_op->{$srcnt_conn_op}\n";
				}
			}
			if (exists($filter_conn_op->{$srcnt_conn_op}) && defined($filter_conn_op->{$srcnt_conn_op})) {
				if ($verb eq "yes" || $usage =~ /u/) {
					print "  -  Search Filter:         $filter_conn_op->{$srcnt_conn_op}\n";
				}
				$uniqFilt{$filter_conn_op->{$srcnt_conn_op}}++;
			}
			$notesCount++;
		}
		if ($usage =~ /U/) {
			print "\n  Unindexed Search Summary - $notesCount total unindexed searches\n";
			foreach my $key (sort { $uniqFilt{$b} <=> $uniqFilt{$a} } keys %uniqFilt) {
				if ($uniqFilt{$key} > 0) {
					printf "  -  Number of times used unindexed: %10d Filter: $key\n", $uniqFilt{$key};
				}
			}
		}
	}
	if ($unindexedSrchCountNotesU > 0){
		my $conn_hash = $hashes->{conn_hash};
		my $notesu_conn_op = $hashes->{notesu_conn_op};
		my $time_conn_op = $hashes->{time_conn_op};
		my $etime_conn_op = $hashes->{etime_conn_op};
		my $nentries_conn_op = $hashes->{nentries_conn_op};
		my $base_conn_op = $hashes->{base_conn_op};
		my $scope_conn_op = $hashes->{scope_conn_op};
		my $filter_conn_op = $hashes->{filter_conn_op};

		my $notesCount = 1;
		my $unindexedIp;
		my %uniqFilt = (); # hash of unique filters
		while (my ($srcnt_conn_op, $count) = each %{$notesu_conn_op}) {
			if ($verb eq "yes" || $usage =~ /u/) {
				my ($srvRstCnt, $conn, $op) = split(",", $srcnt_conn_op);
				$unindexedIp = getIPfromConn($conn, $srvRstCnt);
				print "\n  Unindexed Component #".$notesCount." (notes=U)\n";
				print "  -  Date/Time:             $time_conn_op->{$srcnt_conn_op}\n";
				print "  -  Connection Number:     $conn\n";
				print "  -  Operation Number:      $op\n";
				print "  -  Etime:                 $etime_conn_op->{$srcnt_conn_op}\n";
				print "  -  Nentries:              $nentries_conn_op->{$srcnt_conn_op}\n";
				print "  -  IP Address:            $unindexedIp\n";
				if (exists($base_conn_op->{$srcnt_conn_op}) && defined($base_conn_op->{$srcnt_conn_op})) {
					print "  -  Search Base:           $base_conn_op->{$srcnt_conn_op}\n";
				}
				if (exists($scope_conn_op->{$srcnt_conn_op}) && defined($scope_conn_op->{$srcnt_conn_op})) {
					print "  -  Search Scope:          $scope_conn_op->{$srcnt_conn_op}\n";
				}
			}
			if (exists($filter_conn_op->{$srcnt_conn_op}) && defined($filter_conn_op->{$srcnt_conn_op})) {
				if ($verb eq "yes" || $usage =~ /u/) {
					print "  -  Search Filter:         $filter_conn_op->{$srcnt_conn_op}\n";
				}
				$uniqFilt{$filter_conn_op->{$srcnt_conn_op}}++;
			}
			$notesCount++;
		}
		if ($usage =~ /U/) {
			print "\n  Unindexed Component Summary - $notesCount total unindexed components\n";
			foreach my $key (sort { $uniqFilt{$b} <=> $uniqFilt{$a} } keys %uniqFilt) {
				if ($uniqFilt{$key} > 0) {
					printf "  -  Number of times used unindexed: %10d Filter: $key\n", $uniqFilt{$key};
				}
			}
		}
	}
} # end of unindexed search report

print "\n";
print "FDs Taken:                    $fdTaken\n";
print "FDs Returned:                 $fdReturned\n";
print "Highest FD Taken:             $highestFdTaken\n\n";
print "Broken Pipes:                 $brokenPipeCount\n";
if ($brokenPipeCount > 0){
	my $rc = $hashes->{rc};
	my @etext;
	foreach my $key (sort { $rc->{$b} <=> $rc->{$a} } keys %{$rc}) {
		if ($rc->{$key} > 0){
			if ($conn{$key} eq ""){$conn{$key} = "**Unknown**";}
			push @etext, sprintf "     -  %-4s (%2s) %-40s\n",$rc->{$key},$conn{$key},$connmsg{$key};
		}
	}
	print @etext;
	print "\n";
}

print "Connections Reset By Peer:    $connResetByPeerCount\n";
if ($connResetByPeerCount > 0){
	my $src = $hashes->{src};
	my @retext;
	foreach my $key (sort { $src->{$b} <=> $src->{$a} } keys %{$src}) {
		if ($src->{$key} > 0){
			if ($conn{$key} eq ""){$conn{$key} = "**Unknown**";}
			push @retext, sprintf "     -  %-4s (%2s) %-40s\n",$src->{$key},$conn{$key},$connmsg{$key};
		}
	}
	print @retext;
	print "\n";
}

print "Resource Unavailable:         $resourceUnavailCount\n";
if ($resourceUnavailCount > 0){
	my $rsrc = $hashes->{rsrc};
	my @rtext;
	foreach my $key (sort { $rsrc->{$b} <=> $rsrc->{$a} } keys %{$rsrc}) {
		if ($rsrc->{$key} > 0){
			if ($conn{$key} eq ""){$conn{$key} = "**Resource Issue**";}
			push @rtext, sprintf "     -  %-4s (%2s) %-40s\n",$rsrc->{$key},$conn{$key},$connmsg{$key};
		}
	}
	print @rtext;
}
print "Max BER Size Exceeded:        $maxBerSizeCount\n";
print "\n";
print "Binds:                        $bindCount\n";
print "Unbinds:                      $unbindCount\n";
print " - LDAP v2 Binds:             $v2BindCount\n";
print " - LDAP v3 Binds:             $v3BindCount\n";
print " - AUTOBINDs:                 $autobindCount\n";
print " - SSL Client Binds:          $sslClientBindCount\n";
print " - Failed SSL Client Binds:   $sslClientFailedCount\n";
print " - SASL Binds:                $saslBindCount\n";
if ($saslBindCount > 0){
	my $saslmech = $hashes->{saslmech};
	foreach my $saslb ( sort {$saslmech->{$b} <=> $saslmech->{$a} } (keys %{$saslmech}) ){
		printf "    %-4s  %-12s\n",$saslmech->{$saslb}, $saslb;
	}
}

print " - Directory Manager Binds:   $rootDNBindCount\n";
print " - Anonymous Binds:           $anonymousBindCount\n";
my $otherBindCount = $bindCount -($rootDNBindCount + $anonymousBindCount);
print " - Other Binds:               $otherBindCount\n\n";

##########################################################################
#                       Verbose Logging Section                          #
##########################################################################

###################################
#                                 #
#   Display Connection Latency    #
#                                 #
###################################

if ($verb eq "yes" || $usage =~ /y/){
	print "\n\n----- Connection Latency Details -----\n\n";
	print " (in seconds)\t\t<=1\t2\t3\t4-5\t6-10\t11-15\t>15\n";
	print " --------------------------------------------------------------------------\n";
	print " (# of connections)\t";
	for (my $i=0; $i <=$#latency; $i++) {
		if (defined($latency[$i])) {
			print "$latency[$i]\t";
		}
	}
}

###################################
#                                 #
#    Display Open Connections     #
#                                 #
###################################

if ($verb eq "yes" || $usage =~ /p/){
	if (%openConnection){
		print "\n\n----- Current Open Connection IDs ----- \n\n";
		while (my ($connid, $ip) = each %openConnection) {
			print "Conn Number:  $connid ($ip)\n";
		}
	}
}

###################################
#                                 #
#      Display Error Codes        #
#                                 #
###################################

if ($usage =~ /e/i || $verb eq "yes"){
	print "\n\n----- Errors -----\n";

	for (my $i = 0; $i<98; $i++){
		if (defined($err[$i]) && $err[$i] ne "" && defined($errorCode[$i]) && $errorCode[$i] >0) {
			push @errtext, sprintf "%-8s       %12s    %-25s","err=$i",$errorCode[$i],$err[$i];
		}
	}

	for (my $i = 0; $i < $#errtext; $i++){
		for (my $ii = 0; $ii < $#errtext; $ii++){
			my $yy="0";
			my $zz="0";
			while ($errtext[$ii] =~ /(\w+)\s/g){
				$errornum[$yy]="$1";
				$yy++;
			}
			while ($errtext[$ii+1] =~ /(\w+)\s/g){
				$errornum2[$zz]="$1";
				$zz++;
			}
			if ($errornum2[1] > $errornum[1]){
				$tmp = $errtext[$ii];
				$errtext[$ii] = $errtext[$ii+1];
				$errtext[$ii+1] = $tmp;
			}
		}
	}
	for (my $i = 0; $i <= $#errtext; $i++){
		$errtext[$i] =~ s/\n//g;
		print  "\n" . $errtext[$i];
	}
}

####################################
#			   #
#     Print Failed Logins          #
#				   #
####################################

if ($verb eq "yes" || $usage =~ /f/ ){
	if ($badPwdCount > 0){
		print "\n\n----- Top $sizeCount Failed Logins ------\n\n";

		if ($ds6x eq "true"){
			my $ds6xbadpwd = $hashes->{ds6xbadpwd};
			my $ds6loop = 0;
			foreach my $ds6bp (sort { $ds6xbadpwd->{$b} <=> $ds6xbadpwd->{$a} } keys %{$ds6xbadpwd}) {
				if ($ds6loop > $sizeCount){ last; }
				printf "%-4s        %-40s\n", $ds6xbadpwd->{$ds6bp}, $ds6bp;
				$ds6loop++;
			}
		} else {
			# key is conn,op - val is binddn
			my $bind_conn_op = $hashes->{bind_conn_op};
			# key is conn,op - val is count
			my $badpwd_conn_op = $hashes->{badpwd_conn_op};
			# key is binddn - val is count
			my %badPassword = ();
			my %badPasswordIp = ();
			while (my ($srcnt_conn_op, $ip) = each %{$badpwd_conn_op}) {
				my ($srvRstCnt, $conn, $op) = split(",", $srcnt_conn_op);
				if (exists($bind_conn_op->{$srcnt_conn_op}) && defined($bind_conn_op->{$srcnt_conn_op})) {
					my $binddn = $bind_conn_op->{$srcnt_conn_op};
					$badPassword{$binddn}++;
					$badPasswordIp{$ip}++;
				}
			}
			# sort the new hash of $badPassword{}
			my $bpTotal = 0;
			my $bpCount = 0;
			foreach my $badpw (sort {$badPassword{$b} <=> $badPassword{$a} } keys %badPassword){
				if ($bpCount > $sizeCount){ last;}
				$bpCount++;
				$bpTotal = $bpTotal + $badPassword{"$badpw"};
				printf "%-4s        %-40s\n", $badPassword{"$badpw"}, $badpw;
			}
			print "\nFrom the IP address(s) :\n\n";
			$bpCount = 0;
			foreach my $ip (sort {$badPasswordIp{$b} <=> $badPasswordIp{$a} } keys %badPasswordIp){
				if ($bpCount > $sizeCount){ last;}
				$bpCount++;
				printf "%-4s        %-16s\n", $badPasswordIp{$ip}, $ip;
			}
			if ($bpTotal > $badPwdCount){
				print "\n** Warning : Wrongly reported failed login attempts : ". ($bpTotal - $badPwdCount) . "\n";
			}
		}  # this ends the if $ds6x = true
	}
}

####################################
#                                  #
#     Print Connection Codes       #
#                                  #
####################################


if ($connCodeCount > 0){
	if ($usage =~ /c/i || $verb eq "yes"){
		print "\n\n----- Total Connection Codes -----\n\n";
		my $conncount = $hashes->{conncount};
		my @conntext;
		foreach my $key (sort { $conncount->{$b} <=> $conncount->{$a} } keys %{$conncount}) {
			if ($conncount->{$key} > 0){
				push @conntext, sprintf "%-4s %6s   %-40s\n",$key,$conncount->{$key},$connmsg{ $key };
			}
		}
		print @conntext;
	}
}

########################################
#                                      #
#  Gather and Process all unique IPs   #
#                                      #
########################################

if ($usage =~ /i/i || $verb eq "yes"){
	my $ip_hash = $hashes->{ip_hash};
	my $exCount = $hashes->{excount};
	my @ipkeys = keys %{$ip_hash};
	my @exxCount = keys %${exCount};
	my $ip_count = ($#ipkeys + 1)-($#exxCount + 1);
	my $ccount = 0;
	if ($ip_count > 0){
		print "\n\n----- Top $sizeCount Clients -----\n\n";
		print "Number of Clients:  $ip_count\n\n";
		foreach my $key (sort { $ip_hash->{$b} <=> $ip_hash->{$a} } @ipkeys) {
			my $exc = "no";
			if ($ccount > $sizeCount){ last;}
			$ccount++;
			for (my $xxx =0; $xxx < $#excludeIP; $xxx++){
				if ($excludeIP[$xxx] eq $key){$exc = "yes";}
			}
			if ($exc ne "yes"){
				if ($ip_hash->{ $key } eq ""){$ip_hash->{ $key } = "?";}
				printf "[%s] Client: %s\n",$ccount, $key;
				printf "%10s - Connections\n", $ip_hash->{ $key };
				my %counts;
				map { $counts{$_} = $hashes->{$_}->{$key} if (defined($hashes->{$_}->{$key})) } @conncodes;
				foreach my $code (sort { $counts{$b} <=> $counts{$a} } keys %counts) {
					if ($code eq 'count' ) { next; }
					printf "%10s - %s (%s)\n", $counts{ $code }, $code, $connmsg{ $code };
				}
				print "\n";
			}
		}
	}
}

###################################
#                                 #
#   Gather All unique Bind DN's   #
#                                 #
###################################

if ($usage =~ /b/i || $verb eq "yes"){
	my $bindlist = $hashes->{bindlist};
	my @bindkeys = keys %{$bindlist};
	my $bind_count = $#bindkeys + 1;
	if ($bind_count > 0){
		print "\n\n----- Top $sizeCount Bind DN's -----\n\n";
		print "Number of Unique Bind DN's: $bind_count\n\n";
		my $bindcount = 0;
		foreach my $dn (sort { $bindlist->{$b} <=> $bindlist->{$a} } @bindkeys) {
			if ($bindcount < $sizeCount){
				printf "%-8s        %-40s\n", $bindlist->{ $dn },$dn;
			} else {
				last;
			}
			$bindcount++;
		}
	}
}

#########################################
#					#
#  Gather and process search bases      #
#					#
#########################################

if ($usage =~ /a/i || $verb eq "yes"){
	my $base = $hashes->{base};
	my @basekeys = keys %{$base};
	my $base_count = $#basekeys + 1;
	if ($base_count > 0){
		print "\n\n----- Top $sizeCount Search Bases -----\n\n";
		print "Number of Unique Search Bases: $base_count\n\n";
		my $basecount = 0;
		foreach my $bas (sort { $base->{$b} <=> $base->{$a} } @basekeys) {
			if ($basecount < $sizeCount){
				printf "%-8s        %-40s\n", $base->{ $bas },$bas;
			} else {
				last;
			}
			$basecount++;
		}
	}
}

#########################################
#                                       #
#   Gather and process search filters   #
#                                       #
#########################################

if ($usage =~ /l/ || $verb eq "yes"){
	my $filter = $hashes->{filter};
	my @filterkeys = keys %{$filter};
	my $filter_count = $#filterkeys + 1;
	if ($filter_count > 0){
		print "\n\n----- Top $sizeCount Search Filters -----\n";
		print "\nNumber of Unique Search Filters: $filter_count\n\n";
		my $filtercount = 0;
		foreach my $filt (sort { $filter->{$b} <=> $filter->{$a} } @filterkeys){
			if ($filtercount < $sizeCount){
				printf "%-8s        %-40s\n", $filter->{$filt}, $filt;
			} else {
				last;
			}
			$filtercount++;
		}
	}
}

#########################################
#                                       #
# Gather and Process the unique etimes  #
#                                       #
#########################################

my $first;
if ($usage =~ /t/i || $verb eq "yes"){
	my $etime = $hashes->{etime};
	my @ekeys = keys %{$etime};
	#
	# print most often etimes
	#
	print "\n\n----- Top $sizeCount Most Frequent etimes -----\n\n";
	my $eloop = 0;
	my $retime = 0;
	foreach my $et (sort { $etime->{$b} <=> $etime->{$a} } @ekeys) {
		if ($eloop == $sizeCount) { last; }
		if ($retime ne "2"){
			$first = $et;
			$retime = "2";
		}
		printf "%-8s        %-12s\n", $etime->{ $et }, "etime=$et";
		$eloop++;
	}
	#
	# print longest etimes
	#
	print "\n\n----- Top $sizeCount Longest etimes -----\n\n";
	$eloop = 0;
	foreach my $et (sort { $b <=> $a } @ekeys) {
		if ($eloop == $sizeCount) { last; }
		printf "%-12s    %-10s\n","etime=$et",$etime->{ $et };
		$eloop++;
	}
}

#######################################
#                                     #
# Gather and Process unique nentries  #
#                                     #
#######################################


if ($usage =~ /n/i || $verb eq "yes"){
	my $nentries = $hashes->{nentries};
	my @nkeys = keys %{$nentries};
	print "\n\n----- Top $sizeCount Largest nentries -----\n\n";
	my $eloop = 0;
	foreach my $nentry (sort { $b <=> $a } @nkeys){
		if ($eloop == $sizeCount) { last; }
		printf "%-18s   %12s\n","nentries=$nentry", $nentries->{ $nentry };
		$eloop++;
	}
	print "\n\n----- Top $sizeCount Most returned nentries -----\n\n";
	$eloop = 0;
	foreach my $nentry (sort { $nentries->{$b} <=> $nentries->{$a} } @nkeys){
		if ($eloop == $sizeCount) { last; }
		printf "%-12s    %-14s\n", $nentries->{ $nentry }, "nentries=$nentry";
		$eloop++;
	}
	print "\n";
}

##########################################
#                                        #
# Gather and process extended operations #
#                                        #
##########################################

if ($usage =~ /x/i || $verb eq "yes"){
	if ($extopCount > 0){
		my $oid = $hashes->{oid};
		print "\n\n----- Extended Operations -----\n\n";
		foreach my $oids (sort { $oid->{$b} <=> $oid->{$a} } (keys %{$oid}) ){
			my $oidmessage;
			if ($oids eq "2.16.840.1.113730.3.5.1"){ $oidmessage = "Transaction Request"} #depreciated?
			elsif ($oids eq "2.16.840.1.113730.3.5.2"){ $oidmessage = "Transaction Response"} #depreciated?
			elsif ($oids eq "2.16.840.1.113730.3.5.3"){ $oidmessage = "Start Replication Request (incremental update)"}
			elsif ($oids eq "2.16.840.1.113730.3.5.4"){ $oidmessage = "Replication Response"}
			elsif ($oids eq "2.16.840.1.113730.3.5.5"){ $oidmessage = "End Replication Request (incremental update)"}
			elsif ($oids eq "2.16.840.1.113730.3.5.6"){ $oidmessage = "Replication Entry Request"}
			elsif ($oids eq "2.16.840.1.113730.3.5.7"){ $oidmessage = "Start Bulk Import"}
			elsif ($oids eq "2.16.840.1.113730.3.5.8"){ $oidmessage = "Finished Bulk Import"}
			elsif ($oids eq "2.16.840.1.113730.3.5.9"){ $oidmessage = "DS71 Replication Entry Request"}
			elsif ($oids eq "2.16.840.1.113730.3.6.1"){ $oidmessage = "Incremental Update Replication Protocol"}
			elsif ($oids eq "2.16.840.1.113730.3.6.2"){ $oidmessage = "Total Update Replication Protocol (Initialization)"}
			elsif ($oids eq "2.16.840.1.113730.3.4.13"){ $oidmessage = "Replication Update Info Control"}
			elsif ($oids eq "2.16.840.1.113730.3.6.4"){ $oidmessage = "DS71 Replication Incremental Update Protocol"}
			elsif ($oids eq "2.16.840.1.113730.3.6.3"){ $oidmessage = "DS71 Replication Total Update Protocol"}
			elsif ($oids eq "2.16.840.1.113730.3.5.12"){ $oidmessage = "DS90 Start Replication Request"}
			elsif ($oids eq "2.16.840.1.113730.3.5.13"){ $oidmessage = "DS90 Replication Response"}
			elsif ($oids eq "1.2.840.113556.1.4.841"){ $oidmessage = "Replication Dirsync Control"}
			elsif ($oids eq "1.2.840.113556.1.4.417"){ $oidmessage = "Replication Return Deleted Objects"}
			elsif ($oids eq "1.2.840.113556.1.4.1670"){ $oidmessage = "Replication WIN2K3 Active Directory"}
			elsif ($oids eq "2.16.840.1.113730.3.6.5"){ $oidmessage = "Replication CleanAllRUV"}
			elsif ($oids eq "2.16.840.1.113730.3.6.6"){ $oidmessage = "Replication Abort CleanAllRUV"}
			elsif ($oids eq "2.16.840.1.113730.3.6.7"){ $oidmessage = "Replication CleanAllRUV Get MaxCSN"}
			elsif ($oids eq "2.16.840.1.113730.3.6.8"){ $oidmessage = "Replication CleanAllRUV Check Status"}
			elsif ($oids eq "2.16.840.1.113730.3.5.10"){ $oidmessage = "DNA Plugin Request"}
			elsif ($oids eq "2.16.840.1.113730.3.5.11"){ $oidmessage = "DNA Plugin Response"}
			elsif ($oids eq "1.3.6.1.4.1.1466.20037"){ $oidmessage = "Start TLS"}
			elsif ($oids eq "1.3.6.1.4.1.4203.1.11.1"){ $oidmessage = "Password Modify"}
			elsif ($oids eq "2.16.840.1.113730.3.4.20"){ $oidmessage = "MTN Control Use One Backend"}
			else {$oidmessage = "Other"}
			printf "%-6s      %-23s     %-60s\n", $oid->{ $oids }, $oids, $oidmessage;
		}
	}
}

############################################
#                                          #
# Print most commonly requested attributes #
#                                          #
############################################

if ($usage =~ /r/i || $verb eq "yes"){
	if ($anyAttrs > 0){
		my $attr = $hashes->{attr};
		print "\n\n----- Top $sizeCount Most Requested Attributes -----\n\n";
		my $eloop = 0;
		foreach my $mostAttr (sort { $attr->{$b} <=> $attr->{$a} } (keys %{$attr}) ){
			if ($eloop eq $sizeCount){ last; }
			printf "%-10s  %-19s\n", $attr->{$mostAttr}, $mostAttr;
			$eloop++;
		}
	}
}

#############################
#                           #
# abandoned operation stats #
#                           #
#############################

if ($usage =~ /g/i || $verb eq "yes"){
	my $abandonTotal = $srchCount + $delCount + $modCount + $addCount + $modrdnCount + $bindCount + $extopCount + $cmpCount;
	if ($verb eq "yes" && $abandonCount > 0 && $abandonTotal > 0){
		my $conn_hash = $hashes->{conn_hash};
		my $abandon_conn_op = $hashes->{abandon_conn_op};
		my $srch_conn_op = $hashes->{srch_conn_op};
		my $del_conn_op = $hashes->{del_conn_op};
		my $add_conn_op = $hashes->{add_conn_op};
		my $mod_conn_op = $hashes->{mod_conn_op};
		my $cmp_conn_op = $hashes->{cmp_conn_op};
		my $mdn_conn_op = $hashes->{mdn_conn_op};
		my $bind_conn_op = $hashes->{bind_conn_op};
		my $unbind_conn_op = $hashes->{unbind_conn_op};
		my $ext_conn_op = $hashes->{ext_conn_op};

		print "\n\n----- Abandon Request Stats -----\n\n";

		while (my ($srcnt_conn_op, $targ_msgid) = each %{$abandon_conn_op}) {
			my ($srvRstCnt, $conn, $op) = split(",", $srcnt_conn_op);
			my ($targetop, $msgid) = split(",", $targ_msgid);
			my $srcnt_conn_targ = "$srvRstCnt,$conn,$targetop";
			my $clientIP = getIPfromConn($conn, $srvRstCnt);
			if (exists($srch_conn_op->{$srcnt_conn_targ}) && defined($srch_conn_op->{$srcnt_conn_targ})) {
				print " - SRCH conn=$conn op=$targetop msgid=$msgid client=$clientIP\n";
			} elsif (exists($del_conn_op->{$srcnt_conn_targ}) && defined($del_conn_op->{$srcnt_conn_targ})) {
				print " - DEL  conn=$conn op=$targetop msgid=$msgid client=$clientIP\n";
			} elsif (exists($add_conn_op->{$srcnt_conn_targ}) && defined($add_conn_op->{$srcnt_conn_targ})) {
				print " - ADD  conn=$conn op=$targetop msgid=$msgid client=$clientIP\n";
			} elsif (exists($mod_conn_op->{$srcnt_conn_targ}) && defined($mod_conn_op->{$srcnt_conn_targ})) {
				print " - MOD  conn=$conn op=$targetop msgid=$msgid client=$clientIP\n";
			} elsif (exists($cmp_conn_op->{$srcnt_conn_targ}) && defined($cmp_conn_op->{$srcnt_conn_targ})) {
				print " - CMP  conn=$conn op=$targetop msgid=$msgid client=$clientIP\n";
			} elsif (exists($mdn_conn_op->{$srcnt_conn_targ}) && defined($mdn_conn_op->{$srcnt_conn_targ})) {
				print " - MODRDN conn=$conn op=$targetop msgid=$msgid client=$clientIP\n";
			} elsif (exists($bind_conn_op->{$srcnt_conn_targ}) && defined($bind_conn_op->{$srcnt_conn_targ})) {
				print " - BIND conn=$conn op=$targetop msgid=$msgid client=$clientIP\n";
			} elsif (exists($unbind_conn_op->{$srcnt_conn_targ}) && defined($unbind_conn_op->{$srcnt_conn_targ})) {
				print " - UNBIND conn=$conn op=$targetop msgid=$msgid client=$clientIP\n";
			} elsif (exists($ext_conn_op->{$srcnt_conn_targ}) && defined($ext_conn_op->{$srcnt_conn_targ})) {
				print " - EXT  conn=$conn op=$targetop msgid=$msgid client=$clientIP\n";
			} else {
				print " - UNKNOWN conn=$conn op=$targetop msgid=$msgid client=$clientIP\n";
			}
		}
	}
}
print "\n";

#######################################
#                                     #
#       Recommendations               #
#                                     #
#######################################

if ($usage =~ /j/i || $verb eq "yes"){
	my $conncount = $hashes->{conncount};
	print "\n----- Recommendations -----\n";
	my $recCount = "1";
	if ($unindexedSrchCountNotesA > 0){
		print "\n $recCount.  You have unindexed searches, this can be caused from a search on an unindexed attribute, or your returned results exceeded the allidsthreshold.  Unindexed searches are not recommended. To refuse unindexed searches, switch \'nsslapd-require-index\' to \'on\' under your database entry (e.g. cn=UserRoot,cn=ldbm database,cn=plugins,cn=config).\n";
		$recCount++;
	}
	if ($unindexedSrchCountNotesU > 0){
		print "\n $recCount.  You have unindexed components, this can be caused from a search on an unindexed attribute, or your returned results exceeded the allidsthreshold.  Unindexed components are not recommended. To refuse unindexed searches, switch \'nsslapd-require-index\' to \'on\' under your database entry (e.g. cn=UserRoot,cn=ldbm database,cn=plugins,cn=config).\n";
		$recCount++;
	}
	if (defined($conncount->{"T1"}) and $conncount->{"T1"} > 0){
		print "\n $recCount.  You have some connections that are are being closed by the idletimeout setting. You may want to increase the idletimeout if it is set low.\n";
		$recCount++;
	}
	if (defined($conncount->{"T2"}) and $conncount->{"T2"} > 0){
		print "\n $recCount.  You have some coonections that are being closed by the ioblocktimeout setting. You may want to increase the ioblocktimeout.\n";
		$recCount++;
	}
	# compare binds to unbinds, if the difference is more than 30% of the binds, then report a issue
	if (($bindCount - $unbindCount) > ($bindCount*.3)){
		print "\n $recCount.  You have a significant difference between binds and unbinds.  You may want to investigate this difference.\n";
		$recCount++;
	}
	# compare fds taken and return, if the difference is more than 30% report a issue
	if (($fdTaken -$fdReturned) > ($fdTaken*.3)){
		print "\n $recCount.  You have a significant difference between file descriptors taken and file descriptors returned.  You may want to investigate this difference.\n";
		$recCount++;
	}
	if ($rootDNBindCount > ($bindCount *.2)){
		print "\n $recCount.  You have a high number of Directory Manager binds.  The Directory Manager account should only be used under certain circumstances.  Avoid using this account for client applications.\n";
		$recCount++;
	}
	if ($errorCount > $successCount){
		print "\n $recCount.  You have more unsuccessful operations than successful operations.  You should investigate this difference.\n";
		$recCount++;
	}
	if (defined($conncount->{"U1"}) and $conncount->{"U1"} < ($connCodeCount - $conncount->{"U1"})){
		print "\n $recCount.  You have more abnormal connection codes than cleanly closed connections.  You may want to investigate this difference.\n";
		$recCount++;
	}
	if ($first > 0){
		print "\n $recCount.  You have a majority of etimes that are greater than zero, you may want to investigate this performance problem.\n";
		$recCount++;
	}
	if ($objectclassTopCount > ($srchCount *.25)){
		print "\n $recCount.  You have a high number of searches that query the entire search base.  Although this is not necessarily bad, it could be resource intensive if the search base contains many entries.\n"; 
		$recCount++;
	}
	if ($recCount == 1){
		print "\nNone.\n";
	}
	print "\n";
}

#
# We're done, clean up the data files
#
removeDataFiles();

exit (0);

#######################
#                     #
#    Display Usage    #
#                     #
#######################

sub displayUsage {

	print "Usage:\n\n";

	print " ./logconv.pl [-h] [-d|--rootdn <rootDN>] [-s|--sizeLimit <size limit>] [-v|verison] [-Vi|verbose]\n";
	print " [-S|--startTime <start time>] [-E|--endTime <end time>] \n";
	print " [-efcibaltnxrgjuyp] [ access log ... ... ]\n\n";

	print "- Commandline Switches:\n\n";

	print "         -h, --help         help/usage\n";
	print "         -d, --rootDN       <Directory Managers DN>  default is \"cn=directory manager\"\n";
	print "         -D, --data         <Location for temporary data files>  default is \"/tmp\"\n";    
	print "         -s, --sizeLimit    <Number of results to return per catagory>  default is 20\n";
	print "         -X, --excludeIP    <IP address to exclude from connection stats>  E.g. Load balancers\n";
	print "         -v, --version      show version of tool\n";
	print "         -S, --startTime    <time to begin analyzing logfile from>\n";
	print "             E.g. \"[28/Mar/2002:13:14:22 -0800]\"\n";
	print "         -E, --endTime      <time to stop analyzing logfile>\n";
	print "             E.g. \"[28/Mar/2002:13:24:62 -0800]\"\n";
	print "         -m, --reportFileSecs  <CSV output file - per second stats>\n"; 
	print "         -M, --reportFileMins  <CSV output file - per minute stats>\n";	
	print "         -B, --bind         <ALL | ANONYMOUS | \"Actual Bind DN\">\n";
	print "	        -T, --minEtime     <minimum etime to report unindexed searches>\n";
	print "         -V, --verbose      <enable verbose output - includes all stats listed below>\n";
	print "         -[efcibaltnxrgjuyp]\n\n";

	print "                 e       Error Code stats\n";
	print "                 f       Failed Login Stats\n";
	print "                 c       Connection Code Stats\n";
	print "                 i       Client Stats\n";
	print "                 b       Bind Stats\n";
	print "                 a       Search Base Stats\n";
	print "                 l       Search Filter Stats\n";
	print "                 t       Etime Stats\n";
	print "                 n       Nentries Stats\n";
	print "                 x       Extended Operations\n";
	print "                 r       Most Requested Attribute Stats\n";
	print "                 g       Abandoned Operation Stats\n";
	print "                 j       Recommendations\n";
	print "                 u       Unindexed Search Stats\n";
	print "                 y       Connection Latency Stats\n";
	print "                 p       Open Connection ID Stats\n\n";

	print "  Examples:\n\n";

	print "         logconv.pl -s 10 -V /logs/access*\n\n";
	print "         logconv.pl --rootDN cn=dm /logs/access*\n\n";
	print "         logconv.pl --sizeLimit 50 -ibgju /logs/access*\n\n";
	print "         logconv.pl -S \"\[28/Mar/2002:13:14:22 -0800\]\" --endTime \"\[28/Mar/2002:13:50:05 -0800\]\" -e /logs/access*\n\n";
	print "         logconv.pl -m log-minute-stats-csv.out /logs/access*\n\n";
	print "         logconv.pl -B ANONYMOUS /logs/access*\n\n";
	print "         logconv.pl -B \"uid=mreynolds,dc=example,dc=com\" /logs/access*\n";
	print "         logconv.pl -u -T 1 /logs/access*\n\n";

	exit 1;
}

######################################################
#                                                    #
# Parsing Routines That Do The Actual Parsing Work   #
#                                                    #
######################################################

sub
parseLine {
	if($reportBinds eq "yes"){
		&parseLineBind();
	} else {
		&parseLineNormal();
	}
}

sub
parseLineBind {
	$linesProcessed++;
	$lineBlockCount++;
	local $_ = $logline;
	my $ip;

	statusreport();

	# skip blank lines
	return if $_ =~ /^\s/;

	if($firstFile == 1 && $_ =~ /^\[/){
		$start = $_;
		if ($start =~ / *([0-9a-z:\/]+)/i){$start=$1;}
		$firstFile = 0;
	}
	if ($endFlag != 1 && $_ =~ /^\[/ && $_ =~ / *([0-9a-z:\/]+)/i){
		$end =$1;
	}
	if ($startTime && !$startFlag) {
		if (index($_, $startTime) == 0) {
			$startFlag = 1;
			($start) = $startTime =~ /\D*(\S*)/;
		} else {
			return;
		}
	}
	if ($endTime && !$endFlag) {
		if (index($_, $endTime) == 0) {
			$endFlag = 1;
			($end) = $endTime =~ /\D*(\S*)/;
		}
	}
	if ($_ =~ /connection from *([0-9A-Fa-f\.\:]+)/i ) {
		my $skip = "yes";
		for (my $excl =0; $excl < $#excludeIP; $excl++){
			if ($excludeIP[$excl] eq $1){
				$skip = "yes";
				last;
			}
		}
		if ($skip eq "yes"){
			return ;
		}
		$ip = $1;
		if ($_ =~ /conn= *([0-9A-Z]+)/i ){
			$connList{$ip} = $connList{$ip} . " $1 ";
		}
		return;
	}
	if (/ BIND/ && $_ =~ /dn=\"(.*)\" method/i ){
		my $dn;
		if ($1 eq ""){
			$dn = "Anonymous";
		} else {
			$dn = $1;
			$dn =~ tr/A-Z/a-z/;
		}
		if($bindReportDN ne ""){
			if($dn ne $bindReportDN){
				#  We are not looking for this DN, skip it
				return;
			}
		}
		$bindReport{$dn}{"binds"}++;
		if ($bindReport{$dn}{"binds"} == 1){
			# For hashes we need to init the counters
			$bindReport{$dn}{"srch"} = 0;
			$bindReport{$dn}{"add"} = 0;
			$bindReport{$dn}{"mod"} = 0;
			$bindReport{$dn}{"del"} = 0;
			$bindReport{$dn}{"cmp"} = 0;
			$bindReport{$dn}{"ext"} = 0;
			$bindReport{$dn}{"modrdn"} = 0;
			$bindReport{$dn}{"failedBind"} = 0;
		}
		if ($_ =~ /conn= *([0-9A-Z]+)/i) {
			$bindReport{$dn}{"conn"} = $bindReport{$dn}{"conn"} . " $1 ";
		}
		return;
	}
	if (/ RESULT err=49 /){
		processOpForBindReport("failedBind",$logline);
	}
	if (/ SRCH base=/){
		processOpForBindReport("srch",$logline);
	} elsif (/ ADD dn=/){
		processOpForBindReport("add",$logline);
	} elsif (/ MOD dn=/){
		processOpForBindReport("mod",$logline);
	} elsif (/ DEL dn=/){
		processOpForBindReport("del",$logline);
	} elsif (/ MODRDN dn=/){
		processOpForBindReport("modrdn",$logline);
	} elsif (/ CMP dn=/){
		processOpForBindReport("cmp",$logline);
	} elsif (/ EXT oid=/){
		processOpForBindReport("ext",$logline);
	}
}

sub
processOpForBindReport
{
	my $op = shift;
	my $data = shift;

	if ($data =~ /conn= *([0-9A-Z]+)/i) {
		foreach my $dn (keys %bindReport){
			if ($bindReport{$dn}{"conn"} =~ / $1 /){
				$bindReport{$dn}{$op}++;
				return;
			}
		}
	}
}

my ($last_tm, $lastzone, $last_min, $gmtime, $tzoff);
sub handleConnClose
{
	my $connID = shift;
	$fdReturned++;
	$simConnection--;

	delete $openConnection{$connID};
	if ($reportStats or ($verb eq "yes") || ($usage =~ /y/)) {
		# if we didn't see the start time of this connection
		# i.e. due to truncation or log rotation
		# then just set to 0
		my $stoc = $hashes->{start_time_of_connection}->{$connID} || 0;
		$hashes->{end_time_of_connection}->{$connID} = $gmtime || 0;
		my $diff = $hashes->{end_time_of_connection}->{$connID} - $stoc;
		$hashes->{start_time_of_connection}->{$connID} = $hashes->{end_time_of_connection}->{$connID} = 0;
		if ($diff <= 1) { $latency[0]++;}
		if ($diff == 2) { $latency[1]++;}
		if ($diff == 3) { $latency[2]++;}
		if ($diff >= 4 && $diff <=5 ) { $latency[3]++;}
		if ($diff >= 6 && $diff <=10 ) { $latency[4]++;}
		if ($diff >= 11 && $diff <=15 ) { $latency[5]++;}
		if ($diff >= 16) { $latency[6] ++;}
	}
}

sub handleRestart
{
	# there are some stats which depend on restarts
	if ($verb eq "yes") {
		print "Found a restart - resetting stats that depend on restarts";
	}
	for my $connID (keys %openConnection) {
		handleConnClose($connID);
	}
	%openConnection = (); # reset open connections
	$serverRestartCount++;
}

sub parseLineNormal
{
	local $_ = $logline;
	my $ip;
	my $tmpp;
	my $exc = "no";
	my $connID;
	my $con;
	my $op;
	$linesProcessed++;
	$lineBlockCount++;

	# lines starting blank are restart
	return if $_ =~ /^\s/;

	statusreport();

	# gather/process the timestamp
	if($firstFile == 1 && $_ =~ /^\[/){
		# if we are using startTime & endTime, this will get overwritten, which is ok
		$start = $_;
		if ($start =~ / *([0-9a-z:\/]+)/i){$start=$1;}
		$firstFile = 0;
	}
	if ($endFlag != 1 && $_ =~ /^\[/ && $_ =~ / *([0-9a-z:\/]+)/i){$end =$1;}
	if ($startTime && !$startFlag) {
		if (index($_, $startTime) == 0) {
			$startFlag = 1;
			($start) = $startTime =~ /\D*(\S*)/;
		} else {
			return;
		}
	}
	if ($endTime && !$endFlag) {
		if (index($_, $endTime) == 0) {
			$endFlag = 1;
			($end) = $endTime =~ /\D*(\S*)/;
		}
	}

	# Additional performance stats
	my ($time, $tzone) = split (' ', $_);
	if (($reportStats or ($verb eq "yes") || ($usage =~ /y/)) && (!defined($last_tm) or ($time ne $last_tm)))
	{
		$last_tm = $time;
		$time =~ s/\[//;
		$tzone =~ s/\].*//;

		if(!defined($lastzone) or $tzone ne $lastzone)
		{
			# tz offset change
			$lastzone=$tzone;
			my ($sign,$hr,$min) = $tzone =~ m/(.)(\d\d)(\d\d)/;
			$tzoff = $hr*3600 + $min*60;
			$tzoff *= -1
				if $sign eq '-';
			# to be subtracted from converted values.
		}
		my ($date, $hr, $min, $sec) = split (':', $time);
		my ($day, $mon, $yr) = split ('/', $date);
		my $newmin = timegm(0, $min, $hr, $day, $monthname{$mon}, $yr) - $tzoff;
		$gmtime = $newmin + $sec;
		print_stats_block( $s_stats );
		reset_stats_block( $s_stats, $gmtime, $time.' '.$tzone );
		if (!defined($last_min) or $newmin != $last_min)
		{
			print_stats_block( $m_stats );
			$time =~ s/\d\d$/00/;
			reset_stats_block( $m_stats, $newmin, $time.' '.$tzone );
			$last_min = $newmin;
		}
	}

	if (m/ RESULT err/){
		$allResults++;
		if($reportStats){ inc_stats('results',$s_stats,$m_stats); }
	}
	if (m/ SRCH/){
		$srchCount++;
		if($reportStats){ inc_stats('srch',$s_stats,$m_stats); }
		if ($_ =~ / attrs=\"(.*)\"/i){
			$anyAttrs++;
			my $attr = $hashes->{attr};
			map { $attr->{$_}++ } split /\s/, $1;
		}
		if (/ attrs=ALL/){
			my $attr = $hashes->{attr};
			$attr->{"All Attributes"}++;
			$anyAttrs++;
		}
		if ($verb eq "yes"){
			if ($_ =~ /conn= *([0-9A-Z]+) +op= *([0-9\-]+)/i){ $hashes->{srch_conn_op}->{"$serverRestartCount,$1,$2"}++;}
		}
	}
	if (m/ DEL/){
		$delCount++;
		if($reportStats){ inc_stats('del',$s_stats,$m_stats); }
		if ($verb eq "yes"){
			if ($_ =~ /conn= *([0-9A-Z]+) +op= *([0-9\-]+)/i){ $hashes->{del_conn_op}->{"$serverRestartCount,$1,$2"}++;}
		}
	}
	if (m/ MOD dn=/){
		$modCount++;
		if($reportStats){ inc_stats('mod',$s_stats,$m_stats); }
		if ($verb eq "yes"){
			if ($_ =~ /conn= *([0-9A-Z]+) +op= *([0-9\-]+)/i){ $hashes->{mod_conn_op}->{"$serverRestartCount,$1,$2"}++;}
		}
	}
	if (m/ ADD/){
		$addCount++;
		if($reportStats){ inc_stats('add',$s_stats,$m_stats); }
		if ($verb eq "yes"){
			if ($_ =~ /conn= *([0-9A-Z]+) +op= *([0-9\-]+)/i){ $hashes->{add_conn_op}->{"$serverRestartCount,$1,$2"}++;}
		}
	}
	if (m/ MODRDN/){
		$modrdnCount++;
		if($reportStats){ inc_stats('modrdn',$s_stats,$m_stats); }
		if ($verb eq "yes"){
			if ($_ =~ /conn= *([0-9A-Z]+) +op= *([0-9\-]+)/i){ $hashes->{mdn_conn_op}->{"$serverRestartCount,$1,$2"}++;}
		}
	}
	if (m/ CMP dn=/){
		$cmpCount++;
		if($reportStats){ inc_stats('cmp',$s_stats,$m_stats); }
		if ($verb eq "yes"  || $usage =~ /g/i){
			if ($_ =~ /conn= *([0-9A-Z]+) +op= *([0-9\-]+)/i){ $hashes->{cmp_conn_op}->{"$serverRestartCount,$1,$2"}++;}
		}
	}
	if (m/ ABANDON /){
		# there are two cases for abandon
		# [17/Sep/2013:01:52:11 -0400] conn=1482074 op=4 ABANDON targetop=3 msgid=4
		# [17/Sep/2013:01:52:11 -0400] conn=1482074 op=4 ABANDON targetop=NOTFOUND msgid=4
		# if the op to be abandoned completes before the abandon request can be processed
		# the server will use NOTFOUND as the op number
		$abandonCount++;
		if($reportStats){ inc_stats('abandon',$s_stats,$m_stats); }
		$allResults++;
		if ($_ =~ /conn= *([0-9A-Z]+) +op= *([0-9\-]+) +ABANDON +targetop= *([0-9a-zA-Z]+) +msgid= *([0-9\-]+)/i ){
			# abandon_conn_op - key is the conn,op of the ABANDON request
			# the value is the targetop,msgid of the ABANDON request
			$hashes->{abandon_conn_op}->{"$serverRestartCount,$1,$2"} = "$3,$4"; # targetop,msgid
		}
	}
	if (m/ VLV /){
		if ($_ =~ /conn= *([0-9A-Z]+)/i){ $vlvconn[$vlvCount] = $1;}
		if ($_ =~ /op= *([0-9\-]+)/i){  $vlvop[$vlvCount] = $1;}
		$vlvCount++;
	}
	if (m/ authzid=/){
		$proxiedAuthCount++;
	}
	if (m/ SORT /){$vlvSortCount++}
	if (m/ version=2/){$v2BindCount++}
	if (m/ version=3/){$v3BindCount++}
	if (m/ conn=1 fd=/){
		handleRestart();
	}
	if (m/ SSL connection from/){$sslCount++; if($reportStats){ inc_stats('sslconns',$s_stats,$m_stats); }}
	if (m/ connection from local to /){$ldapiCount++;}
	if($_ =~ /AUTOBIND dn=\"(.*)\"/){
		$autobindCount++;
		$bindCount++;
		if($reportStats){ inc_stats('bind',$s_stats,$m_stats); }
		if ($1 ne ""){
			$tmpp = $1;
			$tmpp =~ tr/A-Z/a-z/;
			$hashes->{bindlist}->{$tmpp}++;
			if($1 eq $rootDN){
				$rootDNBindCount++;
			}
		} else {
			$anonymousBindCount++;
			$hashes->{bindlist}->{"Anonymous Binds"}++;
			inc_stats('anonbind',$s_stats,$m_stats);
		}
	}
	if (m/ connection from/){
		my $ip;
		if ($_ =~ /connection from *([0-9A-Fa-f\.\:]+)/i ){
			$ip = $1;
			for (my $xxx =0; $xxx < $#excludeIP; $xxx++){
				if ($excludeIP[$xxx] eq $ip){$exc = "yes";}
			}
			if ($exc ne "yes"){
				$connectionCount++;
				if($reportStats){ inc_stats('conns',$s_stats,$m_stats); }
			}
		}
		$simConnection++;
		if ($simConnection > $maxsimConnection) {
			$maxsimConnection = $simConnection;
		}
		($connID) = $_ =~ /conn=(\d*)\s/;
		$openConnection{$connID} = $ip;
		if ($reportStats or ($verb eq "yes") || ($usage =~ /y/)) {
			$hashes->{start_time_of_connection}->{$connID} = $gmtime;
		}
	}
	if (m/ SSL client bound as /){$sslClientBindCount++;}
	if (m/ SSL failed to map client certificate to LDAP DN/){$sslClientFailedCount++;}
	if (m/ fd=/ && m/slot=/){$fdTaken++}
	if (m/ fd=/ && m/closed/){
		($connID) = $_ =~ /conn=(\d*)\s/;
		handleConnClose($connID);
	}
	if (m/ BIND/ && $_ =~ /dn=\"(.*)\" method/i ){
		my $binddn = $1;
		if($reportStats){ inc_stats('bind',$s_stats,$m_stats); }
		$bindCount++;
		my ($conn, $op);
		if ($_ =~ /conn= *([0-9A-Z]+) +op= *([0-9\-]+)/i){
			$conn = $1;
			$op = $2;
		}
		if ($binddn ne ""){
			if($binddn eq $rootDN){$rootDNBindCount++;}
			$tmpp = $binddn;
			$tmpp =~ tr/A-Z/a-z/;
			$hashes->{bindlist}->{$tmpp}++;
			if($usage =~ /f/ || $verb eq "yes"){
				$hashes->{bind_conn_op}->{"$serverRestartCount,$conn,$op"} = $tmpp;
			}
		} else {
			$anonymousBindCount++;
			$hashes->{bindlist}->{"Anonymous Binds"}++;
			if($usage =~ /f/ || $verb eq "yes"){
				$hashes->{bind_conn_op}->{"$serverRestartCount,$conn,$op"} = "";
			}
			inc_stats('anonbind',$s_stats,$m_stats);
		}
	}
	if (m/ UNBIND/){
		$unbindCount++;
		if ($verb eq "yes"){
			if ($_ =~ /conn= *([0-9A-Z]+) +op= *([0-9\-]+)/i){ $hashes->{unbind_conn_op}->{"$serverRestartCount,$1,$2"}++;}
		}
	}
	if (m/ RESULT err=/ && m/ notes=[A-Z,]*P/){
		$pagedSearchCount++;
	}
	if (m/ notes=[A-Z,]*A/){
		$con = "";
		if ($_ =~ /conn= *([0-9A-Z]+)/i){
			$con = $1;
			if ($_ =~ /op= *([0-9\-]+)/i){ $op = $1;}
		}
		for (my $i=0; $i < $vlvCount;$i++){
			if ($vlvconn[$i] eq $con && $vlvop[$i] eq $op){ $vlvNotesACount++; $isVlvNotes="1";}
		}
		if($isVlvNotes == 0){
			#  We don't want to record vlv unindexed searches for our regular "bad" 
			#  unindexed search stat, as VLV unindexed searches aren't that bad
			$unindexedSrchCountNotesA++;
			if($reportStats){ inc_stats('notesA',$s_stats,$m_stats); }
			if ($usage =~ /u/ || $usage =~ /U/ || $verb eq "yes"){
				if($_ =~ /etime= *([0-9.]+)/i ){
					if($1 >= $minEtime){
						$hashes->{etime_conn_op}->{"$serverRestartCount,$con,$op"} = $1;
						$hashes->{notesa_conn_op}->{"$serverRestartCount,$con,$op"}++;
						if ($_ =~ / *([0-9a-z:\/]+)/i){ $hashes->{time_conn_op}->{"$serverRestartCount,$con,$op"} = $1; }
						if ($_ =~ /nentries= *([0-9]+)/i ){ $hashes->{nentries_conn_op}->{"$serverRestartCount,$con,$op"} = $1; }
					}
				}
			}
		}
		$isVlvNotes = 0;
	}
	if (m/ notes=[A-Z,]*U/){
		$con = "";
		if ($_ =~ /conn= *([0-9A-Z]+)/i){
			$con = $1;
			if ($_ =~ /op= *([0-9\-]+)/i){ $op = $1;}
		}
		for (my $i=0; $i < $vlvCount;$i++){
			if ($vlvconn[$i] eq $con && $vlvop[$i] eq $op){ $vlvNotesUCount++; $isVlvNotes="1";}
		}
		if($isVlvNotes == 0){
			#  We don't want to record vlv unindexed searches for our regular "bad" 
			#  unindexed search stat, as VLV unindexed searches aren't that bad
			$unindexedSrchCountNotesU++;
			if($reportStats){ inc_stats('notesU',$s_stats,$m_stats); }
			if ($usage =~ /u/ || $usage =~ /U/ || $verb eq "yes"){
				if($_ =~ /etime= *([0-9.]+)/i ){
					if($1 >= $minEtime){
						$hashes->{etime_conn_op}->{"$serverRestartCount,$con,$op"} = $1;
						$hashes->{notesu_conn_op}->{"$serverRestartCount,$con,$op"}++;
						if ($_ =~ / *([0-9a-z:\/]+)/i){ $hashes->{time_conn_op}->{"$serverRestartCount,$con,$op"} = $1; }
						if ($_ =~ /nentries= *([0-9]+)/i ){ $hashes->{nentries_conn_op}->{"$serverRestartCount,$con,$op"} = $1; }
					}
				}
			}
		}
		$isVlvNotes = 0;
	}
	if (m/ closed error 32/){
		$brokenPipeCount++;
		if (m/- T1/){ $hashes->{rc}->{"T1"}++; }
		elsif (m/- T2/){ $hashes->{rc}->{"T2"}++; }
		elsif (m/- A1/){ $hashes->{rc}->{"A1"}++; }
		elsif (m/- B1/){ $hashes->{rc}->{"B1"}++; }
		elsif (m/- B4/){ $hashes->{rc}->{"B4"}++; }
		elsif (m/- B2/){ $hashes->{rc}->{"B2"}++; }
		elsif (m/- B3/){ $hashes->{rc}->{"B3"}++; }
		elsif (m/- R1/){ $hashes->{rc}->{"R1"}++; }
		elsif (m/- P1/){ $hashes->{rc}->{"P1"}++; }
		elsif (m/- P1/){ $hashes->{rc}->{"P2"}++; }
		elsif (m/- U1/){ $hashes->{rc}->{"U1"}++; }
		else { $hashes->{rc}->{"other"}++; }
	}
	if (m/ closed error 131/ || m/ closed error -5961/){
		$connResetByPeerCount++;
		if (m/- T1/){ $hashes->{src}->{"T1"}++; }
		elsif (m/- T2/){ $hashes->{src}->{"T2"}++; }
		elsif (m/- A1/){ $hashes->{src}->{"A1"}++; }
		elsif (m/- B1/){ $hashes->{src}->{"B1"}++; }
		elsif (m/- B4/){ $hashes->{src}->{"B4"}++; }
		elsif (m/- B2/){ $hashes->{src}->{"B2"}++; }
		elsif (m/- B3/){ $hashes->{src}->{"B3"}++; }
		elsif (m/- R1/){ $hashes->{src}->{"R1"}++; }
		elsif (m/- P1/){ $hashes->{src}->{"P1"}++; }
		elsif (m/- P1/){ $hashes->{src}->{"P2"}++; }
		elsif (m/- U1/){ $hashes->{src}->{"U1"}++; }
		else { $hashes->{src}->{"other"}++; }
	}
	if (m/ closed error 11/){
		$resourceUnavailCount++;
		if (m/- T1/){ $hashes->{rsrc}->{"T1"}++; }
		elsif (m/- T2/){ $hashes->{rsrc}->{"T2"}++; }
		elsif (m/- A1/){ $hashes->{rsrc}->{"A1"}++; }
		elsif (m/- B1/){ $hashes->{rsrc}->{"B1"}++; }
		elsif (m/- B4/){ $hashes->{rsrc}->{"B4"}++; }
		elsif (m/- B2/){ $hashes->{rsrc}->{"B2"}++; }
		elsif (m/- B3/){ $hashes->{rsrc}->{"B3"}++; }
		elsif (m/- R1/){ $hashes->{rsrc}->{"R1"}++; }
		elsif (m/- P1/){ $hashes->{rsrc}->{"P1"}++; }
		elsif (m/- P1/){ $hashes->{rsrc}->{"P2"}++; }
		elsif (m/- U1/){ $hashes->{rsrc}->{"U1"}++; }
		else { $hashes->{rsrc}->{"other"}++; }
	}
	if ($usage =~ /g/ || $usage =~ /c/ || $usage =~ /i/ || $usage =~ /f/ || $verb eq "yes"){
		$exc = "no";
		if ($_ =~ /connection from *([0-9A-fa-f\.\:]+)/i ) {
			for (my $xxx = 0; $xxx < $#excludeIP; $xxx++){
				if ($1 eq $excludeIP[$xxx]){
					$exc = "yes";
					$hashes->{excount}->{$1}++;
				}
			}
			$ip = $1;
			$hashes->{ip_hash}->{$ip}++;
			if ($_ =~ /conn= *([0-9A-Z]+)/i ){
				if ($exc ne "yes"){
					$hashes->{conn_hash}->{"$serverRestartCount,$1"} = $ip;
				}
			}
		}
		if (m/- A1/){
			if ($_ =~ /conn= *([0-9A-Z]+)/i) {
				$exc = "no";
				$ip = getIPfromConn($1, $serverRestartCount);
				for (my $xxx = 0; $xxx < $#excludeIP; $xxx++){
					if ($ip eq $excludeIP[$xxx]){$exc = "yes";}
				}
				if ($exc ne "yes"){
					$hashes->{A1}->{$ip}++;
					$hashes->{conncount}->{"A1"}++;
					$connCodeCount++;
				}
			}
		}
		if (m/- B1/){
			if ($_ =~ /conn= *([0-9A-Z]+)/i) {
				$exc = "no";
				$ip = getIPfromConn($1, $serverRestartCount);
				for (my $xxx = 0; $xxx < $#excludeIP; $xxx++){
					if ($ip eq $excludeIP[$xxx]){$exc = "yes";}
				}
				if ($exc ne "yes"){
					$hashes->{B1}->{$ip}++;
					$hashes->{conncount}->{"B1"}++;
					$connCodeCount++;
				}
			}
		}
		if (m/- B4/){
			if ($_ =~ /conn= *([0-9A-Z]+)/i) {
				$exc = "no";
				$ip = getIPfromConn($1, $serverRestartCount);
				for (my $xxx = 0; $xxx < $#excludeIP; $xxx++){
					if ($ip eq $excludeIP[$xxx]){$exc = "yes";}
				}
				if ($exc ne "yes"){
					$hashes->{B4}->{$ip}++;
					$hashes->{conncount}->{"B4"}++;
					$connCodeCount++;
				}
			}
		}
		if (m/- T1/){
			if ($_ =~ /conn= *([0-9A-Z]+)/i) {
				$exc = "no";
				$ip = getIPfromConn($1, $serverRestartCount);
				for (my $xxx = 0; $xxx < $#excludeIP; $xxx++){
					if ($ip eq $excludeIP[$xxx]){$exc = "yes";}
				}
				if ($exc ne "yes"){
					$hashes->{T1}->{$ip}++;
					$hashes->{conncount}->{"T1"}++;
					$connCodeCount++;
				}
			}
		}
		if (m/- T2/){
			if ($_ =~ /conn= *([0-9A-Z]+)/i) {
				$exc = "no";
				$ip = getIPfromConn($1, $serverRestartCount);
				for (my $xxx = 0; $xxx < $#excludeIP; $xxx++){
					if ($ip eq $excludeIP[$xxx]){$exc = "yes";}
				}
				if ($exc ne "yes"){
					$hashes->{T2}->{$ip}++;
					$hashes->{conncount}->{"T2"}++;
					$connCodeCount++;
				}
			}
		}
		if (m/- B2/){
			if ($_ =~ /conn= *([0-9A-Z]+)/i) {
				$exc = "no";
				$ip = getIPfromConn($1, $serverRestartCount);
				$maxBerSizeCount++;
				for (my $xxx = 0; $xxx < $#excludeIP; $xxx++){
					if ($ip eq $excludeIP[$xxx]){$exc = "yes";}
				}
				if ($exc ne "yes"){
					$hashes->{B2}->{$ip}++;
					$hashes->{conncount}->{"B2"}++;
					$connCodeCount++;
				}
			}
		}
		if (m/- B3/){
			if ($_ =~ /conn= *([0-9A-Z]+)/i) {
				$exc = "no";
				$ip = getIPfromConn($1, $serverRestartCount);
				for (my $xxx = 0; $xxx < $#excludeIP; $xxx++){
					if ($ip eq $excludeIP[$xxx]){$exc = "yes";}
				}
				if ($exc ne "yes"){
					$hashes->{B3}->{$ip}++;
					$hashes->{conncount}->{"B3"}++;
					$connCodeCount++;
				}
			}
		}
		if (m/- R1/){
			if ($_ =~ /conn= *([0-9A-Z]+)/i) {
				$exc = "no";
				$ip = getIPfromConn($1, $serverRestartCount);
				for (my $xxx = 0; $xxx < $#excludeIP; $xxx++){
					if ($ip eq $excludeIP[$xxx]){$exc = "yes";}
				}
				if ($exc ne "yes"){
					$hashes->{R1}->{$ip}++;
					$hashes->{conncount}->{"R1"}++;
					$connCodeCount++;
				}
			}
		}
		if (m/- P1/){
			if ($_ =~ /conn= *([0-9A-Z]+)/i) {
				$exc = "no";
				$ip = getIPfromConn($1, $serverRestartCount);
				for (my $xxx = 0; $xxx < $#excludeIP; $xxx++){
					if ($ip eq $excludeIP[$xxx]){$exc = "yes";}
				}
				if ($exc ne "yes"){
					$hashes->{P1}->{$ip}++;
					$hashes->{conncount}->{"P1"}++;
					$connCodeCount++;
				}
			}
		}
		if (m/- P2/){
			if ($_ =~ /conn= *([0-9A-Z]+)/i) {
				$exc = "no";
				$ip = getIPfromConn($1, $serverRestartCount);
				for (my $xxx = 0; $xxx < $#excludeIP; $xxx++){
					if ($ip eq $excludeIP[$xxx]){$exc = "yes";}
				}
				if ($exc ne "yes"){
					$hashes->{P2}->{$ip}++;
					$hashes->{conncount}->{"P2"}++;
					$connCodeCount++;
				}
			}
		}
		if (m/- U1/){
			if ($_ =~ /conn= *([0-9A-Z]+)/i) {
				$exc = "no";
				$ip = getIPfromConn($1, $serverRestartCount);
				for (my $xxx = 0; $xxx < $#excludeIP; $xxx++){
					if ($ip eq $excludeIP[$xxx]){$exc = "yes";}
				}
				if ($exc ne "yes"){
					$hashes->{U1}->{$ip}++;
					$hashes->{conncount}->{"U1"}++;
					$connCodeCount++;
				}
			}
		}
	}
	if ($_ =~ /err= *([0-9]+)/i){
		$errorCode[$1]++;
		if ($1 ne "0"){ $errorCount++;}
		else { $successCount++;}
	}
	if ($_ =~ /etime= *([0-9.]+)/ ) { $hashes->{etime}->{$1}++; inc_stats_val('etime',$1,$s_stats,$m_stats); }
	if ($_ =~ / tag=101 / || $_ =~ / tag=111 / || $_ =~ / tag=100 / || $_ =~ / tag=115 /){
		if ($_ =~ / nentries= *([0-9]+)/i ){ $hashes->{nentries}->{$1}++; }
	}
	if (m/objectclass=\*/i || m/objectclass=top/i ){
		if (m/ scope=2 /){ $objectclassTopCount++;}
	}
	if (m/ EXT oid=/){
		$extopCount++;
		if ($_ =~ /oid=\" *([0-9\.]+)/i ){ $hashes->{oid}->{$1}++; }
		if ($1 && $1 eq $startTLSoid){$startTLSCount++;}
		if ($verb eq "yes"){
			if ($_ =~ /conn= *([0-9A-Z]+) +op= *([0-9\-]+)/i){ $hashes->{ext_conn_op}->{"$serverRestartCount,$1,$2"}++;}
		}
	}
	if (($usage =~ /l/ || $verb eq "yes" || $usage =~ /u/ || $usage =~ /U/) and / SRCH /){
		my ($filterConn, $filterOp);
		if (/ SRCH / && / attrs=/ && $_ =~ /filter=\"(.*)\" /i ){
			$tmpp = $1;
			$tmpp =~ tr/A-Z/a-z/;
			$tmpp =~ s/\\22/\"/g;
			$hashes->{filter}->{$tmpp}++;
			if ($_ =~ /conn= *([0-9A-Z]+)/i) { $filterConn = $1; }
			if ($_ =~ /op= *([0-9\-]+)/i) { $filterOp = $1; }
		} elsif (/ SRCH / && $_ =~ /filter=\"(.*)\"/i){
			$tmpp = $1;
			$tmpp =~ tr/A-Z/a-z/;
			$tmpp =~ s/\\22/\"/g;
			$hashes->{filter}->{$tmpp}++;
			if ($_ =~ /conn= *([0-9A-Z]+)/i) { $filterConn = $1; }
			if ($_ =~ /op= *([0-9\-]+)/i) { $filterOp = $1; }
		}
		$filterCount++;
		if($usage =~ /u/ || $usage =~ /U/ || $verb eq "yes"){
			# we only need this for the unindexed search report
			$hashes->{filter_conn_op}->{"$serverRestartCount,$filterConn,$filterOp"} = $tmpp;
		}
	}
	if (($usage =~ /a/ || $verb eq "yes" || $usage =~ /u/ || $usage =~ /U/) and / SRCH /){
		if ($_ =~ /base=\"(.*)\" scope/i ){
			my ($conn, $op, $scopeVal);
			if ($1 eq ""){
				$tmpp = "Root DSE";
			} else {
				$tmpp = $1;
			}
			$tmpp =~ tr/A-Z/a-z/;
			$hashes->{base}->{$tmpp}++;
			#
			# grab the search bases & scope for potential unindexed searches
			#
			if ($_ =~ /scope= *([0-9]+)/i) {
				$scopeVal = $1;
			}
			if ($_ =~ /conn= *([0-9A-Z]+)/i) {
				$conn = $1;
			}
			if ($_ =~ /op= *([0-9\-]+)/i) {
				$op = $1;
			}
			if($usage =~ /u/ || $usage =~ /U/ || $verb eq "yes"){
				# we only need this for the unindexed search report
				$hashes->{base_conn_op}->{"$serverRestartCount,$conn,$op"} = $tmpp;
				$hashes->{scope_conn_op}->{"$serverRestartCount,$conn,$op"} = $scopeTxt[$scopeVal];
			}
			$baseCount++;
			$scopeCount++;
		}
	}
	if ($_ =~ /fd= *([0-9]+)/i ) {
		$fds[$fdds] = $1;
		if ($fds[$fdds] > $highestFdTaken) {$highestFdTaken = $fds[$fdds];}
		$fdds++;
	}
	if ($usage =~ /f/ || $verb eq "yes"){
		if (/ err=49 tag=/ && / dn=\"/){
			if ($_ =~ /dn=\"(.*)\"/i ){
				$hashes->{ds6xbadpwd}->{$1}++;
			}
			$ds6x = "true";
			$badPwdCount++;
		} elsif (/ err=49 tag=/ ){
			$badPwdCount++;
			if ($_ =~ /conn= *([0-9A-Z]+) +op= *([0-9\-]+)/i){
				$hashes->{badpwd_conn_op}->{"$serverRestartCount,$1,$2"} = getIPfromConn($1, $serverRestartCount);
			}
		}
	}
	if (/ BIND / && /method=sasl/i){
		$saslBindCount++;
		if ($_ =~ /mech=(.*)/i ){
			$hashes->{saslmech}->{$1}++;
		}
	}
	if (/ conn=Internal op=-1 / && !/ RESULT err=/){ $internalOpCount++; }
	if (/ ENTRY dn=/ ){ $entryOpCount++; }
	if (/ conn=/ && /op=/ && / REFERRAL/){ $referralCount++; }
	if (/ options=persistent/){$persistentSrchCount++;}
}

#######################################
#                                     #
#          CSV Helper Routines        #
#                                     #
#######################################
#                                     #
# To convert the CSV to chart in OO   #
#                                     #
#  * Select active rows and columns   #
#  * Insert -> Chart                  #
#  * Chart type "XY (Scatter)"        #
#  *   sub-type "Lines Only"          #
#  * select "Sort by X values"        #
#  * "Next"                           #
#  * select "Data series in columns"  #
#  * select "First row as label"      #
#  * select "First column as label"   #
#  * "Next"                           #
#  * "Next"                           #
#  * "Finish"                         #
#                                     #
#######################################

sub
reset_stats_block
{
	my $stats = shift;

	$stats->{'last'} = shift || 0;
	$stats->{'last_str'} = shift || '';

	for my $sn (@statnames) {
		next if ($sn eq 'last' or $sn eq 'last_str');
		$stats->{$sn}=0;
	}

	return;
}

sub
new_stats_block
{
	my $name = shift || '';
	my $stats = {
		'active' => 0,
	};
	if ($name){
		$stats = openHashFiles($dataLocation, @statnames);
		$stats->{'filename'} = $name;
		$stats->{'fh'} = new IO::File;
		$stats->{'active'} = open($stats->{'fh'},">$name");
	}
	reset_stats_block( $stats );

	return $stats;
}

sub
print_stats_block
{
	foreach my $stats( @_ ){
		if ($stats->{'active'}){
			if ($stats->{'last'}){
				$stats->{'fh'}->print(
					join(',',
						 $stats->{'last_str'},
						 $stats->{'last'},
						 $stats->{'results'},
						 $stats->{'srch'},
						 $stats->{'add'},
						 $stats->{'mod'},
						 $stats->{'modrdn'},
						 $stats->{'moddn'},
						 $stats->{'cmp'},
						 $stats->{'del'},
						 $stats->{'abandon'},
						 $stats->{'conns'},
						 $stats->{'sslconns'},
						 $stats->{'bind'},
						 $stats->{'anonbind'},
						 $stats->{'unbind'},
						 $stats->{'notesA'},
						 $stats->{'notesU'},
						 $stats->{'etime'}),
					"\n" );
			} else {
				$stats->{'fh'}->print(
					"Time,time_t,Results,Search,Add,Mod,Modrdn,Moddn,Compare,Delete,Abandon,".
					"Connections,SSL Conns,Bind,Anon Bind,Unbind,Unindexed search,Unindexed component,ElapsedTime\n"
					);
			}
		}
	}
	return;
}

sub
inc_stats
{
	my $n = shift;
	foreach(@_){
		$_->{$n}++
			if exists $_->{$n};
	}
	return;
}

# like inc_stats, but increments the block counter with the given value e.g.
# 'statname1', val, statblock1, statblock2, ...
sub
inc_stats_val
{
	my $n = shift;
	my $val = shift;
	foreach(@_){
		$_->{$n} += $val
			if exists $_->{$n};
	}
	return;
}

sub
displayBindReport
{
	#
	#  Loop for each DN - sort alphabetically
	#
	#  Display all the IP addresses, then counts of all the operations it did
	#

	print "\nBind Report\n";
	print "====================================================================\n\n";
	foreach my $bindDN (sort { $bindReport{$a} <=> $bindReport{$b} } keys %bindReport) {
		print("Bind DN: $bindDN\n");
		print("--------------------------------------------------------------------\n");
		print("   Client Addresses:\n\n");
		&printClients($bindReport{$bindDN}{"conn"});
		print("\n   Operations Performed:\n\n");
		&printOpStats($bindDN);
		print("\n");
	}
	print "Done.\n";
	exit (0);
}

sub
printClients
{
	my @bindConns = &cleanConns(split(' ', $_[0]));
	my $IPcount = "1";

	foreach my $ip ( keys %connList ){   # Loop over all the IP addresses
		foreach my $bc (@bindConns){ # Loop over each bind conn number and compare it 
			if($connList{$ip} =~ / $bc /){
				print("        [$IPcount]  $ip\n");
				$IPcount++;
				last;
			}
		}
	}
}

sub
cleanConns
{
	my @dirtyConns = @_;
	my @retConns = ();
	my $c = 0;

	for (my $i = 0; $i <=$#dirtyConns; $i++){
		if($dirtyConns[$i] ne ""){
			$retConns[$c++] = $dirtyConns[$i];
		}
	}
	return @retConns;
}

sub
printOpStats
{
	my $dn = $_[0];

	if( $bindReport{$dn}{"failedBind"} == 0 ){
		print("        Binds:        " . $bindReport{$dn}{"binds"} . "\n");
	} else {
		print("        Binds:        " . $bindReport{$dn}{"binds"} . "  (Invalid Credentials: " . $bindReport{$dn}{"failedBind"} . ")\n");
	}
	print("        Searches:     " . $bindReport{$dn}{"srch"} . "\n");
	print("        Modifies:     " . $bindReport{$dn}{"mod"} . "\n");
	print("        Adds:         " . $bindReport{$dn}{"add"} . "\n");
	print("        Deletes:      " . $bindReport{$dn}{"del"} . "\n");
	print("        Compares:     " . $bindReport{$dn}{"cmp"} . "\n");
	print("        ModRDNs:      " . $bindReport{$dn}{"modrdn"} . "\n");
	print("        Ext Ops:      " . $bindReport{$dn}{"ext"} . "\n\n");
}

#######################
#                     #
# Hash File Functions #
#                     #
#######################

sub
openFailed
{
	my $open_error = $_[0];
	my $file_name = $_[1];
	removeDataFiles();
	die ("Can not open $file_name error ($open_error)");
}

sub
openHashFiles
{
	my $dir = shift;
	my %hashes = ();
	for my $hn (@_) {
		my %h = (); # using my in inner loop will create brand new hash every time through for tie
		my $fn = "$dir/$hn.logconv.db";
		push @removefiles, $fn;
		tie %h, "DB_File", $fn, O_CREAT|O_RDWR, 0600, $DB_HASH or do { openFailed($!, $fn) };
		$hashes{$hn} = \%h;
	}
	return \%hashes;
}

sub
removeDataFiles
{
	if (!$needCleanup) { return ; }

	for my $h (keys %{$hashes}) {
		untie %{$hashes->{$h}};
	}
	for my $file (@removefiles) {
		unlink $file;
	}
	$needCleanup = 0;
}

END { print "Cleaning up temp files . . .\n"; removeDataFiles(); print "Done\n"; }

sub
getIPfromConn
{
	my $connid = shift;
	my $serverRestartCount = shift;
	my $key = "$serverRestartCount,$connid";
	if (exists($hashes->{conn_hash}->{$key}) &&
		defined($hashes->{conn_hash}->{$key})) {
		return $hashes->{conn_hash}->{$key};
	}

	return "Unknown_Host";
}

#######################################
#                                     #
#             The  End                #
#                                     #
#######################################
