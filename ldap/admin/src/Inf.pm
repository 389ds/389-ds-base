#!perl
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

package NSSetupSDK::Inf;

use NSSetupSDK::Base;

%otherEnc = ('local' => "utf8", utf8 => "local");
# mapping of encoding to the subroutine which converts from that encoding
%convertEnc = ('local' => \&toUTF8, utf8 => \&toLocal);

#############################################################################
# Creator, the argument (optional) is the INF file to read
#
sub new {
	my ($class, $filename) = @_;
	my $self = {};

	bless $self, $class;

	if ($filename) {
		$self->read($filename);
	}

	return $self;
}

#############################################################################
# Read in and initialize ourself with the given INF file.  The file will be
# encoded in either local or utf8 encoding.  The type of encoding is given
# in the $enc argument.  We first read in the values in the given encoding,
# then convert the file to the other encoding, then read in and initialize
# the values for the other encoding
#
sub read {
	my ($self, $filename, $enc) = @_;
	my $inf = {}; # the contents of the inf file
	my @sectionList = (); # a list of section names, in order
	my $sectionName = "General"; # default section name

	open INF, $filename or return undef;

	push @sectionList, $sectionName;
	while (<INF>) {
		next if /^\s*#/; # skip comments
		next if /^\s*$/; # skip blank lines
		if (/^\s*\[([^\]]+)\]/) {
			$sectionName = $1; # new section
			if ($sectionName cmp "General") {
				# General is already in the list
				push @sectionList, $sectionName;
			}
		} else {
			chop;
			($name, $value) = split(/\s*=\s*/, $_, 2);
#			print "name=$name value=$value\n";
			$inf->{$sectionName}{$enc}{$name} = $value;
			$inf->{$sectionName}{$otherEnc{$enc}}{$name} =
				&{$convertEnc{$enc}}($value);
		}
	}
	close INF;

	$self->{inf} = $inf;
	$self->{sections} = [ @sectionList ];

#	foreach $section (keys %inf) {
#		print '[', $section, ']', "\n";
#		foreach $name (keys %{ $inf{$section} }) {
#			print "local $name=$inf{$section}{local}{$name}\n";
#			print "UTF8 $name=$inf{$section}{utf8}{$name}\n";
#		}
#	}

	return 1;
}

sub readLocal {
	my $self = shift;
	return $self->read(@_, 'local');
}

sub readUTF8 {
	my $self = shift;
	return $self->read(@_, 'utf8');
}

#############################################################################
# Init from a hash; used to create a subsection as another inf
#
sub init {
	my ($self, $hashRef) = @_;
	my $inf = {};
	$inf->{General} = $hashRef;
	$self->{inf} = $inf;
	$self->{sections} = [ "General" ];

	return 1;
}

#############################################################################
# return the number of sections
#
sub numSections {
	my $self = shift;
	return scalar(@{$self->{sections}});
}

#############################################################################
# return the section corresponding to the given name or number
#
sub getSection {
	my ($self, $section) = @_;
	if ($section =~ /\d+/) { # section is a number
		$section = $self->{sections}->[$section];
	}

	my $newSec = new Inf;
	$newSec->init($self->{inf}->{$section});
	return $newSec;
}

#############################################################################
# return the value of the given name in local encoding
#
sub getLocal {
	my ($self, $name) = @_;
	return getFromSection($self, "General", $name, "local");
}

#############################################################################
# return the value of the given name in UTF8 encoding
#
sub getUTF8 {
	my ($self, $name) = @_;
	return getFromSection($self, "General", $name, "utf8");
}

#############################################################################
# return the value of the given name in UTF8 encoding
#
sub get {
	my ($self, $name) = @_;
	return getFromSection($self, "General", $name, "utf8");
}

#############################################################################
# return the value of the given name in the given section
#
sub getFromSection {
	my ($self, $section, $name, $enc) = @_;
#	print "self inf = ", %{ $self->{inf} }, "\n";
#	print "self inf section = ", %{ $self->{inf}->{$section} }, "\n";
	return $self->{inf}->{$section}{$enc}{$name};
}

#############################################################################
# set the value
#
sub setInSection {
	my ($self, $section, $name, $value, $enc) = @_;
	if (!$enc) {
		$enc = 'utf8';
	}
	$self->{inf}->{$section}{$enc}{$name} = $value;
	$self->{inf}->{$section}{$otherEnc{$enc}}{$name} =
		&{$convertEnc{$enc}}($value);
}

#############################################################################
# set the value; value is locally encoded
#
sub setLocal {
	my ($self, $name, $value) = @_;
	setInSection($self, "General", $name, $value, "local");
}

#############################################################################
# set the value; value is UTF-8 encoded
#
sub setUTF8 {
	my ($self, $name, $value) = @_;
	setInSection($self, "General", $name, $value, "utf8");
}

#############################################################################
# set the value, assume UTF-8 encoding
#
sub set {
	my ($self, $name, $value) = @_;
	setInSection($self, "General", $name, $value, "utf8");
}

sub write {
	my ($self, $ref, $enc) = @_;
	my $needClose = undef;
	if (!$enc) {
		$enc = "local"; # write file in local encoding by default
	}
	if (!$ref) {
		# no filehandle given
		$ref = \*STDOUT;
	} elsif (!ref($ref)) { # not a ref, assume scalar filename
		# filename
		open(OUTPUT, ">$ref") or die "Error: could not write file $ref: $!";
		$ref = \*OUTPUT;
		$needClose = 1; # be sure to close
	} elsif (ref($ref) eq 'SCALAR') {
		# filename
		open(OUTPUT, ">$$ref") or die "Error: could not write file $$ref: $!";
		$ref = \*OUTPUT;
		$needClose = 1; # be sure to close
	} # else already a file handle ref
	foreach $secName (@{ $self->{sections} }) {
		print $ref "[", $secName, "]\n";
		foreach $name (keys %{ $self->{inf}->{$secName}{$enc} }) {
			$value = $self->{inf}->{$secName}{$enc}{$name};
			print $ref $name, "=", $value, "\n";
		}
		print $ref "\n";
	}
	if ($needClose) {
		close $ref;
	}
}

sub writeLocal {
	my ($self, $ref) = @_;
	$self->write($ref, 'local');
}

sub writeUTF8 {
	my ($self, $ref) = @_;
	$self->write($ref, 'utf8');
}

1; # the mandatory TRUE return from the package
