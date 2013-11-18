use Mozilla::LDAP::Conn;
use Mozilla::LDAP::Utils qw(normalizeDN);
use Mozilla::LDAP::API qw(:constant ldap_url_parse ldap_explode_dn);

sub runinst {
    my ($inf, $inst, $dseldif, $conn) = @_;

    my @errs;
    my $ldapifile;

    # Turn on nsslapd-pluginbetxn for 
    #     cn=Multimaster Replication Plugin
    #     cn=Roles Plugin,cn=plugins,cn=config
    #     cn=USN,cn=plugins,cn=config
    #     cn=Retro Changelog Plugin,cn=plugins,cn=config
    my @objplugins = (
        "cn=Multimaster Replication Plugin,cn=plugins,cn=config",
        "cn=Roles Plugin,cn=plugins,cn=config",
        "cn=USN,cn=plugins,cn=config",
        "cn=Retro Changelog Plugin,cn=plugins,cn=config"
    );
    foreach my $plugin (@objplugins) {
        my $ent = $conn->search($plugin, "base", "(cn=*)");
        if (!$ent) {
            return ('error_finding_config_entry', $plugin, $conn->getErrorString());
        }
        $ent->setValues('nsslapd-pluginbetxn', "on");
        $conn->update($ent);
    }

    # Set betxnpreoperation to nsslapd-plugintype for 
    #     cn=7-bit check,cn=plugins,cn=config
    #     cn=attribute uniqueness,cn=plugins,cn=config
    #     cn=Auto Membership Plugin,cn=plugins,cn=config
    #     cn=Linked Attributes,cn=plugins,cn=config
    #     cn=Managed Entries,cn=plugins,cn=config
    #     cn=PAM Pass Through Auth,cn=plugins,cn=config
    @preplugins = (
          "cn=7-bit check,cn=plugins,cn=config",
          "cn=attribute uniqueness,cn=plugins,cn=config",
          "cn=Auto Membership Plugin,cn=plugins,cn=config",
          "cn=Linked Attributes,cn=plugins,cn=config",
          "cn=Managed Entries,cn=plugins,cn=config",
          "cn=PAM Pass Through Auth,cn=plugins,cn=config"
    );
    foreach my $plugin (@preplugins) {
        my $ent = $conn->search($plugin, "base", "(cn=*)");
        if (!$ent) {
            return ('error_finding_config_entry', $plugin, $conn->getErrorString());
        }
        $ent->setValues('nsslapd-pluginType', "betxnpreoperation");
        $conn->update($ent);
    }

    # Set betxnpostoperation to nsslapd-plugintype for 
    #     cn=MemberOf Plugin,cn=plugins,cn=config
    #     cn=referential integrity postoperation,cn=plugins,cn=config
    #     cn=State Change Plugin,cn=plugins,cn=config
    @postplugins = (
          "cn=MemberOf Plugin,cn=plugins,cn=config",
          "cn=referential integrity postoperation,cn=plugins,cn=config",
          "cn=State Change Plugin,cn=plugins,cn=config"
    );
    foreach my $plugin (@postplugins) {
        my $ent = $conn->search($plugin, "base", "(cn=*)");
        if (!$ent) {
            return ('error_finding_config_entry', $plugin, $conn->getErrorString());
        }
        $ent->setValues('nsslapd-pluginType', "betxnpostoperation");
        $conn->update($ent);
    }

    return ();
}
