#
# BEGIN COPYRIGHT BLOCK
# This Program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; version 2 of the License.
# 
# This Program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License along with
# this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
# Place, Suite 330, Boston, MA 02111-1307 USA.
# 
# In addition, as a special exception, Red Hat, Inc. gives You the additional
# right to link the code of this Program with code not covered under the GNU
# General Public License ("Non-GPL Code") and to distribute linked combinations
# including the two, subject to the limitations in this paragraph. Non-GPL Code
# permitted under this exception must only link to the code of this Program
# through those well defined interfaces identified in the file named EXCEPTION
# found in the source code files (the "Approved Interfaces"). The files of
# Non-GPL Code may instantiate templates or use macros or inline functions from
# the Approved Interfaces without causing the resulting work to be covered by
# the GNU General Public License. Only Red Hat, Inc. may make changes or
# additions to the list of Approved Interfaces. You must obey the GNU General
# Public License in all respects for all of the Program code and other code used
# in conjunction with the Program except the Non-GPL Code covered by this
# exception. If you modify this file, you may extend this exception to your
# version of the file, but you are not obligated to do so. If you do not wish to
# provide this exception without modification, you must delete this exception
# statement from your version and license this file solely under the GPL without
# exception. 
# 
# 
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
