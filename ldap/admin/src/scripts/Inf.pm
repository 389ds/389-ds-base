# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2007 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details. 
# END COPYRIGHT BLOCK
#

# manages inf files - gets values
# given keys

package Inf;

use DSUtil;
use File::Temp qw(tempfile tempdir);

#require    Exporter;
#@ISA       = qw(Exporter);
#@EXPORT    = qw();

sub new {
    my $type = shift;
    my $self = {};

    $self->{filename} = shift;
    $self->{writable} = shift; # do not overwrite user supplied file
    # if you want to init an Inf with a writable file, use
    # $inf = new Inf($filename, 1)

    $self = bless $self, $type;

    if ($self->{filename}) {
        if($self->read() != 0){
            undef $self;
        }
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
        if (!open(INF, $filename)) {
            debug(0, "Error: could not open inf file $filename: $!\n");
            return -1;
        }
        $inffh = \*INF;
    }
    my $line;
    while ($line = <$inffh>) {
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
        if ($line =~ /\\$/) { # line ends in \ - continued on next line
            chop $line;
            $iscontinuation = 1;
        }
        if ($incontinuation) {
            if ($curval) {
                $self->{$curSection}->{$curkey}->[$curval] .= "\n" . $line; # add line in entirety to current value
            } else {
                $self->{$curSection}->{$curkey} .= "\n" . $line; # add line in entirety to current value
            }
        } elsif ($line =~ /^\[(.*?)\]/) { # e.g. [General]
            $curSection = $1;
            $iscontinuation = 0; # disallow section continuations
        } elsif ($line =~ /^\s*(.*?)\s*=\s*(.*?)\s*$/) { # key = value
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

    return 0;
}

sub section {
    my $self = shift;
    my $key = shift;

    if (!exists($self->{$key})) {
        debug(0, "Error: unknown inf section $key\n");
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
        for my $key (sort keys %{$section}) {
            if (exists($section->{$key}) and defined($section->{$key}) and
                (length($section->{$key}) > 0)) {
                my @vals = ();
                if (ref($section->{$key})) {
                    @vals = @{$section->{$key}};
                } else {
                    @vals = ($section->{$key});
                }
                for my $val (@vals) {
                    $val =~ s/\n/\\\n/g; # make continuation lines
                    print $fh "$key = $val\n";
                }
            }
        }
    }
}

sub write {
    my $self = shift;
    my $filename = shift;
    my $fh;

    return if ($filename and $filename eq "-");

    # see if user wants to force use of a temp file
    if ($filename and $filename eq '__temp__') {
        $self->{writable} = 1;
        $filename = '';
        delete $self->{filename};
    }

    if (!$self->{writable}) {
        return; # do not overwrite read only file
    }

    if ($filename) { # use user supplied filename
        $self->{filename} = $filename;
    } elsif ($self->{filename}) { # use existing filename
        $filename = $self->{filename};
    } else { # create temp filename
        ($fh, $self->{filename}) = tempfile("setupXXXXXX", UNLINK => 0,
                                            SUFFIX => ".inf", OPEN => 1,
                                            DIR => File::Spec->tmpdir);
    }

    my $savemask = umask(0077);
    if (!$fh) {
        if (!open(INF, ">$filename")) {
            debug(0, "Error: could not write inf file $filename: $!\n");
            umask($savemask);
            return;
        }
        $fh = *INF;
    }
    # write General section first
    $self->writeSection('General', $fh);
    for my $key (keys %{$self}) {
        next if ($key eq 'General');
        $self->writeSection($key, $fh);
    }
    close $fh;
    umask($savemask);
}

sub updateFromArgs {
    my $self = shift;
    my $argsinf = {}; # tmp for args read in

    if (!@_) {
        return 1; # no args - just return
    }

    # read args into temp inf
    for my $arg (@_) {
        if ($arg =~ /^([\w_-]+)\.([\w_-]+)=(.*)$/) { # e.g. section.param=value
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
            debug(0, "Error: unknown command line option $arg\n");
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
