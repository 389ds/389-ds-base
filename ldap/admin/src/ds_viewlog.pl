#!perl
#
# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#

# view the last N lines of the given file

BEGIN {
	# make stdout unbuffered for friendly CGI output
	$| = 1;
	# print CGI header
	print "Content-type: text/plain\n\n";
	# add the current directory to the beginning of the module
	# search path (for our CGI.pm)
	unshift @INC, '.';

}

my $dbfh; # debugging file handler
sub debug {
	# comment out the return line to enable debugging
	return;

	if (!$dbfh) {
		$dbfh = 'mylog.txt';
		open $dbfh, ">$dbfh" or die "Error: could not write $dbfh: $!";
	}
	print $dbfh "@_\n";
}

sub sigDieHandler {
	&debug(@_, "\n");
	print @_, "\n";
	&debug("NMC_STATUS: ", $!+0, "\n");
	print "NMC_STATUS: ", $!+0, "\n";
	exit $!;
}

sub rpt_err {
	my ($code, $value) = @_;
	$! = $code;
	die "Error: value $value is invalid: code $code";
}

$SIG{__DIE__} = 'sigDieHandler';
my $DEF_SIZE = 25;

# constants from dsalib.h
my $DS_UNKNOWN_ERROR = -1;
my $DS_NO_SERVER_ROOT = -10;
my $DS_CANNOT_EXEC = -11;
my $DS_CANNOT_OPEN_STAT_FILE = -12;
my $DS_NULL_PARAMETER = -13;
my $DS_SERVER_MUST_BE_DOWN = -14;
my $DS_CANNOT_OPEN_BACKUP_FILE = -15;
my $DS_NOT_A_DIRECTORY = -16;
my $DS_CANNOT_CREATE_DIRECTORY = -17;
my $DS_CANNOT_OPEN_LDIF_FILE = -18;
my $DS_IS_A_DIRECTORY = -19;
my $DS_CANNOT_CREATE_FILE = -20;
my $DS_UNDEFINED_VARIABLE = -21;
my $DS_NO_SUCH_FILE = -22;
my $DS_CANNOT_DELETE_FILE = -23;
my $DS_UNKNOWN_SNMP_COMMAND = -24;
my $DS_NON_NUMERIC_VALUE = -25;
my $DS_NO_LOGFILE_NAME = -26;
my $DS_CANNOT_OPEN_LOG_FILE = -27;
my $DS_HAS_TOBE_READONLY_MODE = -28;
my $DS_INVALID_LDIF_FILE = -29;

# process the CGI input
use Cgi;

my $num = $cgiVars{num};
my $str = $cgiVars{str};
my $logfile = $cgiVars{logfile};

&debug("ENV:");
foreach $item (keys %ENV) {
	&debug("ENV $item = $ENV{$item}");
}
&debug("query string = ", $Cgi::QUERY_STRING);
&debug("content = ", $CONTENT);
&debug("cgiVars = ", %cgiVars);
&debug("num = $num str = $str logfile = $logfile");

if (! $num) {
	$num = $DEF_SIZE;
}

if (! ($num =~ /\d+/))  {
	&rpt_err( $DS_NON_NUMERIC_VALUE, $num );
	return 1;
}

if (! $logfile) {
	&rpt_err( $DS_NO_LOGFILE_NAME, "no logfile");
}

if (! -f $logfile) {
	&rpt_err( $DS_CANNOT_OPEN_LOG_FILE, $logfile);
}

open(INP, $logfile) or &rpt_err( $DS_CANNOT_OPEN_LOG_FILE, $logfile);

my $ii = 0;
my @buf = ();
while (<INP>) {
	&debug("raw: $_");
	if (!$str || /$str/i) {
		$ii++;
		$buf[$ii%$num] = $_;
	}
}
close INP;

my @tail = (@buf[ ($ii%$num + 1) .. $#buf ], 
			@buf[  0 .. $ii%$num ]);
&debug("tail size = ", scalar(@tail), " first line = $tail[0]");
for (@tail) {
	print if $_; # @tail may begin or end with undef
	&debug($_) if $_;
}

die "Finished";
