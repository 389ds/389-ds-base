#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#

# The first argument is the file to edit
# The remaining arguments are pairs of values: the first value of the pair is
# the token to look for, and the second is the value to replace it with e.g.
# if the input file foo contains
#    $NETSITE_ROOT/%%%PERL_RUNTIME%%% -w perlscript ...
# then running $(PERL) thisscript foo %%%PERL_RUNTIME%%% foo/bar/perl5 > output/foo
# will result in output/foo containing
#    NETSITE_ROOT/foo/bar/perl5 -w perlscript ...

($input, %tokens) = @ARGV;

if (! $input) {
	print STDERR "Usage: $ $0 <inputfilename> [token1 replace1] ... [tokenN replaceN]\n";
	exit 1;
}

open(INPUT, $input) or die "Error: could not open file $input: $!";

while (<INPUT>) {
	while (($key, $value) = each %tokens) {
		s/$key/$value/g;
	}
	print;
}

close INPUT;
