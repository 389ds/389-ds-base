use Mozilla::LDAP::Conn;
use Mozilla::LDAP::Utils qw(normalizeDN);
use Mozilla::LDAP::API qw(:constant ldap_url_parse ldap_explode_dn);

sub runinst {
    my ($inf, $inst, $dseldif, $conn) = @_;

    my @errs;
    my $mode;

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

    # ensure that other doesn't have permissions on rundir
    $mode = (stat($inf->{slapd}->{run_dir}))[2] or return ('error_chmoding_file', $inf->{slapd}->{run_dir}, $!);
    # mask off permissions for other
    $mode &= 07770;
    $! = 0; # clear errno
    chmod $mode, $inf->{slapd}->{run_dir};
    if ($!) {
        return ('error_chmoding_file', $inf->{slapd}->{run_dir}, $!);
    }

    return ();
}
