#{{PERL-EXEC}}
#
# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2003-2004 AOL, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
#

sub getDbDir
{
    (my $here) = @_;
    my @dbdirs = ();

    opendir(DIR, $here) or die "can't opendir $here : $!";
    while (defined($dir = readdir(DIR)))
    {
        my $thisdir;
        if ("$here" eq ".")
        {
            $thisdir = $dir;
        }
        else
        {
            $thisdir = $here . "{{SEP}}" . $dir;
        }
        if (-d $thisdir)
        {
            if (!($thisdir =~ /\./))
            {
                opendir(SUBDIR, "$thisdir") or die "can't opendir $thisdir : $!";
                while (defined($file = readdir(SUBDIR)))
                {
                    if ($file eq "DBVERSION")
                    {
                        $#dbdirs++;
                        $dbdirs[$#dbdirs] = $thisdir;
                    }
                }
                closedir(SUBDIR);
            }
        }
    }
    closedir(DIR);

    return \@dbdirs;
}

sub getLastLogfile
{
    (my $here) = @_;
    my $logfile = "";

    opendir(DIR, $here) or die "can't opendir $here : $!";
    while (defined($file = readdir(DIR)))
    {
        if ($file =~ /log./)
        {
            $logfile = $file;
        }
    }
    closedir(DIR);

    return \$logfile;
}

print("*****************************************************************\n");
print("verify-db: This tool should only be run if recovery start fails\n" .
      "and the server is down.  If you run this tool while the server is\n" .
      "running, you may get false reports of corrupted files or other\n" .
      "false errors.\n");
print("*****************************************************************\n");

# get dirs having DBVERSION
my $dbdirs = getDbDir(".");

for (my $i = 0; $i < @$dbdirs; $i++)
{
    # run ../bin/slapd/server/db_printlog -h <dbdir> for each <dbdir>
    print "Verify log files in $$dbdirs[$i] ... ";
    open(PRINTLOG, "..{{SEP}}bin{{SEP}}slapd{{SEP}}server{{SEP}}db_printlog -h $$dbdirs[$i] 2>&1 1> nul |");
    sleep 1;
    my $haserr = 0;
    while ($l = <PRINTLOG>)
    {
        if ("$l" ne "")
        {
            if ($haserr == 0)
            {
                print "\n";
            }
            print "LOG ERROR: $l";
            $haserr++;
        }
    }
    close(PRINTLOG);
    if ($haserr == 0 && $? == 0)
    {
        print "Good\n";
    }
    else
    {
        my $logfile = getLastLogfile($$dbdirs[$i]);
        print "Log file(s) in $$dbdirs[$i] could be corrupted.\n";
        print "Please delete a log file $$logfile, and try restarting the server.\n";
    }
}

for (my $i = 0; $i < @$dbdirs; $i++)
{
    # changelog
    opendir(DB, $$dbdirs[$i]) or die "can't opendir $$dbdirs[$i] : $!";
    while (defined($db = readdir(DB)))
    {
        if ($db =~ /\.db/)
        {
            my $thisdb = $$dbdirs[$i] . "{{SEP}}" . $db;
            print "Verify $thisdb ... ";
            open(DBVERIFY, "..{{SEP}}bin{{SEP}}slapd{{SEP}}server{{SEP}}db_verify $thisdb 2>&1 1> nul |");
            sleep 1;
            my $haserr = 0;
            while ($l = <DBVERIFY>)
            {
                if ($haserr == 0)
                {
                    print "\n";
                }
                if ("$l" ne "")
                {
                    $haserr++;
                    print "DB ERROR: $l";
                }
            }
            close(DBVERIFY);
            if ($haserr == 0 && $? == 0)
            {
                print "Good\n";
            }
            else
            {
                print "changelog file $db in $$dbdirs[$i] is corrupted.\n";
                print "Please restore your backup and recover the database.\n";
            }
        }
    }
    closedir(DB);

    # backend: get instance dirs under <dbdir>
    my $instdirs = getDbDir($$dbdirs[$i]);

    for (my $j = 0; $j < @$instdirs; $j++)
    {
        opendir(DIR, $$instdirs[$j]) or die "can't opendir $here : $!";
        while (defined($db = readdir(DIR)))
        {
            if ($db =~ /\.db/)
            {
                my $thisdb = $$instdirs[$j] . "{{SEP}}" . $db;
                print "Verify $thisdb ... ";
                open(DBVERIFY, "..{{SEP}}bin{{SEP}}slapd{{SEP}}server{{SEP}}db_verify $thisdb 2>&1 1> null |");
                sleep 1;
                my $haserr = 0;
                while ($l = <DBVERIFY>)
                {
                    if ($haserr == 0)
                    {
                        print "\n";
                    }
                    if ("$l" ne "")
                    {
                        $haserr++;
                        print "DB ERROR: $l";
                    }
                }
                close(DBVERIFY);
                if ($haserr == 0 && $? == 0)
                {
                    print "Good\n";
                }
                else
                {
                    if ("$db" =~ /id2entry.db/)
                    {
                        print "Primary db file $db in $$instdirs[$j] is corrupted.\n";
                        print "Please restore your backup and recover the database.\n";
                    }
                    else
                    {
                        print "Secondary index file $db in $$instdirs[$j] is corrupted.\n";
                        print "Please run db2index(.pl) for reindexing.\n";
                    }
                }
            }
        }
        closedir(DIR);
    }
}
