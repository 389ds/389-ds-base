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
# FileConn is a subclass of Mozilla::LDAP::Conn.  This class does
# not use LDAP.  Instead, it operates on a given LDAP file, allowing
# you to search, add, modify, and delete entries in the file.
#
package FileConn;

use Mozilla::LDAP::Conn;
use Mozilla::LDAP::API qw(:constant ldap_explode_dn ldap_err2string); # Direct access to C API
use Mozilla::LDAP::Utils qw(normalizeDN);
use Mozilla::LDAP::LDIF;

require    Exporter;
@ISA       = qw(Exporter Mozilla::LDAP::Conn);
@EXPORT    = qw();
@EXPORT_OK = qw();

sub new {
    my $class = shift;
    my $filename = shift;
    my $readonly = shift;
    my $self = {};

    $self = bless $self, $class;

    $self->{readonly} = $readonly;
    $self->read($filename);

    return $self;
}

sub getParentDN {
    my $dn = shift;
    my @rdns = ldap_explode_dn($dn, 0);
    shift @rdns;
    return join(',', @rdns);
}

sub read {
    my $self = shift;
    my $filename = shift;

    if ($filename) {
        $self->{filename} = $filename;
    } else {
        $filename = $self->{filename};
    }

    if (!$self->{filename}) {
        return;
    }

    open( MYLDIF, "$filename" ) || die "Can't open $filename: $!";
    my $in = new Mozilla::LDAP::LDIF(*MYLDIF);
    while ($ent = readOneEntry $in) {
        if (!$self->add($ent)) {
            die "Error: could not add entry ", $ent->getDN(), ":", $self->getErrorString();
        }
    }
    close( MYLDIF );
}

# return all nodes below the given node
sub iterate {
    my $self = shift;
    my $dn = shift;
    my $scope = shift;
    my $callback = shift;
    my $context = shift;
    my $suppress = shift;
    my $ndn = normalizeDN($dn);
    my $children;
    if (exists($self->{$ndn}) and exists($self->{$ndn}->{children})) {
        $children = $self->{$ndn}->{children};
    }
    if (($scope != LDAP_SCOPE_ONELEVEL) && exists($self->{$ndn}) &&
        exists($self->{$ndn}->{data}) && $self->{$ndn}->{data} && !$suppress) {
        &{$callback}($self->{$ndn}->{data}, $context);
    }

    if ($scope == LDAP_SCOPE_BASE) {
        return;
    }

    for my $node (@{$children}) {
        &{$callback}($node->{data}, $context);
    }
    if ($scope == LDAP_SCOPE_SUBTREE) {
        for my $node (@{$children}) {
            $self->iterate($node->{data}->getDN(), $scope, $callback, $context, 1);
        }
    }
}

sub writecb {
    my $entry = shift;
    my $fh = shift;
    if (! $entry->getDN()) { # rootDSE requires special hack around perldap bug
        my $ary = $entry->getLDIFrecords();
        shift @$ary; # remove "dn"
        shift @$ary; # remove the empty dn value
        print $fh "dn:\n";
        print $fh (Mozilla::LDAP::LDIF::pack_LDIF (78, $ary), "\n");
    } else {
        Mozilla::LDAP::LDIF::put_LDIF($fh, 78, $entry);
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

    if (!$self->{filename} or $self->{readonly}) {
        return;
    }

    open( MYLDIF, ">$filename" ) || die "Can't write $filename: $!";
    $self->iterate("", LDAP_SCOPE_SUBTREE, \&writecb, \*MYLDIF);
    close( MYLDIF );
}

sub setErrorCode {
    my $self = shift;
    $self->{lastErrorCode} = shift;
}

sub getErrorCode {
    my $self = shift;
    return $self->{lastErrorCode};
}

sub getErrorString {
    my $self = shift;
    return ($self->{lastErrorCode} ? ldap_err2string($self->{lastErrorCode}) : LDAP_SUCCESS);
}

#############################################################################
# Print the last error code...
#
sub printError
{
  my ($self, $str) = @_;

  $str = "LDAP error:" unless defined($str);
  print "$str ", $self->getErrorString(), "\n";
}

sub DESTROY {
    my $self = shift;
    $self->close();
}

sub close {
    my $self = shift;
    return if ($self->{readonly});
    $self->write();
}

sub printcb {
    my $entry = shift;

    print $entry->getDN(), "\n";
}

sub print {
    my $self = shift;
    my $dn = shift;
    my $scope = shift;
    $self->iterate($dn, $scope, \&printcb);
}

# for each entry, call the user provided filter callback
# with the entry and the user provided filter context
# if the filtercb returns true, add the entry to the
# list of entries to return
sub searchcb {
    my $entry = shift;
    my $context = shift;
    my $self = $context->[0];
    my $filtercb = $context->[1];
    my $filtercontext = $context->[2];
    if (&{$filtercb}($entry, $filtercontext)) {
        push @{$self->{entries}}, $entry;
    }
}

sub matchall {
    return 1;
}

sub matchAttrVal {
    my $entry = shift;
    my $context = shift;
    my $attr = $context->[0];
    my $val = $context->[1];

    if ($val eq "*") {
        return $entry->exists($attr);
    }
    return $entry->hasValue($attr, $val, 1);
}   

my $attrpat  = '[-;.:\w]*[-;\w]';

# given a string filter, figure out which subroutine to 
# use to match
sub filterToMatchSub {
    my $self = shift;
    my ($basedn, $scope, $filter, $attrsonly, @rest) = @_;
    my ($matchsub, $context);
# do some filter processing
    if (!$filter or ($filter eq "(objectclass=*)") or
        ($filter eq "objectclass=*")) {
        $matchsub = \&matchall;
    } elsif ($filter =~ /^\(($attrpat)=(.+)\)$/o) {
        push @{$context}, $1, $2;
        $matchsub = \&matchAttrVal;
#     } elsif ($filter =~ /^\(\|\(($attrpat)=(.+)\)\(($attrpat)=(.+)\)\)$/o) {
#         $attr = $1;
#         $val = $2;
#         $attr1 = $1;
#         $val1 = $2;
#         $isand = 0;
#     } elsif ($filter =~ /^\(\&\(($attrpat)=(.+)\)\(($attrpat)=(.+)\)\)$/o) {
#         $attr = $1;
#         $val = $2;
#         $attr1 = $1;
#         $val1 = $2;
#         $isand = 1;
#     } elsif ($filter =~ /^\(\|\(($attrpat)=(.+)\)\(($attrpat)=(.+)\)\)$/o) {) {
# # 						   "(&(objectclass=nsBackendInstance)(|(nsslapd-suffix=$suffix)(nsslapd-suffix=$nsuffix)))");
    }

    $self->iterate($basedn, $scope, \&searchcb, [$self, $matchsub, $context]);
}

# simple searches only
sub search {
    my $self = shift;
    my ($basedn, $scope, $filter, $attrsonly, @rest) = @_;
    my $attrs;
    if (ref($rest[0]) eq "ARRAY") {
        $attrs = $rest[0];
    } elsif (scalar(@rest) > 0) {
        $attrs = \@rest;
    }

    $scope = Mozilla::LDAP::Utils::str2Scope($scope);

    $self->{entries} = [];

    my $ndn = normalizeDN($basedn);
    if (!exists($self->{$ndn}) or !exists($self->{$ndn}->{data})) {
        $self->setErrorCode(LDAP_NO_SUCH_OBJECT);
        return undef;
    }

    $self->setErrorCode(0);
    if (ref($filter) eq 'CODE') {
        $self->iterate($basedn, $scope, \&searchcb, [$self, $filter, $attrsonly]);
    } else {
        $self->filterToMatchSub($basedn, $scope, $filter, $attrsonly);
    }

    return $self->nextEntry();
}

sub nextEntry {
    my $self = shift;
    return shift @{$self->{entries}};
}

sub add {
    my $self = shift;
    my $entry = shift;
    my $dn = $entry->getDN();
    my $ndn = normalizeDN($dn);
    my $parentdn = getParentDN($dn);
    my $nparentdn = normalizeDN($parentdn);


    $self->setErrorCode(0);
    # special case of root DSE
    if (!$ndn and exists($self->{$ndn}) and
        !exists($self->{$ndn}->{data})) {
        $self->{$ndn}->{data} = $entry;
        $self->write();
        return 1;
    }

    if (exists($self->{$ndn})) {
        $self->setErrorCode(LDAP_ALREADY_EXISTS);
        return 0;
    }

    if ($ndn && $nparentdn && !exists($self->{$nparentdn})) {
        $self->setErrorCode(LDAP_NO_SUCH_OBJECT);
        return 0;
    }
    # each hash entry has two keys
    # data is the actual Entry
    # children is the array ref of the one level children of this dn
    $self->{$ndn}->{data} = $entry;
    # don't add parent to list of children
    if ($nparentdn ne $ndn) {
        push @{$self->{$nparentdn}->{children}}, $self->{$ndn};
    }

    return 1;
}

sub update {
    my $self = shift;
    my $entry = shift;
    my $dn = $entry->getDN();
    my $ndn = normalizeDN($dn);

    $self->setErrorCode(0);
    if (!exists($self->{$ndn})) {
        $self->setErrorCode(LDAP_NO_SUCH_OBJECT);
        return 0;
    }

    $self->{$ndn}->{data} = $entry;
    $self->write();

    return 1;
}

sub delete {
    my $self = shift;
    my $dn = shift;

    if (ref($dn)) {
        $dn = $dn->getDN(); # an Entry
    }
    my $ndn = normalizeDN($dn);

    $self->setErrorCode(0);
    if (!exists($self->{$ndn})) {
        $self->setErrorCode(LDAP_NO_SUCH_OBJECT);
        return 0;
    }

    if (@{$self->{$ndn}->{children}}) {
        $self->setErrorCode(LDAP_NOT_ALLOWED_ON_NONLEAF);
        return 0;
    }

    # delete the data associated with this node
    delete $self->{$ndn}->{data};
    delete $self->{$ndn}->{children};

    my $parentdn = getParentDN($dn);
    my $nparentdn = normalizeDN($parentdn);
    # delete this node from its parent
    if ($ndn ne $nparentdn) {
        for (my $ii = 0; $ii < @{$self->{$nparentdn}->{children}}; ++$ii) {
            # find matching hash ref in parent's child list
            if ($self->{$nparentdn}->{children}->[$ii] eq $self->{$ndn}) {
                # remove that element from the array
                splice @{$self->{$nparentdn}->{children}}, $ii, 1;
                # done - should only ever be one matching child
                last;
            }
        }
    }

    # delete this node
    delete $self->{$ndn};

    $self->write();
    return 1;
}

1;
