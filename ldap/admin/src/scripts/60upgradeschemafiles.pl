
use Mozilla::LDAP::LDIF;
use DSCreate qw(installSchema);

sub runinst {
    my ($inf, $inst, $dseldif, $conn) = @_;

    if (!$inf->{slapd}->{schema_dir} or (! -d $inf->{slapd}->{schema_dir})) {
        return ('error_reading_schema_dir', $inf->{slapd}->{schema_dir});
    }

    # these schema files are obsolete, or we want to replace
    # them with newer versions
    my @toremove = qw(00core.ldif 01core389.ldif 01common.ldif 02common.ldif 05rfc2247.ldif 05rfc4523.ldif 10presence.ldif 28pilot.ldif 30ns-common.ldif 50ns-directory.ldif 60mozilla.ldif);

    # these hashes will be used to check for obsolete schema
    # in 99user.ldif
    my %attrsbyname;
    my %attrsbyoid;
    my %objclassesbyname;
    my %objclassesbyoid;
    my $userschemaentry;

    # make a backup directory to store the deleted schema, then
    # don't really delete it, just move it to that directory
    my $mode = (stat($inf->{slapd}->{schema_dir}))[2];
    my $bakdir = $inf->{slapd}->{schema_dir} . ".bak";
    if (! -d $bakdir) {
        $! = 0; # clear
        mkdir $bakdir, $mode;
        if ($!) {
            return ('error_creating_directory', $bakdir, $!);
        }
    }

    my @errs;
    for my $file (@toremove) {
        my $oldname = $inf->{slapd}->{schema_dir} . "/" . $file;
        next if (! -f $oldname); # does not exist - skip - already (re)moved
        my $newname = "$bakdir/$file";
        $! = 0; # clear
        rename $oldname, $newname;
        if ($!) {
            push @errs, ["error_renaming_schema", $oldname, $newname, $!];
        }
    }

    # Remove obsolete schema from 99user.ldif. Compare by name and OID.
    if (!open( OLDUSERSCHEMA, $inf->{slapd}->{schema_dir} . "/99user.ldif")) {
        push @errs, ["error_reading_schema_file", $inf->{slapd}->{schema_dir} . "/99user.ldif", $!];
    } else {
        my $olduserschema = new Mozilla::LDAP::LDIF(*OLDUSERSCHEMA);

        # Find the cn=schema entry.
        while ($userschemaentry = readOneEntry $olduserschema) {
            my $dn = $userschemaentry->getDN();
            # The only entry should be cn=schema, but best to play it safe.
            next if ($dn ne "cn=schema");

            # create the attributeTypes hashes (name->value, oid->value)
            my @attrtypes = $userschemaentry->getValues("attributeTypes");
            foreach my $attrtype (@attrtypes) {
                # parse out the attribute name and oid
                if ($attrtype =~ /^\(\s*([\d\.]+)\s+NAME\s+'(\w+)'/) {
                    # normalize the attribute name
                    $attrsbyname{lc "$2"} = "$attrtype";
                    $attrsbyoid{"$1"} = "$attrtype";
                }
            }

            # create the objectClasses hashes (name->value, oid->value)
            my @objclasses = $userschemaentry->getValues("objectClasses");
            foreach my $objclass (@objclasses) {
                # parse out the objectclass name and oid
                if ($objclass =~ /^\(\s*([\d\.]+)\s+NAME\s+'(\w+)'/) {
                    # normalize the objectclass name
                    $objclassesbyname{lc "$2"} = "$objclass";
                    $objclassesbyoid{"$1"} = "$objclass";
                }
            }

            # We found the cn=schema entry, so there's no need
            # to look for more entries.
            last;
        }

        close OLDUSERSCHEMA;
    }

    for my $file (@toremove) {
        my $fullname = "$bakdir/$file";

        next if (! -f $fullname); # does not exist - skip - already (re)moved

        if (!open( OBSOLETESCHEMA, "$fullname")) {
            push @errs, ["error_reading_schema_file", $fullname, $!];
        } else {
            my $obsoleteschema = new Mozilla::LDAP::LDIF(*OBSOLETESCHEMA);

            # Find the cn=schema entry.
            while (my $entry = readOneEntry $obsoleteschema) {
                my $dn = $entry->getDN();
                # The only entry should be cn=schema, but best to play it safe.
                next if ($dn ne "cn=schema");

                # Check if any of the attributeTypes in this file
                # are defined in 99user.ldif and remove them if so.
                my @attrtypes = $entry->getValues("attributeTypes");
                foreach $attrtype (@attrtypes) {
                    # parse out the attribute name and oid
                    if ($attrtype =~ /^\(\s*([\d\.]+)\s+NAME\s+'(\w+)'/) {
                        # normalize the attribute name
                        if ($attrsbyname{lc "$2"}) {
                            $userschemaentry->removeValue("attributeTypes", $attrsbyname{lc "$2"});
                        } elsif ($attrsbyoid{"$1"}) {
                            $userschemaentry->removeValue("attributeTypes", $attrsbyoid{"$1"});
                        }
                    }
                }

                # Check if any of the objectClasses in this file
                # are defined in 99user.ldif and remove them if so.
                my @objclasses = $entry->getValues("objectClasses");
                foreach $objclass (@objclasses) {
                    # parse out the objectclass name and oid
                    if ($objclass =~ /^\(\s*([\d\.]+)\s+NAME\s+'(\w+)'/) {
                        # normalize the objectclass name
                        if ($objclassesbyname{lc "$2"}) {
                            $userschemaentry->removeValue("objectClasses", $objclassesbyname{lc "$2"});
                        } elsif ($objclassesbyoid{"$1"}) {
                            $userschemaentry->removeValue("objectClasses", $objclassesbyoid{"$1"});
                        }
                    }
                }
            }

            close OBSOLETESCHEMA;
        }
    }

    # Backup the original 99user.ldif
    $! = 0; # clear
    rename $inf->{slapd}->{schema_dir} . "/99user.ldif", "$bakdir/99user.ldif";
    if ($!) {
        push @errs, ["error_renaming_schema", $inf->{slapd}->{schema_dir} . "/99user.ldif", "$bakdir/99user.ldif", $!];
    }

    # Write the new 99user.ldif
    if (!open ( NEWUSERSCHEMA, ">" . $inf->{slapd}->{schema_dir} . "/99user.ldif")) {
        push @errs, ["error_writing_schema_file", $inf->{slapd}->{schema_dir} . "/99user.ldif", $!];
    } else {
        my $newuserschema = new Mozilla::LDAP::LDIF(*NEWUSERSCHEMA);
        writeOneEntry $newuserschema $userschemaentry;
        close NEWUSERSCHEMA;

        # Set permissions based off of the original 99user.ldif.
        my @stat = stat("$bakdir/99user.ldif");
        my $mode = $stat[2];
        my $uid = $stat[4];
        my $gid = $stat[5];
        chmod $mode, $inf->{slapd}->{schema_dir} . "/99user.ldif";
        chown $uid, $gid, $inf->{slapd}->{schema_dir} . "/99user.ldif";
    }

    # If we've encountered any errors up to this point, restore
    # the original schema.
    if (@errs) {
        # restore the original schema files
        for my $file (@toremove) {
            my $oldname = "$bakdir/$file";
            next if (! -f $oldname); # does not exist - not backed up
            my $newname = $inf->{slapd}->{schema_dir} . "/" . $file;
            next if (-f $newname); # not removed
            rename $oldname, $newname;
        }

        # Restore 99user.ldif. We overwrite whatever is there since
        # it is possible that we have modified it.
        if (-f "$bakdir/99user.ldif") {
                rename "$bakdir/99user.ldif", $inf->{slapd}->{schema_dir} . "/99user.ldif";
        }

        return @errs;
    }

    # after removing them, just add everything in the default
    # schema directory
    return installSchema($inf, 1);
}
