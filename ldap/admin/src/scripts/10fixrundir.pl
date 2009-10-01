use Mozilla::LDAP::Conn;
use Mozilla::LDAP::Utils qw(normalizeDN);
use Mozilla::LDAP::API qw(:constant ldap_url_parse ldap_explode_dn);

sub runinst {
    my ($inf, $inst, $dseldif, $conn) = @_;

    my @errs;

    # see if nsslapd-rundir is defined
    my $ent = $conn->search("cn=config", "base", "(objectclass=*)");
    if (!$ent) {
        return ('error_finding_config_entry', 'cn=config', $conn->getErrorString());
    }

    if (!$ent->getValues('nsslapd-rundir')) {
        $ent->setValues('nsslapd-rundir', $inf->{slapd}->{run_dir});
        # mark as modified so update will use a replace instead of an add
        $ent->attrModified('nsslapd-rundir');
        $conn->update($ent);
        my $rc = $conn->getErrorCode();
        if ($rc) {
            return ('error_updating_entry', 'cn=config', $conn->getErrorString());
        }
    }

    return ();
}
