#
# BEGIN COPYRIGHT BLOCK
# Copyright 2001 Sun Microsystems, Inc.
# Portions copyright 1999, 2001-2003 Netscape Communications Corporation.
# All rights reserved.
# END COPYRIGHT BLOCK
#
# parameters: BUILD_MODULE versionString buildnum.dat input security name isdirLite output [expDefine]

$module = shift;
$version = shift;
$buildFile = shift;
$input = shift;
$security = shift;
$name = shift;
$isdirLite = shift;
$instanceNamePrefix = shift;
$output = shift;
$expDefine = shift;
if ($expDefine) {
	( $junk, $expires ) = split( /=/, $expDefine );
	if ( ! $expires ) {
		$expires = 0;
	}
} else {
	$expires = 0;
}

# get the build number
open( FILE, $buildFile );
while ( <FILE> ) {
    last if ( $buildNum ) = /\\"(.*)\\"/;
}
close( FILE );

# copy the input file to the output file changing stuff along the way
open( FILE, $input );
open( OUT, ">$output" );
while ( <FILE> ) {
    s/%%%INSTANCE_NAME_PREFIX%%%/$instanceNamePrefix/;
    s/%%%SERVER_NAME%%%/$name/;
    s/%%%SERVER_VERSION%%%/$version/;
    s/%%%SERVER_BUILD_NUM%%%/$buildNum/;
    s/%%%PUMPKIN_HOUR%%%/$expires/;
    s/%%%SECURITY%%%/$security/;
    s/%%%IS_DIR_LITE%%%/$isdirLite/;
	print OUT;
}
close( OUT );
close( FILE );
