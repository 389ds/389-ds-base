use Mozilla::LDAP::Conn;
use Mozilla::LDAP::Utils qw(normalizeDN);
use Mozilla::LDAP::API qw(:constant ldap_url_parse ldap_explode_dn);

sub runinst {
    my ($inf, $inst, $dseldif, $conn) = @_;

    my @errs;

    # see if nsslapd-ldapiautodnsuffix is defined
    my $ent = $conn->search("cn=config", "base", "(objectclass=*)");
    if (!$ent) {
        return ('error_finding_config_entry', 'cn=config', $conn->getErrorString());
    }

    if ($ent->getValues('nsslapd-ldapiautodnsuffix')) {
        $ent->remove('nsslapd-ldapiautodnsuffix');
        $conn->update($ent);
        # ignore errors - cn=config attr deletion not allowed over ldap
    }

    return ();
}
