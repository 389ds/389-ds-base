# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2007 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details. 
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
        if (!open(LOGFILE, ">$filename")) {
            print STDERR "Error: could not open logfile $filename: $!\n";
            return;
        }
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

sub logDebug {
    my ($self, @msg) = @_;
    if (!$self->{fh}) {
        return;
    }
    print { $self->{fh} } @msg;
}

sub levels {
    my $self = shift;
    return ($FATAL, $START, $SUCCESS, $WARN, $INFO, $DEBUG);
}

#############################################################################
# Mandatory TRUE return value.
#
1;
