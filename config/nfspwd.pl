#! /usr/local/bin/perl
#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#

require "fastcwd.pl";

$_ = &fastcwd;
if (m@^/[uh]/@o || s@^/tmp_mnt/@/@o) {
    print("$_\n");
} elsif ((($user, $rest) = m@^/usr/people/(\w+)/(.*)@o)
      && readlink("/u/$user") eq "/usr/people/$user") {
    print("/u/$user/$rest\n");
} else {
    chop($host = `hostname`);
    print("/h/$host$_\n");
}
