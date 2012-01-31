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

    my $dbconf = $conn->search("cn=config,cn=ldbm database,cn=plugins,cn=config", "base", "(objectclass=*)");
    if (!$dbconf) {
        push @errs, ['error_finding_config_entry',
                     'cn=config,cn=ldbm database,cn=plugins,cn=config',
                     $conn->getErrorString()];
        return @errs;
    }

    # Get the value of nsslapd-subtree-rename-switch.
    my $switch = $dbconf->getValues('nsslapd-subtree-rename-switch');
    if ("" eq $switch) {
        return (); # subtree-rename-switch does not exist; do nothing.
    } elsif ("off" eq $switch || "OFF" eq $switch) {
        return (); # subtree-rename-switch is OFF; do nothing.
    }

    my $dbdir = $dbconf->getValues('nsslapd-directory');
    my $dbversion0 = $dbdir . "/DBVERSION";
    my $is_rdn_format = 0;
    my $dbversionstr = "";
    if (!open(DBVERSION, "$dbversion0")) {
        push @errs, ['error_opening_file', $dbversion0, $!];
        return @errs;
    } else {
        while (<DBVERSION>) {
            if ($_ =~ /rdn-format/) {
                $is_rdn_format = 1;
                $dbversionstr = $_;
                if ($_ =~ /rdn-format-1/) {
                    $is_rdn_format = 2;
                }
                if ($_ =~ /rdn-format-2/) {
                    $is_rdn_format = 3;
                }
            }
        }
        close DBVERSION;

        if (3 == $is_rdn_format) {
            return (); # DB already has the new rdn format.
        }

        if (0 == $is_rdn_format) {
            push @errs, ['error_format_error', 'database'];
            return @errs;
        }
    }

    my $instconf = $conn->search("cn=ldbm database,cn=plugins,cn=config", "onelevel", "(objectclass=*)");
    if (!$instconf) {
        push @errs, ['error_finding_config_entry',
                     'cn=*,cn=ldbm database,cn=plugins,cn=config',
                     $conn->getErrorString()];
        return @errs;
    }

    my $instancedir = $config->getValues('nsslapd-instancedir');
    my $reindex = $instancedir . "/db2index";

    while ($instconf) {
        my $backend= $instconf->getValues('cn');
        if (($backend eq "config") || ($backend eq "monitor")) {
            goto NEXT;
        }
        my $instdbdir = $instconf->getValues('nsslapd-directory');
        my $dbversion1 = $instdbdir . "/DBVERSION";
        if (!open(DBVERSION, "$dbversion1")) {
            push @errs, ['error_opening_file', $dbversion1, $!];
            goto NEXT;
        } else {
            my $versionstr = "";
            while (<DBVERSION>) {
                if ($_ =~ /rdn-format/) {
                    $is_rdn_format = 1;
                    $versionstr = $_;
                    if ($_ =~ /rdn-format-1/) {
                        $is_rdn_format = 2;
                    }
                    if ($_ =~ /rdn-format-2/) {
                        $is_rdn_format = 3;
                    }
                }
            }
            close DBVERSION;
    
            if (3 == $is_rdn_format) {
                # DB already has the new rdn format.
                goto NEXT;
            }
    
            if (0 == $is_rdn_format) {
                push @errs, ['error_format_error', $instdbdir];
                goto NEXT;
            }

            # reindex entryrdn
            my $rc = system("$reindex -n $backend -t entryrdn");

            # update instance DBVERSION file
            if ($versionstr ne "") {
                if (!open(DBVERSION, "> $dbversion1")) {
                    push @errs, ['error_opening_file', $dbversion1, $!];
                } else {
                    $versionstr =~ s,rdn\-format\-1/,rdn\-format\-2/,;
                    $versionstr =~ s,rdn\-format/,rdn\-format\-2/,;
                    print DBVERSION $versionstr; # not chomp'd above, already has newline
                    close DBVERSION;
                }
            }
        }
NEXT:
        $instconf = $conn->nextEntry();
    }

    # update main DBVERSION file
    if (!open(DBVERSION, "> $dbversion0")) {
        push @errs, ['error_opening_file', $dbversion0, $!];
    } else {
        $dbversionstr =~ s,rdn\-format\-1/,rdn\-format\-2/,;
        $dbversionstr =~ s,rdn\-format/,rdn\-format\-2/,;
        print DBVERSION $dbversionstr; # not chomp'd above, already has newline
        close DBVERSION;
    }

    return @errs;
}
