# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2009 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details. 
# END COPYRIGHT BLOCK
#

package DSUpdateDialogs;

use strict;

use DialogManager;
use Setup;
use Dialog;
use DSUtil;
use FileConn;

my @updateadmindialogs;

my $updatewelcome = new DialogYesNo (
    $EXPRESS,
    ['update_dialog_first', 'brand', 'brand'],
    1,
    sub {
        my $self = shift;
        my $ans = shift;
        my $res = $self->handleResponse($ans);
        if ($res == $DialogManager::NEXT) {
            $res = $DialogManager::ERR if (!$self->isYes());
        }
        return $res;
    },
    ['update_dialog_first_prompt'],
);

my $updatemode = new Dialog (
    $EXPRESS,
    'update_dialog_mode',
    sub {
        my $self = shift;
        return $self->{manager}->{inf}->{General}->{UpdateMode} ||
            'quit';
    },
    sub {
        my $self = shift;
        my $ans = shift;
        my $res = $DialogManager::ERR;

        if ($ans =~ /^off/i) {
            $self->{manager}->{inf}->{General}->{UpdateMode} = 'offline';
            $res = $DialogManager::NEXT;
            for (@updateadmindialogs) {
                $_->disable(); # don't need admins and passwords
            }
        } elsif ($ans =~ /^on/i) {
            $self->{manager}->{inf}->{General}->{UpdateMode} = 'online';
            $res = $DialogManager::NEXT;
            if (!@updateadmindialogs) {
                @updateadmindialogs = makeInstanceDialogs($self->{manager});
                $self->{manager}->addDialog(@updateadmindialogs);
            }
            for (@updateadmindialogs) {
                $_->enable(); # need admins and passwords
            }
        }
        return $res;
    },
    ['update_dialog_mode_prompt']
);

sub makeInstanceDialogs {
    my $manager = shift;
    # for each directory server instance, create a dialog that prompts
    # for the admin user and password for that instance
    # the default admin user for each instance is the rootdn for that
    # instance
    for my $inst ($manager->{setup}->getDirServers()) {
        my $innerinst = $inst;
        if (!$manager->{inf}->{$inst}->{RootDN}) {
            # if we don't already have an admin DN set for this
            # instance, look in the dse.ldif for the nsslapd-rootdn
            my $dseldif = $manager->{setup}->{configdir} . "/" . $inst . "/dse.ldif";
            my $conn = new FileConn($dseldif, 1);
            my $rootdn;
            if ($conn) {
                my $ent = $conn->search("cn=config", "base", '(objectclass=*)');
                if ($ent) {
                    $rootdn = $ent->getValue('nsslapd-rootdn');
                } else {
                    $manager->alert('error_finding_config_entry',
                                    "cn=config", $dseldif, $conn->getErrorString());
                }
                $conn->close();
            } else {
                $manager->alert('error_opening_dseldif', $dseldif, $!);
            }
            if ($rootdn) {
                $manager->{inf}->{$inst}->{RootDN} = $rootdn;
            } else {
                $manager->{inf}->{$inst}->{RootDN} = "cn=Directory Manager";
            }
        }
        my $dlg = new Dialog (
            $EXPRESS,
            ['update_admin_dialog', $innerinst],
            sub {
                my $self = shift;
                my $index = shift;
                my $id;
                if ($index == 0) { # return undef for password defaults
                    $id = $self->{manager}->{inf}->{$innerinst}->{RootDN};
                }
                return $id;
            },
            sub {
                my $self = shift;
                my $ans = shift;
                my $index = shift;

                my $res = $DialogManager::SAME;
                if ($index == 0) {
                    if (!isValidDN($ans)) {
                        $self->{manager}->alert("dialog_dsrootdn_error", $ans);
                    } else {
                        $self->{manager}->{inf}->{$innerinst}->{RootDN} = $ans;
                        $res = $DialogManager::NEXT;
                    }
                } else {
                    if (!$ans or !length($ans)) {
                        $self->{manager}->alert("dialog_dsrootpw_invalid");
                    } else {
                        $self->{manager}->{inf}->{$innerinst}->{RootDNPwd} = $ans;
                        $res = $DialogManager::NEXT;
                    }
                }
                return $res;
            },
            ['update_admin_id_prompt'], ['update_admin_pwd_prompt', 1]
        );
        push @updateadmindialogs, $dlg;
    }

    return @updateadmindialogs;
}

sub getDialogs {
    return ($updatewelcome, $updatemode);
}

1;
