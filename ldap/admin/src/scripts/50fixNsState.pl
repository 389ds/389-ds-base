use Mozilla::LDAP::Conn;
use Mozilla::LDAP::Entry;
use Mozilla::LDAP::Utils qw(normalizeDN);
use Mozilla::LDAP::API qw(:constant ldap_url_parse ldap_explode_dn);
use DSUtil qw(debug);
use Config;

# # Determine the endianness of your system
my $packfmt32 = "QA6SCx3"; # must be 20 bytes
my $packfmt64 = "QA6SCx7"; # must be 24 bytes

my $is_big_endian = unpack('xc', pack('s', 1));
# see if we are on an LP64 system
my $is64 = ($Config{longsize} == 8);

sub convert_uniqueid {
    my $ent = shift;
    my $val = shift;

    if (!$ent || !$val) {
        return (0, 0);
    }

    my $hex = unpack('H*', $val);
    #print "hex=$hex\n";

    my $fmt32 = "QA6SC";
    my $fmt64 = "QA6SC";
    my $fmt = $fmt32;
    if (length($val) > 20) {
        $fmt = $fmt64;
    } elsif ($is64) {
        # cannot convert 32-bit to 64-bit - just delete the entry and continue
        debug(1, "Cannot convert 32-bit nsState value $hex to 64-bit - deleting entry " .
              $ent->getDN() . " and continuing\n");
        return (-1, 0);
    }
    if ($is_big_endian) {
        $packfmt32 = "(QA6SCx3)>";
        $packfmt64 = "(QA6SCx7)>";
    }

    my $packfmt = $packfmt32;
    if ($is64) {
        $packfmt = $packfmt64;
    }
    
    my ($ts, $node, $clockseq, $last_update) = unpack($fmt, $val);
    # if we think it is from bigendian, do 
    # $bigfmt = "(" . $fmt . ")>";
    my $tssecs = ($ts - 0x01B21DD213814000) / 10000000;
    my $curts = time;
    my $tsdiff = abs($curts - $tssecs);
    my $maxdiff = 86400*365*10; # 10 years
    if (($tsdiff > $maxdiff) || (($last_update != 0) && ($last_update != 1))) {
        # try big endian
        ($ts, $node, $clockseq, $last_update) = unpack("($fmt)>", $val);
        $tssecs = ($ts - 0x01B21DD213814000) / 10000000;
        $tsdiff = abs($curts - $tssecs);
        if (($tsdiff > $maxdiff) || (($last_update != 0) && ($last_update != 1))) {
            debug(0, "Error: could not parse nsstate $hex - tsdiff is $tsdiff seconds or ", ($tsdiff/86400), " days\n");
            return (0, 0, 'error_could_not_parse_nsstate', $ent->getDN(), $hex);
        }
    }

    # format for the target system
    my $newval = pack($packfmt, $ts, $node, $clockseq, $last_update);
    my $rc = 0;
    if ($val != $newval) { # changed
        my $hex2 = unpack('H*', $newval);
        debug(1, "Converted old nsState val in ", $ent->getDN(), " from $hex to $hex2\n");
        $rc = 1; # changed
    }
    return ($rc, $newval);
}

sub convert_replica {
    my $ent = shift;
    my $val = shift;

    if (!$ent || !$val) {
        return (0, 0);
    }

    my $len = length($val);
    my $pad;
    my $timefmt;
    if ($len <= 20) {
        $pad = 2; # padding for short H values
        $timefmt = 'I'; # timevals are unsigned 32-bit int
    } else {
        $pad = 6; # padding for short H values
        $timefmt = 'Q'; # timevals are unsigned 64-bit int
    }
    # short - padbytes - 3 timevals - short - padbytes
    my $fmtstr = "Sx" . $pad . $timefmt . "3Sx" . $pad;
    my ($rid, $sampled_time, $local_offset, $remote_offset, $seq_num) = unpack($fmtstr, $val);
    my $hex = unpack('H*', $val);
    my $now = time;
    my $tdiff = abs($now - $sampled_time);
    my $maxdiff = 86400*365*10; # 10 years
    if ($tdiff > $maxdiff) { # try big endian
        ($rid, $sampled_time, $local_offset, $remote_offset, $seq_num) = unpack("($fmtstr)>", $val);
        my $tdiff = abs($now - $sampled_time);
        if ($tdiff > $maxdiff) { # error
            debug(0, "Error: could not parse nsstate $hex - tdiff is $tdiff seconds or", ($tdiff/86400), " days\n");
            return (0, 0, 'error_could_not_parse_nsstate', $ent->getDN(), $hex);
        }
    }
    # format for the target system
    if ($is_big_endian) {
        $fmtstr = "($fmtstr)>";
    }
    my $newval = pack($fmtstr, $rid, $sampled_time, $local_offset, $remote_offset, $seq_num);
    my $rc = 0;
    if ($val != $newval) { # changed
        my $hex2 = unpack('H*', $newval);
        debug(1, "Converted old nsState val in ", $ent->getDN(), " from $hex to $hex2\n");
        $rc = 1; # changed
    }
    return ($rc, $newval);
}

sub runinst {
    my ($inf, $inst, $dseldif, $conn) = @_;

    my $ent = $conn->search("cn=config", "sub", "(cn=uniqueid generator)");
    if ($ent) {
        my ($rc, $newval, @errs) = convert_uniqueid($ent, $ent->getValues('nsState'));
        if (@errs) {
            return @errs;
        }
        if ($rc) { # changed
            if ($rc == -1) { # delete it
                if (!$conn->delete($ent->getDN())) {
                    return ("error_deleteall_entries", $ent->getDN(), $conn->getErrorString());
                }
            } else {
                $ent->setValues('nsState', $newval);
                if (!$conn->update($ent)) {
                    return ("error_updating_entry", $ent->getDN(), $conn->getErrorString());
                }
            }
        }
    }

    for ($ent = $conn->search("cn=config", "sub", "(cn=replica)");
        $ent; $ent = $conn->nextEntry) {
        my ($rc, $newval, @errs) = convert_replica($ent, $ent->getValues('nsState'));
        if (@errs) {
            return @errs;
        }
        if ($rc) { # changed
            $ent->setValues('nsState', $newval);
            if (!$conn->update($ent)) {
                return ("error_updating_entry", $ent->getDN(), $conn->getErrorString());
            }
        }
    }

    return ();
}

sub testit {
#my $val = 'ACm2BdIdsgH+tw/8AAB+swEAAAA=';
#my $val = 'AOj+tyuA4AHsNZ7S9NnxZwEAAAAAAAAA';
my $testval = "00a43cb4d11db2018b7912fd0000a42e01000000";
my $testdecval = $testval;
# base16 decode
$testdecval =~ s/(..)/chr(hex($1))/eg;
my $ent = new Mozilla::LDAP::Entry;
$ent->setDN("cn=uniqueid generator");
my ($rc, $newval) = convert_uniqueid($ent, $testdecval);
}

1;
