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
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#

#
# Check for usage
#
use Time::Local;
use IO::File;

if ($#ARGV < 0){;
&displayUsage;
}

#######################################
#                                     #
# parse commandline switches          #
#                                     #
#######################################

$x = "0";
$fc = 0;
$sn = 0;
$manager = "cn=directory manager";
$logversion = "6.1";
$sizeCount = "20";
$startFlag = 0;
$startTime = 0;
$endFlag = 0;
$endTime = 0;
$s_stats = new_stats_block( );
$m_stats = new_stats_block( );


while ($sn <= $#ARGV)
{
  if ("$ARGV[$sn]" eq "-d")
  {
    $manager = $ARGV[++$sn];
  }
  elsif ("$ARGV[$sn]" eq "-v")
  {
    print "Access Log Analyzer v$logversion\n";;
    exit (0);
  }
  elsif ("$ARGV[$sn]" eq "-V")
  {
    $verb = "yes";
  }
  elsif ("$ARGV[$sn]" eq "-X"){
	$exclude[$x] = $ARGV[++$sn];
	$x++;
  }
  elsif ("$ARGV[$sn]" eq "-s")
  {
	$sizeCount = $ARGV[++$sn];
  }
  elsif ("$ARGV[$sn]" eq "-S")
  {
	$startTime = $ARGV[++$sn];
  }
  elsif ("$ARGV[$sn]" eq "-E")
  {
	$endTime = $ARGV[++$sn];
  }
  elsif ("$ARGV[$sn]" eq "-m")
  {
	$s_stats = new_stats_block( $ARGV[++$sn] );
  }
  elsif ("$ARGV[$sn]" eq "-M")
  {
	$m_stats = new_stats_block( $ARGV[++$sn] );
  }
  elsif ("$ARGV[$sn]" eq "-h")
  {
    &displayUsage;
  }
  elsif ("$ARGV[$sn]" =~ m/^-/){
	$usage = $ARGV[$sn];
  }
  else
  {
    $files[$fc] = $ARGV[$sn];
    $fc++;
  }
  $sn++;
}

if ($sizeCount eq "all"){$sizeCount = "100000";}

#######################################
#                                     #
# Initialize Arrays and variables     #
#                                     #
#######################################

print "\nAccess Log Analyzer $logversion\n";
print "\nCommand : logconv.pl @ARGV\n\n";

$dirmgr = "0";
$notes = "0";
$vlvnotes= "0";
$search = "0";
$fdtake = "0";
$fdreturn = "0";
$highfd = "0";
$bind = "0";
$unbind = "0";
$anony = "0";
$mod = "0";
$delete = "0";
$add = "0";
$modrdn = "0";
$moddn = "0";
$compare = "0";
$proxiedAuth = "0";
$restarts = "0";
$resource = "0";
$broken = "0";
$vlv = "0";
$version2 = "0";
$version3 = "0";
$sortvlv = "0";
$reset = "0";
$vet = "0";
$v = "0";
$errorck = "0";
$errorsucc = "0";
$sslconn = "0";
$sslClientBind = "0";
$sslClientFailed = "0";
$objectclass= "0";
$nc = "0";
$no = "0";
$nt = "0";
$nb = "0";
$bc = "0";
$fcc = "0";
$nent = "0";
$allOps = "0";
$allResults = "0";
$bpc = "0";
$bpo = "0";
$bpi = 0;
$abandon = "0";
$mmasterop = "0";
$extendedop = "0";
$sasl = "0";
$internal = "0";
$entryOp = "0";
$referral = "0";
$anyAttrs = "0";
$persistent = "0";
$sconn = "0";
$dconn = "0";
$aconn = "0";
$mconn = "0";
$mdconn = "0";
$mddconn = "0";
$bconn = "0";
$ubconn = "0";
$econn = "0";
$cconn = "0";
$connectionCount = "0";
$timerange = 0;
$simConnection = 0;
$maxsimConnection = 0;
$firstFile = "1";
$elapsedDays = "0";
$logCount = "0";
$limit = "10000"; # number of lines processed to trigger output

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

##########################################
#                                        #
#         Parse Access Logs              #
#                                        # 
##########################################

if ($files[$#files] =~ m/access.rotationinfo/) {  $fc--; }

print "Processing $fc Access Log(s)...\n\n";

print "Filename\t\t\t   Total Lines\n";
print "--------------------------------------------------\n";

$ofc = $fc;

if ($fc > 1 && $files[0] =~ /\/access$/){
        $files[$fc] = $files[0];
        $fc++;
        $skipFirstFile = "1";
}

for ($count=0; $count < $fc; $count++){
	# we moved access to the end of the list, so if its the first file skip it
        if($fc > 1 && $count == 0 && $skipFirstFile eq "1"){
                next;
        }
        $logCount++;
        $logsize = `wc -l $files[$count]`;
        $logsize =~ /([0-9]+)/;
        $ff="";$iff="";
	if($logCount < 10 ){ 
		# add a zero for formatting purposes
		$lc = "0" . $logCount;
	} else {
		$lc = $logCount;
	}
	print sprintf "[%s] %-30s %7s\n",$lc, $files[$count], $1;

	open(LOG,"$files[$count]") || die "Error: Can't open file $infile: $!";

	$firstline = "yes";
	while (<LOG>) {
		unless ($endFlag) {
			if ($firstline eq "yes"){
				if (/^\[/) {
                        		$tline = $_;
                        		$firstline = "no";
				}
				$ff++;$iff++;
                	} elsif (/^\[/ && $firstline eq "no"){
                         	&parseLine($tline);
                         	$tline = $_;
                	} else {
                        	$tline = $tline . $_;
                        	$tline =~ s/\n//;
                	}
		}
	}
	&parseLine($tline);
	close (LOG);
	print_stats_block( $s_stats );
	print_stats_block( $m_stats );
	$tlc = $tlc + $ff;
	if($ff => $limit){print sprintf " %10s Lines Processed\n\n",--$ff;}
}

print "\n\nTotal Log Lines Analysed:  " . ($tlc - 1) . "\n";

$allOps = $search + $mod + $add + $delete + $modrdn + $bind + $extendedop;

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
#  Get the start time in seconds
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
#  Get the end time in seconds
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
#  Tally the numbers
#
$totalTimeInSecs = $endTotal - $startTotal;
$remainingTimeInSecs = $totalTimeInSecs;

#
#  Calculate the elapsed time
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

#  minutes
while($remainingTimeInSecs - 60 > 0){
	$elapsedMinutes++;
	$remainingTimeInSecs = $remainingTimeInSecs - 60;
}

#seconds
$elapsedSeconds = $remainingTimeInSecs;

#####################################
#                                   #
#     Display Basic Results         #
#                                   #
#####################################


print "\n\n----------- Access Log Output ------------\n";
print "\nStart of Log:    $start\n";
print "End of Log:      $end\n\n";
if($elapsedDays eq "0"){
        print "Processed Log Time:  $elapsedHours Hours, $elapsedMinutes Minutes, $elapsedSeconds Seconds\n\n";
} else {
        print "Processed Log Time:  $elapsedDays Days, $elapsedHours Hours, $elapsedMinutes Minutes, $elapsedSeconds Seconds\n\n";
}
print "Restarts:                     $restarts\n";
print "Total Connections:            $connectionCount\n";
print "SSL Connections:              $sslconn\n";
print "Peak Concurrent Connections:  $maxsimConnection\n";
print "Total Operations:             $allOps\n";
print "Total Results:                $allResults\n";
if ($allOps ne "0"){
 print sprintf "Overall Performance:          %.1f%\n\n" , ($perf = ($tmp = ($allResults / $allOps)*100) > 100 ? 100.0 : $tmp) ;
 }
else {
 print "Overall Performance:          No Operations to evaluate\n\n";
}

$searchStat = sprintf "(%.2f/sec)  (%.2f/min)\n",($search / $totalTimeInSecs), $search / ($totalTimeInSecs/60);
$modStat = sprintf "(%.2f/sec)  (%.2f/min)\n",$mod / $totalTimeInSecs, $mod/($totalTimeInSecs/60);
$addStat = sprintf "(%.2f/sec)  (%.2f/min)\n",$add/$totalTimeInSecs, $add/($totalTimeInSecs/60);
$deleteStat = sprintf "(%.2f/sec)  (%.2f/min)\n",$delete/$totalTimeInSecs, $delete/($totalTimeInSecs/60);
$modrdnStat = sprintf "(%.2f/sec)  (%.2f/min)\n",$modrdn/$totalTimeInSecs, $modrdn/($totalTimeInSecs/60);
$moddnStat = sprintf "(%.2f/sec)  (%.2f/min)\n",$moddn/$totalTimeInSecs, $moddn/($totalTimeInSecs/60);
$compareStat = sprintf "(%.2f/sec)  (%.2f/min)\n",$compare/$totalTimeInSecs, $compare/($totalTimeInSecs/60);
$bindStat = sprintf "(%.2f/sec)  (%.2f/min)\n",$bind/$totalTimeInSecs, $bind/($totalTimeInSecs/60);

format STDOUT =
Searches:                     @<<<<<<<<< @<<<<<<<<<<<<<<<<<<<<<<<<
                              $search,        $searchStat
Modifications:                @<<<<<<<<< @<<<<<<<<<<<<<<<<<<<<<<<<
                              $mod,           $modStat
Adds:                         @<<<<<<<<< @<<<<<<<<<<<<<<<<<<<<<<<<
                              $add,           $addStat
Deletes:                      @<<<<<<<<< @<<<<<<<<<<<<<<<<<<<<<<<<
                              $delete,        $deleteStat
Mod RDNs:                     @<<<<<<<<< @<<<<<<<<<<<<<<<<<<<<<<<<
                              $modrdn,        $modrdnStat
Mod DNs:                      @<<<<<<<<< @<<<<<<<<<<<<<<<<<<<<<<<<
                              $moddn,         $moddnStat
Compares:                     @<<<<<<<<< @<<<<<<<<<<<<<<<<<<<<<<<<
                              $compare,       $compareStat
Binds:                        @<<<<<<<<< @<<<<<<<<<<<<<<<<<<<<<<<<
                              $bind           $bindStat
.
write STDOUT;

print "\n";
print "Proxied Auth Operations:      $proxiedAuth\n";
print "Persistent Searches:          $persistent\n";
print "Internal Operations:          $internal\n";
print "Entry Operations:             $entryOp\n";
print "Extended Operations:          $extendedop\n";
print "Abandoned Requests:           $abandon\n";
print "Smart Referrals Received:     $referral\n";
print "\n";
print "VLV Operations:               $vlv\n";
print "VLV Unindexed Searches:       $vlvnotes\n";
print "SORT Operations:              $sortvlv\n";
print "\n";
print "Entire Search Base Queries:   $objectclass\n";
print "Unindexed Searches:           $notes\n";
if ($verb eq "yes" || $usage =~ /u/){
if ($notes > 0){
	$ns = "1";
	for ($n = 0; $n <= $#notesEtime; $n++){
		@alreadyseenDN = ();
		print "\n  Unindexed Search #".$ns."\n";
		$ns++;
		print "  -  Date/Time:             $notesTime[$n]\n";
		print "  -  Connection Number:     $notesConn[$n]\n";
		print "  -  Operation Number:      $notesOp[$n]\n";
		print "  -  Etime:                 $notesEtime[$n]\n";
		print "  -  Nentries:              $notesNentries[$n]\n";
		print "  -  IP Address:            $conn_hash{$notesConn[$n]}\n";
		print "  -  Search Base:           $notesBase[$n]\n";
		print "  -  Scope:                 $notesScope[$n]\n";
		for ($nn = 0; $nn <= $bc; $nn++){
			if ($notesConn[$n] eq $bindInfo[$nn][1]) {

					## Here, we check if the bindDN is already printed.
					## If not, we print it and push it to @alreadyseenDN.
					## So, in the beginning, we iterate thru @alreadyseenDN

					for ($j=0, $DNisThere=0; $j <=$#alreadyseenDN; $j++) {
						if ($alreadyseenDN[$j] eq $bindInfo[$nn][0]) {
							$DNisThere = 1;
						}
					}
					unless ($DNisThere) {
						print "  -  Bind DN:               $bindInfo[$nn][0]\n";
						push @alreadyseenDN, $bindInfo[$nn][0];
					}
			}
		}
		for ($nnn = 0; $nnn <= $fcc; $nnn++){	
			if ($notesConn[$n] eq $filterInfo[$nnn][1] && $notesOp[$n] eq $filterInfo[$nnn][2]){
				print "  -  Search Filter:         $filterInfo[$nnn][0]\n";
			}	
		}
	}
}

}

print "\n";
print "FDs Taken:                    $fdtake\n";
print "FDs Returned:                 $fdreturn\n";
print "Highest FD Taken:             $highfd\n\n";
print "Broken Pipes:                 $broken\n";
if ($broken > 0){
	foreach $key (sort { $rc{$b} <=> $rc{$a} } keys %rc) {
          if ($rc{$key} > 0){
           if ($conn{$key} eq ""){$conn{$key} = "** Unknown **";}
           push @etext, sprintf "     -  %-4s (%2s) %-40s\n",$rc{$key},$conn{$key},$connmsg{$key
};
          }
        }
        print @etext;
        print "\n";
}

print "Connections Reset By Peer:    $reset\n";
if ($reset > 0){
	foreach $key (sort { $src{$b} <=> $src{$a} } keys %src) {
          if ($src{$key} > 0){
           if ($conn{$key} eq ""){$conn{$key} = "** Unknown **";}
           push @retext, sprintf "     -  %-4s (%2s) %-40s\n",$src{$key},$conn{$key},$connmsg{$key
};
          }
        }
        print @retext;
	print "\n";
}

print "Resource Unavailable:         $resource\n";
if ($resource > 0){
	foreach $key (sort { $rsrc{$b} <=> $rsrc{$a} } keys %rsrc) {
          if ($rsrc{$key} > 0){
           if ($conn{$key} eq ""){$conn{$key} = "** Resource Issue **";}
           push @rtext, sprintf "     -  %-4s (%2s) %-40s\n",$rsrc{$key},$conn{$key},$connmsg{$key};
          }
  	}
  	print @rtext;
}
print "\n";
print "Binds:                        $bind\n";
print "Unbinds:                      $unbind\n";
print "\n LDAP v2 Binds:               $version2\n";
print " LDAP v3 Binds:               $version3\n";
print " SSL Client Binds:            $sslClientBind\n";
print " Failed SSL Client Binds:     $sslClientFailed\n";
print " SASL Binds:                  $sasl\n";
if ($sasl > 0){
 foreach $saslb ( sort {$saslmech{$b} <=> $saslmech{$a} } (keys %saslmech) ){
	printf "  %-4s  %-12s\n",$saslmech{$saslb}, $saslb;   
 }
}

print "\n Directory Manager Binds:     $dirmgr\n";
print " Anonymous Binds:             $anony\n";
$other = $bind -($dirmgr + $anony);
print " Other Binds:                 $other";

if ($verb eq "yes" || $usage =~ /y/){
print "\n\n----- Connection Latency Details -----\n\n";
print " (in seconds)\t\t<=1\t2\t3\t4-5\t6-10\t11-15\t>15\n";
print " --------------------------------------------------------------------------\n";
print " (# of connections)\t";
for ($i=0; $i <=$#latency; $i++) {
	print "$latency[$i]\t";
}
}

if ($verb eq "yes" || $usage =~ /p/){
print "\n\n----- Current Open Connection IDs ----- \n\n";
for ($i=0; $i <= $#openConnection ; $i++) {
	if ($openConnection[$i]) {
		print "\t$i\n";
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
      if ($err[$i] ne "" && $er[$i] >0) {
                push @errtext, sprintf "%-8s       %12s    %-25s","err=$i",$er[$i],$err[$i];
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

if ($verb eq "yes" || $usage =~ /f/i ){
if ($bpc > 0){
print "\n\n----- Top $sizeCount Failed Logins ------\n\n";

if ($ds6x eq "true"){
	$eloop = "0";
	foreach $dsbp (sort { $ds6xbadpwd{$b} <=> $ds6xbadpwd{$a} } keys %ds6xbadpwd) {
		if ($eloop > $sizeCount){ last; }
		printf "%-4s        %-40s\n", $ds6xbadpwd{$dsbp}, $dsbp;
	}

} else { 
for ($ii =0 ; $ii < $bpc; $ii++){
 for ($i = 0; $i < $bc; $i++){
	if ($badPasswordConn[$ii] eq $bindInfo[$i][1] && $badPasswordOp[$ii] eq $bindInfo[$i][2] ){
		$badPassword{ "$bindInfo[$i][0]" } = $badPassword{ "$bindInfo[$i][0]" } + 1;
	}
 }
}

# sort the new list of $badPassword{}

$bpTotal = "0";
$bpCount = "0";
foreach $badpw (sort {$badPassword{$b} <=> $badPassword{$a} } keys %badPassword){
	if ($bpCount > $sizeCount){ last;}
	$bpCount++;
	$bpTotal = $bpTotal + $badPassword{"$badpw"};
	printf "%-4s        %-40s\n", $badPassword{"$badpw"}, $badpw;
}

print "\nFrom the IP address(s) :\n\n";
for ($i=0; $i<$bpi; $i++) {
	print "\t\t$badPasswordIp[$i]\n";
}

if ($bpTotal > $bpc){
	print "\n** Warning : Wrongly reported failed login attempts : ". ($bpTotal - $bpc) . "\n";
}
}  # this ends the if $ds6x = true

}

}


####################################
#                                  #
#     Print Connection Codes       #
#                                  #
####################################


if ($concount > 0){
if ($usage =~ /c/i || $verb eq "yes"){
 print "\n\n----- Total Connection Codes -----\n\n";

  foreach $key (sort { $conncount{$b} <=> $conncount{$a} } keys %conncount) {
          if ($conncount{$key} > 0){
                  push @conntext, sprintf "%-4s                 %6s    %-40s\n",$conn{ $key },$conncount{$key},$connmsg{ $key };
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
			for ($xxx =0; $xxx <= $#exclude; $xxx++){
				if ($exclude[$xxx] eq $key){$exc = "yes";}
			}
			if ($exc ne "yes"){
				if ($ip_hash{ $key }{"count"} eq ""){$ip_hash{ $key }{"count"} = "*";}
                		printf "%-6s %-17s\n", $ip_hash{ $key }{"count"}, $key;
			}

     			if ($exc ne "yes"){ 	
				foreach $code (sort { $ip_hash{ $key }{$b} <=> $ip_hash{ $key }{$a} } keys %{$ip_hash{ $key }}) {
		 			if ($code eq 'count' ) { next; }
                        		printf "\t\t %6s - %3s   %s\n", $ip_hash{ $key }{ $code }, $code, $connmsg{ $code };
			        }
     			}
                	
                	if ($exc ne "yes"){ print "\n";}
        }
}
}



###################################
#                                 #
#   Gather All unique Bind DN's   #
#                                 #
###################################

if ($usage =~ /b/i || $verb eq "yes"){
@bindkeys = keys %bindlist;
$bind_count = $#bindkeys + 1;
if ($bind_count > 0){
        print "\n----- Top $sizeCount Bind DN's -----\n\n";
        print "Number of Unique Bind DN's: $bind_count\n\n"; 

	$bindcount = 0;
        
        foreach $dn (sort { $bindlist{$b} <=> $bindlist{$a} } keys %bindlist) {
                if ($bindcount < $sizeCount){
			printf "%-8s        %-40s\n", $bindlist{ $dn },$dn;
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

}
 
#########################################
#                                       #
#   Gather and process search filters   #
#                                       #
#########################################

if ($usage =~ /l/ || $verb eq "yes"){

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

}

#########################################
#                                       #
# Gather and Process the unique etimes  #
#                                       # 
#########################################


#
# print most often etimes
#

if ($usage =~ /t/i || $verb eq "yes"){
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
        
}

#######################################
#                                     #
# Gather and Process unique nentries  #
#                                     #
#######################################


if ($usage =~ /n/i || $verb eq "yes"){
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
}


##########################################
#                                        #
# Gather and process extended operations #
#                                        #
##########################################

if ($usage =~ /x/i || $verb eq "yes"){

if ($extendedop > 0){
print "\n\n----- Extended Operations -----\n\n";
foreach $oids (sort { $oid{$b} <=> $oid{$a} } (keys %oid) ){

	if ($oids eq "2.16.840.1.113730.3.5.1"){ $oidmessage = "Transaction Request"}
	elsif ($oids eq "2.16.840.1.113730.3.5.2"){ $oidmessage = "Transaction Response"}
	elsif ($oids eq "2.16.840.1.113730.3.5.3"){ $oidmessage = "Start Replication Request (incremental update)"}
	elsif ($oids eq "2.16.840.1.113730.3.5.4"){ $oidmessage = "Replication Response"}
	elsif ($oids eq "2.16.840.1.113730.3.5.5"){ $oidmessage = "End Replication Request (incremental update)"}
	elsif ($oids eq "2.16.840.1.113730.3.5.6"){ $oidmessage = "Replication Entry Request"}
	elsif ($oids eq "2.16.840.1.113730.3.5.7"){ $oidmessage = "Start Bulk Import"}
	elsif ($oids eq "2.16.840.1.113730.3.5.8"){ $oidmessage = "Finished Bulk Import"}
	elsif ($oids eq "2.16.840.1.113730.3.6.1"){ $oidmessage = "Incremental Update Replication Protocol"}
	elsif ($oids eq "2.16.840.1.113730.3.6.2"){ $oidmessage = "Total Update Replication Protocol (Initialization)"}
	elsif ($oids eq "2.16.840.1.113730.3.5.9"){ $oidmessage = "Digest Authentication"}
	else {$oidmessage = "Other"}

	printf "%-6s      %-23s     %-60s\n", $oid{ $oids }, $oids, $oidmessage;
}
}

}

############################################
#					   	
# Print most commonly requested attributes 
#					   
############################################

if ($usage =~ /r/i || $verb eq "yes"){
if ($anyAttrs > 0){
print "\n\n----- Top $sizeCount Most Requested Attributes -----\n\n";
$eloop = "0";
foreach $mostAttr (sort { $attr{$b} <=> $attr{$a} } (keys %attr) ){
	if ($eloop eq $sizeCount){ last; }
	printf "%-10s  %-19s\n", $attr{$mostAttr}, $mostAttr;
	$eloop++;
}
}

}

#################################
#
# abandoned operation stats 
#				
#################################

if ($usage =~ /g/i || $verb eq "yes"){
$acTotal = $sconn + $dconn + $mconn + $aconn + $mdconn + $bconn + $ubconn + $econn + $mddconn + $cconn;
if ($verb eq "yes" && $ac > 0 && $acTotal > 0){

print "\n\n----- Abandon Request Stats -----\n\n";

 for ($g = 0; $g < $ac; $g++){
  for ($sc = 0; $sc < $sconn; $sc++){
	if ($srchConn[$sc] eq $targetConn[$g] && $srchOp[$sc] eq $targetOp[$g] ){
		print " - SRCH conn=$targetConn[$g] op=$targetOp[$g] msgid=$msgid[$g] client=$conn_hash{$targetConn[$g]}\n";	
	}
  }
  for ($dc = 0; $dc < $dconn; $dc++){
	if ($delConn[$dc] eq $targetConn[$g] && $delOp[$dc] eq $targetOp[$g]){
		print " - DEL conn=$targetConn[$g] op=$targetOp[$g] msgid=$msgid[$g] client=$conn_hash{$targetConn[$g]}\n";
	}
  }
  for ($adc = 0; $adc < $aconn; $adc++){
	if ($addConn[$adc] eq $targetConn[$g] && $addOp[$adc] eq $targetOp[$g]){
		print " - ADD conn=$targetConn[$g] op=$targetOp[$g] msgid=$msgid[$g] client=$conn_hash{$targetConn[$g]}\n";
	}
  }
  for ($mc = 0; $mc < $mconn; $mc++){
	if ($modConn[$mc] eq $targetConn[$g] && $modOp[$mc] eq $targetOp[$g]){
		print " - MOD conn=$targetConn[$g] op=$targetOp[$g] msgid=$msgid[$g] client=$conn_hash{$targetConn[$g]}\n";
	}
  }
  for ($mddc = 0; $mddc < $mddconn; $mddc++){
        if ($moddnConn[$mdc] eq $targetConn[$g] && $moddnOp[$mdc] eq $targetOp[$g]){
                print " - MODDN conn=$targetConn[$g] op=$targetOp[$g] msgid=$msgid[$g] client=$conn_hash{$targetConn[$g]}\n";
        }
  }
  for ($cc = 0; $cc < $cconn; $cc++){
        if ($compConn[$mdc] eq $targetConn[$g] && $compOp[$mdc] eq $targetOp[$g]){
                print " - CMP conn=$targetConn[$g] op=$targetOp[$g] msgid=$msgid[$g] client=$conn_hash{$targetConn[$g]}\n";
        }
  }
  for ($mdc = 0; $mdc < $mdconn; $mdc++){
	if ($modrdnConn[$mdc] eq $targetConn[$g] && $modrdnOp[$mdc] eq $targetOp[$g]){
		print " - MODRDN conn=$targetConn[$g] op=$targetOp[$g] msgid=$msgid[$g] client=$conn_hash{$targetConn[$g]}\n";
	}
  }
  for ($bcb = 0; $bcb < $bconn; $bcb++){
	if ($bindConn[$bcb] eq $targetConn[$g] && $bindOp[$bcb] eq $targetOp[$g]){
		print " - BIND conn=$targetConn[$g] op=$targetOp[$g] msgid=$msgid[$g] client=$conn_hash{$targetConn[$g]}\n";
	}
  }
  for ($ubc = 0; $ubc < $ubconn; $ubc++){
	if ($unbindConn[$ubc] eq $targetConn[$g] && $unbindOp[$ubc] eq $targetOp[$g]){
		print " - UNBIND conn=$targetConn[$g] op=$targetOp[$g] msgid=$msgid[$g] client=$conn_hash{$targetConn[$g]}\n";
	}
  }
  for ($ec = 0; $ec < $econn; $ec++){
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
print "\n----- Recommendations -----\n";
$recCount = "1";
if ($notes > 0){
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

if (($bind - $unbind) > ($bind*.3)){
	print "\n $recCount.  You have a significant difference between binds and unbinds.  You may want to investigate this difference.\n";
	$recCount++;
	}

# compare fds taken and return, if the difference is more than 30% report a issue

if (($fdtaken -$fdreturn) > ($fdtaken*.3)){
	print "\n $recCount.  You have a significant difference between file descriptors taken and file descriptors returned.  You may want to investigate this difference.\n";
	$recCount++;
	}

if ($dirmgr > ($bind *.2)){
	print "\n $recCount.  You have a high number of Directory Manager binds.  The Directory Manager account should only be used under certain circumstances.  Avoid using this account for client applications.\n";
	$recCount++;
	}

if ($errorck > $errorsucc){
	print "\n $recCount.  You have more unsuccessful operations than successful operations.  You should investigate this difference.\n";
	$recCount++;
	}

if ($conncount{"U1"} < ($concount - $conncount{"U1"})){
	print "\n $recCount.  You have more abnormal connection codes than cleanly closed connections.  You may want to investigate this difference.\n";
	$recCount++;
	}

if ($first > 0){
	print "\n $recCount.  You have a majority of etimes that are greater than zero, you may want to investigate this performance problem.\n";
	$recCount++;
	}

if ($objectclass > ($search *.25)){
	print "\n $recCount.  You have a high number of searches that query the entire search base.  Although this is not necessarily bad, it could be resource intensive if the search base contains many entries.\n"; 
	$recCount++;
	}

if ($recCount == 1){
	print "\nNone.\n";
	}
}

print "\n";

# dispaly usage

sub displayUsage {

	print "Usage:\n\n";

	print " ./logconv.pl [-h] [-d <rootDN>] [-s <size limit>] [-v] [-V]\n";
	print " [-S <start time>] [-E <end time>]\n"; 
	print " [-efcibaltnxgju] [ access log ... ... ]\n\n"; 

	print "- Commandline Switches:\n\n";

	print "         -h help/usage\n";
	print "         -d <Directory Managers DN>  DEFAULT -> cn=directory manager\n";
	print "         -s <Number of results to return per catagory>  DEFAULT -> 20\n";
	print "         -X <IP address to exclude from connection stats>  E.g. Load balancers\n";
	print "         -v show version of tool\n"; 
	print "         -S <time to begin analyzing logfile from>\n";
	print "             E.g. [28/Mar/2002:13:14:22 -0800]\n";
	print "         -E <time to stop analyzing logfile>\n";
	print "             E.g. [28/Mar/2002:13:24:62 -0800]\n";
	print "         -m <CSV output file - per second stats>\n"; 
	print "         -M <CSV output file - per minute stats>\n";	
	print "         -V <enable verbose output - includes all stats listed below>\n";
	print "         -[efcibaltnxgju]\n\n";

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

	print "         ./logconv.pl -d cn=dm /logs/access*\n\n";

	print "         ./logconv.pl -s 50 -ibgju /logs/access*\n\n";

	print "         ./logconv.pl -S \"\[28/Mar/2002:13:14:22 -0800\]\" -E \"\[28/Mar/2002:13:50:05 -0800\]\" -e /logs/access*\n\n";
	print "         ./logconv.pl -m log-minute-stats-csv.out /logs/access*\n\n";

	exit 1;
}

######################################################
#
# Parsing Routine That Does The Actual Parsing Work
#
######################################################

sub parseLine {
local $_ = $tline;

# lines starting blank are restart
return if $_ =~ /^\s/;

if($firstFile eq "1" && $_ =~ /^\[/){
	# if we are using startTime & endTime, this will get overwritten, which is ok
	$start = $_;
	if ($start =~ / *([0-9a-z:\/]+)/i){$start=$1;}
	$firstFile = "0";
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

$ff++;
$iff++;
if ($iff >= $limit){ print STDERR sprintf" %10s Lines Processed\n",$ff; $iff="0";}

# Additional performance stats
($time, $tzone) = split (' ', $_);
if ($time ne $last_tm)
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

if (m/ RESULT err/){$allResults++;inc_stats('results',$s_stats,$m_stats);}
if (m/ SRCH/){
	$search++;
	inc_stats('srch',$s_stats,$m_stats);
	if ($_ =~ / attrs=\"(.*)\"/i){
		$anyAttrs++;
		$attrs = $1 . " ";
		while ($attrs =~ /(\S+)\s/g){
			$attr{$1}++;
		}
	} 
	if (/ attrs=ALL/){
		$attr{"All Attributes"}++;
		$anyAttrs++;
	}

	if ($verb eq "yes"){ 
		if ($_ =~ /conn= *([0-9]+)/i){ $srchConn[$sconn] = $1;}
		if ($_ =~ /op= *([0-9]+)/i){ $srchOp[$sconn] = $1;}
		$sconn++;
	}
		
	##### This to get the Base and Scope value
	##### just in case this happens to be an 
	##### unindexed search....

	if ($_ =~ /base=\"(.*)\" scope=(\d) filter/) {
		$tmpBase = $1;
		$tmpScope = $2;
	}
}
if (m/ DEL/){
	$delete++;
	inc_stats('del',$s_stats,$m_stats);
	if ($verb eq "yes"){
		if ($_ =~ /conn= *([0-9]+)/i){ $delConn[$dconn] = $1;}
		if ($_ =~ /op= *([0-9]+)/i){ $delOp[$dconn] = $1;}
		$dconn++;
	}
}
if (m/ MOD dn=/){
	$mod++;
	inc_stats('mod',$s_stats,$m_stats);
	if ($verb eq "yes"){
                if ($_ =~ /conn= *([0-9]+)/i){ $modConn[$mconn] = $1;}
		if ($_ =~ /op= *([0-9]+)/i){ $modOp[$mconn] = $1; }
		$mconn++;
	}
}
if (m/ MODDN dn=/){
        $moddn++;
        inc_stats('moddn',$s_stats,$m_stats);
        if ($verb eq "yes"){
                if ($_ =~ /conn= *([0-9]+)/i){ $moddnConn[$mconn] = $1;}
                if ($_ =~ /op= *([0-9]+)/i){ $moddnOp[$mconn] = $1; }
                $mddconn++;
        }
}
if (m/ ADD/){
	$add++;
	inc_stats('add',$s_stats,$m_stats);
	if ($verb eq "yes"){
                if ($_ =~ /conn= *([0-9]+)/i){ $addConn[$aconn] = $1; }
		if ($_ =~ /op= *([0-9]+)/i){ $addOp[$aconn] = $1; }
		$aconn++;
	}
}
if (m/ MODRDN/){
	$modrdn++;
	inc_stats('modrdn',$s_stats,$m_stats);
	if ($verb eq "yes"){
                if ($_ =~ /conn= *([0-9]+)/i){ $modrdnConn[$mdconn] = $1; }
		if ($_ =~ /op= *([0-9]+)/i){ $modrdnOp[$mdconn] = $1; }
		$mdconn++;
	}
}
if (m/ CMP dn=/){
	$compare++;
	inc_stats('cmp',$s_stats,$m_stats);	
	if ($verb eq "yes"  || $usage =~ /g/i){
		if ($_ =~ /conn= *([0-9]+)/i){ $compConn[$dconn] = $1;}
		if ($_ =~ /op= *([0-9]+)/i){ $compOp[$dconn] = $1;}
		$cconn++;
	}
}
if (m/ ABANDON /){
	$abandon++;
	inc_stats('abandon',$s_stats,$m_stats);
	$allResults++;
	if ($_ =~ /targetop= *([0-9a-zA-Z]+)/i ){
		$targetOp[$ac] = $1;
		if ($_ =~ /conn= *([0-9]+)/i){ $targetConn[$ac] = $1; }
		if ($_ =~ /msgid= *([0-9]+)/i){ $msgid[$ac] = $1;}
		$ac++;
	}
}
if (m/ VLV /){
        if ($_ =~ /conn= *([0-9]+)/i){ $vlvconn[$vlv] = $1;}
        if ($_ =~ /op= *([0-9]+)/i){ $vlvop[$vlv] = $1;}
        $vlv++;
}
if (m/ authzid=/){
	$proxiedAuth++;
}
if (m/ SORT /){$sortvlv++}
if (m/ version=2/){$version2++}
if (m/ version=3/){$version3++}
if (m/ conn=1 fd=/){$restarts++}
if (m/ SSL connection from/){$sslconn++;}
if (m/ connection from/){
     $exc = "no";
     if ($_ =~ /connection from *([0-9\.]+)/i ){ 
	for ($xxx =0; $xxx <= $#exclude; $xxx++){
		if ($exclude[$xxx] eq $1){$exc = "yes";}
	}
	if ($exc ne "yes"){ $connectionCount++;}
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

if (m/ SSL client bound as /){$sslClientBind++;}
if (m/ SSL failed to map client certificate to LDAP DN/){$sslClientFailed++;}
if (m/ fd=/ && m/slot=/){$fdtake++}
if (m/ fd=/ && m/closed/){
	$fdreturn++;
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
if (m/ BIND/){
	$bind++;
	inc_stats('bind',$s_stats,$m_stats);
	if ($verb eq "yes"){
                if ($_ =~ /conn= *([0-9]+)/i){ $bindConn[$bconn] = $1; }
		if ($_ =~ /op= *([0-9]+)/i){ $bindOp[$bconn] = $1; }
		$bconn++;
	}
}
if (m/ BIND/ && m/$manager/i){$dirmgr++}
if (m/ BIND/ && m/dn=""/){$anony++; $bindlist{"Anonymous Binds"}++;inc_stats('anonbind',$s_stats,$m_stats);}
if (m/ UNBIND/){
	$unbind++;
	if ($verb eq "yes"){
                if ($_ =~ /conn= *([0-9]+)/i){ $unbindConn[$ubconn] = $1; }
		if ($_ =~ /op= *([0-9]+)/i){ $unbindOp[$ubconn] = $1; }
		$ubconn++;
	}
}
if (m/ notes=U/){
        if ($_ =~ /conn= *([0-9]+)/i){
                $con = $1;
                if ($_ =~ /op= *([0-9]+)/i){ $op = $1;}
        }
        for ($i=0; $i <= $vlv;$i++){
                if ($vlvconn[$i] eq $con && $vlvop[$i] eq $op){ $vlvnotes++; $v="1";}
        }
        if($v ne "1"){
		#  We don't want to record vlv unindexed searches for our regular "bad" 
		#  unindexed search stat, as VLV unindexed searches aren't that bad
		$notes++;
		inc_stats('notesu',$s_stats,$m_stats);
        }
        if ($usage =~ /u/ || $verb eq "yes"){
        if ($v eq "0" ){
                if ($_ =~ /etime= *([0-9]+)/i ) {
                        $notesEtime[$vet]=$1;
                        $vet++;
                }
                if ($_ =~ /conn= *([0-9]+)/i){
                        $notesConn[$nc]=$1;
                        $nc++;
                }
                if ($_ =~ /op= *([0-9]+)/i){
                        $notesOp[$no]=$1;
                        $no++;
                }
                if ($_ =~ / *([0-9a-z:\/]+)/i){
                        $notesTime[$nt] = $1;
                        $nt++;
                }
				$notesBase[$nb] = $tmpBase;
				$notesScope[$nb] = $tmpScope;
				$nb++;
				}
		if ($_ =~ /nentries= *([0-9]+)/i ){
			$notesNentries[$nent] = $1;
			$nent++;
		}
	}
        $v = "0";
}

if (m/ closed error 32/){
        $broken++;
        if (m/- T1/){ $rc{"T1"}++ }
        elsif (m/- T2/){ $rc{"T2"}++ }
        elsif (m/- A1/){ $rc{"A1"}++ }
        elsif (m/- B1/){ $rc{"B1"}++ }
        elsif (m/- B4/){ $rc{"B4"}++ }
        elsif (m/- B2/){ $rc{"B2"}++ }
        elsif (m/- B3/){ $rc{"B3"}++ }
        elsif (m/- R1/){ $rc{"R1"}++ }
        elsif (m/- P1/){ $rc{"P1"}++ }
        elsif (m/- P1/){ $rc{"P2"}++ }
        elsif (m/- U1/){ $rc{"U1"}++ }
        else { $rc{"other"}++; }
}
if (m/ closed error 131/ || m/ closed error -5961/){
        $reset++;
        if (m/- T1/){ $src{"T1"}++ }
        elsif (m/- T2/){ $src{"T2"}++ }
        elsif (m/- A1/){ $src{"A1"}++ }
        elsif (m/- B1/){ $src{"B1"}++ }
        elsif (m/- B4/){ $src{"B4"}++ }
        elsif (m/- B2/){ $src{"B2"}++ }
        elsif (m/- B3/){ $src{"B3"}++ }
        elsif (m/- R1/){ $src{"R1"}++ }
        elsif (m/- P1/){ $src{"P1"}++ }
        elsif (m/- P1/){ $src{"P2"}++ }
        elsif (m/- U1/){ $src{"U1"}++ }
        else { $src{"other"}++ }
}

if (m/ closed error 11/){
        $resource++;
        if (m/- T1/){ $rsrc{"T1"}++ }
        elsif (m/- T2/){ $rsrc{"T2"}++ }
        elsif (m/- A1/){ $rsrc{"A1"}++ }
        elsif (m/- B1/){ $rsrc{"B1"}++ }
        elsif (m/- B4/){ $rsrc{"B4"}++ }
        elsif (m/- B2/){ $rsrc{"B2"}++ }
        elsif (m/- B3/){ $rsrc{"B3"}++ }
        elsif (m/- R1/){ $rsrc{"R1"}++ }
        elsif (m/- P1/){ $rsrc{"P1"}++ }
        elsif (m/- P1/){ $rsrc{"P2"}++ }
        elsif (m/- U1/){ $rsrc{"U1"}++ }
        else { $rsrc{"other"}++ }
}

if ($usage =~ /g/ || $usage =~ /c/ || $usage =~ /i/ || $verb eq "yes"){

$exc = "no";

if ($_ =~ /connection from *([0-9\.]+)/i ) {
	for ($xxx = 0; $xxx <= $#exclude; $xxx++){
		if ($1 eq $exclude[$xxx]){
			$exc = "yes";
			$exCount{$1}++;
		}
	}
        $ip = $1;
        $ip_hash{$ip}{"count"}++;
        if ($_ =~ /conn= *([0-9]+)/i ){ 
	if ($exc ne "yes"){	$ip_hash2{$ip} = sprintf "%-12s               %18s\n",$1,$ip;} 
		$conn_hash{$1} = $ip;
		
	}
	
}
if (m/- A1/){
        if ($_ =~ /conn= *([0-9]+)/i) {
		$exc = "no";
                $ip = $conn_hash{$1};
		if ($ip eq ""){$ip = "Unknown Host";}
		for ($xxx = 0; $xxx <= $#exclude; $xxx++){
                	if ($ip eq $exclude[$xxx]){$exc = "yes";}
        	}
		if ($exc ne "yes"){
                	$ip_hash{$ip}{"A1"}++;
			$conncount{"A1"}++;
			$concount++;
		}
        }
}
if (m/- B1/){
        if ($_ =~ /conn= *([0-9]+)/i) {
		$exc = "no";
                $ip = $conn_hash{$1};
		if ($ip eq ""){$ip = "Unknown Host";}
		for ($xxx = 0; $xxx <= $#exclude; $xxx++){
                        if ($ip eq $exclude[$xxx]){$exc = "yes";}
                }
                if ($exc ne "yes"){
                	$ip_hash{$ip}{"B1"}++;
			$conncount{"B1"}++;
			$concount++;	
		}
        }
}
if (m/- B4/){
        if ($_ =~ /conn= *([0-9]+)/i) {
		$exc = "no";
                $ip = $conn_hash{$1};
		if ($ip eq ""){$ip = "Unknown Host";}
		for ($xxx = 0; $xxx <= $#exclude; $xxx++){
                        if ($ip eq $exclude[$xxx]){$exc = "yes";}
                }
                if ($exc ne "yes"){
                	$ip_hash{$ip}{"B4"}++;
			$conncount{"B4"}++;
			$concount++;
		}
    }
}
if (m/- T1/){
        if ($_ =~ /conn= *([0-9]+)/i) {
		$exc = "no";
               	$ip = $conn_hash{$1};
		if ($ip eq ""){$ip = "Unknown Host";}
		for ($xxx = 0; $xxx <= $#exclude; $xxx++){
                        if ($ip eq $exclude[$xxx]){$exc = "yes";}
                }
                if ($exc ne "yes"){
                	$ip_hash{$ip}{"T1"}++;
			$conncount{"T1"}++;
			$concount++;	
		}
        }
}
if (m/- T2/){
        if ($_ =~ /conn= *([0-9]+)/i) {
		$exc = "no"; 
                $ip = $conn_hash{$1};
		if ($ip eq ""){$ip = "Unknown Host";}
		for ($xxx = 0; $xxx <= $#exclude; $xxx++){
                        if ($ip eq $exclude[$xxx]){$exc = "yes";}
                }
                if ($exc ne "yes"){
                	$ip_hash{$ip}{"T2"}++;
			$conncount{"T2"}++;
			$concount++;	
		}
        }
}
if (m/- B2/){
        if ($_ =~ /conn= *([0-9]+)/i) {
		$exc = "no";
                $ip = $conn_hash{$1};
		if ($ip eq ""){$ip = "Unknown Host";}
		for ($xxx = 0; $xxx <= $#exclude; $xxx++){
                        if ($ip eq $exclude[$xxx]){$exc = "yes";}
                }
                if ($exc ne "yes"){
                	$ip_hash{$ip}{"B2"}++;
			$conncount{"B2"}++;
			$concount++;	
		}
        }
}
if (m/- B2/){
        if ($_ =~ /conn= *([0-9]+)/i) {
		$exc = "no";
                $ip = $conn_hash{$1};
		if ($ip eq ""){$ip = "Unknown Host";}
		for ($xxx = 0; $xxx <= $#exclude; $xxx++){
                        if ($ip eq $exclude[$xxx]){$exc = "yes";}
                }
                if ($exc ne "yes"){
                	$ip_hash{$ip}{"B2"}++;
			$conncount{"B2"}++;
			$concount++;	
		}
        }
}
if (m/- B3/){
        if ($_ =~ /conn= *([0-9]+)/i) {
		$exc = "no";
                $ip = $conn_hash{$1};
		if ($ip eq ""){$ip = "Unknown Host";}
		for ($xxx = 0; $xxx <= $#exclude; $xxx++){
                        if ($ip eq $exclude[$xxx]){$exc = "yes";}
                }
                if ($exc ne "yes"){
                	$ip_hash{$ip}{"B3"}++;
			$conncount{"B3"}++;
			$concount++;	
		}
        }
}
if (m/- R1/){
        if ($_ =~ /conn= *([0-9]+)/i) {
		$exc = "no";
                $ip = $conn_hash{$1};
		if ($ip eq ""){$ip = "Unknown Host";}
		for ($xxx = 0; $xxx <= $#exclude; $xxx++){
                        if ($ip eq $exclude[$xxx]){$exc = "yes";}
                }
		if ($exc ne "yes"){
                	$ip_hash{$ip}{"R1"}++;
			$conncount{"R1"}++;
			$concount++;
		}
        }
}
if (m/- P1/){
        if ($_ =~ /conn= *([0-9]+)/i) {
		$exc = "no";
                $ip = $conn_hash{$1};
		if ($ip eq ""){$ip = "Unknown Host";}
		for ($xxx = 0; $xxx <= $#exclude; $xxx++){
                        if ($ip eq $exclude[$xxx]){$exc = "yes";}
                }
                if ($exc ne "yes"){
                	$ip_hash{$ip}{"P1"}++;
			$conncount{"P1"}++;
			$concount++;	
		}
        }
}
if (m/- P2/){
        if ($_ =~ /conn= *([0-9]+)/i) {
		$exc = "no";
                $ip = $conn_hash{$1};
		if ($ip eq ""){$ip = "Unknown Host";}
		for ($xxx = 0; $xxx <= $#exclude; $xxx++){
                        if ($ip eq $exclude[$xxx]){$exc = "yes";}
                }
                if ($exc ne "yes"){
                	$ip_hash{$ip}{"P2"}++;
			$conncount{"P2"}++;
			$concount++;
		}
        }
}
if (m/- U1/){
        if ($_ =~ /conn= *([0-9]+)/i) {
		$exc = "no";
                $ip = $conn_hash{$1};
		if ($ip eq ""){$ip = "Unknown Host";}
		for ($xxx = 0; $xxx <= $#exclude; $xxx++){
                        if ($ip eq $exclude[$xxx]){$exc = "yes";}
                }
                if ($exc ne "yes"){
                	$ip_hash{$ip}{"U1"}++;
			$conncount{"U1"}++;
			$concount++;
		}
        }

}

}
if ($_ =~ /err= *([0-9]+)/i){
        $er[$1]++;
        if ($1 ne "0"){ $errorck++;}
        else { $errorsucc++;}
}
if ($_ =~ /etime= *([0-9]+)/i ) { $etime{$1}++;}
if ($_ =~ / tag=101 nentries= *([0-9]+)/i ) {$nentries{$1}++}
if ($_ =~ / tag=111 nentries= *([0-9]+)/i ) {$nentries{$1}++}
if ($_ =~ / tag=100 nentries= *([0-9]+)/i ) {$nentries{$1}++}
if ($_ =~ / tag=115 nentries= *([0-9]+)/i ) {$nentries{$1}++}
if (m/objectclass=\*/i || m/objectclass=top/i ){
	if (m/ scope=2 /){ $objectclass++;}
}

if (m/ EXT oid=/){
        $extendedop++;
        if ($_ =~ /oid=\" *([0-9\.]+)/i ){ $oid{$1}++; }
	if ($verb eq "yes"){
                if ($_ =~ /conn= *([0-9]+)/i){ $extConn[$econn] = $1; }
		if ($_ =~ /op= *([0-9]+)/i){ $extOp[$econn] = $1; }
		$econn++;
	}
}

if (m/ BIND/ && $_ =~ /dn=\"(.*)\" method/i ){
	if ($1 ne ""){ 
	$tmpp = $1;
	$tmpp =~ tr/A-Z/a-z/;
	$bindlist{$tmpp} = $bindlist{$tmpp} + 1; 

	$bindInfo[$bc][0] = $tmpp;
	if ($_ =~ /conn= *([0-9]+)/i) { $bindInfo[$bc][1] = $1; }
	if ($_ =~ /op= *([0-9]+)/i) { $bindInfo[$bc][2] = $1; }
	$bc++;
	}
}

if ($usage =~ /l/ || $verb eq "yes"){
	if (/ SRCH / && / attrs=/ && $_ =~ /filter=\"(.*)\" /i ){
		$tmpp = $1;
		$tmpp =~ tr/A-Z/a-z/;
		$tmpp =~ s/\\22/\"/g;
		$filter{$tmpp} = $filter{$tmpp} + 1; 
		$filterInfo[$fcc][0] = $tmpp;
		if ($_ =~ /conn= *([0-9]+)/i) { $filterInfo[$fcc][1] = $1; }
		if ($_ =~ /op= *([0-9]+)/i) { $filterInfo[$fcc][2] = $1; }
		$fcc++;
	} elsif (/ SRCH / && $_ =~ /filter=\"(.*)\"/i){
        	$tmpp = $1;
        	$tmpp =~ tr/A-Z/a-z/;
        	$tmpp =~ s/\\22/\"/g;
        	$filter{$tmpp} = $filter{$tmpp} + 1;
        	$filterInfo[$fcc][0] = $tmpp;
        	if ($_ =~ /conn= *([0-9]+)/i) { $filterInfo[$fcc][1] = $1; }
        	if ($_ =~ /op= *([0-9]+)/i) { $filterInfo[$fcc][2] = $1; }
        	$fcc++;
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
        	$base{$tmpp} = $base{$tmpp} + 1;
	}
}

if ($_ =~ /fd= *([0-9]+)/i ) {
        $fds[$fdds] = $1;
        if ($fds[$fdds] > $highfd) {$highfd = $fds[$fdds];}
        $fdds++;
}


if ($usage =~ /f/ || $verb eq "yes"){
	if (/ err=49 tag=/ && / dn=\"/){
		if ($_ =~ /dn=\"(.*)\"/i ){
			$ds6xbadpwd{$1}++;
		}
		$ds6x = "true";
		$bpc++;

	} elsif (/ err=49 tag=/ ){
		if ($_ =~ /conn= *([0-9]+)/i ){
			$badPasswordConn[$bpc] = $1;
			$bpc++;
		}
		if ($_ =~ /op= *([0-9]+)/i ){
			$badPasswordOp[$bpo] = $1;
			$bpo++;
		}
		$badPasswordIp[$bpi] = $ip;
		$bpi++;
	}
}

if (/ BIND / && /method=sasl/i){
	$sasl++;
	if ($_ =~ /mech=(.*)/i ){     
		$saslmech{$1}++;
	}
}

if (/ conn=Internal op=-1 / && !/ RESULT err=/){ $internal++; }

if (/ ENTRY dn=/ ){ $entryOp++; }

if (/ conn=/ && /op=/ && / REFERRAL/){ $referral++; }

if (/ options=persistent/){$persistent++;}

}

 #######################################
 # #                                     #
 # +#          CSV Helper Routines        #
 # +#                                     #
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

    if ($name)
    {
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
    foreach my $stats( @_ )
    {
	if ($stats->{'active'})
	{
	    if ($stats->{'last'})
	    {
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
	    }else
	    {
		$stats->{'fh'}->print(
		    "Time,time_t,Results,Search,Add,Mod,Modrdn,Delete,Abandon,".
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
    foreach(@_)
    {
	$_->{$n}++
	    if exists $_->{$n};
    }
    return;
}

#######################################
#                                     #
#             The  End                #
#                                     #
#######################################

