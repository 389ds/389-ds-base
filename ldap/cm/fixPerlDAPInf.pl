#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
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
	s/The Sun \| Netscape Alliance/Netscape/g;
	s/iPlanet/Netscape/g;
  } elsif (/^Vendor/) {
	s/Sun \| Netscape Alliance/Netscape Communications Corp./g;
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
