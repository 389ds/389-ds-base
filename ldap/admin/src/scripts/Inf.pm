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

# manages inf files - gets values
# given keys

package Inf;

#require    Exporter;
#@ISA       = qw(Exporter);
#@EXPORT    = qw();

sub new {
    my $type = shift;
    my $self = {};

    $self->{filename} = shift;

    $self = bless $self, $type;

    if ($self->{filename}) {
        $self->read();
    }

    return $self;
}

sub read {
# each key in the table is a section name
# the value is a hash ref of the items in that section
#   in that hash ref, each key is the config param name,
#   and the value is the config param value
    my $self = shift;
    my $filename = shift;
    my $curSection = "";

    if ($filename) {
        $self->{filename} = $filename;
    } else {
        $filename = $self->{filename};
    }

    my $incontinuation = 0;
    my $curkey;
    my $curval;
    my $inffh;
    if ($filename eq "-") {
        $inffh = \*STDIN;
    } else {
        open INF, $filename or die "Error: could not open inf file $filename: $!";
        $inffh = \*INF;
    }
    while (<$inffh>) {
        my $iscontinuation;
        chop; # trim trailing newline
        if (/^\s*$/) { # skip blank/empty lines
            $incontinuation = 0;
            next;
        }
        if (/^\s*\#/) { # skip comment lines
            $incontinuation = 0;
            next;
        }
        if (/\\$/) { # line ends in \ - continued on next line
            chop;
            $iscontinuation = 1;
        }
        if ($incontinuation) {
            if ($curval) {
                $self->{$curSection}->{$curkey}->[$curval] .= "\n" . $_; # add line in entirety to current value
            } else {
                $self->{$curSection}->{$curkey} .= "\n" . $_; # add line in entirety to current value
            }
        } elsif (/^\[(.*?)\]/) { # e.g. [General]
            $curSection = $1;
            $iscontinuation = 0; # disallow section continuations
        } elsif (/^\s*(.*?)\s*=\s*(.*?)\s*$/) { # key = value
            $curkey = $1;
            # a single value is just a single scalar
            # multiple values are represented by an array ref
            if (exists($self->{$curSection}->{$curkey})) {
                if (!ref($self->{$curSection}->{$curkey})) {
                    # convert single scalar to array ref
                    my $ary = [$self->{$curSection}->{$curkey}];
                    $self->{$curSection}->{$curkey} = $ary;
                }
                # just push the new value
                push @{$self->{$curSection}->{$curkey}}, $2;
                $curval = @{$self->{$curSection}->{$curkey}} - 1; # curval is index of last item
            } else {
                # single value
                $self->{$curSection}->{$curkey} = $2;
                $curval = 0; # only 1 value
            }
        }
        if ($iscontinuation) { # if line ends with a backslash, continue the data on the next line
            $incontinuation = 1;
        } else {
            $incontinuation = 0;
        }
	}
    if ($inffh ne \*STDIN) {
        close $inffh;
    }
}

sub section {
    my $self = shift;
    my $key = shift;

    if (!exists($self->{$key})) {
        print "Error: unknown inf section $key\n";
        return undef;
    }

    return $self->{$key};
}

sub writeSection {
    my $self = shift;
    my $name = shift;
    my $fh = shift;
    my $section = $self->{$name};
    if (ref($section) eq 'HASH') {
        print $fh "[$name]\n";
        for my $key (keys %{$section}) {
            if (defined($section->{$key})) {
                my $val = $section->{$key};
                $val =~ s/\n/\\\n/g; # make continuation lines
                print $fh "$key = $val\n";
            }
        }
    }
}

sub write {
    my $self = shift;
    my $filename = shift;

    if ($filename) {
        $self->{filename} = $filename;
    } else {
        $filename = $self->{filename};
    }

    return if ($filename eq "-");

    open INF, ">$filename" or die "Error: could not write inf file $filename: $!";
    # write General section first
    $self->writeSection('General', \*INF);
    print INF "\n";
    for my $key (keys %{$self}) {
        next if ($key eq 'General');
        $self->writeSection($key, \*INF);
        print INF "\n";
    }
    close INF;
}

sub updateFromArgs {
    my $self = shift;
    my $argsinf = {}; # tmp for args read in

    if (!@_) {
        return 1; # no args - just return
    }

    # read args into temp inf
    for (@_) {
        if (/^([\w_-]+)\.([\w_-]+)=(.*)$/) { # e.g. section.param=value
            my $sec = $1;
            my $parm = $2;
            my $val = $3;
            # a single value is just a single scalar
            # multiple values are represented by an array ref
            if (exists($argsinf->{$sec}->{$parm})) {
                if (!ref($argsinf->{$sec}->{$parm})) {
                    # convert single scalar to array ref
                    my $ary = [$argsinf->{$sec}->{$parm}];
                    $argsinf->{$sec}->{$parm} = $ary;
                }
                # just push the new value
                push @{$argsinf->{$sec}->{$parm}}, $val;
            } else {
                # single value
                $argsinf->{$sec}->{$parm} = $val;
            }
        } else { # error
            print STDERR "Error: unknown command line option $_\n";
            return;
        }
    }

    # no args read - just return true
    if (!$argsinf || !%{$argsinf}) {
        return 1;
    }

    # override inf with vals read from args
    while (my ($name, $sec) = each %{$argsinf}) {
        if (ref($sec) eq 'HASH') {
            for my $key (keys %{$sec}) {
                if (defined($sec->{$key})) {
                    my $val = $sec->{$key};
                    $self->{$name}->{$key} = $val;
                }
            }
        }
    }

    return 1;
}

#############################################################################
# Mandatory TRUE return value.
#
1;
