#
# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#

# the argument is the full path and filename of the perldap.inf file

$infile = $ARGV[0];
$outfile = $ARGV[0] . ".tmp";
open(IN, $infile) or die "Error: could not read file $infile: $!";
open(OUT, ">$outfile") or die "Error: could not write file $outfile: $!";

while (<IN>) {
  if (/^Description/) {
	s/The Sun \| Netscape Alliance/Fedora/g;
	s/iPlanet/Fedora/g;
  } elsif (/^Vendor/) {
	s/The Sun \| Netscape Alliance/Fedora/g;
  }
  print OUT;
  if (/^Archive=perldap14.zip/) {
    print OUT "Visible=FALSE\n";
  }
}

close OUT;
close IN;

unlink $infile;
rename $outfile, $infile;
