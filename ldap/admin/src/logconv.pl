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
use Time::Local;
use IO::File;
use Getopt::Long;

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

$file_count = 0;
$arg_count = 0;
$logversion = "7.0";
$sizeCount = "20";
$startFlag = 0;
$startTime = 0;
$endFlag = 0;
$endTime = 0;
$reportStats = "";
$dataLocation = "/tmp";
$startTLSoid = "1.3.6.1.4.1.1466.20037";
$s_stats = new_stats_block( );
$m_stats = new_stats_block( );

GetOptions(
	'd|rootDN=s' => \$rootDN,
	'v|version' => sub { print "Access Log Analyzer v$logversion\n"; exit (0); },
	'V|verbose' => sub { $verb = "yes"; },
	'D|data=s' => \$dataLocation,
	'X|excludeIP=s' => \$excludeIP[$xi++],
	's|sizeLimit=s' => \$sizeCount,
	'S|startTime=s' => \$startTime,
	'E|endTime=s' => \$endTime,
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
	'p' => sub { $usage = $usage . "p"; }
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
# Initialize Arrays and variables     #
#                                     #
#######################################

print "\nAccess Log Analyzer $logversion\n";
print "\nCommand: logconv.pl @ARGV\n\n";

$rootDNBindCount = 0;
$anonymousBindCount = 0;
$unindexedSrchCount = 0;
$vlvNotesCount= 0;
$srchCount = 0;
$fdTaken = 0;
$fdReturned = 0;
$highestFdTaken = 0;
$unbindCount = 0;
$cmpCount = 0;
$modCount = 0;
$delCount = 0;
$addCount = 0;
$modrdnCount = 0;
$abandonCount = 0;
$extopCount = 0;
$vlvCount = 0;
$errorCount = 0;
$proxiedAuthCount = 0;
$serverRestartCount = 0;
$resourceUnavailCount = 0;
$brokenPipeCount = 0;
$v2BindCount = 0;
$v3BindCount = 0;
$vlvSortCount = 0;
$connResetByPeerCount = 0;
$isVlvNotes = 0;
$successCount = 0;
$sslCount = 0;
$sslClientBindCount = 0;
$sslClientFailedCount = 0;
$objectclassTopCount= 0;
$pagedSearchCount = 0;
$bindCount = 0;
$filterCount = 0;
$baseCount = 0;
$scopeCount = 0;
$allOps = 0;
$allResults = 0;
$badPwdCount = 0;
$saslBindCount = 0;
$internalOpCount = 0;
$entryOpCount = 0;
$referralCount = 0;
$anyAttrs = 0;
$persistentSrchCount = 0;
$maxBerSizeCount = 0;
$connectionCount = 0;
$timerange = 0;
$simConnection = 0;
$maxsimConnection = 0;
$firstFile = 1;
$elapsedDays = 0;
$logCount = 0;
$startTLSCount = 0;
$ldapiCount = 0;
$autobindCount = 0;
$limit = 25000; # number of lines processed to trigger output

# hash files
$ATTR = "$dataLocation/attr.logconv";
$RC = "$dataLocation/rc.logconv";
$SRC = "$dataLocation/src.logconv";
$RSRC = "$dataLocation/rsrc.logconv";
$EXCOUNT = "$dataLocation/excount.logconv";
$CONN_HASH = "$dataLocation/conn_hash.logconv";
$IP_HASH = "$dataLocation/ip_hash.logconv";
$CONNCOUNT = "$dataLocation/conncount.logconv";
$NENTRIES = "$dataLocation/nentries.logconv";
$FILTER = "$dataLocation/filter.logconv";
$BASE = "$dataLocation/base.logconv";
$DS6XBADPWD = "$dataLocation/ds6xbadpwd.logconv";
$SASLMECH = "$dataLocation/saslmech.logconv";
$BINDLIST = "$dataLocation/bindlist.logconv";
$ETIME = "$dataLocation/etime.logconv";
$OID = "$dataLocation/oid.logconv";

# array files
$SRCH_CONN = "$dataLocation/srchconn.logconv";
$SRCH_OP = "$dataLocation/srchop.logconv";
$DEL_CONN = "$dataLocation/delconn.logconv";
$DEL_OP = "$dataLocation/delop.logconv";
$MOD_CONN = "$dataLocation/modconn.logconv";
$MOD_OP = "$dataLocation/modop.logconv";
$ADD_CONN = "$dataLocation/addconn.logconv";
$ADD_OP = "$dataLocation/addop.logconv";
$MODRDN_CONN = "$dataLocation/modrdnconn.logconv";
$MODRDN_OP = "$dataLocation/modrdnop.logconv";
$CMP_CONN = "$dataLocation/cmpconn.logconv";
$CMP_OP = "$dataLocation/cmpop.logconv";
$TARGET_CONN = "$dataLocation/targetconn.logconv";
$TARGET_OP = "$dataLocation/targetop.logconv";
$MSGID = "$dataLocation/msgid.logconv";
$BIND_CONN = "$dataLocation/bindconn.logconv";
$BIND_OP = "$dataLocation/bindop.logconv";
$UNBIND_CONN = "$dataLocation/unbindconn.logconv";
$UNBIND_OP = "$dataLocation/unbindop.logconv";
$EXT_CONN = "$dataLocation/extconn.logconv";
$EXT_OP = "$dataLocation/extop.logconv";
$NOTES_ETIME = "$dataLocation/notesetime.logconv";
$NOTES_CONN = "$dataLocation/notesconn.logconv";
$NOTES_OP = "$dataLocation/notesop.logconv";
$NOTES_TIME = "$dataLocation/notestime.logconv";
$NOTES_NENTRIES = "$dataLocation/notesnentries.logconv";
$BADPWDCONN = "$dataLocation/badpwdconn.logconv";
$BADPWDOP = "$dataLocation/badpwdop.logconv";
$BADPWDIP = "$dataLocation/badpwdip.logconv";

# info files
$BINDINFO = "$dataLocation/bindinfo.logconv";
$BASEINFO = "$dataLocation/baseinfo.logconv";
$FILTERINFO = "$dataLocation/filterinfo.logconv";
$SCOPEINFO = "$dataLocation/scopeinfo.logconv";

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


$conn{"A1"} = "A1";
$conn{"B1"} = "B1";
$conn{"B4"} = "B4";
$conn{"T1"} = "T1";
$conn{"T2"} = "T2";
$conn{"B2"} = "B2";
$conn{"B3"} = "B3";
$conn{"R1"} = "R1";
$conn{"P1"} = "P1";
$conn{"P2"} = "P2";
$conn{"U1"} = "U1";

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

%monthname = (
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

openDataFiles();

##########################################
#                                        #
#         Parse Access Logs              #
#                                        # 
##########################################

if ($files[$#files] =~ m/access.rotationinfo/) {  $file_count--; }

print "Processing $file_count Access Log(s)...\n\n";

#print "Filename\t\t\t   Total Lines\n";
#print "--------------------------------------------------\n";

if ($file_count > 1 && $files[0] =~ /\/access$/){
        $files[$file_count] = $files[0];
        $file_count++;
        $skipFirstFile = 1;
}
$logCount = $file_count;

for ($count=0; $count < $file_count; $count++){
	# we moved access to the end of the list, so if its the first file skip it
        if($file_count > 1 && $count == 0 && $skipFirstFile == 1){
                next;
        }
        $logsize = `wc -l $files[$count]`;
        $logsize =~ /([0-9]+)/;
        $linesProcessed = 0; $lineBlockCount = 0;
	$logCount--;
	if($logCount < 10 ){ 
		# add a zero for formatting purposes
		$logCountStr = "0" . $logCount;
	} else {
		$logCountStr = $logCount;
	}
	print sprintf "[%s] %-30s\tlines: %7s\n",$logCountStr, $files[$count], $1;
	
	open(LOG,"$files[$count]") or do { openFailed($!, $files[$count]) };
	$firstline = "yes";
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
	if($linesProcessed => $limit){print sprintf " %10s Lines Processed\n\n",--$linesProcessed;}
}

print "\n\nTotal Log Lines Analysed:  " . ($totalLineCount - 1) . "\n";

$allOps = $srchCount + $modCount + $addCount + $cmpCount + $delCount + $modrdnCount + $bindCount + $extopCount + $abandonCount + $vlvCount;

##################################################################
#                                                                #
#  Calculate the total elapsed time of the processed access logs #
#                                                                #
##################################################################

# if we are using startTime & endTime then we need to clean it up for our processing

if($startTime){
	if ($start =~ / *([0-9a-z:\/]+)/i){$start=$1;}
}
if($endTime){
	if ($end =~ / *([0-9a-z:\/]+)/i){$end =$1;}
}

#
# Get the start time in seconds
#  

$logStart = $start;

if ($logStart =~ / *([0-9A-Z\/]+)/i ){
        $logDate = $1;
        @dateComps = split /\//, $logDate;

        $timeMonth = 1 +$monthname{$dateComps[1]};
        $timeMonth = $timeMonth * 3600 *24 * 30; 
        $timeDay= $dateComps[0] * 3600 *24;
        $timeYear = $dateComps[2] *365 * 3600 * 24;
        $dateTotal = $timeMonth + $timeDay + $timeYear;
}

if ($logStart =~ / *(:[0-9:]+)/i ){
        $logTime = $1;
        @timeComps = split /:/, $logTime;

        $timeHour = $timeComps[1] * 3600;
        $timeMinute = $timeComps[2] *60;
        $timeSecond = $timeComps[3];
        $timeTotal = $timeHour + $timeMinute + $timeSecond;
}

$startTotal = $timeTotal + $dateTotal;

#
# Get the end time in seconds
#

$logEnd = $end;

if ($logEnd =~ / *([0-9A-Z\/]+)/i ){
        $logDate = $1;
        @dateComps = split /\//, $logDate;

        $endDay = $dateComps[0] *3600 * 24;
        $endMonth = 1 + $monthname{$dateComps[1]};
        $endMonth = $endMonth * 3600 * 24 * 30;
        $endYear = $endTotal + $dateComps[2] *365 * 3600 * 24 ;
        $dateTotal = $endDay + $endMonth + $endYear;
}

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
$totalTimeInSecs = $endTotal - $startTotal;
$remainingTimeInSecs = $totalTimeInSecs;

#
# Calculate the elapsed time
#

# days
while(($remainingTimeInSecs - 86400) > 0){
	$elapsedDays++;
	$remainingTimeInSecs =  $remainingTimeInSecs - 86400;

}

# hours
while(($remainingTimeInSecs - 3600) > 0){
	$elapsedHours++;
	$remainingTimeInSecs = $remainingTimeInSecs - 3600;
}

# minutes
while($remainingTimeInSecs - 60 > 0){
	$elapsedMinutes++;
	$remainingTimeInSecs = $remainingTimeInSecs - 60;
}

# seconds
$elapsedSeconds = $remainingTimeInSecs;

# Initialize empty values
if($elapsedHours eq ""){
	$elapsedHours = "0";
}
if($elapsedMinutes eq ""){
	$elapsedMinutes = "0";
}
if($elapsedSeconds eq ""){
	$elapsedSeconds = "0";
}

&closeDataFiles();


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
print " - LDAPI Conections:          $ldapiCount\n";
print "Peak Concurrent Connections:  $maxsimConnection\n";
print "Total Operations:             $allOps\n";
print "Total Results:                $allResults\n";
if ($allOps ne "0"){
 print sprintf "Overall Performance:          %.1f%\n\n" , ($perf = ($tmp = ($allResults / $allOps)*100) > 100 ? 100.0 : $tmp) ;
 }
else {
 print "Overall Performance:          No Operations to evaluate\n\n";
}

$searchStat = sprintf "(%.2f/sec)  (%.2f/min)\n",($srchCount / $totalTimeInSecs), $srchCount / ($totalTimeInSecs/60);
$modStat = sprintf "(%.2f/sec)  (%.2f/min)\n",$modCount / $totalTimeInSecs, $modCount/($totalTimeInSecs/60);
$addStat = sprintf "(%.2f/sec)  (%.2f/min)\n",$addCount/$totalTimeInSecs, $addCount/($totalTimeInSecs/60);
$deleteStat = sprintf "(%.2f/sec)  (%.2f/min)\n",$delCount/$totalTimeInSecs, $delCount/($totalTimeInSecs/60);
$modrdnStat = sprintf "(%.2f/sec)  (%.2f/min)\n",$modrdnCount/$totalTimeInSecs, $modrdnCount/($totalTimeInSecs/60);
$compareStat = sprintf "(%.2f/sec)  (%.2f/min)\n",$cmpCount/$totalTimeInSecs, $cmpCount/($totalTimeInSecs/60);
$bindCountStat = sprintf "(%.2f/sec)  (%.2f/min)\n",$bindCount/$totalTimeInSecs, $bindCount/($totalTimeInSecs/60);

format STDOUT =
Searches:                     @<<<<<<<<< @<<<<<<<<<<<<<<<<<<<<<<<<
                              $srchCount,        $searchStat
Modifications:                @<<<<<<<<< @<<<<<<<<<<<<<<<<<<<<<<<<
                              $modCount,           $modStat
Adds:                         @<<<<<<<<< @<<<<<<<<<<<<<<<<<<<<<<<<
                              $addCount,           $addStat
Deletes:                      @<<<<<<<<< @<<<<<<<<<<<<<<<<<<<<<<<<
                              $delCount,        $deleteStat
Mod RDNs:                     @<<<<<<<<< @<<<<<<<<<<<<<<<<<<<<<<<<
                              $modrdnCount,        $modrdnStat
Compares:                     @<<<<<<<<< @<<<<<<<<<<<<<<<<<<<<<<<<
                              $cmpCount,       $compareStat
Binds:                        @<<<<<<<<< @<<<<<<<<<<<<<<<<<<<<<<<<
                              $bindCount           $bindCountStat
.
write STDOUT;

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
print "VLV Unindexed Searches:       $vlvNotesCount\n";
print "SORT Operations:              $vlvSortCount\n";
print "\n";
print "Entire Search Base Queries:   $objectclassTopCount\n";
print "Paged Searches:               $pagedSearchCount\n";
print "Unindexed Searches:           $unindexedSrchCount\n";
if ($verb eq "yes" || $usage =~ /u/){
	if ($unindexedSrchCount > 0){
		%conn_hash = getHashFromFile($CONN_HASH);
		@notesConn = getArrayFromFile($NOTES_CONN);
		@notesOp = getArrayFromFile($NOTES_OP);
		@notesEtime = getArrayFromFile($NOTES_ETIME);
		@notesTime = getArrayFromFile($NOTES_TIME);
		@notesNentries = getArrayFromFile($NOTES_NENTRIES);
		getInfoArraysFromFile($BASEINFO);
		@base_val = @fileArray1;
		@base_conn = @fileArray2;
		@base_op = @fileArray3;
		getInfoArraysFromFile($SCOPEINFO);
		@scope_val = @fileArray1;
		@scope_conn = @fileArray2;
		@scope_op = @fileArray3;
		getInfoArraysFromFile($FILTERINFO);
		@filter_val = @fileArray1;
		@filter_conn = @fileArray2;
		@filter_op = @fileArray3;

		$notesCount = "1";
		for ($n = 0; $n <= $#notesEtime; $n++){
			@alreadyseenDN = ();
			if($conn_hash{$notesConn[$n]} eq ""){
				$unindexedIp = "?";
			} else {
				$unindexedIp = $conn_hash{$notesConn[$n]};
			}
			print "\n  Unindexed Search #".$notesCount."\n"; $notesCount++;
			print "  -  Date/Time:             $notesTime[$n]\n";
			print "  -  Connection Number:     $notesConn[$n]\n";
			print "  -  Operation Number:      $notesOp[$n]\n";
			print "  -  Etime:                 $notesEtime[$n]\n";
			print "  -  Nentries:              $notesNentries[$n]\n";
			print "  -  IP Address:            $unindexedIp\n";

			for ($nnn = 0; $nnn < $baseCount; $nnn++){
				if ($notesConn[$n] eq $base_conn[$nnn] && $notesOp[$n] eq $base_op[$nnn]){
					print "  -  Search Base:           $base_val[$nnn]\n";
					last;
				}
			}
			for ($nnn = 0; $nnn < $scopeCount; $nnn++){
				if ($notesConn[$n] eq $scope_conn[$nnn] && $notesOp[$n] eq $scope_op[$nnn]){
					print "  -  Search Scope:          $scope_val[$nnn]\n";
					last;
				}
			}
			for ($nnn = 0; $nnn < $filterCount; $nnn++){	
				if ($notesConn[$n] eq $filter_conn[$nnn] && $notesOp[$n] eq $filter_op[$nnn]){
					print "  -  Search Filter:         $filter_val[$nnn]\n";
					last;
				}	
			}
		}
		undef %conn_hash;
		undef @notesConn;
		undef @notesOp;
		undef @notesEtime;
		undef @notesTime;
		undef @notesNentries;
		undef @notesIp;
		undef @filter_val;
		undef @filter_conn;
		undef @filter_op;
		undef @base_val;
		undef @base_conn;
		undef @base_op;
		undef @scope_val;
		undef @scope_conn;
		undef @scope_op;	
	}
} # end of unindexed search report

print "\n";
print "FDs Taken:                    $fdTaken\n";
print "FDs Returned:                 $fdReturned\n";
print "Highest FD Taken:             $highestFdTaken\n\n";
print "Broken Pipes:                 $brokenPipeCount\n";
if ($brokenPipeCount > 0){
	foreach $key (sort { $rc{$b} <=> $rc{$a} } keys %rc) {
          if ($rc{$key} > 0){
           if ($conn{$key} eq ""){$conn{$key} = "**Unknown**";}
           push @etext, sprintf "     -  %-4s (%2s) %-40s\n",$rc{$key},$conn{$key},$connmsg{$key
};
          }
        }
        print @etext;
        print "\n";
}

print "Connections Reset By Peer:    $connResetByPeerCount\n";
if ($connResetByPeerCount > 0){
	foreach $key (sort { $src{$b} <=> $src{$a} } keys %src) {
          if ($src{$key} > 0){
           if ($conn{$key} eq ""){$conn{$key} = "**Unknown**";}
           push @retext, sprintf "     -  %-4s (%2s) %-40s\n",$src{$key},$conn{$key},$connmsg{$key
};
          }
        }
        print @retext;
	print "\n";
}

print "Resource Unavailable:         $resourceUnavailCount\n";
if ($resourceUnavailCount > 0){
	foreach $key (sort { $rsrc{$b} <=> $rsrc{$a} } keys %rsrc) {
          if ($rsrc{$key} > 0){
           if ($conn{$key} eq ""){$conn{$key} = "**Resource Issue**";}
           push @rtext, sprintf "     -  %-4s (%2s) %-40s\n",$rsrc{$key},$conn{$key},$connmsg{$key};
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
 foreach $saslb ( sort {$saslmech{$b} <=> $saslmech{$a} } (keys %saslmech) ){
	printf "    %-4s  %-12s\n",$saslmech{$saslb}, $saslb;   
 }
}

print " - Directory Manager Binds:   $rootDNBindCount\n";
print " - Anonymous Binds:           $anonymousBindCount\n";
$otherBindCount = $bindCount -($rootDNBindCount + $anonymousBindCount);
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
	for ($i=0; $i <=$#latency; $i++) {
		print "$latency[$i]\t";
	}
}

###################################
#                                 #
#    Display Open Connections     #
#                                 #
###################################

if ($verb eq "yes" || $usage =~ /p/){
	if ($openConnection[0] ne ""){
		print "\n\n----- Current Open Connection IDs ----- \n\n";
		for ($i=0; $i <= $#openConnection ; $i++) {
			if ($openConnection[$i]) {
				print "Conn Number:  $i (" . getIPfromConn($i) . ")\n";
			}
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

	%er = sort( {$b <=> $a} %er);
	for ($i = 0; $i<98; $i++){
		if ($err[$i] ne "" && $errorCode[$i] >0) {
			push @errtext, sprintf "%-8s       %12s    %-25s","err=$i",$errorCode[$i],$err[$i];
		}
	}

	for ($i = 0; $i < $#errtext; $i++){
		for ($ii = 0; $ii < $#errtext; $ii++){
			$yy="0";
			$zz="0";
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
	for ($i = 0; $i <= $#errtext; $i++){
		$errtext[$i] =~ s/\n//g;
		print  "\n" . $errtext[$i];
	} 
}

####################################
#            			   #
#     Print Failed Logins          #
# 				   #
####################################

if ($verb eq "yes" || $usage =~ /f/ ){
	if ($badPwdCount > 0){
		print "\n\n----- Top $sizeCount Failed Logins ------\n\n";

		if ($ds6x eq "true"){
			%ds6xbadpwd = getCounterHashFromFile($DS6XBADPWD);
			$ds6loop = 0;
			foreach $ds6bp (sort { $ds6xbadpwd{$b} <=> $ds6xbadpwd{$a} } keys %ds6xbadpwd) {
				if ($eloop > $sizeCount){ last; }
				printf "%-4s        %-40s\n", $ds6xbadpwd{$ds6bp}, $ds6bp;
				$ds6loop++;
			}
			undef %ds6xbadpwd;
		} else {
			getInfoArraysFromFile($BINDINFO);
			@bindVal = @fileArray1;
			@bindConn = @fileArray2;
			@bindOp = @fileArray3;
			@badPasswordConn = getArrayFromFile($BADPWDCONN);
			@badPasswordOp = getArrayFromFile($BADPWDOP);
			@badPasswordIp = getArrayFromFile($BADPWDIP);
			for ($ii =0 ; $ii < $badPwdCount; $ii++){
		 		for ($i = 0; $i < $bindCount; $i++){
					if ($badPasswordConn[$ii] eq $bindConn[$i] && $badPasswordOp[$ii] eq $bindOp[$i] ){
						$badPassword{ "$bindVal[$i]" } = $badPassword{ "$bindVal[$i]" } + 1;
					}
		 		}
			}
			# sort the new hash of $badPassword{}
			$bpTotal = 0;
			$bpCount = 0;
			foreach $badpw (sort {$badPassword{$b} <=> $badPassword{$a} } keys %badPassword){
				if ($bpCount > $sizeCount){ last;}
				$bpCount++;
				$bpTotal = $bpTotal + $badPassword{"$badpw"};
				printf "%-4s        %-40s\n", $badPassword{"$badpw"}, $badpw;
			}
			print "\nFrom the IP address(s) :\n\n";
			for ($i=0; $i<$badPwdCount; $i++) {
				print "\t\t$badPasswordIp[$i]\n";
			}
			if ($bpTotal > $badPwdCount){
				print "\n** Warning : Wrongly reported failed login attempts : ". ($bpTotal - $badPwdCount) . "\n";
			}
			undef @bindVal;
			undef @bindConn;
			undef @bindOp;
			undef @badPasswordConn;
			undef @badPasswordOp;
			undef @badPasswordIp;
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
		%conncount = &getCounterHashFromFile($CONNCOUNT);

	  	foreach $key (sort { $conncount{$b} <=> $conncount{$a} } keys %conncount) {
		  	if ($conncount{$key} > 0){
				push @conntext, sprintf "%-4s %6s   %-40s\n",$key,$conncount{$key},$connmsg{ $key };
		  	}
	  	}
		print @conntext;
		undef %conncount;
	}
}

########################################
#                                      #
#  Gather and Process all unique IPs   #
#                                      #
########################################

if ($usage =~ /i/i || $verb eq "yes"){
	%ip_hash = getTwoDimHashFromFile($IP_HASH);
	%exCount = getCounterHashFromFile($EXCOUNT);
	@ipkeys = keys %ip_hash;
	@exxCount = keys %exCount;
	$ip_count = ($#ipkeys + 1)-($#exxCount + 1); 
	if ($ip_count > 0){
	 	print "\n\n----- Top $sizeCount Clients -----\n\n";
	 	print "Number of Clients:  $ip_count\n\n";
		foreach $key (sort { $ip_hash{$b}{"count"} <=> $ip_hash{$a}{"count"} } keys %ip_hash) {
			$exc = "no";
			if ($ccount > $sizeCount){ last;}
			$ccount++;
			for ($xxx =0; $xxx <= $#excludeIP; $xxx++){
				if ($excludeIP[$xxx] eq $key){$exc = "yes";}
			}
			if ($exc ne "yes"){
				if ($ip_hash{ $key }{"count"} eq ""){$ip_hash{ $key }{"count"} = "?";}
				printf "[%s] Client: %s\n",$ccount, $key;
				printf "%10s - Connections\n", $ip_hash{ $key }{"count"};
				foreach $code (sort { $ip_hash{ $key }{$b} <=> $ip_hash{ $key }{$a} } keys %{$ip_hash{ $key }}) {
		 			if ($code eq 'count' ) { next; }
					printf "%10s - %s (%s)\n", $ip_hash{ $key }{ $code }, $code, $connmsg{ $code };
				}
				print "\n";
			}
		}
	}
	undef %exCount;
	undef %ip_hash;
}

###################################
#                                 #
#   Gather All unique Bind DN's   #
#                                 #
###################################

if ($usage =~ /b/i || $verb eq "yes"){
	%bindlist = getCounterHashFromFile($BINDLIST);
	@bindkeys = keys %bindlist;
	$bind_count = $#bindkeys + 1;
	if ($bind_count > 0){
		print "\n\n----- Top $sizeCount Bind DN's -----\n\n";
		print "Number of Unique Bind DN's: $bind_count\n\n"; 
		$bindcount = 0;
		foreach $dn (sort { $bindlist{$b} <=> $bindlist{$a} } keys %bindlist) {
		        if ($bindcount < $sizeCount){
				printf "%-8s        %-40s\n", $bindlist{ $dn },$dn;
			}
			$bindcount++;
		}
	}
	undef %bindlist;
}

#########################################
#					#
#  Gather and process search bases      #
#					#
#########################################

if ($usage =~ /a/i || $verb eq "yes"){
	%base = getCounterHashFromFile($BASE);
	@basekeys = keys %base;
	$base_count = $#basekeys + 1;
	if ($base_count > 0){
		print "\n\n----- Top $sizeCount Search Bases -----\n\n";
		print "Number of Unique Search Bases: $base_count\n\n";
		$basecount = 0;
		foreach $bas (sort { $base{$b} <=> $base{$a} } keys %base) {
		        if ($basecount < $sizeCount){
		                printf "%-8s        %-40s\n", $base{ $bas },$bas;
		        }
		        $basecount++;
		}
	}
	undef %base;
}
 
#########################################
#                                       #
#   Gather and process search filters   #
#                                       #
#########################################

if ($usage =~ /l/ || $verb eq "yes"){
	%filter = getCounterHashFromFile($FILTER);
	@filterkeys = keys %filter;
	$filter_count = $#filterkeys + 1;
	if ($filter_count > 0){
		print "\n\n----- Top $sizeCount Search Filters -----\n";  
		print "\nNumber of Unique Search Filters: $filter_count\n\n";
		$filtercount = 0;
		foreach $filt (sort { $filter{$b} <=> $filter{$a} } keys %filter){
			if ($filtercount < $sizeCount){
				printf "%-8s        %-40s\n", $filter{$filt}, $filt;
			}
			$filtercount++;
		}
	}
	undef %filter;
}

#########################################
#                                       #
# Gather and Process the unique etimes  #
#                                       # 
#########################################

if ($usage =~ /t/i || $verb eq "yes"){
	%etime = getCounterHashFromFile($ETIME);
	#
	# print most often etimes
	#
	print "\n\n----- Top $sizeCount Most Frequent etimes -----\n\n";
	$eloop = 0;
	foreach $et (sort { $etime{$b} <=> $etime{$a} } keys %etime) {
		if ($eloop == $sizeCount) { last; }
		if ($retime ne "2"){
			$first = $et;
			$retime = "2";
		}
		printf "%-8s        %-12s\n", $etime{ $et }, "etime=$et";
		$eloop++;
	}
	#
	# print longest etimes
	#
	print "\n\n----- Top $sizeCount Longest etimes -----\n\n";
	$eloop = 0;
	foreach $et (sort { $b <=> $a } (keys %etime)) {
		if ($eloop == $sizeCount) { last; }
		printf "%-12s    %-10s\n","etime=$et",$etime{ $et };
		$eloop++;
	}   
	undef %etime;
}

#######################################
#                                     #
# Gather and Process unique nentries  #
#                                     #
#######################################


if ($usage =~ /n/i || $verb eq "yes"){
	%nentries = getCounterHashFromFile($NENTRIES);
	print "\n\n----- Top $sizeCount Largest nentries -----\n\n";
	$eloop = 0;
	foreach $nentry (sort { $b <=> $a } (keys %nentries)){
		if ($eloop == $sizeCount) { last; }
	    	printf "%-18s   %12s\n","nentries=$nentry", $nentries{ $nentry };
		$eloop++;
	}
	print "\n\n----- Top $sizeCount Most returned nentries -----\n\n";
	$eloop = 0;
	foreach $nentry (sort { $nentries{$b} <=> $nentries{$a} } (keys %nentries)){
		if ($eloop == $sizeCount) { last; }
		printf "%-12s    %-14s\n", $nentries{ $nentry }, "nentries=$nentry";
		$eloop++;
	}
	print "\n";
	undef %nentries;
}

##########################################
#                                        #
# Gather and process extended operations #
#                                        #
##########################################

if ($usage =~ /x/i || $verb eq "yes"){
	if ($extopCount > 0){
		%oid = getCounterHashFromFile($OID);
		print "\n\n----- Extended Operations -----\n\n";
		foreach $oids (sort { $oid{$b} <=> $oid{$a} } (keys %oid) ){
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
			printf "%-6s      %-23s     %-60s\n", $oid{ $oids }, $oids, $oidmessage;
		}
		undef %oid;
	}
}

############################################
#                                          #	
# Print most commonly requested attributes #
#                                          #
############################################

if ($usage =~ /r/i || $verb eq "yes"){
	if ($anyAttrs > 0){
		%attr = getCounterHashFromFile($ATTR);
		print "\n\n----- Top $sizeCount Most Requested Attributes -----\n\n";
		$eloop = 0;
		foreach $mostAttr (sort { $attr{$b} <=> $attr{$a} } (keys %attr) ){
			if ($eloop eq $sizeCount){ last; }
			printf "%-10s  %-19s\n", $attr{$mostAttr}, $mostAttr;
			$eloop++;
		}
		undef %attr;
	}
}

#############################
#                           # 
# abandoned operation stats #
#                           #
#############################

if ($usage =~ /g/i || $verb eq "yes"){
	$abandonTotal = $srchCount + $delCount + $modCount + $addCount + $modrdnCount + $bindCount + $extopCount + $cmpCount;
	if ($verb eq "yes" && $abandonCount > 0 && $abandonTotal > 0){
		%conn_hash = getHashFromFile($CONN_HASH);
		@srchConn = getArrayFromFile($SRCH_CONN);
		@srchOp = getArrayFromFile($SRCH_OP);
		@delConn = getArrayFromFile($DEL_CONN);
		@delOp = getArrayFromFile($DEL_OP);
		@targetConn = getArrayFromFile($TARGET_CONN);
		@targetOp = getArrayFromFile($TARGET_OP);
		@msgid = getArrayFromFile($MSGID);
		@addConn = getArrayFromFile($ADD_CONN);
		@addOp = getArrayFromFile($ADD_OP);
		@modConn = getArrayFromFile($MOD_CONN);
		@modOp = getArrayFromFile($MOD_OP);
		@cmpConn = getArrayFromFile($CMP_CONN);
		@cmpOp = getArrayFromFile($CMP_OP);
		@modrdnConn = getArrayFromFile($MODRDN_CONN);
		@modrdnOp = getArrayFromFile($MODRDN_OP);
		@bindConn = getArrayFromFile($BIND_CONN);
		@bindOp = getArrayFromFile($BIND_OP);
		@unbindConn = getArrayFromFile($UNBIND_CONN);
		@unbindOp = getArrayFromFile($UNBIND_OP);
		@extConn = getArrayFromFile($EXT_CONN);
		@extOp = getArrayFromFile($EXT_OP);

		print "\n\n----- Abandon Request Stats -----\n\n";

		for ($g = 0; $g < $abandonCount; $g++){
			for ($sc = 0; $sc < $srchCount; $sc++){
				if ($srchConn[$sc] eq $targetConn[$g] && $srchOp[$sc] eq $targetOp[$g] ){
					print " - SRCH conn=$targetConn[$g] op=$targetOp[$g] msgid=$msgid[$g] client=$conn_hash{$targetConn[$g]}\n";	
				}
			}
			for ($dc = 0; $dc < $delCount; $dc++){
				if ($delConn[$dc] eq $targetConn[$g] && $delOp[$dc] eq $targetOp[$g]){
					print " - DEL conn=$targetConn[$g] op=$targetOp[$g] msgid=$msgid[$g] client=$conn_hash{$targetConn[$g]}\n";
				}
			}
			for ($adc = 0; $adc < $addCount; $adc++){
				if ($addConn[$adc] eq $targetConn[$g] && $addOp[$adc] eq $targetOp[$g]){
					print " - ADD conn=$targetConn[$g] op=$targetOp[$g] msgid=$msgid[$g] client=$conn_hash{$targetConn[$g]}\n";
				}
			}
			for ($mc = 0; $mc < $modCount; $mc++){
				if ($modConn[$mc] eq $targetConn[$g] && $modOp[$mc] eq $targetOp[$g]){
					print " - MOD conn=$targetConn[$g] op=$targetOp[$g] msgid=$msgid[$g] client=$conn_hash{$targetConn[$g]}\n";
				}
			}
			for ($cc = 0; $cc < $cmpCount; $cc++){
				if ($cmpConn[$mdc] eq $targetConn[$g] && $cmpOp[$mdc] eq $targetOp[$g]){
					print " - CMP conn=$targetConn[$g] op=$targetOp[$g] msgid=$msgid[$g] client=$conn_hash{$targetConn[$g]}\n";
				}
			}
			for ($mdc = 0; $mdc < $modrdnCount; $mdc++){
				if ($modrdnConn[$mdc] eq $targetConn[$g] && $modrdnOp[$mdc] eq $targetOp[$g]){
					print " - MODRDN conn=$targetConn[$g] op=$targetOp[$g] msgid=$msgid[$g] client=$conn_hash{$targetConn[$g]}\n";
				}
			}
			for ($bcb = 0; $bcb < $bindCount; $bcb++){
				if ($bindConn[$bcb] eq $targetConn[$g] && $bindOp[$bcb] eq $targetOp[$g]){
					print " - BIND conn=$targetConn[$g] op=$targetOp[$g] msgid=$msgid[$g] client=$conn_hash{$targetConn[$g]}\n";
				}
			}
			for ($ubc = 0; $ubc < $unbindCount; $ubc++){
				if ($unbindConn[$ubc] eq $targetConn[$g] && $unbindOp[$ubc] eq $targetOp[$g]){
					print " - UNBIND conn=$targetConn[$g] op=$targetOp[$g] msgid=$msgid[$g] client=$conn_hash{$targetConn[$g]}\n";
				}
			}
			for ($ec = 0; $ec < $extopCount; $ec++){
				if ($extConn[$ec] eq $targetConn[$g] && $extOp[$ec] eq $targetOp[$g]){
					print " - EXT conn=$targetConn[$g] op=$targetOp[$g] msgid=$msgid[$g] client=$conn_hash{$targetConn[$g]}\n";
				}
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
	%conncount = getCounterHashFromFile($CONNCOUNT);
	print "\n----- Recommendations -----\n";
	$recCount = "1";
	if ($unindexedSrchCount > 0){
		print "\n $recCount.  You have unindexed searches, this can be caused from a search on an unindexed attribute, or your returned results exceeded the allidsthreshold.  Unindexed searches are not recommended. To refuse unindexed searches, switch \'nsslapd-require-index\' to \'on\' under your database entry (e.g. cn=UserRoot,cn=ldbm database,cn=plugins,cn=config).\n";
		$recCount++;
	}
	if ($conncount{"T1"} > 0){
		print "\n $recCount.  You have some connections that are are being closed by the idletimeout setting. You may want to increase the idletimeout if it is set low.\n";
		$recCount++;
	}
	if ($conncount{"T2"} > 0){
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
	if ($conncount{"U1"} < ($connCodeCount - $conncount{"U1"})){
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
	undef %conncount;
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

	print "         ./logconv.pl -s 10 -V /logs/access*\n\n";
	print "         ./logconv.pl --rootDN cn=dm /logs/access*\n\n";
	print "         ./logconv.pl --sizeLimit 50 -ibgju /logs/access*\n\n";
	print "         ./logconv.pl -S \"\[28/Mar/2002:13:14:22 -0800\]\" --endTime \"\[28/Mar/2002:13:50:05 -0800\]\" -e /logs/access*\n\n";
	print "         ./logconv.pl -m log-minute-stats-csv.out /logs/access*\n\n";
	print "         ./logconv.pl -B ANONYMOUS /logs/access*\n\n";
	print "         ./logconv.pl -B \"uid=mreynolds,dc=example,dc=com\" /logs/access*\n\n";

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

	if ($lineBlockCount >= $limit){
		print STDERR sprintf" %10s Lines Processed\n",$linesProcessed;
		$lineBlockCount="0";
	}

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
		for ($excl =0; $excl <= $#excludeIP; $excl++){
			if ($excludeIP[$excl] eq $1){
				$skip = "yes";
				last;
			}
                }
                if ($skip eq "yes"){
			return ;
		}
		$ip = $1;
		if ($_ =~ /conn= *([0-9]+)/i ){
			$connList{$ip} = $connList{$ip} . " $1 ";
		}
		return;
	}
 	if (/ BIND/ && $_ =~ /dn=\"(.*)\" method/i ){
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
		if ($_ =~ /conn= *([0-9]+)/i) {
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
	$op = @_[0];
	$data = @_[1];

	if ($data =~ /conn= *([0-9]+)/i) {
		foreach $dn (keys %bindReport){
			if ($bindReport{$dn}{"conn"} =~ / $1 /){
				$bindDN = $dn;
				$bindReport{$bindDN}{$op}++;
				return;
			}
		}
	}
}

sub parseLineNormal
{
	local $_ = $logline;

	# lines starting blank are restart
	return if $_ =~ /^\s/;

	$linesProcessed++;
	$lineBlockCount++;
	if ($lineBlockCount >= $limit){ print STDERR sprintf" %10s Lines Processed\n",$linesProcessed; $lineBlockCount="0";}

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
	($time, $tzone) = split (' ', $_);
	if ($reportStats && $time ne $last_tm)
	{
		$last_tm = $time;
		$time =~ s/\[//;
		$tzone =~ s/\].*//;

		if($tzone ne $lastzone)
		{
		    # tz offset change
		    $lastzone=$tzone;
		    ($sign,$hr,$min) = $tzone =~ m/(?)(\d\d)(\d\d)/;
		    $tzoff = $hr*3600 + $min*60;
		    $tzoff *= -1
		    if $sign eq '-';
		    # to be subtracted from converted values.
		}
		($date, $hr, $min, $sec) = split (':', $time);
		($day, $mon, $yr) = split ('/', $date);
		$newmin = timegm(0, $min, $hours, $day, $monthname{$mon}, $yr) - $tzoff;
		$gmtime = $newmin + $sec;
		print_stats_block( $s_stats );
		reset_stats_block( $s_stats, $gmtime, $time.' '.$tzone );
		if ($newmin != $last_min)
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
			$attrs = $1 . " ";
			while ($attrs =~ /(\S+)\s/g){
				writeFile($ATTR, $1);
			}
		} 
		if (/ attrs=ALL/){
			writeFile($ATTR, "All Attributes");
			$anyAttrs++;
		}
		if ($verb eq "yes"){ 
			if ($_ =~ /conn= *([0-9]+)/i){ writeFile($SRCH_CONN, $1);}
			if ($_ =~ /op= *([0-9]+)/i){ writeFile($SRCH_OP, $1);}
		}
	}
	if (m/ DEL/){
		$delCount++;
		if($reportStats){ inc_stats('del',$s_stats,$m_stats); }
		if ($verb eq "yes"){
			if ($_ =~ /conn= *([0-9]+)/i){ writeFile($DEL_CONN, $1);}
			if ($_ =~ /op= *([0-9]+)/i){ writeFile($DEL_OP, $1);}
		}
	}
	if (m/ MOD dn=/){
		$modCount++;
		if($reportStats){ inc_stats('mod',$s_stats,$m_stats); }
		if ($verb eq "yes"){
		        if ($_ =~ /conn= *([0-9]+)/i){ writeFile($MOD_CONN, $1);}
			if ($_ =~ /op= *([0-9]+)/i){ writeFile($MOD_OP, $1); }
		}
	}
	if (m/ ADD/){
		$addCount++;
		if($reportStats){ inc_stats('add',$s_stats,$m_stats); }
		if ($verb eq "yes"){
		        if ($_ =~ /conn= *([0-9]+)/i){ writeFile($ADD_CONN, $1); }
			if ($_ =~ /op= *([0-9]+)/i){ writeFile($ADD_OP, $1); }
		}
	}
	if (m/ MODRDN/){
		$modrdnCount++;
		if($reportStats){ inc_stats('modrdn',$s_stats,$m_stats); }
		if ($verb eq "yes"){
		        if ($_ =~ /conn= *([0-9]+)/i){ writeFile($MODRDN_CONN, $1); }
			if ($_ =~ /op= *([0-9]+)/i){ writeFile($MODRDN_OP, $1); }
		}
	}
	if (m/ CMP dn=/){
		$cmpCount++;
		if($reportStats){ inc_stats('cmp',$s_stats,$m_stats); }
		if ($verb eq "yes"  || $usage =~ /g/i){
			if ($_ =~ /conn= *([0-9]+)/i){ writeFile($CMP_CONN, $1);}
			if ($_ =~ /op= *([0-9]+)/i){ writeFile($CMP_OP, $1);}
		}
	}
	if (m/ ABANDON /){
		$abandonCount++;
		if($reportStats){ inc_stats('abandon',$s_stats,$m_stats); }
		$allResults++;
		if ($_ =~ /targetop= *([0-9a-zA-Z]+)/i ){
			writeFile($TARGET_OP, $1);
			if ($_ =~ /conn= *([0-9]+)/i){ writeFile($TARGET_CONN, $1); }
			if ($_ =~ /msgid= *([0-9]+)/i){ writeFile($MSGID, $1);}
		}
	}
	if (m/ VLV /){
		if ($_ =~ /conn= *([0-9]+)/i){ $vlvconn[$vlvCount] = $1;}
		if ($_ =~ /op= *([0-9]+)/i){  $vlvop[$vlvCount] = $1;}
		$vlvCount++;
	}
	if (m/ authzid=/){
		$proxiedAuthCount++;
	}
	if (m/ SORT /){$vlvSortCount++}
	if (m/ version=2/){$v2BindCount++}
	if (m/ version=3/){$v3BindCount++}
	if (m/ conn=1 fd=/){$serverRestartCount++}
	if (m/ SSL connection from/){$sslCount++; if($reportStats){ inc_stats('sslconns',$s_stats,$m_stats); }}
	if (m/ connection from local to /){$ldapiCount++;}
	if($_ =~ /AUTOBIND dn=\"(.*)\"/){
		$autobindCount++;
		$bindCount++;
		if($reportStats){ inc_stats('bind',$s_stats,$m_stats); }
		if ($1 ne ""){ 
			$tmpp = $1;
			$tmpp =~ tr/A-Z/a-z/;
			writeFile($BINDLIST, $tmpp); 
			if($1 eq $rootDN){ 
				$rootDNBindCount++;
			}
		} else {
			$anonymousBindCount++;
			writeFile($BINDLIST, "Anonymous Binds");
			inc_stats('anonbind',$s_stats,$m_stats);
		}
	}
	if (m/ connection from/){
		$exc = "no";
		if ($_ =~ /connection from *([0-9A-Fa-f\.\:]+)/i ){ 
			for ($xxx =0; $xxx <= $#excludeIP; $xxx++){
				if ($excludeIP[$xxx] eq $1){$exc = "yes";}
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
		$openConnection[$connID]++;
		($time, $tzone) = split (' ', $_);
		($date, $hr, $min, $sec) = split (':', $time);
		($day, $mon, $yr) = split ('/', $date);
		$day =~ s/\[//;
		$start_time_of_connection[$connID] = timegm($sec, $min, $hours, $day, $monthname{$mon}, $yr);
	}
	if (m/ SSL client bound as /){$sslClientBindCount++;}
	if (m/ SSL failed to map client certificate to LDAP DN/){$sslClientFailedCount++;}
	if (m/ fd=/ && m/slot=/){$fdTaken++}
	if (m/ fd=/ && m/closed/){
		$fdReturned++;
		$simConnection--;

		($connID) = $_ =~ /conn=(\d*)\s/;
		$openConnection[$connID]--;
		$end_time_of_connection[$connID] = $gmtime;
		$diff = $end_time_of_connection[$connID] - $start_time_of_connection[$connID];
		$start_time_of_connection[$connID] =  $end_time_of_connection[$connID] = 0;
		if ($diff <= 1) { $latency[0]++;}
		if ($diff == 2) { $latency[1]++;}
		if ($diff == 3) { $latency[2]++;}
		if ($diff >= 4 && $diff <=5 ) { $latency[3]++;}
		if ($diff >= 6 && $diff <=10 ) { $latency[4]++;}
		if ($diff >= 11 && $diff <=15 ) { $latency[5]++;}
		if ($diff >= 16) { $latency[6] ++;}
	}
	if (m/ BIND/ && $_ =~ /dn=\"(.*)\" method/i ){
		if($reportStats){ inc_stats('bind',$s_stats,$m_stats); }
		$bindCount++;
		if ($1 ne ""){ 
			if($1 eq $rootDN){$rootDNBindCount++;}
			$tmpp = $1;
			$tmpp =~ tr/A-Z/a-z/;
			writeFile($BINDLIST, $tmpp); 
			$bindVal = $tmpp;
			if ($_ =~ /conn= *([0-9]+)/i) { $bindConn = $1; writeFile($BIND_CONN, $1);}
			if ($_ =~ /op= *([0-9]+)/i) { $bindOp = $1; writeFile($BIND_OP, $1);}
			if($usage =~ /f/ || $verb eq "yes"){
				# only need this for the failed bind report
				writeFile($BINDINFO, "$bindVal ,, $bindConn ,, $bindOp");
			}
		} else {
			$anonymousBindCount++;
			writeFile($BINDLIST, "Anonymous Binds");
			inc_stats('anonbind',$s_stats,$m_stats);
		}
	}
	if (m/ UNBIND/){
		$unbindCount++;
		if ($verb eq "yes"){
		        if ($_ =~ /conn= *([0-9]+)/i){ writeFile($UNBIND_CONN, $1); }
			if ($_ =~ /op= *([0-9]+)/i){ writeFile($UNBIND_OP, $1); }
		}
	}
	if (m/ RESULT err=/ && m/ notes=P/){
		$pagedSearchCount++;
	}
	if (m/ notes=U/){
		if ($_ =~ /conn= *([0-9]+)/i){
		        $con = $1;
		        if ($_ =~ /op= *([0-9]+)/i){ $op = $1;}
		}
		for ($i=0; $i <= $vlvCount;$i++){
		        if ($vlvconn[$i] eq $con && $vlvop[$i] eq $op){ $vlvNotesCount++; $isVlvNotes="1";}
		}
		if($isVlvNotes == 0){
			#  We don't want to record vlv unindexed searches for our regular "bad" 
			#  unindexed search stat, as VLV unindexed searches aren't that bad
			$unindexedSrchCount++;
			if($reportStats){ inc_stats('notesu',$s_stats,$m_stats); }
		}
		if ($usage =~ /u/ || $verb eq "yes"){
			if ($isVlvNnotes == 0 ){
		        	if ($_ =~ /etime= *([0-9.]+)/i ){
		                	writeFile($NOTES_ETIME, $1);
		        	}
		        	if ($_ =~ /conn= *([0-9]+)/i){
		                	writeFile($NOTES_CONN, $1);
		        	}
		        	if ($_ =~ /op= *([0-9]+)/i){
		                	writeFile($NOTES_OP, $1);
		        	}
		        	if ($_ =~ / *([0-9a-z:\/]+)/i){
		                	writeFile($NOTES_TIME, $1);
		        	}
				if ($_ =~ /nentries= *([0-9]+)/i ){
					writeFile($NOTES_NENTRIES, $1);
				}
			}
		}
		$isVlvNotes = 0;
	}
	if (m/ closed error 32/){
		$brokenPipeCount++;
		if (m/- T1/){ writeFile($RC,"T1"); }
		elsif (m/- T2/){ writeFile($RC,"T2"); }
		elsif (m/- A1/){ writeFile($RC,"A1"); }
		elsif (m/- B1/){ writeFile($RC,"B1"); }
		elsif (m/- B4/){ writeFile($RC,"B4"); }
		elsif (m/- B2/){ writeFile($RC,"B2"); }
		elsif (m/- B3/){ writeFile($RC,"B3"); }
		elsif (m/- R1/){ writeFile($RC,"R1"); }
		elsif (m/- P1/){ writeFile($RC,"P1"); }
		elsif (m/- P1/){ writeFile($RC,"P2"); }
		elsif (m/- U1/){ writeFile($RC,"U1"); }
		else { writeFile($RC,"other"); }
	}
	if (m/ closed error 131/ || m/ closed error -5961/){
		$connResetByPeerCount++;
		if (m/- T1/){ writeFile($SRC,"T1"); }
		elsif (m/- T2/){ writeFile($SRC,"T2"); }
		elsif (m/- A1/){ writeFile($SRC,"A1"); }
		elsif (m/- B1/){ writeFile($SRC,"B1"); }
		elsif (m/- B4/){ writeFile($SRC,"B4"); }
		elsif (m/- B2/){ writeFile($SRC,"B2"); }
		elsif (m/- B3/){ writeFile($SRC,"B3"); }
		elsif (m/- R1/){ writeFile($SRC,"R1"); }
		elsif (m/- P1/){ writeFile($SRC,"P1"); }
		elsif (m/- P1/){ writeFile($SRC,"P2"); }
		elsif (m/- U1/){ writeFile($SRC,"U1"); }
		else { writeFile($SRC,"other"); }
	}
	if (m/ closed error 11/){
		$resourceUnavailCount++;
		if (m/- T1/){ writeFile($RSRC,"T1"); }
		elsif (m/- T2/){ writeFile($RSRC,"T2"); }
		elsif (m/- A1/){ writeFile($RSRC,"A1"); }
		elsif (m/- B1/){ writeFile($RSRC,"B1"); }
		elsif (m/- B4/){ writeFile($RSRC,"B4"); }
		elsif (m/- B2/){ writeFile($RSRC,"B2"); }
		elsif (m/- B3/){ writeFile($RSRC,"B3"); }
		elsif (m/- R1/){ writeFile($RSRC,"R1"); }
		elsif (m/- P1/){ writeFile($RSRC,"P1"); }
		elsif (m/- P1/){ writeFile($RSRC,"P2"); }
		elsif (m/- U1/){ writeFile($RSRC,"U1"); }
		else { writeFile($RSRC,"other"); }
	}
	if ($usage =~ /g/ || $usage =~ /c/ || $usage =~ /i/ || $verb eq "yes"){
		$exc = "no";
		if ($_ =~ /connection from *([0-9A-fa-f\.\:]+)/i ) {
			for ($xxx = 0; $xxx <= $#excludeIP; $xxx++){
				if ($1 eq $excludeIP[$xxx]){
					$exc = "yes";
					writeFile($EXCOUNT,$1);
				}
			}
			$ip = $1;
			writeFile($IP_HASH, "$ip count");
			if ($_ =~ /conn= *([0-9]+)/i ){ 
				if ($exc ne "yes"){	
					writeFile($CONN_HASH, "$1 $ip");
				}
			}
		}
		if (m/- A1/){
			if ($_ =~ /conn= *([0-9]+)/i) {
				$exc = "no";
				$ip = getIPfromConn($1);
				if ($ip eq ""){$ip = "Unknown_Host";}
				for ($xxx = 0; $xxx <= $#excludeIP; $xxx++){
					if ($ip eq $excludeIP[$xxx]){$exc = "yes";}
				}
				if ($exc ne "yes"){
					writeFile($IP_HASH, "$ip A1");
					writeFile($CONNCOUNT, "A1");
					$connCodeCount++;
				}
			}
		}
		if (m/- B1/){
			if ($_ =~ /conn= *([0-9]+)/i) {
				$exc = "no";
				$ip = getIPfromConn($1);
				if ($ip eq ""){$ip = "Unknown_Host";}
				for ($xxx = 0; $xxx <= $#excludeIP; $xxx++){
				        if ($ip eq $excludeIP[$xxx]){$exc = "yes";}
				}
				if ($exc ne "yes"){
					writeFile($IP_HASH, "$ip B1");
					writeFile($CONNCOUNT, "B1");
					$connCodeCount++;	
				}
			}
		}
		if (m/- B4/){
			if ($_ =~ /conn= *([0-9]+)/i) {
				$exc = "no";
				$ip = getIPfromConn($1);
				if ($ip eq ""){$ip = "Unknown_Host";}
				for ($xxx = 0; $xxx <= $#excludeIP; $xxx++){
				        if ($ip eq $excludeIP[$xxx]){$exc = "yes";}
				}
				if ($exc ne "yes"){
					writeFile($IP_HASH, "$ip B4");
					writeFile($CONNCOUNT, "B4");
					$connCodeCount++;
				}
		    	}
		}
		if (m/- T1/){
			if ($_ =~ /conn= *([0-9]+)/i) {
				$exc = "no";
			       	$ip = getIPfromConn($1);
				if ($ip eq ""){$ip = "Unknown_Host";}
				for ($xxx = 0; $xxx <= $#excludeIP; $xxx++){
				        if ($ip eq $excludeIP[$xxx]){$exc = "yes";}
				}
				if ($exc ne "yes"){
					writeFile($IP_HASH, "$ip T1");
					writeFile($CONNCOUNT, "T1");
					$connCodeCount++;	
				}
			}
		}
		if (m/- T2/){
			if ($_ =~ /conn= *([0-9]+)/i) {
				$exc = "no"; 
				$ip = getIPfromConn($1);
				if ($ip eq ""){$ip = "Unknown_Host";}
				for ($xxx = 0; $xxx <= $#excludeIP; $xxx++){
				        if ($ip eq $excludeIP[$xxx]){$exc = "yes";}
				}
				if ($exc ne "yes"){
					writeFile($IP_HASH, "$ip T2");
					writeFile($CONNCOUNT, "T2");
					$connCodeCount++;	
				}
			}
		}
		if (m/- B2/){
			if ($_ =~ /conn= *([0-9]+)/i) {
				$exc = "no";
				$ip = getIPfromConn($1);
				$maxBerSizeCount++;
				if ($ip eq ""){$ip = "Unknown_Host";}
				for ($xxx = 0; $xxx <= $#excludeIP; $xxx++){
				        if ($ip eq $excludeIP[$xxx]){$exc = "yes";}
				}
				if ($exc ne "yes"){
					writeFile($IP_HASH, "$ip B2");
					writeFile($CONNCOUNT, "B2");
					$connCodeCount++;	
				}
			}
		}
		if (m/- B3/){
			if ($_ =~ /conn= *([0-9]+)/i) {
				$exc = "no";
				$ip = getIPfromConn($1);
				if ($ip eq ""){$ip = "Unknown_Host";}
				for ($xxx = 0; $xxx <= $#excludeIP; $xxx++){
				        if ($ip eq $excludeIP[$xxx]){$exc = "yes";}
				}
				if ($exc ne "yes"){
					writeFile($IP_HASH, "$ip B3");
					writeFile($CONNCOUNT, "B3");
					$connCodeCount++;	
				}
			}
		}
		if (m/- R1/){
			if ($_ =~ /conn= *([0-9]+)/i) {
				$exc = "no";
				$ip = getIPfromConn($1);
				if ($ip eq ""){$ip = "Unknown_Host";}
				for ($xxx = 0; $xxx <= $#excludeIP; $xxx++){
				        if ($ip eq $excludeIP[$xxx]){$exc = "yes";}
				}
				if ($exc ne "yes"){
					writeFile($IP_HASH, "$ip R1");
					writeFile($CONNCOUNT, "R1");
					$connCodeCount++;
				}
			}
		}
		if (m/- P1/){
			if ($_ =~ /conn= *([0-9]+)/i) {
				$exc = "no";
				$ip = getIPfromConn($1);
				if ($ip eq ""){$ip = "Unknown_Host";}
				for ($xxx = 0; $xxx <= $#excludeIP; $xxx++){
				        if ($ip eq $excludeIP[$xxx]){$exc = "yes";}
				}
				if ($exc ne "yes"){
					writeFile($IP_HASH, "$ip P1");
					writeFile($CONNCOUNT, "P1");
					$connCodeCount++;	
				}
			}
		}
		if (m/- P2/){
			if ($_ =~ /conn= *([0-9]+)/i) {
				$exc = "no";
				$ip = getIPfromConn($1);
				if ($ip eq ""){$ip = "Unknown_Host";}
				for ($xxx = 0; $xxx <= $#excludeIP; $xxx++){
				        if ($ip eq $excludeIP[$xxx]){$exc = "yes";}
				}
				if ($exc ne "yes"){
					writeFile($IP_HASH, "$ip P2");
					writeFile($CONNCOUNT, "P2");
					$connCodeCount++;
				}
			}
		}
		if (m/- U1/){
			if ($_ =~ /conn= *([0-9]+)/i) {
				$exc = "no";
				$ip = getIPfromConn($1);
				if ($ip eq ""){$ip = "Unknown_Host";}
				for ($xxx = 0; $xxx <= $#excludeIP; $xxx++){
				        if ($ip eq $excludeIP[$xxx]){$exc = "yes";}
				}
				if ($exc ne "yes"){
					writeFile($IP_HASH, "$ip U1");
					writeFile($CONNCOUNT, "U1");
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
	if ($_ =~ /etime= *([0-9.]+)/ ) { writeFile($ETIME, $1);}
	if ($_ =~ / tag=101 / || $_ =~ / tag=111 / || $_ =~ / tag=100 / || $_ =~ / tag=115 /){
		if ($_ =~ / nentries= *([0-9]+)/i ){ writeFile($NENTRIES, $1); }
	}
	if (m/objectclass=\*/i || m/objectclass=top/i ){
		if (m/ scope=2 /){ $objectclassTopCount++;}
	}
	if (m/ EXT oid=/){
		$extopCount++;
		if ($_ =~ /oid=\" *([0-9\.]+)/i ){ writeFile($OID,$1); }
		if ($1 && $1 eq $startTLSoid){$startTLSCount++;}
		if ($verb eq "yes"){
		        if ($_ =~ /conn= *([0-9]+)/i){ writeFile($EXT_CONN, $1); }
			if ($_ =~ /op= *([0-9]+)/i){ writeFile($EXT_OP, $1); }
		}
	}
	if ($usage =~ /l/ || $verb eq "yes"){
		if (/ SRCH / && / attrs=/ && $_ =~ /filter=\"(.*)\" /i ){
			$tmpp = $1;
			$tmpp =~ tr/A-Z/a-z/;
			$tmpp =~ s/\\22/\"/g;
			writeFile($FILTER, $tmpp); 
			$filterVal = $tmpp;
			if ($_ =~ /conn= *([0-9]+)/i) { $filterConn = $1; }
			if ($_ =~ /op= *([0-9]+)/i) { $filterOp = $1; }
		} elsif (/ SRCH / && $_ =~ /filter=\"(.*)\"/i){
			$tmpp = $1;
			$tmpp =~ tr/A-Z/a-z/;
			$tmpp =~ s/\\22/\"/g;
			writeFile($FILTER, $tmpp);
			$filterVal = $tmpp;
			if ($_ =~ /conn= *([0-9]+)/i) { $filterConn = $1; }
			if ($_ =~ /op= *([0-9]+)/i) { $filterOp = $1; }
		}
		$filterCount++;
		if($usage =~ /u/ || $verb eq "yes"){
			# we noly need this for the unindexed search report
			writeFile($FILTERINFO, "$filterVal ,, $filterConn ,, $filterOp");
		}
	}
	if ($usage =~ /a/ || $verb eq "yes"){
		if (/ SRCH /   && $_ =~ /base=\"(.*)\" scope/i ){
			if ($1 eq ""){
				$tmpp = "Root DSE";
			} else {
				$tmpp = $1;
			}
			$tmpp =~ tr/A-Z/a-z/;
			writeFile($BASE, $tmpp);
			#
			# grab the search bases & scope for potential unindexed searches
			#
			$baseVal = $tmpp;
			if ($_ =~ /scope= *([0-9]+)/i) { 
				$scopeVal = $1; 
			}
			if ($_ =~ /conn= *([0-9]+)/i) { 
				$baseConn = $1; 
				$scopeConn = $1;	
			}
			if ($_ =~ /op= *([0-9]+)/i) { 
				$baseOp = $1;
				$scopeOp = $1;
			}
			if($usage =~ /u/ || $verb eq "yes"){
				# we noly need this for the unindexed search report
				writeFile($BASEINFO, "$baseVal ,, $baseConn ,, $baseOp");
				writeFile($SCOPEINFO, "$scopeVal ,, $scopeConn ,, $scopeOp");
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
				writeFile($DS6XBADPWD, $1);
			}
			$ds6x = "true";
			$badPwdCount++;
		} elsif (/ err=49 tag=/ ){
			if ($_ =~ /conn= *([0-9]+)/i ){
				writeFile($BADPWDCONN, $1);
				$ip = getIPfromConn($1);
				$badPwdCount++;
			}
			if ($_ =~ /op= *([0-9]+)/i ){
				writeFile($BADPWDOP, $1);
			}
			writeFile($BADPWDIP, $ip);
		}
	}
	if (/ BIND / && /method=sasl/i){
		$saslBindCount++;
		if ($_ =~ /mech=(.*)/i ){     
			writeFile($SASLMECH, $1);
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

    $stats->{'results'}=0;
    $stats->{'srch'}=0;
    $stats->{'add'}=0;
    $stats->{'mod'}=0;
    $stats->{'modrdn'}=0;
    $stats->{'moddn'}=0;
    $stats->{'cmp'}=0;
    $stats->{'del'}=0;
    $stats->{'abandon'}=0;
    $stats->{'conns'}=0;
    $stats->{'sslconns'}=0;
    $stats->{'bind'}=0;
    $stats->{'anonbind'}=0;
    $stats->{'unbind'}=0;
    $stats->{'notesu'}=0;
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
					    $stats->{'notesu'} ),
			    		"\n" );
			} else {
				$stats->{'fh'}->print(
			    		"Time,time_t,Results,Search,Add,Mod,Modrdn,Moddn,Compare,Delete,Abandon,".
			    		"Connections,SSL Conns,Bind,Anon Bind,Unbind,Unindexed\n"
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
	foreach $bindDN (sort { $bindReport{$a} <=> $bindReport{$b} } keys %bindReport) {
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
	@bindConns = &cleanConns(split(' ', @_[0]));
	$IPcount = "1";

	foreach $ip ( keys %connList ){   # Loop over all the IP addresses
		foreach $bc (@bindConns){ # Loop over each bind conn number and compare it 
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
	@dirtyConns = @_;
	$#cleanConns = -1;
	$c = 0;

	for ($i = 0; $i <=$#dirtyConns; $i++){
		if($dirtyConns[$i] ne ""){
			$cleanConns[$c++] = $dirtyConns[$i];
		}
	}	
	return @cleanConns;
}

sub
printOpStats
{
	$dn = @_[0];

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
	$open_error = @_[0];
	$file_name = @_[1];
	closeDataFiles();
	removeDataFiles();
	die ("Can not open $file_name error ($open_error)");
}

sub
openDataFiles
{
	# hash files
	open ($ATTR, ">$ATTR") or do { openFailed($!, $ATTR) };
	open ($RC, ">$RC") or do { openFailed($!, $RC) };
	open ($SRC, ">$SRC") or do { openFailed($!, $SRC) };
	open ($RSRC, ">$RSRC") or do { openFailed($!, $RSRC) };
	open ($EXCOUNT, ">$EXCOUNT") or do { openFailed($!, $EXCOUNT) };
	open ($CONN_HASH, ">$CONN_HASH") or do { openFailed($!, $CONN_HASH) };
	open ($IP_HASH, ">$IP_HASH") or do { openFailed($!, $IP_HASH) };
	open ($CONNCOUNT, ">$CONNCOUNT") or do { openFailed($!, $CONNCOUNT) };
	open ($NENTRIES, ">$NENTRIES") or do { openFailed($!, $NENTRIES) };
	open ($FILTER, ">$FILTER") or do { openFailed($!, $FILTER) };
	open ($BASE, ">$BASE") or do { openFailed($!, $BASE) };
	open ($DS6XBADPWD, ">$DS6XBADPWD") or do { openFailed($!, $DS6XBADPWD) };
	open ($SASLMECH, ">$SASLMECH") or do { openFailed($!, $SASLMECH) };
	open ($BINDLIST, ">$BINDLIST") or do { openFailed($!, $BINDLIST) };
	open ($ETIME, ">$ETIME") or do { openFailed($!, $ETIME) };
	open ($OID, ">$OID") or do { openFailed($!, $OID) };

	# array files
	open($SRCH_CONN,">$SRCH_CONN") or do { openFailed($!, $SRCH_CONN) };
	open($SRCH_OP, ">$SRCH_OP") or do { openFailed($!, $SRCH_OP) };
	open($DEL_CONN, ">$DEL_CONN") or do { openFailed($!, $DEL_CONN) };
	open($DEL_OP, ">$DEL_OP") or do { openFailed($!, $DEL_OP) };
	open($MOD_CONN, ">$MOD_CONN") or do { openFailed($!, $MOD_CONN) };
	open($MOD_OP, ">$MOD_OP") or do { openFailed($!, $MOD_OP) };
	open($ADD_CONN, ">$ADD_CONN") or do { openFailed($!, $ADD_CONN) };
	open($ADD_OP, ">$ADD_OP") or do { openFailed($!, $ADD_OP) };
	open($MODRDN_CONN, ">$MODRDN_CONN") or do { openFailed($!, $MODRDN_CONN) };
	open($MODRDN_OP, ">$MODRDN_OP") or do { openFailed($!, $MODRDN_OP) };
	open($CMP_CONN, ">$CMP_CONN") or do { openFailed($!, $CMP_CONN) };
	open($CMP_OP,">$CMP_OP") or do { openFailed($!, $CMP_OP) };
	open($TARGET_CONN, ">$TARGET_CONN") or do { openFailed($!, $TARGET_CONN) };
	open($TARGET_OP, ">$TARGET_OP") or do { openFailed($!, $TARGET_OP) };
	open($MSGID, ">$MSGID") or do { openFailed($!, $MSGID) };
	open($BIND_CONN, ">$BIND_CONN") or do { openFailed($!, $BIND_CONN) };
	open($BIND_OP, ">$BIND_OP") or do { openFailed($!, $BIND_OP) };
	open($UNBIND_CONN, ">$UNBIND_CONN") or do { openFailed($!, $UNBIND_CONN) };
	open($UNBIND_OP, ">$UNBIND_OP") or do { openFailed($!, $UNBIND_OP) };
	open($EXT_CONN, ">$EXT_CONN") or do { openFailed($!, $EXT_CONN) };
	open($EXT_OP, ">$EXT_OP") or do { openFailed($!, $EXT_OP) };
	open($NOTES_ETIME, ">$NOTES_ETIME") or do { openFailed($!, $NOTES_ETIME) };
	open($NOTES_CONN, ">$NOTES_CONN") or do { openFailed($!, $NOTES_CONN) };
	open($NOTES_OP, ">$NOTES_OP") or do { openFailed($!, $NOTES_OP) };
	open($NOTES_TIME, ">$NOTES_TIME") or do { openFailed($!, $NOTES_TIME) };
	open($NOTES_NENTRIES, ">$NOTES_NENTRIES") or do { openFailed($!, $NOTES_NENTRIES) };
	open($BADPWDCONN, ">$BADPWDCONN")  or do { openFailed($!, $BADPWDCONN) };
	open($BADPWDOP, ">$BADPWDOP")  or do { openFailed($!, $BADPWDOP) };
	open($BADPWDIP, ">$BADPWDIP")  or do { openFailed($!, $NADPWDIP) };

	# info files
	open($BINDINFO, ">$BINDINFO")  or do { openFailed($!, $BINDINFO) };
	open($BASEINFO, ">$BASEINFO")  or do { openFailed($!, $BASEINFO) };
	open($SCOPEINFO, ">$SCOPEINFO")  or do { openFailed($!, $SCOPEINFO) };
	open($FILTERINFO, ">$FILTERINFO")  or do { openFailed($!, $FILTERINFO) };
}

sub
closeDataFiles
{
	close $ATTR;
	close $RC;
	close $SRC;
	close $RSRC;
	close $EXCOUNT;
	close $CONN_HASH;
	close $IP_HASH;
	close $CONNCOUNT;
	close $NENTRIES;
	close $FILTER;
	close $BASE;
	close $DS6XBADPWD;
	close $SASLMECH;
	close $BINDLIST;
	close $ETIME;
	close $OID;

	# array files
	close $SRCH_CONN;
	close $SRCH_OP;
	close $DEL_CONN;
	close $DEL_OP;
	close $MOD_CONN;
	close $MOD_OP;
	close $ADD_CONN;
	close $ADD_OP;
	close $MODRDN_CONN;
	close $MODRDN_OP;
	close $CMP_CONN;
	close $CMP_OP;
	close $TARGET_CONN;
	close $TARGET_OP;
	close $MSGID;
	close $BIND_CONN;
	close $BIND_OP;
	close $UNBIND_CONN;
	close $UNBIND_OP;
	close $EXT_CONN;
	close $EXT_OP;
	close $NOTES_ETIME;
	close $NOTES_CONN;
	close $NOTES_OP;
	close $NOTES_TIME;
	close $NOTES_NENTRIES;
	close $BADPWDCONN;
	close $BADPWDOP;
	close $BADPWDIP;

	# info files
	close $BINDINFO;
	close $BASEINFO;
	close $SCOPEINFO;
	close $FILTERINFO;
}

sub
removeDataFiles
{
	unlink $ATTR;
	unlink $RC;
	unlink $SRC;
	unlink $RSRC;
	unlink $EXCOUNT;
	unlink $CONN_HASH;
	unlink $IP_HASH;
	unlink $CONNCOUNT;
	unlink $NENTRIES;
	unlink $FILTER;
	unlink $BASE;
	unlink $DS6XBADPWD;
	unlink $SASLMECH;
	unlink $BINDLIST;
	unlink $ETIME;
	unlink $OID;

	# array files
	unlink $SRCH_CONN;
	unlink $SRCH_OP;
	unlink $DEL_CONN;
	unlink $DEL_OP;
	unlink $MOD_CONN;
	unlink $MOD_OP;
	unlink $ADD_CONN;
	unlink $ADD_OP;
	unlink $MODRDN_CONN;
	unlink $MODRDN_OP;
	unlink $CMP_CONN;
	unlink $CMP_OP;
	unlink $TARGET_CONN;
	unlink $TARGET_OP;
	unlink $MSGID;
	unlink $BIND_CONN;
	unlink $BIND_OP;
	unlink $UNBIND_CONN;
	unlink $UNBIND_OP;
	unlink $EXT_CONN;
	unlink $EXT_OP;
	unlink $NOTES_ETIME;
	unlink $NOTES_CONN;
	unlink $NOTES_OP;
	unlink $NOTES_TIME;
	unlink $NOTES_NENTRIES;
	unlink $BADPWDCONN;
	unlink $BADPWDOP;
	unlink $BADPWDIP;

	# info files
	unlink $BINDINFO;
	unlink $BASEINFO;
	unlink $SCOPEINFO;
	unlink $FILTERINFO;
}

sub
getIPfromConn
{
	$connip = @_[0];
	$retval = "";

	close $CONN_HASH; # we can not read the file is its already open
	open(CONN,"$CONN_HASH") or do { openFailed($!, $CONN_HASH) };
	while (<CONN>){
		if($_ =~ /$connip (.*)/){
			$retval = $1;
			last;
		}
	}
	close CONN;
	#reopen file for writing(append)
	open($CONN_HASH,">>$CONN_HASH") or do { openFailed($!, $CONN_HASH) };

	return $retval;
}

sub
writeFile
{
	$file = @_[0];
	$text = @_[1] . "\n";

	print $file $text;
}

# This hash file stores one value per line
sub
getCounterHashFromFile
{
	$file = @_[0];
	my %hash = ();

	open(FILE,"$file") or do { openFailed($!, $file) };
	while(<FILE>){
		chomp;
		$hash{$_}++;
	}
	close FILE;

	return %hash;
}

# this hash file stores two values per line (2 dimension hash)
sub
getTwoDimHashFromFile
{
	$file = @_[0];
	my %hash = ();

	open(FILE,"$file") or do { openFailed($!, $file) };
	while(<FILE>){
		@parts = split (' ', $_);
		chomp(@parts);
		$hash{$parts[0]}{$parts[1]}++;
	}
	close FILE;

	return %hash;
}

# this hash file stores two values per line (1 dimension hash)
sub
getHashFromFile
{
	$file = @_[0];
	my %hash = ();
	@parts = ();

	open(FILE,"$file") or do { openFailed($!, $file ) };
	while(<FILE>){
		@parts = split (' ',$_);
		chomp(@parts);
		$hash{$parts[0]} = $parts[1];
	}
	close FILE;

	return %hash;
}

# Return array of values from the file
sub
getArrayFromFile
{
	my @arry;
	$file = @_[0];
	$array_count = 0;

	open(FILE,"$file") or do { openFailed($!, $file) };
	while(<FILE>){
		chomp;
		$arry[$array_count] = $_;
		$array_count++;
	}
	close FILE;

	return @arry;
}

# build the three array
sub
getInfoArraysFromFile
{
	$file = @_[0];
	$array_count = 0;
	@parts = ();

	open(FILE,"<$file") or do { openFailed($!, $file) };
	while(<FILE>){
		@parts = split (' ,, ',$_);
		chomp(@parts);
		if($#parts > 0){
			$fileArray1[$array_count] = $parts[0];
			$fileArray2[$array_count] = $parts[1];
			$fileArray3[$array_count] = $parts[2];
			$array_count++;
		}
	}
	close FILE;
}



#######################################
#                                     #
#             The  End                #
#                                     #
#######################################

