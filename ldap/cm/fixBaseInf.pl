#
# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#

# the first argument is the full path and filename of ths nsperl.inf file
# the second argument is the name of the sub component to use

$infile = $ARGV[0];
$outfile = $ARGV[0] . ".tmp";
open(IN, $infile) or die "Error: could not read file $infile: $!";
open(OUT, ">$outfile") or die "Error: could not write file $outfile: $!";

$inBaseSection = 0;
while (<IN>) {
  my $printIt = 1;
  if ($inBaseSection && /^Archive/) {
	$printIt = 0; # remove the Archive directives
  } elsif ($inBaseSection && /^System32Archive/) {
	$printIt = 0; # remove the Archive directives
  } elsif ($inBaseSection && /^RestoreFiles/) {
	$printIt = 0; # these files may not be present
  }
  if (/^\[base\]/) {
	$inBaseSection = 1;
  } elsif (/^\[/) {
	$inBaseSection = 0;
  }
  print OUT if $printIt;
}

close OUT;
close IN;

unlink $infile;
rename $outfile, $infile;
