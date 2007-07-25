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

package DSDialogs;

use strict;

use Net::Domain qw(hostname hostfqdn);
use DialogManager;
use Setup;
use Dialog;
use Util;

my $dsport = new Dialog (
    $TYPICAL,
    'dialog_dsport_text',
    sub {
        my $self = shift;
        my $port = $self->{manager}->{inf}->{slapd}->{ServerPort};
        if (!defined($port)) {
            $port = 389;
        }
        if (!portAvailable($port)) {
            $port = getAvailablePort();
        }
        return $port;
    },
    sub {
        my $self = shift;
        my $ans = shift;
        my $res = $DialogManager::SAME;
        if ($ans !~ /\d+/) {
            $self->{manager}->alert("dialog_dsport_error", $ans);
        } elsif (!portAvailable($ans)) {
            $self->{manager}->alert("dialog_dsport_error", $ans);
        } else {
            $res = $DialogManager::NEXT;
            $self->{manager}->{inf}->{slapd}->{ServerPort} = $ans;
        }
        return $res;
    },
    ['dialog_dsport_prompt']
);

my $dsserverid = new Dialog (
    $TYPICAL,
    'dialog_dsserverid_text',
    sub {
        my $self = shift;
        my $serverid = $self->{manager}->{inf}->{slapd}->{ServerIdentifier};
        if (!defined($serverid)) {
            $serverid = $self->{manager}->{inf}->{General}->{FullMachineName};
            if (!defined($serverid)) {
                $serverid = hostname;
            } else { # strip out the leftmost domain component
                $serverid =~ s/\..*$//;
            }
        }
        return $serverid;
    },
    sub {
        my $self = shift;
        my $ans = shift;
        my $res = $DialogManager::SAME;
        my $path = $self->{manager}->{setup}->{configdir} . "/slapd-" . $ans;
        if (!isValidServerID($ans)) {
            $self->{manager}->alert("dialog_dsserverid_error", $ans);
        } elsif (-d $path) {
            $self->{manager}->alert("dialog_dsserverid_inuse", $ans);
        } else {
            $res = $DialogManager::NEXT;
            $self->{manager}->{inf}->{slapd}->{ServerIdentifier} = $ans;
        }
        return $res;
    },
    ['dialog_dsserverid_prompt']
);

my $dssuffix = new Dialog (
    $TYPICAL,
    'dialog_dssuffix_text',
    sub {
        my $self = shift;
        my $suffix = $self->{manager}->{inf}->{slapd}->{Suffix};
        if (!defined($suffix)) {
            $suffix = $self->{manager}->{inf}->{General}->{FullMachineName};
            if (!defined($suffix)) {
                $suffix = hostfqdn;
            }
            $suffix =~ s/^[^\.]*\.//; # just the domain part
            # convert fqdn to dc= domain components
            $suffix = "dc=$suffix";
            $suffix =~ s/\./, dc=/g;
        }
        return $suffix;
    },
    sub {
        my $self = shift;
        my $ans = shift;
        my $res = $DialogManager::SAME;
        if (!isValidDN($ans)) {
            $self->{manager}->alert("dialog_dssuffix_error", $ans);
        } else {
            $res = $DialogManager::NEXT;
            $self->{manager}->{inf}->{slapd}->{Suffix} = $ans;
        }
        return $res;
    },
    ['dialog_dssuffix_prompt']
);

my $dsrootdn = new Dialog (
    $EXPRESS,
    'dialog_dsrootdn_text',
    sub {
        my $self = shift;
        my $index = shift;
        my $rootdn;
        if ($index == 0) { # return undef for password defaults
            $rootdn = $self->{manager}->{inf}->{slapd}->{RootDN};
            if (!defined($rootdn)) {
                $rootdn = "cn=Directory Manager";
            }
        }
        return $rootdn;
    },
    sub {
        my $self = shift;
        my $ans = shift;
        my $index = shift;
        my $res = $DialogManager::SAME;
        if ($index == 0) { # verify DN
            if (!isValidDN($ans)) {
                $self->{manager}->alert("dialog_dsrootdn_error", $ans);
            } else {
                $res = $DialogManager::NEXT;
                $self->{manager}->{inf}->{slapd}->{RootDN} = $ans;
            }
        } elsif ($index == 1) { # verify initial password
            my $test = $ans;
            if ($test) {
                $test =~ s/\s//g;
            }
            if (!$ans or (length($ans) < 8)) {
                $self->{manager}->alert("dialog_dsrootpw_tooshort", 8);
            } elsif (length($test) != length($ans)) {
                $self->{manager}->alert("dialog_dsrootpw_invalid");
            } else {
                $res = $DialogManager::NEXT;
                $self->{firstpassword} = $ans; # save for next index
            }
        } elsif ($index == 2) { # verify second password
            if ($ans ne $self->{firstpassword}) {
                $self->{manager}->alert("dialog_dsrootpw_nomatch");
            } else {
                $self->{manager}->{inf}->{slapd}->{RootDNPwd} = $ans;
                $res = $DialogManager::NEXT;
            }
        }
        return $res;
    },
    ['dialog_dsrootdn_prompt'], ['dialog_dsrootpw_prompt1', 1], ['dialog_dsrootpw_prompt2', 1]
);

my $dssample = new DialogYesNo (
    $CUSTOM,
    'dialog_dssample_text',
    0,
    sub {
        my $self = shift;
        my $ans = shift;
        my $res = $self->handleResponse($ans);
        if ($res == $DialogManager::NEXT) {
            $self->{manager}->{inf}->{slapd}->{AddSampleEntries} = ($self->isYes() ? 'Yes' : 'No');
        }
        return $res;
    },
    ['dialog_dssample_prompt'],
);

my $dspopulate = new Dialog (
    $CUSTOM,
    'dialog_dspopulate_text',
    sub {
        my $self = shift;
        my $val = $self->{manager}->{inf}->{slapd}->{InstallLdifFile};
        if (!defined($val)) {
            $val = 'suggest';
            $self->{manager}->{inf}->{slapd}->{AddOrgEntries} = 'Yes';
        }
        return $val;
    },
    sub {
        my $self = shift;
        my $ans = shift;
        my $res = $DialogManager::SAME;
        if ($ans eq 'none') {
            $self->{manager}->{inf}->{slapd}->{InstallLdifFile} = 'none';
            $self->{manager}->{inf}->{slapd}->{AddOrgEntries} = 'No';
            $res = $DialogManager::NEXT;
        } elsif ($ans eq 'suggest') {
            $self->{manager}->{inf}->{slapd}->{InstallLdifFile} = 'suggest';
            $self->{manager}->{inf}->{slapd}->{AddOrgEntries} = 'Yes';
            $res = $DialogManager::NEXT;
        } else { # a file
            if (! -f $ans) {
                $self->{manager}->alert("dialog_dspopulate_error", $ans);
            } else {
                $self->{manager}->{inf}->{slapd}->{InstallLdifFile} = $ans;
                $self->{manager}->{inf}->{slapd}->{AddOrgEntries} = 'No';
                $res = $DialogManager::NEXT;
            }
        }
        return $res;
    },
    ['dialog_dspopulate_prompt']
);

sub getDialogs {
    return ($dsport, $dsserverid, $dssuffix, $dsrootdn, $dssample, $dspopulate);
}

1;
