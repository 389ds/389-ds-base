#!/usr/bin/perl -w

# take a solaris8 patch list and a solaris9 patch list and merge them
# together, removing duplicates
# we are looking for patches that have the same major revision
# number and release OS.  We only want to keep the one with the highest
# minor revision number

# key is the major patch number
# the value is a hash ref which has two keys 'iminor' and 'val'
# the value of key 'iminor' is the minor patch number
# the system keeps track of all revisions (minor number) for each patch (major number)
# we only want to list the highest revision, since on Solaris higher revisions include
# and supersede lower revisions
# the value of key 'val' is the string to print out
%patches = ();
@lines = ();

for $file (@ARGV) {
  open IN, $file or die "Error: could not open $file: $!";
  while (<IN>) {
	if (/^\s*\{(\d+),(\d+),\d,(\d+),/) {
	  $major = $1;
	  $minor = $2;
	  $rel = $3;
	  my $h = { 'val' => $_ };
	  $patches{$major}{$rel}{$minor} = $h;
	  if (! $patches{$major}{$rel}{highestminor}) {
		$patches{$major}{$rel}{highestminor} = $minor;
	  } elsif ($patches{$major}{$rel}{highestminor} <= $minor) { # highest minor rev is lt or eq new minor
		my $oldminor = $patches{$major}{$rel}{highestminor};
		$patches{$major}{$rel}{$oldminor}->{skip} = 1;
		$patches{$major}{$rel}{highestminor} = $minor;
	  } elsif ($patches{$major}{$rel}{highestminor} > $minor) {
		# skip the new one
		$h->{skip} = 1;
	  }
	  push @lines, $h; # put a hash ref into lines
	} else {
	  push @lines, $_; # put the scalar value into lines
	}
  }
  close IN;
}

for (@lines) {
  if (ref($_)) {
	if ($_->{skip}) {
	  chomp $_->{val};
	  print "/* duplicate or superseded ", $_->{val}, " */\n";
	} else {
	  print $_->{val};
	}
  } else {
	print;
  }
}
