#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#
#
#

$isNT = -d '\\';

if ($isNT) {
  $ServerDir = "/Netscape/Servers";
} else {
  $ServerDir = "/usr/netscape/servers";
}
$ServerDirKey = "DefaultInstallDirectory";

$input = shift;
$output = shift;
die "cannot open input file $input" unless open( FILE, $input );
die "cannot open output file $output" unless open( OUT, ">$output" );
$inGeneralSection = 0;
$addServerDir = 1; # add the server dir if it's not already there
while ( <FILE> ) {
	# if the line begins with Components and does not contain
	# slapd already, add ", slapd" to the end 
	# else, just copy the line to output
	if ( /^Components/ ) {
		chomp;
		if (! /slapd/) {
			$_ .= ", slapd";
			$addedSlapd = 1;
		}
		if (! /nsperl/) {
			$_ .= ", nsperl";
			$addedNSperl = 1;
		}
		if (! /perldap/) {
			$_ .= ", perldap";
			$addedPerLDAP = 1;
		}
		$_ .= "\n";
	}
	if ( $inGeneralSection && /^$ServerDirKey/ ) {
		$addServerDir = 0; # already there, don't add it
	}
	if ( $inGeneralSection && /^\[/ ) {
		if ( $addServerDir ) {
			$_ = "$ServerDirKey = $ServerDir\n$_";
			$addServerDir = 0;
		}
		$inGeneralSection = 0;
	}
	if ( /^\[General\]/ ) {
		$inGeneralSection = 1;
	}
	print OUT $_;
}

close ( FILE );

# now, print the slapd section information
if ($addedSlapd) {
	print OUT "\n[slapd]\n";
	print OUT "ComponentInfoFile = slapd/slapd.inf\n";
}

if ($addedNSperl) {
	print OUT "\n[nsperl]\n";
	print OUT "ComponentInfoFile = nsperl/nsperl.inf\n";
}

if ($addedPerLDAP) {
	print OUT "\n[perldap]\n";
	print OUT "ComponentInfoFile = perldap/perldap.inf\n";
}

close ( OUT ); 

exit(0);
