#!/usr/local/bin/perl
#
# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#

# NT doesn't reliably do perl -e, so we have to do this.
die "Usage: $0 <days> <file>\n" unless $#ARGV == 1;

open( PUMPKIN, ">$ARGV[1]" ) || die "Can't create $ARGV[1]: $!\n";
print PUMPKIN time + $ARGV[0] * 24 * 60 * 60;
close( PUMPKIN );
