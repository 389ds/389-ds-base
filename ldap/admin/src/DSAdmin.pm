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

package DSAdmin;

use POSIX;

use strict;
use vars qw($VERSION @ISA @EXPORT @EXPORT_OK $AUTOLOAD
$isNT $PATHSEP $quote $script_suffix $exe_suffix $os
$dll_suffix $argumentative @args $first $rest $errs $pos
);

require Exporter;
require DynaLoader;
require AutoLoader;

@ISA = qw(Exporter DynaLoader);
# Items to export into callers namespace by default. Note: do not export
# names by default without a very good reason. Use EXPORT_OK instead.
# Do not simply export all your public functions/methods/constants.

# Items to export into callers namespace by default. Note: do not export
# names by default without a very good reason. Use EXPORT_OK instead.
# Do not simply export all your public functions/methods/constants.
@EXPORT = qw(
	normalizeDN toLocal toUTF8
);
$VERSION = '1.00';

bootstrap DSAdmin $VERSION;

BEGIN {
	require 'uname.lib';
	$isNT = -d '\\';
#	@INC = ( '.', '../../../admin/admin/bin' );
#	grep { s@/@\\@g } @INC if $isNT;
	$PATHSEP = $isNT ? '\\' : '/';
	# NT needs quotes around some things unix doesn't
	$quote = $isNT ? "\"" : "";

	$script_suffix = $isNT ? ".bat" : "";
	$exe_suffix = $isNT ? ".exe" : "";
    if ($isNT) {
		$os = "WINNT";
    } else {
    	$os = &uname("-s");
    }

	# dll suffix for shared libraries in old instance; note that the dll suffix
	# may have changed for the new instance e.g. AIX now uses .so
    if ( $os eq "AIX" ) {
		$dll_suffix = "_shr.a";
    }	
    elsif ( $os eq "HP-UX" ) {
	$arch = &uname("-p");
	if ( $arch eq "ia64" ) {
		$dll_suffix = ".so";
	} else {
		$dll_suffix = ".sl";
        }
    }	
    elsif ( $os eq "WINNT" ) {
		$dll_suffix = ".dll";
    }	
    else {
		$dll_suffix = ".so";
    }	
}

sub getCwd {
	my $command = $isNT ? "cd" : "/bin/pwd";
	open(PWDCMD, "$command 2>&1 |") or
		die "Error: could not execute $command: $!";
	# without the following sleep, reading from the pipe will
	# return nothing; I guess it gives the pwd command time
	# to get some data to read . . .
	sleep(1);
	my $curdir;
	while (<PWDCMD>) {
		if (!$curdir) {
			chomp($curdir = $_);
		}
	}
	my $code = close(PWDCMD);
#	if ($code || $?) {
#		print "$command returned code=$code status=$? dir=$curdir\n";
#	}
#	print "getCwd curdir=\[$curdir\]\n";
	return $curdir;
}

# this is used to run the system() call, capture exit and signal codes,
# and die() upon badness; the first argument is a directory to change
# dir to, if any, and the rest are passed to system()
sub mySystem {
	my $rc = &mySystemNoDie(@_);
	my ($dir, @args) = @_;
    if ($rc == 0) {
# success
    } elsif ($rc == 0xff00) {
		die "Error executing @args: error code $rc: $!";
    } elsif ($rc > 0x80) {
        $rc >>= 8;
		die "Error executing @args: error code $rc: $!";
    } else {
        if ($rc &   0x80) {
            $rc &= ~0x80;
        } 
		die "Error executing @args: received signal $rc: $!";
    }

	# usually won't get return value
	return $rc;
}

# This version does not die but just returns the error code
sub mySystemNoDie {
	my ($dir, @args) = @_;
	if ($dir && ($dir ne "")) {
		chdir($dir) or die "Could not change directory to $dir: $!";
	}
	my $cmd = $args[0];
	# the system {$cmd} avoids some NT shell quoting problems if the $cmd
	# needs to be quoted e.g. contains spaces; the map puts double quotes
	# around the arguments on NT which are stripped by the command
	# interpreter cmd.exe; but don't quote things which are already quoted
	my @fixargs = map { /^[\"].*[\"]$/ ? $_ : $quote . $_ . $quote } @args;
	my $rc = 0;
	if ($cmd =~ /[.](bat|cmd)$/) {
		# we have to pass batch files directly to the NT command interpreter
		$cmd = $ENV{COMSPEC};
		if (!$cmd) {
			$cmd = 'c:\winnt\system32\cmd.exe';
		}
#		print "system $cmd /c \"@fixargs\"\n";
		$rc = 0xffff & system {$cmd} '/c', "\"@fixargs\"";
	} else {
		print "system $cmd @fixargs\n";
        $rc = 0xffff & system {$cmd} @fixargs;
    }
	return $rc;
}

sub getTempFileName {
	my $tmp = tmpnam();
	while (-f $tmp) {
		$tmp = tmpnam();
	}

	return $tmp;
}

sub getopts {
    local($argumentative) = @_;
    local(@args,$_,$first,$rest);
    local($errs) = 0;
    local($[) = 0;

    @args = split( / */, $argumentative );
    while(@ARGV && ($_ = $ARGV[0]) =~ /^-(.)(.*)/) {
        ($first,$rest) = ($1,$2);
        $pos = index($argumentative,$first);
        if($pos >= $[) {
            if($args[$pos+1] eq ':') {
                shift(@ARGV);
                if($rest eq '') {
                    ++$errs unless @ARGV;
                    $rest = shift(@ARGV);
                }
                eval "\$main::opt_$first = \$rest;";
            }
            else {
                eval "\$main::opt_$first = 1";
                if($rest eq '') {
                    shift(@ARGV);
                }
                else {
                    $ARGV[0] = "-$rest";
                }
            }
        }
        else {
            print STDERR "Unknown option: $first\n";
            ++$errs;
            if($rest ne '') {
                $ARGV[0] = "-$rest";
            }
            else {
                shift(@ARGV);
            }
        }
    }
    $errs == 0;
}

# Autoload methods go after =cut, and are processed by the autosplit program.

1;
__END__
# Below is the stub of documentation for your module. You better edit it!

=head1 NAME

DSAdmin - Perl extension for directory server administrative utility functions

=head1 SYNOPSIS

  use DSAdmin;

=head1 DESCRIPTION

The DSAdmin module is used by directory server administration scripts, such as
those used for installation/uninstallation, instance creation/removal, CGIs,
etc.

=head1 AUTHOR

Richard Megginson richm@netscape.com

=head1 SEE ALSO

perl(1).

=cut
