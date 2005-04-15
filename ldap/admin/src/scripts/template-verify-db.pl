#{{PERL-EXEC}}
#
# BEGIN COPYRIGHT BLOCK
# This Program is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation; version 2 of the License.
# 
# This Program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License along with
# this Program; if not, write to the Free Software Foundation, Inc., 59 Temple
# Place, Suite 330, Boston, MA 02111-1307 USA.
# 
# In addition, as a special exception, Red Hat, Inc. gives You the additional
# right to link the code of this Program with code not covered under the GNU
# General Public License ("Non-GPL Code") and to distribute linked combinations
# including the two, subject to the limitations in this paragraph. Non-GPL Code
# permitted under this exception must only link to the code of this Program
# through those well defined interfaces identified in the file named EXCEPTION
# found in the source code files (the "Approved Interfaces"). The files of
# Non-GPL Code may instantiate templates or use macros or inline functions from
# the Approved Interfaces without causing the resulting work to be covered by
# the GNU General Public License. Only Red Hat, Inc. may make changes or
# additions to the list of Approved Interfaces. You must obey the GNU General
# Public License in all respects for all of the Program code and other code used
# in conjunction with the Program except the Non-GPL Code covered by this
# exception. If you modify this file, you may extend this exception to your
# version of the file, but you are not obligated to do so. If you do not wish to
# do so, delete this exception statement from your version. 
# 
# 
# Copyright (C) 2005 Red Hat, Inc.
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
