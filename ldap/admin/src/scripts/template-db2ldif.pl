#{{PERL-EXEC}}
#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2004 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#

sub usage {
	print(STDERR "Usage: $0 [-v] -D rootdn { -w password | -w - | -j filename } \n");
	print(STDERR "        {-n instance}* | {-s include}* [{-x exclude}*] \n");
	print(STDERR "        [-m] [-M] [-u] [-C] [-N] [-U] [-a filename]\n");
	print(STDERR " Opts: -D rootdn   - Directory Manager\n");
	print(STDERR "     : -w password - Directory Manager's password\n");
	print(STDERR "     : -w -        - Prompt for Directory Manager's password\n");
	print(STDERR "     : -j filename - Read Directory Manager's password from file\n");
	print(STDERR "     : -n instance - instance to be exported\n");
	print(STDERR "     : -a filename - output ldif file\n");
	print(STDERR "     : -s include  - included suffix(es)\n");
	print(STDERR "     : -x exclude  - excluded suffix(es)\n");
	print(STDERR "     : -m          - minimal base64 encoding\n");
	print(STDERR "     : -M          - output ldif is stored in multiple files\n");
	print(STDERR "                     these files are named : <instance>_<filename>\n");
	print(STDERR "                     by default, all instances are stored in <filename>\n");
	print(STDERR "     : -r          - export replica\n");
	print(STDERR "     : -u          - do not export unique id\n");
	print(STDERR "     : -C          - use main db file only\n");
	print(STDERR "     : -N          - suppress printing sequential number\n");
	print(STDERR "     : -U          - output ldif is not folded\n");
	print(STDERR "     : -E          - Decrypt encrypted data when exporting\n");
	print(STDERR "     : -1          - do not print version line\n");
	print(STDERR "     : -v          - verbose\n");
}

@instances = (
	"", "", "", "", "", "", "", "", "", "",
	"", "", "", "", "", "", "", "", "", "",
	"", "", "", "", "", "", "", "", "", "",
	"", "", "", "", "", "", "", "", "", "",
	"", "", "", "", "", "", "", "", "", "",
	""
);
@included = (
	"", "", "", "", "", "", "", "", "", "",
	"", "", "", "", "", "", "", "", "", "",
	"", "", "", "", "", "", "", "", "", "",
	"", "", "", "", "", "", "", "", "", "",
	"", "", "", "", "", "", "", "", "", "",
	""
);
@excluded = (
	"", "", "", "", "", "", "", "", "", "",
	"", "", "", "", "", "", "", "", "", "",
	"", "", "", "", "", "", "", "", "", "",
	"", "", "", "", "", "", "", "", "", "",
	"", "", "", "", "", "", "", "", "", "",
	""
);
$maxidx = 50;
$nowrap = 0;
$nobase64 = 0;
$noversion = 0;
$nouniqueid = 0;
$useid2entry = 0;
$onefile = 1;
$printkey = 1;
$taskname = "";
$ldiffile = "";
$doreplica = 0;
$dsroot = "{{DS-ROOT}}";
$mydsroot = "{{MY-DS-ROOT}}";
$verbose = 0;
$rootdn = "";
$passwd = "";
$passwdfile = "";
$i = 0;
$insti = 0;
$incli = 0;
$excli = 0;
$decrypt_on_export = 0;
while ($i <= $#ARGV) {
	if ( "$ARGV[$i]" eq "-n" ) {	# instances
		$i++;
		if ($insti < $maxidx) {
			$instances[$insti] = $ARGV[$i]; $insti++;
		} else {
			&usage; exit(1);
		}
	} elsif ("$ARGV[$i]" eq "-s") {	# included suffix
		$i++;
		if ($incli < $maxidx) {
			$included[$incli] = $ARGV[$i]; $incli++;
		} else {
			&usage; exit(1);
		}
	} elsif ("$ARGV[$i]" eq "-x") {	# excluded suffix
		$i++;
		if ($excli < $maxidx) {
			$excluded[$excli] = $ARGV[$i]; $excli++;
		} else {
			&usage; exit(1);
		}
	} elsif ("$ARGV[$i]" eq "-a") {	# ldif file
		$i++; $ldiffile = $ARGV[$i];
	} elsif ("$ARGV[$i]" eq "-D") {	# Directory Manager
		$i++; $rootdn = $ARGV[$i];
	} elsif ("$ARGV[$i]" eq "-w") {	# Directory Manager's password
		$i++; $passwd = $ARGV[$i];
	} elsif ("$ARGV[$i]" eq "-j") { # Read Directory Manager's password from a file
		$i++; $passwdfile = $ARGV[$i];
	} elsif ("$ARGV[$i]" eq "-M") {	# multiple ldif file
		$onefile = 0;
	} elsif ("$ARGV[$i]" eq "-o") {	# one ldif file
		$onefile = 1;
	} elsif ("$ARGV[$i]" eq "-u") {	# no dump unique id
		$nouniqueid = 1;
	} elsif ("$ARGV[$i]" eq "-C") {	# use id2entry
		$useid2entry = 1;
	} elsif ("$ARGV[$i]" eq "-N") {	# does not print key
		$printkey = 0;
	} elsif ("$ARGV[$i]" eq "-r") {	# export replica
		$doreplica = 1;
	} elsif ("$ARGV[$i]" eq "-m") {	# no base64
		$nobase64 = 1;
	} elsif ("$ARGV[$i]" eq "-U") {	# no wrap
		$nowrap = 1;
	} elsif ("$ARGV[$i]" eq "-1") {	# no version line
		$noversion = 1;
	} elsif ("$ARGV[$i]" eq "-E") {	# decrypt
		$decrypt_on_export = 1;
	} elsif ("$ARGV[$i]" eq "-v") {	# verbose
		$verbose = 1;
	} else {
		&usage; exit(1);
	}
	$i++;
}
if ($passwdfile ne ""){
# Open file and get the password
	unless (open (RPASS, $passwdfile)) {
		die "Error, cannot open password file $passwdfile\n";
	}
	$passwd = <RPASS>;
	chomp($passwd);
	close(RPASS);
} elsif ($passwd eq "-"){
# Read the password from terminal
	die "The '-w -' option requires an extension library (Term::ReadKey) which is not\n",
	    "part of the standard perl distribution. If you want to use it, you must\n",
	    "download and install the module. You can find it at\n",
	    "http://www.perl.com/CPAN/CPAN.html\n";
# Remove the previous line and uncomment the following 6 lines once you have installed Term::ReadKey module.
# use Term::ReadKey; 
#	print "Bind Password: ";
#	ReadMode('noecho'); 
#	$passwd = ReadLine(0); 
#	chomp($passwd);
#	ReadMode('normal');
}
if (($instances[0] eq "" && $included[0] eq "") || $rootdn eq "" || $passwd eq "") { &usage; exit(1); }
($s, $m, $h, $dy, $mn, $yr, $wdy, $ydy, $r) = localtime(time);
$mn++; $yr += 1900;
$taskname = "export_${yr}_${mn}_${dy}_${h}_${m}_${s}";
if ($ldiffile eq "") {
	$ldiffile = "${mydsroot}{{SEP}}ldif{{SEP}}${yr}_${mn}_${dy}_${h}_${m}_${s}.ldif";
}
$dn = "dn: cn=$taskname, cn=export, cn=tasks, cn=config\n";
$misc = "changetype: add\nobjectclass: top\nobjectclass: extensibleObject\n";
$cn =  "cn: $taskname\n";
$i = 0;
$nsinstance = "";
while ("" ne "$instances[$i]") {
	$nsinstance = "${nsinstance}nsInstance: $instances[$i]\n";
	$i++;
}
$i = 0;
$nsincluded = "";
while ("" ne "$included[$i]") {
	$nsincluded = "${nsincluded}nsIncludeSuffix: $included[$i]\n";
	$i++;
}
$i = 0;
$nsexcluded = "";
while ("" ne "$excluded[$i]") {
	$nsexcluded = "${nsexcluded}nsExcludeSuffix: $excluded[$i]\n";
	$i++;
}
$nsreplica = "";
if ($doreplica != 0) { $nsreplica = "nsExportReplica: true\n"; }
$nsnobase64 = "";
if ($nobase64 != 0) { $nsnobase64 = "nsMinimalEncoding: true\n"; }
$nsnowrap = "";
if ($nowrap != 0) { $nsnowrap = "nsNoWrap: true\n"; }
$nsnoversion = "";
if ($noversion != 0) { $nsnoversion = "nsNoVersionLine: true\n"; }
$nsnouniqueid = "";
if ($nouniqueid != 0) { $nsnouniqueid = "nsDumpUniqId: false\n"; }
$nsuseid2entry = "";
if ($useid2entry != 0) { $nsuseid2entry = "nsUseId2Entry: true\n"; }
$nsonefile = "";
if ($onefile != 0) { $nsonefile = "nsUseOneFile: true\n"; }
if ($onefile == 0) { $nsonefile = "nsUseOneFile: false\n"; }
$nsexportdecrypt = "";
if ($decrypt_on_export != 0) { $nsexportdecrypt = "nsExportDecrypt: true\n"; }
$nsprintkey = "";
if ($printkey == 0) { $nsprintkey = "nsPrintKey: false\n"; }
$nsldiffile = "nsFilename: ${ldiffile}\n";
$entry = "${dn}${misc}${cn}${nsinstance}${nsincluded}${nsexcluded}${nsreplica}${nsnobase64}${nsnowrap}${nsnoversion}${nsnouniqueid}${nsuseid2entry}${nsonefile}${nsexportdecrypt}${nsprintkey}${nsldiffile}";
$vstr = "";
if ($verbose != 0) { $vstr = "-v"; }
chdir("$dsroot{{SEP}}shared{{SEP}}bin");
open(FOO, "| $dsroot{{SEP}}shared{{SEP}}bin{{SEP}}ldapmodify $vstr -h {{SERVER-NAME}} -p {{SERVER-PORT}} -D \"$rootdn\" -w \"$passwd\" -a" );
print(FOO "$entry");
close(FOO);
