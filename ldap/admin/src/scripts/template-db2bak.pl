#{{PERL-EXEC}}
#
# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#

sub usage {
	print(STDERR "Usage: $0 [-v] -D rootdn { -w password | -w - | -j filename } \n");
	print(STDERR "          [-a dirname] [-t dbtype]\n");
	print(STDERR " Opts: -D rootdn   - Directory Manager\n");
	print(STDERR "     : -w password - Directory Manager's password\n");
	print(STDERR "     : -w -        - Prompt for Directory Manager's password\n");
	print(STDERR "     : -j filename - Read Directory Manager's password from file\n");
	print(STDERR "     : -a dirname  - backup directory\n");
	print(STDERR "     : -t dbtype   - database type (default: ldbm database)\n");
	print(STDERR "     : -v          - verbose\n");
}
$taskname = "";
$archivedir = "";
$dbtype = "ldbm database";
$dsroot = "{{DS-ROOT}}";
$mydsroot = "{{MY-DS-ROOT}}";
$verbose = 0;
$rootdn = "";
$passwd = "";
$passwdfile = "";
$i = 0;
while ($i <= $#ARGV) {
	if ("$ARGV[$i]" eq "-a") {	# backup directory
		$i++; $archivedir = $ARGV[$i];
	} elsif ("$ARGV[$i]" eq "-D") {	# Directory Manager
		$i++; $rootdn = $ARGV[$i];
	} elsif ("$ARGV[$i]" eq "-w") {	# Directory Manager's password
		$i++; $passwd = $ARGV[$i];
	} elsif ("$ARGV[$i]" eq "-j") { # Read Directory Manager's password from a file
		$i++; $passwdfile = $ARGV[$i];
	} elsif ("$ARGV[$i]" eq "-t") {	# database type
		$i++; $dbtype = $ARGV[$i];
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
if ( $rootdn eq "" || $passwd eq "") { &usage; exit(1); }
($s, $m, $h, $dy, $mn, $yr, $wdy, $ydy, $r) = localtime(time);
$mn++; $yr += 1900;
$taskname = "backup_${yr}_${mn}_${dy}_${h}_${m}_${s}";
if ($archivedir eq "") {
	$archivedir = "${mydsroot}{{SEP}}bak{{SEP}}${yr}_${mn}_${dy}_${h}_${m}_${s}";
}
$dn = "dn: cn=$taskname, cn=backup, cn=tasks, cn=config\n";
$misc = "changetype: add\nobjectclass: top\nobjectclass: extensibleObject\n";
$cn = "cn: $taskname\n";
$nsarchivedir = "nsArchiveDir: $archivedir\n";
$nsdbtype = "nsDatabaseType: $dbtype\n";
$entry = "${dn}${misc}${cn}${nsarchivedir}${nsdbtype}";
$vstr = "";
if ($verbose != 0) { $vstr = "-v"; }
chdir("$dsroot{{SEP}}shared{{SEP}}bin");
open(FOO, "| $dsroot{{SEP}}shared{{SEP}}bin{{SEP}}ldapmodify $vstr -h {{SERVER-NAME}} -p {{SERVER-PORT}} -D \"$rootdn\" -w \"$passwd\" -a" );
print(FOO "$entry");
close(FOO);
