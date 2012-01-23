use Mozilla::LDAP::Conn;
use DSUpdate qw(isOffline);

# Cleanup local changelog db
# If changelog db exists, run db_checkpoint to flush the transaction logs.
# Then, remove the local region files and transaction logs.
sub runinst {
    my ($inf, $inst, $dseldif, $conn) = @_;

    my @errs, $rc;

    my $config = "cn=changelog5,cn=config";
    my $config_entry = $conn->search($config, "base", "(cn=*)");
    if (!$config_entry) {
        # cn=changelog5 does not exist; not a master.
        return ();
    }
    # First, check if the server is up or down.
    ($rc, @errs) = isOffline($inf, $inst, $conn);
    if (!$rc) {
        return @errs;
    }
    my $changelogdir = $config_entry->getValues('nsslapd-changelogdir');

    # Run db_checkpoint
    system("/usr/bin/db_checkpoint -h $changelogdir -1");

    # Remove old db region files and transaction logs
    system("rm -f $changelogdir/__db.*");
    system("rm -f $changelogdir/log.*");
    system("rm -f $changelogdir/guardian");

    return ();
}
