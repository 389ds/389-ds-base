#! /usr/local/bin/perl
#
# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#

unshift(@INC, '/usr/lib/perl');
unshift(@INC, '/usr/local/lib/perl');

require "fastcwd.pl";

$cur = &fastcwd;
chdir($ARGV[0]);
$newcur = &fastcwd;
$newcurlen = length($newcur);

# Skip common separating / unless $newcur is "/"
$cur = substr($cur, $newcurlen + ($newcurlen > 1));
print $cur;
