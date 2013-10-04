use File::Copy;
use Mozilla::LDAP::LDIF;
use DSCreate qw(installSchema);

sub runinst {
    my ($inf, $inst, $dseldif, $conn) = @_;

    if (!$inf->{slapd}->{config_dir} or (! -d $inf->{slapd}->{config_dir})) {
        return ('error_reading_config_dir', $inf->{slapd}->{config_dir});
    }

    # these files are obsolete, or we want to replace
    # them with newer versions
    my @toremove = qw(slapd-collations.conf);

    # make a backup directory to store the deleted config file, then
    # don't really delete it, just move it to that directory
    my $mode = (stat($inf->{slapd}->{config_dir}))[2];
    my $bakdir = $inf->{slapd}->{bak_dir} . ".bak";
    if (! -d $bakdir) {
        $! = 0; # clear
        mkdir $bakdir, $mode;
        if ($!) {
            return ('error_creating_directory', $bakdir, $!);
        }
    }

    my @errs;
    for my $file (@toremove) {
        my $oldname = $inf->{slapd}->{config_dir} . "/" . $file;
        next if (! -f $oldname); # does not exist - skip - already (re)moved
        my $newname = "$bakdir/$file";
        $! = 0; # clear
        rename $oldname, $newname;
        if ($!) {
            push @errs, ["error_renaming_config", $oldname, $newname, $!];
        }
    }

    my $configsrcdir = $inf->{slapd}->{config_dir} . "/../config"; 
    for my $file (@toremove) {
        my $srcname = "$configsrcdir/$file";
        my $newname = $inf->{slapd}->{config_dir} . "/" . $file;

        copy $srcname, $newname;
        if ($!) {
            push @errs, ["error_renaming_config", $srcname, $newname, $!];
        }
    }

    # If we've encountered any errors up to this point, restore
    # the original file.
    if (@errs) {
        # restore the original files
        for my $file (@toremove) {
            my $oldname = "$bakdir/$file";
            next if (! -f $oldname); # does not exist - not backed up
            my $newname = $inf->{slapd}->{config_dir} . "/" . $file;
            next if (-f $newname); # not removed
            rename $oldname, $newname;
        }
        return @errs;
    }

    if (-d $bakdir) {
        system("rm -rf $bakdir");
    }
    return ();
}
