#
# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2001 Sun Microsystems, Inc. Used by permission.
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#
package Cgi;

sub parse {
    my	$line = shift;
    my	$assign;
    my	$var;
    my	$value;

	# save time, don't parse empty lines
	return if (!$line);

    chomp( $line );
    if ( $raw ) {
	$raw .= '&' . $line;
    } else {
	$raw = $line;
    }
	# decode the line first
	$line = &decode($line);
	# this only works if there are no '&' characters in var or value . . .
    foreach $assign ( split( /&/, $line ) ) {
	# assume the var is everything before the first '=' in assign
	# and the value is everything after the first '='
	( $var, $value ) = split( /=/, $assign, 2 );
	$main::cgiVars{$var} = $value;
    }
}

sub decode {
    my	$string = shift;

    $string =~ s/\+/ /g;
    $string =~ s/%(\w\w)/chr(hex($1))/ge;

    return $string;
}

sub main::freakOut {
    my	$i;

    for ( $i = 0 ; $i < scalar( @_ ) ; ++$i ) {
	$_[$i] =~ s/'/\\'/g;
    }
    print "<SCRIPT language=JAVASCRIPT>\n";
    print "alert('@_');\n";
    print "location='index';\n</SCRIPT>\n";
    exit 0;
}

if ($ENV{'QUERY_STRING'}) {
    &parse( $ENV{'QUERY_STRING'} );
    $Cgi::QUERY_STRING = $ENV{'QUERY_STRING'};
}

if ( $ENV{'CONTENT_LENGTH'} ) {
    read STDIN, $Cgi::CONTENT, $ENV{'CONTENT_LENGTH'};
    &parse( $Cgi::CONTENT );
}

# $Cgi::QUERY_STRING contains the query string and
# $Cgi::CONTENT contains what was passed in through stdin

1;
