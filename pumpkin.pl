#!/usr/local/bin/perl
#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#

# NT doesn't reliably do perl -e, so we have to do this.
die "Usage: $0 <days> <file>\n" unless $#ARGV == 1;

open( PUMPKIN, ">$ARGV[1]" ) || die "Can't create $ARGV[1]: $!\n";
print PUMPKIN time + $ARGV[0] * 24 * 60 * 60;
close( PUMPKIN );
