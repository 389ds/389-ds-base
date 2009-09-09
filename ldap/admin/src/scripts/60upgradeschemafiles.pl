
use DSCreate qw(installSchema);

sub runinst {
    my ($inf, $inst, $dseldif, $conn) = @_;

    if (!$inf->{slapd}->{schema_dir} or (! -d $inf->{slapd}->{schema_dir})) {
        return ('error_reading_schema_dir', $inf->{slapd}->{schema_dir});
    }

    # these schema files are obsolete, or we want to replace
    # them with newer versions
    my @toremove = qw(00core.ldif 01common.ldif 05rfc2247.ldif 10presence.ldif 28pilot.ldif 50ns-directory.ldif 60mozilla.ldif);

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

    if (@errs) { # errors backing up schema
        # restore the original schema files
        for my $file (@toremove) {
            my $oldname = "$bakdir/$file";
            next if (! -f $oldname); # does not exist - not backed up
            my $newname = $inf->{slapd}->{schema_dir} . "/" . $file;
            next if (-f $newname); # not removed
            rename $oldname, $newname;
        }
        return @errs;
    }

    # after removing them, just add everything in the default
    # schema directory
    return installSchema($inf, 1);
}
