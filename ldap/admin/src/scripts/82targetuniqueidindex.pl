use Mozilla::LDAP::Conn;
use Mozilla::LDAP::Utils qw(normalizeDN);
use Mozilla::LDAP::API qw(:constant ldap_url_parse ldap_explode_dn);
use DSUpdate qw(isOffline);

sub runinst {
    my ($inf, $inst, $dseldif, $conn) = @_;

    my $rc, @errs;

    my $config = $conn->search("cn=config", "base", "(objectclass=*)");
    if (!$config) {
        push @errs, ['error_finding_config_entry', 'cn=config',
                     $conn->getErrorString()];
        return @errs;
    }

    ($rc, @errs) = isOffline($inf, $inst, $conn);
    if (!$rc) {
        return @errs;
    }

    my $retrocldb = $conn->search("cn=changelog,cn=ldbm database,cn=plugins,cn=config", "base", "(objectclass=*)");
    if (!$retrocldb) {
        return (); # retrocl is not enabled; do nothing
    }

    my $indexdn = "cn=targetuniqueid,cn=index,cn=changelog,cn=ldbm database,cn=plugins,cn=config";
    my $targetuiniqidindex = $conn->search($indexdn, "base", "(objectclass=*)");
    if ($targetuiniqidindex) {
        return (); # targetuiniqidindex is alredy defined; do nothing
    }

    # add the targetuniqeid index to the retrocl backend

    my $entry = new Mozilla::LDAP::Entry();
    $entry->setDN($indexdn);
    $entry->setValues('objectclass', 'top', 'nsIndex');
    $entry->setValues('cn', 'targetuniqueid');
    $entry->setValues('nsSystemIndex', 'false');
    $entry->setValues('nsIndexType', 'eq');
    $conn->add($entry);

    # reindex targetuniquueid
    my $instancedir = $config->getValues('nsslapd-instancedir');
    my $reindex = $instancedir . "/db2index";

    my $rc = system("$reindex -n changelog -t targetuniqeid");


    return @errs;
}
