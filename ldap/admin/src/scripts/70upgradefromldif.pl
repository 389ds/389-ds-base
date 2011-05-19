use Mozilla::LDAP::Conn;
use Mozilla::LDAP::Entry;
use Mozilla::LDAP::Utils qw(normalizeDN);
use Mozilla::LDAP::API qw(:constant ldap_url_parse ldap_explode_dn);
use File::Basename;
use File::Copy;
use DSUtil qw(debug);

# Used to upgrade from an older version whose database might not be
# compatible - also for an upgrade from a machine of a different
# architecture
# For each backend instance, the ldif directory should contain
# a file called BACKEND.ldif e.g. userRoot.ldif NetscapeRoot.ldif etc.
# each file will be imported
# if the import is successful, the file will be renamed so that if
# upgrade is run again, it will not attempt to import it again, but
# it will be left around as a backup
sub runinst {
    my ($inf, $inst, $dseldif, $conn) = @_;

    my @errs;

    my $config = "cn=config";
    my $config_entry = $conn->search($config, "base", "(cn=*)");
    if (!$config_entry) {
        return ("error_no_configuration_entry", $!);
    }
    my $ldifdir = $config_entry->getValues('nsslapd-ldifdir');
    if (!$ldifdir) {
        debug(1, "No such attribute nsslapd-ldifdir in cn=config in $inst\n");
        return (); # nothing to do
    }
    my $rundir = $config_entry->getValues('nsslapd-rundir');
    my $instdir = $config_entry->getValues('nsslapd-instancedir');
    my $isrunning = 0;
    # Check if the server is up or not
    my $pidfile = $rundir . "/" . $inst . ".pid";
    if (-e $pidfile) {
        $isrunning = 1;
    }

    for my $file (glob("$ldifdir/*.upgrade.ldif")) {
        # assumes file name is backendname.upgrade.ldif
        my $dbinst = basename($file, ".upgrade.ldif");
        @errs = importLDIF($conn, $file, $dbinst, $isrunning, $instdir);
        if (@errs) {
            return @errs;
        }
        # else ok - rename file so we don't try to import again
        my $newfile = $file . ".importok";
        rename($file, $newfile);
    }

    return ();
}

sub startTaskAndWait {
    my ($conn, $entry) = @_;

    my $dn = $entry->getDN();
    # start the task
    $conn->add($entry);
    my $rc;
    if ($rc = $conn->getErrorCode()) {
        debug(0, "Couldn't add entry $dn: " . $conn->getErrorString());
        return $rc;
    }

    # wait for task completion - task is complete when the nsTaskExitCode attr is set
    my @attrlist = qw(nsTaskLog nsTaskStatus nsTaskExitCode nsTaskCurrentItem nsTaskTotalItems);
    my $done = 0;
    my $exitCode = 0;
    while (! $done) {
        sleep 1;
        $entry = $conn->search($dn, "base", "(objectclass=*)", 0, @attrlist);
        if ($entry->exists('nsTaskExitCode')) {
            $exitCode = $entry->getValues('nsTaskExitCode');
            $done = 1;
        } else {
            debug(1, $entry->getValues('nsTaskLog') . "\n");
        }
    }

    return $exitCode;
}

sub importLDIF {
  my ($conn, $file, $be, $isrunning, $instdir, $rc) = @_;

  if ($isrunning) {
      my $cn = "import" . time;
      my $dn = "cn=$cn,cn=import,cn=tasks,cn=config";
      my $entry = new Mozilla::LDAP::Entry();
      $entry->setDN($dn);
      $entry->setValues('objectclass', 'top', 'extensibleObject');
      $entry->setValues('cn', $cn);
      $entry->setValues('nsFilename', $file);
      $entry->setValues('nsInstance', $be);
      $rc = startTaskAndWait($conn, $entry);
      if ($rc) {
          return ('error_import_check_log', $file, $be, $rc . ":" . $conn->getErrorString());
      }
  } else { # server down - use ldif2db
      $? = 0; # clear
      if ($rc = system("$instdir/ldif2db -n $be -i $file > /dev/null 2>&1")) {
          debug(0, "Could not import $file to database $be - check errors log\n");
          return ('error_import_check_log', $file, $be, $?);
      }
  }

  return ();
}
