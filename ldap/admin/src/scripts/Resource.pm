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

# manages resource bundle files - gets values
# given keys

package Resource;

use strict;

#require    Exporter;
#@ISA       = qw(Exporter);
#@EXPORT    = qw();

sub new {
    my $type = shift;
    my $self = {};

    while (@_) {
        push @{$self->{filenames}}, shift;
    }

    $self = bless $self, $type;

    if (@{$self->{filenames}}) {
        $self->read();
    }

    return $self;
}

# the resource files are read in order given.  Definitions from
# later files override the same definitions in earlier files.
sub read {
    my $self = shift;

    while (@_) {
        push @{$self->{filenames}}, shift;
    }

    for my $filename (@{$self->{filenames}}) {
        my $incontinuation = 0;
        my $curkey;
        open RES, $filename or die "Error: could not open resource file $filename: $!";
        my $line;
        while ($line = <RES>) {
            my $iscontinuation;
            chop $line; # trim trailing newline
            if ($line =~ /^\s*$/) { # skip blank/empty lines
                $incontinuation = 0;
                next;
            }
            if ($line =~ /^\s*\#/) { # skip comment lines
                $incontinuation = 0;
                next;
            }
            # read name = value pairs like this
            # bol whitespace* name whitespace* '=' whitespace* value eol
            # the value will include any trailing whitespace
            if ($line =~ /\\$/) {
                chop $line;
                $iscontinuation = 1;
            }
            if ($incontinuation) {
                $self->{res}->{$curkey} .= "\n" . $line;
            } elsif ($line =~ /^\s*(.*?)\s*=\s*(.*?)$/) {
                # replace \n with real newline
                if ($curkey) {
                    $self->{res}->{$curkey} =~ s/\\n/\n/g;
                }
                $curkey = $1;
                $self->{res}->{$curkey} = $2;
            }
            if ($iscontinuation) { # if line ends with a backslash, continue the data on the next line
                $incontinuation = 1;
            } else {
                $incontinuation = 0;
            }
        }
        # replace \n with real newline
        if (defined($curkey)) {
            $self->{res}->{$curkey} =~ s/\\n/\n/g;
        }
        close RES;
    }
}

# given a resource key and optional args, return the value
# $text = $res->getText('key');
# or
# $text = $res->getText('key', @args);
# or
# $text = $res->getText($arrayref)
# where $arrayref is ['key', @args]
sub getText {
    my $self = shift;
    my $key = shift;
    my @args = @_;

    if (ref($key) eq 'ARRAY') {
        my $tmpkey = shift @{$key};
        @args = @{$key};
        $key = $tmpkey;
    }

    if (!exists($self->{res}->{$key})) {
        print "Error: unknown resource key $key\n";
        return undef;
    }

    if (!defined($self->{res}->{$key})) {
        print "Error: resource key $key has no value\n";
        return undef;
    }

    # see if the args themselves are resource keys
    for (my $ii = 0; $ii < @args; ++$ii) {
        if (exists($self->{res}->{$args[$ii]})) {
            $args[$ii] = $self->{res}->{$args[$ii]};
        }
    }

    my $text = sprintf $self->{res}->{$key}, @args;

    return $text;
}

#############################################################################
# Mandatory TRUE return value.
#
1;
