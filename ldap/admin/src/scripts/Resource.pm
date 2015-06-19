# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2007 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details. 
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
