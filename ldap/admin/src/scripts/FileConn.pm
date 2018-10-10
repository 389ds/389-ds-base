# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2007 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details. 
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

use DSUtil qw(debug);

require    Exporter;
@ISA       = qw(Exporter Mozilla::LDAP::Conn);
@EXPORT    = qw();
@EXPORT_OK = qw();

sub new {
    my $class = shift;
    my $filename = shift;
    my $readonly = shift;
    my @namingContexts = @_;
    my $self = {};

    $self = bless $self, $class;

    $self->{readonly} = $readonly;
    for my $ctx (@namingContexts) {
        $self->setNamingContext($ctx);
    }
    $self->setNamingContext(""); # root DSE
    if (!$self->read($filename)) {
        return;
    }

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
        return 1; # no filename given - ok
    }

    if (!open( MYLDIF, "$filename" )) {
        debug(1, "Could not open $filename: $!\n");
        return 0;
    }

    my $in = new Mozilla::LDAP::LDIF(*MYLDIF);
    $self->{reading} = 1;
    while ($ent = readOneEntry $in) {
        if (!$self->add($ent)) {
            debug(1, "Error: could not add entry " . $ent->getDN() . ":" . $self->getErrorString());
        }
    }
    delete $self->{reading};
    close( MYLDIF );

    return 1;
}

sub setNamingContext {
    my $self = shift;
    my $nc = shift;
    my $ndn = normalizeDN($nc);
    $self->{namingContexts}->{$ndn} = $ndn;
}

sub isNamingContext {
    my $self = shift;
    my $ndn = shift;
    return exists($self->{namingContexts}->{$ndn});
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

    if (!$self->{filename} or $self->{readonly} or $self->{reading}) {
        return 1; # ok - no filename given - just ignore
    }

    if (!open( MYLDIF, ">$filename" )) {
        debug(1, "Can't write $filename: $!\n");
        return 0;
    }

    $self->iterate("", LDAP_SCOPE_SUBTREE, \&writecb, \*MYLDIF);
    for my $ctx (keys %{$self->{namingContexts}}) {
        next if (!$ctx); # skip "" - we already did that
        $self->iterate($ctx, LDAP_SCOPE_SUBTREE, \&writecb, \*MYLDIF);
    }
    close( MYLDIF );

    return 1;
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
    return ldap_err2string($self->{lastErrorCode});
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

sub cloneEntry {
    my $src = shift;
    if (!$src) {
        return undef;
    }
    my $dest = new Mozilla::LDAP::Entry();
    $dest->setDN($src->getDN());
    for my $key (keys %{$src}) {
        if (ref($src->{$key})) {
            my @copyary = @{$src->{$key}};
            $dest->{$key} = [ @copyary ]; # make a deep copy
        } else {
            $dest->{$key} = $src->{$key};
        }
    }

    return $dest;
}

# have to return a copy of the entry - disallow inplace updates
sub nextEntry {
    my $self = shift;
    my $ent = shift @{$self->{entries}};
    return cloneEntry($ent);
}

sub add {
    my $self = shift;
    my $entry = shift;
    my $dn = $entry->getDN();
    my $ndn = normalizeDN($dn);
    my $parentdn = getParentDN($dn);
    my $nparentdn = normalizeDN($parentdn);

    $self->setErrorCode(0);
    # special case of naming context - has no parent
    if ($self->isNamingContext($ndn) and
        !exists($self->{$ndn}->{data})) {
        $self->{$ndn}->{data} = $entry;
        return $self->write();
    }

    if ($ndn && exists($self->{$ndn})) {
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

    if ($self->{readonly}) {
        debug(1, "Attempt to update read only $self->{filename} entry $dn\n");
        return 0;
    }

    $self->setErrorCode(0);
    if (!exists($self->{$ndn})) {
        $self->setErrorCode(LDAP_NO_SUCH_OBJECT);
        debug(1, "Attempt to update entry $dn that does not exist\n");
        return 0;
    }

    # The cloned entry will not contain the deleted attrs - the cloning
    # process omits the deleted attrs via the Entry FETCH, FIRSTKEY, and NEXTKEY
    # methods
    $self->{$ndn}->{data} = cloneEntry($entry);
    return $self->write();
}

sub delete {
    my $self = shift;
    my $dn = shift;

    if ($self->{readonly}) {
        debug(1, "Attempt to delete read only $self->{filename} entry $dn\n");
        return 0;
    }

    if (ref($dn)) {
        $dn = $dn->getDN(); # an Entry
    }
    my $ndn = normalizeDN($dn);

    $self->setErrorCode(0);
    if (!exists($self->{$ndn})) {
        $self->setErrorCode(LDAP_NO_SUCH_OBJECT);
        debug(1, "Attempt to delete entry $dn that does not exist\n");
        return 0;
    }

    if (@{$self->{$ndn}->{children}}) {
        $self->setErrorCode(LDAP_NOT_ALLOWED_ON_NONLEAF);
        debug(1, "Attempt to delete entry $dn that has children\n");
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

    return $self->write();
}

1;
