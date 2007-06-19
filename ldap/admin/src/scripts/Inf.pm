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
    open INF, $filename or die "Error: could not open inf file $filename: $!";
    while (<INF>) {
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
            $self->{$curSection}->{$curkey} .= "\n" . $_; # add line in entirety to current value
        } elsif (/^\[(.*?)\]/) { # e.g. [General]
            $curSection = $1;
        } elsif (/^\s*(.*?)\s*=\s*(.*?)\s*$/) { # key = value
            $curkey = $1;
            $self->{$curSection}->{$curkey} = $2;
        }
        if ($iscontinuation) { # if line ends with a backslash, continue the data on the next line
            $incontinuation = 1;
        } else {
            $incontinuation = 0;
        }
	}
    close INF;
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
                print $fh "$key = ", $section->{$key}, "\n";
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

#############################################################################
# Mandatory TRUE return value.
#
1;
