# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
use IPC::Open2;
use Symbol;
use URI::Escape;
use Cwd;
use File::Basename;

sub usage {
	my $msg = shift;
	print "Error: $msg\n";
	print "Usage: $0 filename.inf\n";
	exit 1
}

sub getCgiContentAndLength {
  my $args = shift;
  my $escapechars = "^a-zA-Z0-9"; # escape all non alphanum chars
  my $content = "";
  my $firsttime = 1;
  while (my ($kk, $vv) = each %{$args}) {
	if ($firsttime) {
	  $firsttime = 0;
	} else {
	  $content = $content . "&";
	}
	$content = $content . $kk . "=" . uri_escape($vv, $escapechars);
  }
  my $length = length($content);

  return ($content, $length);
}

# fakes out the ds_newinst program into thinking it is getting cgi input
sub cgiFake {
  my ($sroot, $verbose, $prog, $args) = @_;
  # construct content string
  my ($content, $length) = &getCgiContentAndLength($args);

  # setup CGI environment
  $ENV{REQUEST_METHOD} = "POST";
  $ENV{NETSITE_ROOT} = $sroot;
  $ENV{CONTENT_LENGTH} = $length;
  $ENV{SERVER_NAMES} = 'slapd-' . $args->{servid};

#  print "content = $content\n";

  # open the program
  my $curdir = getcwd();
  my $dir = dirname($prog);
  my $exe = basename($prog);
  chdir $dir;
  my $input = gensym();
  my $output = gensym();
  my $pid = open2($input, $output, "./$exe");
  sleep(1); # allow prog to init stdin read buffers
  print $output $content, "\n";
  close $output;

  if ($?) {
	print "Warning: $prog returned code $? and $!\n";
  }

  my $exitCode = 1;
  my @lines;
  while (<$input>) {
	print $_ if ($verbose);
	push @lines, $_;
	if (/^NMC_Status:\s*(\d+)/) {
	  $exitCode = $1;
	  last;
	}
  }
  close $input;
  chdir $curdir;

  if ($exitCode) {
	print "CGI $prog failed with $exitCode: here is the output:\n";
	map { print $_ } @lines;
  }

  if ($exitCode != 0) {
	print "Error: could not run $prog: $exitCode\n";
	return $exitCode;
  }

  return 0;
}

sub addAndCheck {
	my $dest = shift;
	my $dkey = shift;
	my $source = shift;
	my $ssec = shift;
	my $skey = shift;

	if (! $source->{$ssec}->{$skey}) {
		usage("Missing required parameter $ssec - $skey\n");
	}

	$dest->{$dkey} = $source->{$ssec}->{$skey};
}

my $filename = $ARGV[0];
usage("$filename not found") if (! -f $filename);

my $curSection;
# each key in the table is a section name
# the value is a hash ref of the items in that section
#   in that hash ref, each key is the config param name,
#   and the value is the config param value
my %table = ();

open(IN, $filename);
while (<IN>) {
	# e.g. [General]
	if (/^\[(.*?)\]/) {
		$curSection = $1;
	} elsif (/^\s*$/) {
		next; # skip blank lines
	} elsif (/^\s*\#/) {
		next; # skip comment lines
	} elsif (/^\s*(.*?)\s*=\s*(.*?)\s*$/) {
		$table{$curSection}->{$1} = $2;
	}
}
close IN;

#printhash (\%table);

# next, construct a hash table with our arguments

my %cgiargs = ();

# the following items are always required
addAndCheck(\%cgiargs, "sroot", \%table, "General", "ServerRoot");
addAndCheck(\%cgiargs, "servname", \%table, "General", "FullMachineName");
addAndCheck(\%cgiargs, "servuser", \%table, "General", "SuiteSpotUserID");
addAndCheck(\%cgiargs, "servport", \%table, "slapd", "ServerPort");
addAndCheck(\%cgiargs, "rootdn", \%table, "slapd", "RootDN");
addAndCheck(\%cgiargs, "rootpw", \%table, "slapd", "RootDNPwd");
addAndCheck(\%cgiargs, "servid", \%table, "slapd", "ServerIdentifier");
addAndCheck(\%cgiargs, "suffix", \%table, "slapd", "Suffix");

# the following items are optional

# port number for Admin Server - used to configure some web apps
$cgiargs{adminport} = $table{admin}->{Port};

# If this is set, the new DS instance will be set up for use as
# a Configuration DS (e.g. o=NetscapeRoot)
$cgiargs{cfg_sspt} = $table{slapd}->{SlapdConfigForMC};
# set this to 1 to register this DS with an existing Configuration DS
# or 0 to create this DS as a new Configuration DS
$cgiargs{use_existing_config_ds} = $table{slapd}->{UseExistingMC};
# set this to 1 when creating a new Configuration DS if you do not
# want to configure the new DS to also serve user data
$cgiargs{use_existing_user_ds} = $table{slapd}->{UseExistingUG};

# the following items are required to register this new instance with a config DS
# or to make the new instance a Configuration DS
if ($cgiargs{cfg_sspt} ||
	$table{General}->{ConfigDirectoryAdminID} ||
	$table{General}->{ConfigDirectoryAdminPwd} ||
	$table{General}->{ConfigDirectoryLdapURL} ||
	$table{General}->{AdminDomain}) {
	addAndCheck(\%cgiargs, "cfg_sspt_uid", \%table, "General", "ConfigDirectoryAdminID");
	addAndCheck(\%cgiargs, "cfg_sspt_uid_pw", \%table, "General", "ConfigDirectoryAdminPwd");
	addAndCheck(\%cgiargs, "ldap_url", \%table, "General", "ConfigDirectoryLdapURL");
	addAndCheck(\%cgiargs, "admin_domain", \%table, "General", "AdminDomain");
}

#
if ($table{slapd}->{UserDirectoryLdapURL}) {
	$cgiargs{user_ldap_url} = $table{slapd}->{UserDirectoryLdapURL};
} else {
	$cgiargs{user_ldap_url} = $cgiargs{ldap_url};
}

# populate the DS with this file - the suffix in this file must
# be the suffix specified in the suffix argument above
# the filename should use the full absolute path
$cgiargs{install_ldif_file} = $table{slapd}->{InstallLdifFile};

# if for some reason you do not want the server started after instance creation
# the following line can be commented out - NOTE that if you are creating the
# Configuration DS, it will be started anyway
$cgiargs{start_server} = 1;

my $sroot = $cgiargs{sroot};

my $rc = &cgiFake($sroot, $verbose,
				  $sroot . "/bin/slapd/admin/bin/ds_newinst",
				  \%cgiargs);

if (!$rc) {
	print "Success!  Your new directory server instance was created\n";
} else {
	print "Error: Could not create new directory server instance\n";
}

sub printhash {
	my $table = shift;

	while (my ($key,$val) = each %{$table}) {
		print "[$key]\n";
		while (my ($k2,$v2) = each %{$val}) {
			print "$k2 = $v2\n";
		}
	}
}
