#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#

# the first argument is the full path and filename of ths nsperl.inf file
# the second argument is the name of the sub component to use

$infile = $ARGV[0];
$outfile = $ARGV[0] . ".tmp";
open(IN, $infile) or die "Error: could not read file $infile: $!";
open(OUT, ">$outfile") or die "Error: could not write file $outfile: $!";

$PRINT = 1;
while (<IN>) {
  if (/^Components\s*=\s*/) {
    if ($' =~ /$ARGV[1]/) {
      $_ = "Components=$ARGV[1]\n";
    } else {
      die "Error: the version of nsPerl in $infile does not contain $ARGV[1]\n";
    }
  }
  if (/^Archive=/) {
	$_ = "Archive=nsperl561.zip\n";
  }
  if (/^\[(\w+)\]/) {
    if (($1 eq $ARGV[1]) || ($1 eq General)) {
      $PRINT = 1;
    } else {
      $PRINT = 0;
    }
  }

  if ($PRINT) {
    if (/^Description/) {
	  s/The Sun \| Netscape Alliance/Netscape/g;
	  s/iPlanet/Netscape/g;
    } elsif (/^Vendor/) {
	  s/Sun \| Netscape Alliance/Netscape Communications Corp./g;
    }
    print OUT;
    if (/^RunPostInstall/) {
      print OUT "Checked=TRUE\nVisible=FALSE\n";
    }
  }
}

close OUT;
close IN;

unlink $infile;
rename $outfile, $infile;
