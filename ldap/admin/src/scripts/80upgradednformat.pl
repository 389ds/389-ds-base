use Mozilla::LDAP::Conn;
use Mozilla::LDAP::Utils qw(normalizeDN);
use Mozilla::LDAP::API qw(:constant ldap_url_parse ldap_explode_dn);
use File::Basename;
use File::Copy;
use DSUpdate qw(isOffline);

# Upgrade DN format if needed.
# For each backend instance, 
#     run upgradednformat with -N (dryrun mode),
#     if it returns 0 (Upgrade candidates are found), 
#     recursively copy the instance dir to the work dir (dnupgrade)
#     run upgradednformat w/o -N against the DB in the work dir
#     if it went ok, replace the original instance dir with the work dir.
# Note: This script does nothing if the server is up.
sub runinst {
    my ($inf, $inst, $dseldif, $conn) = @_;

    my @errs;

    my $config = "cn=config";
    my $config_entry = $conn->search($config, "base", "(cn=*)");
    if (!$config_entry) {
        return ("error_no_configuration_entry", $!);
    }

    # Check if the server is up or not
    my $rc;
    ($rc, @errs) = isOffline($inf, $inst, $conn);
    if (!$rc) {
        return @errs;
    }
    my $mappingtree = "cn=mapping tree,cn=config";
    my $ldbmbase = "cn=ldbm database,cn=plugins,cn=config";

    my $backend_entry;
    my $mtentry = $conn->search($mappingtree, "onelevel", "(cn=*)", 0, @attr);
    if (!$mtentry) {
        return ("error_no_mapping_tree_entries", $!);
    }

    my $db_config_entry = 
            $conn->search("cn=config,cn=ldbm database,cn=plugins,cn=config",
            "base", "(objectclass=*)");
    if (!$db_config_entry) {
        return ('error_finding_config_entry',
                'cn=config,cn=ldbm database,cn=plugins,cn=config',
                $conn->getErrorString());
    }
    # If subtree rename swich is not found in the config file,
    # set off to the switch and upgrade dn format using entrydn.
    my $switch = $db_config_entry->getValues('nsslapd-subtree-rename-switch');
    if ("" eq $switch) { 
        $db_config_entry->addValue('nsslapd-subtree-rename-switch', "off");
        $conn->update($db_config_entry);
    }

    # If a suffix in the mapping tree is doube-quoted and 
    # the cn value has only the double-quoted value, e.g.
    #   dn: cn="dc=example,dc=com",cn=mapping tree,cn=config
    #   cn: "dc=example,dc=com"
    # the following code adds non-quoted value:
    #   cn: dc=example,dc=com
    while ($mtentry) {
        my $numvals = $mtentry->size("cn");
        my $i;
        my $withquotes = -1;
        my $noquotes = -1;
        for ($i = 0; $i < $numvals; $i++) {
            if ($mtentry->{"cn"}[$i] =~ /^".*"$/) {
                $withquotes = $i;
            } else {
                $noquotes = $i;
            }
        }
        if ($withquotes >= 0 && $noquotes == -1) {
            # Has only cn: "<suffix>"
            # Adding cn: <suffix>
            my $stripped = $mtentry->{"cn"}[$withquotes];
            $stripped =~ s/^"(.*)"$/$1/;
            $mtentry->addValue("cn", $stripped);
            $conn->update($mtentry);
        }
        $mtentry = $conn->nextEntry();
    }

    my $instancedir = $config_entry->{"nsslapd-instancedir"}[0];
    my $upgradednformat = $instancedir . "/upgradednformat";

    # Scan through all of the backends to see if any of them
    # contain escape characters in the DNs.  If we find any
    # escapes, we need to run the conversion tool on that
    # backend.
    $backend_entry = $conn->search($ldbmbase, "onelevel", "(objectClass=nsBackendInstance)", 0, @attr);
    if (!$backend_entry) {
        return ("error_no_backend_entries", $!);
    }

    while ($backend_entry) {
        my $backend = $backend_entry->{"cn"}[0];
        my $dbinstdir = $backend_entry->{"nsslapd-directory"}[0];
        my $workdir = $dbinstdir . "/dnupgrade";
        my $dbdir = dirname($dbinstdir);
        my $pdbdir = dirname($dbdir);
        my $instname = basename($dbinstdir);

        if ("$dbdir" eq "" || "$instname" eq "") {
            push @errs, ["error_invalid_dbinst_dir", $dbinstdir];
            return @errs;
        }

        # clean up db region files, which might contain the old pages
        if ( -d $dbdir  && -f $dbdir."/__db.001") {
            unlink <$dbdir/__db.*>;
        }

        if (-e "$dbinstdir/id2entry.db4") {
            # Check if any DNs contain escape characters with dbscan.
            # dryrun mode
            # return values:  0 -- need to upgrade dn format
            #                 1 -- no need to upgrade dn format
            #                -1 -- error
            my $escapes = system("$upgradednformat -n $backend -a $dbinstdir -N");
            if (0 == $escapes) {
                # need to upgrade dn format
                my $rc = 0;

                if (system("cd $pdbdir; tar cf - db/DBVERSION | (cd $dbinstdir; tar xf -)") ||
                    system("cd $pdbdir; tar cf - db/$instname/DBVERSION | (cd $dbinstdir; tar xf -)") ||
                    system("cd $pdbdir; tar cf - db/$instname/*.db4 | (cd $dbinstdir; tar xf -)")) {
                    push @errs, ["error_cant_backup_db", $backend, $!];
                    return @errs;
                }
                my @stat = stat("$dbdir");
                my $mode = $stat[2];
                my $uid = $stat[4];
                my $gid = $stat[5];

                move("$dbinstdir/db", "$workdir");
                chmod($mode, $workdir);
                chown($uid, $gid, $workdir);

                @stat = stat("$dbinstdir");
                $mode = $stat[2];
                $uid = $stat[4];
                $gid = $stat[5];

                chmod($mode, "$workdir/$instname");
                chown($uid, $gid, "$workdir/$instname");

                # call conversion tool here and get return status.
                $rc = system("$upgradednformat -n $backend -a $workdir/$instname");
                if ($rc == 0) { # success
                    move("$dbinstdir", "$dbinstdir.orig");
                    move("$dbinstdir.orig/dnupgrade/$instname", "$dbinstdir");
                    copy("$dbinstdir.orig/dnupgrade/DBVERSION", "$dbdir");
                } else {
                    # Conversion failed. Cleanup and bail.
                    unlink <$dbinstdir/dnupgrade/$backend/*>;
                    rmdir("$dbinstdir/dnupgrade/$backend");
                    unlink <$dbinstdir/dnupgrade/*>;
                    rmdir("$dbinstdir/dnupgrade");
                    return ("error_cant_convert_db", $backend, $rc);
                }
            } else {
                # already upgraded or an error occurred.
                # check ancestorid to see if it has not-sorted ID list or not.
                my $ancestorid = $dbinstdir . "/ancestorid.db4";
                if (-e "$ancestorid") {
                    my $disorder = 0;
                    open(ANCESTOR, "/usr/bin/dbscan -f $ancestorid -r |");
                    while (<ANCESTOR>) {
                        if (!/^=[0-9]*/) {
                            chomp($_);
                            my @IDs = split(/ |	/, $_);
                            # print "ID count: $#IDs\n";
                            my $lasti = $#IDs;
                            for (my $i = 1; $i < $lasti; $i++) {
                                if ($IDs[$i] >= $IDs[$i + 1]) {
                                    $disorder = 1;
                                    last;
                                }
                            }
                            # print "Result: $disorder \n";
                            if ($disorder) {
                                last;
                            }
                        }
                    }
                    close(ANCESTOR);

                    # ancestorid index is in disorder; need to reindex it.
                    if ($disorder) {
                        print "The ancestorid index in $backend is in disorder; Reindexing $ancestorid.\n";
                        my $reindex = $instancedir . "/db2index";
                        my $rc = system("$reindex -n $backend -t ancestorid");
                    }
                }
            }
        }

        $backend_entry = $conn->nextEntry();
    }

    return ();
}
