#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#
# used to add the perl preamble to all perl scripts used by the directory
# server run time; the problem is that the INC paths are compiled into
# the executable and cannot be overridden at run time, so we have to make
# sure we fix them

# the first argument is the source path for the INC path
# the second argument is the token to replace with the server root
# at install time
# the third argument is the source perl script
# the fourth argument is the destination perl script
# 

($sourceLibPath, $serverRootToken, $sourceScript, $destScript) = @ARGV;
open SRC, $sourceScript or die "Error: could not open $sourceScript: $!";
open DEST, ">$destScript" or die "Error: could not write $destScript: $!";

$isNT = -d '\\';

print DEST<<EOF1;
#!perl
# This preamble must be at the beginning of every perl script so that we can
# find packages and dynamically load code at run time; SERVER_ROOT must
# be replaced with the absolute path to the server root at installation
# time; it is assumed that the perl library will be installed as
# server root/install/perl
BEGIN {
EOF1

# if on NT, just assume we are using activeState, which looks like this
# server root/install/lib
#                    /bin
#                    /site/lib
# there is no arch subdir on NT
if ($isNT) {
	print DEST "\t\@INC = qw( $serverRootToken/install/lib $serverRootToken/install/site/lib . );\n";
	print DEST "}\n";
} else {
	print DEST<<EOF2;
	\$BUILD_PERL_PATH = \"$sourceLibPath\";
	\$RUN_PERL_PATH = \"$serverRootToken/install\";
	# make sure we use the unix path conventions
	grep { s#\$BUILD_PERL_PATH#\$RUN_PERL_PATH#g } \@INC;
}
EOF2
}

# copy the rest of the file
while (<SRC>) {
	print DEST;
}

close DEST;
close SRC;
