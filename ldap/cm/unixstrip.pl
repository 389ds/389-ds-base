#
# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#
use File::Find;

#
# usage: unixstrip.pl [ directory ... ] [ shlibsign ]
#
# if no arguments are passed, strip files under the current directory.
#
# if 1 argument is passed, strip files under the given directory.
#
# if 2 or more arguments are passed,
#   the last argument is considered the path to nss utility shlibsign (NSS3.9~),
#   the preceding args are directories, under which files are to be stripped.
#   And nss libraries libsoftokn3, libfreebl_pure32_3, libfreebl_hybrid_3
#   are to be checksum'ed with shlibsign.

my $SHLIBSIGN = "";
if (@ARGV > 1) {
    $SHLIBSIGN = $ARGV[$#ARGV];
    print STDERR "set $SHLIBSIGN \n";
    for (my $i = 0; $i < $#ARGV; $i++)
    {
        print STDERR "args[$i]: $ARGV[$i]\n";
        find(\&find_cb, $ARGV[$i]);
    }
} elsif (@ARGV == 1) {
    find(\&find_cb, @ARGV);
} else {
    find(\&find_cb, '.');
}

sub find_cb {
    return if (! -f $_);  # only look at plain files
    return if (! -B $_);  # skip text files
    return if (/\.jpg$/); # skip jpg files
    return if (/\.gif$/); # skip gif files
    return if (/\.jar$/); # skip jar files
    return if (/\.zip$/); # skip zip files
    return if (/\.gz$/);  # skip gzip files
    return if (/\.chk$/); # skip chk files
    print STDERR "about to strip $_ .\n";
    system("strip $_");
    print STDERR "strip $_ done.\n";
    if ($SHLIBSIGN ne "" && /libsoftokn3|libfreebl_pure32_3|libfreebl_hybrid_3/)
    {
        print STDERR "$SHLIBSIGN $_\n";
        system("$SHLIBSIGN -v -i $_");
    }
}

exit 0;
