use Mozilla::LDAP::Conn;
use Mozilla::LDAP::Utils qw(normalizeDN);
use Mozilla::LDAP::API qw(:constant ldap_url_parse ldap_explode_dn);
use DSUpdate qw(isOffline);

sub runinst {
    my ($inf, $inst, $dseldif, $conn) = @_;

    my $rc, @errs;

    ($rc, @errs) = isOffline($inf, $inst, $conn);
    if (!$rc) {
        return @errs;
    }

    my $ent0 = $conn->search("cn=config", "base", "(objectclass=*)");
    if (!$ent0) {
        return ('error_finding_config_entry', 'cn=config',
                $conn->getErrorString());
    }

    my $ent1 = $conn->search("cn=config,cn=ldbm database,cn=plugins,cn=config",
                            "base", "(objectclass=*)");
    if (!$ent1) {
        return ('error_finding_config_entry',
                'cn=config,cn=ldbm database,cn=plugins,cn=config',
                $conn->getErrorString());
    }

    # Get the value of nsslapd-subtree-rename-switch.
    my $need_update = 0;
    my $switch = $ent1->getValues('nsslapd-subtree-rename-switch');
    if ("" eq $switch) {
        $ent1->addValue('nsslapd-subtree-rename-switch', "on");
        $need_update = 1;
    } elsif ("off" eq $switch || "OFF" eq $switch) {
        $ent1->setValues('nsslapd-subtree-rename-switch', "on");
        $need_update = 1;
        $conn->update($ent1);
    }

    if (1 == $need_update) {
        $conn->update($ent1);
        # Convert the database format from entrydn to entryrdn
        my $instdir = $ent0->getValue('nsslapd-instancedir');
        my $prog = $instdir . "/dn2rdn";
        my $output = `$prog 2>&1`;
        my $stat = $?;

        if (0 != $stat) {
            $ent1->setValues('nsslapd-subtree-rename-switch', "off");
            $conn->update($ent1);
        }
    }

    return ();
}
