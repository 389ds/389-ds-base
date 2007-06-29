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
# Copyright (C) 2007 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#
# This implements SetupLog from setuputil InstallLog in perl
#
package SetupLog;
use Exporter ();
@ISA       = qw(Exporter);
@EXPORT    = qw($FATAL $START $SUCCESS $WARN $INFO $DEBUG);
@EXPORT_OK = qw($FATAL $START $SUCCESS $WARN $INFO $DEBUG);

use POSIX qw(strftime);

# tempfiles
use File::Temp qw(tempfile tempdir);

# exported variables
$FATAL    = "Fatal";
$START    = "Start";
$SUCCESS  = "Success";
$WARN     = "Warning";
$INFO     = "Info";
$DEBUG    = "Debug";

sub new {
    my $type = shift;
    my $filename = shift;
    my $prefix = shift || "setup";
    my $self = {};
    my $fh;

    if (!$filename) {
        ($fh, $filename) = tempfile("${prefix}XXXXXX", UNLINK => 0,
                                    SUFFIX => ".log", DIR => File::Spec->tmpdir);
    } else {
        open LOGFILE, ">$filename" or die "Error: could not open logfile $filename: $!";
        $fh = \*LOGFILE;
    }
    $self->{fh} = $fh;
    $self->{filename} = $filename;
    $self = bless $self, $type;

    return $self;
}

sub logMessage {
    my ($self, $level, $who, $msg, @rest) = @_;
    if (!$self->{fh}) {
        return;
    }

    my $string = strftime "[%y/%m/%d:%H:%M:%S] - ", localtime;
    $string .= "[$who] $level ";
    $string .= sprintf $msg, @rest;
    print { $self->{fh} } $string;
}

sub levels {
    my $self = shift;
    return ($FATAL, $START, $SUCCESS, $WARN, $INFO, $DEBUG);
}

#############################################################################
# Mandatory TRUE return value.
#
1;
