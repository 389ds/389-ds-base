use Mozilla::LDAP::Conn;
use Mozilla::LDAP::Entry;
use Mozilla::LDAP::Utils qw(normalizeDN);
use Mozilla::LDAP::API qw(:constant ldap_url_parse ldap_explode_dn);
use DSUtil qw(debug);
use Config;
use bigint;

# # Determine the endianness of your system
my $packfmt32 = "VVA6vCx3"; # must be 20 bytes
my $packfmt64 = "VVA6vCx7"; # must be 24 bytes

my $is_big_endian = unpack('xc', pack('s', 1));
# see if we are on an LP64 system
my $is64 = ($Config{longsize} == 8);

sub convert_to_32bit {
    my $val64 = shift;
    return ($val64 >> 32, $val64 & 0xffffffff);
}

sub convert_from_32bit {
    my ($hi, $lo) = @_;
    return ($hi << 32) + $lo;
}

sub convert_uniqueid {
    my $ent = shift;
    my $val = shift;

    if (!$ent || !$val) {
        return (0, 0);
    }

    my $hex = unpack('H*', $val);
    #print "hex=$hex\n";

    my $fmt32 = "VVA6vC";
    my $bigfmt32 = "NNA6nC";
    my $fmt64 = "VVA6vC";
    my $bigfmt64 = "NNA6nC";
    my $fmt = $fmt32;
    my $bigfmt = $bigfmt32;
    if (length($val) > 20) {
        $fmt = $fmt64;
        $bigfmt = $bigfmt64;
    } elsif ($is64) {
        # cannot convert 32-bit to 64-bit - just delete the entry and continue
        debug(1, "Cannot convert 32-bit nsState value $hex to 64-bit - deleting entry " .
              $ent->getDN() . " and continuing\n");
        return (-1, 0);
    } else { # 32-bit to 32-bit - just leave it alone
        debug(1, "Skipping 32-bit nsState value $hex in entry " .
              $ent->getDN() . " and continuing\n");
        return (0, 0);
    }
    if ($is_big_endian) {
        $packfmt32 = "NNA6nCx3";
        $packfmt64 = "NNA6nCx7";
    }

    my $packfmt = $packfmt32;
    if ($is64) {
        $packfmt = $packfmt64;
    }
    
    my ($tslow, $tshigh, $node, $clockseq, $last_update) = unpack($fmt, $val);
    my $ts = convert_from_32bit($tshigh, $tslow);
    my $tssecs = ($ts - 0x01B21DD213814000) / 10000000;
    my $curts = time;
    my $tsdiff = abs($curts - $tssecs);
    my $maxdiff = 86400*365*10; # 10 years
    if (($tsdiff > $maxdiff) || (($last_update != 0) && ($last_update != 1))) {
        # try big endian
        ($tshigh, $tslow, $node, $clockseq, $last_update) = unpack($bigfmt, $val);
        $ts = convert_from_32bit($tshigh, $tslow);
        $tssecs = ($ts - 0x01B21DD213814000) / 10000000;
        $tsdiff = abs($curts - $tssecs);
        if (($tsdiff > $maxdiff) || (($last_update != 0) && ($last_update != 1))) {
            debug(0, "Error: could not parse nsstate $hex - tsdiff is $tsdiff seconds or ", ($tsdiff/86400), " days\n");
            return (0, 0, 'error_could_not_parse_nsstate', $ent->getDN(), $hex);
        }
    }

    # format for the target system
    ($tshigh, $tslow) = convert_to_32bit($ts);
    my $newval = pack($packfmt, $tslow, $tshigh, $node, $clockseq, $last_update);
    my $rc = 0;
    if ($val ne $newval) { # changed
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
    my ($rid, $sampled_time, $local_offset, $remote_offset, $seq_num);
    my ($st_high, $st_low, $lo_high, $lo_low, $ro_high, $ro_low);
    my $fmtstr;
    my $bigfmtstr;
    if ($len <= 20) {
        $pad = 2; # padding for short H values
        $timefmt = 'V'; # timevals are unsigned 32-bit int - try little-endian 'V' first
        $fmtstr = "vx" . $pad . $timefmt . "3vx" . $pad;
        $bigfmtstr = 'nx' . $pad . 'N' . '3nx' . $pad;
        ($rid, $sampled_time, $local_offset, $remote_offset, $seq_num) = unpack($fmtstr, $val);
    } else {
        $pad = 6; # padding for short H values
        $timefmt = 'V'; # timevals are unsigned 64-bit int
        $fmtstr = "vx" . $pad . $timefmt . "6vx" . $pad;
        $bigfmtstr = 'nx' . $pad . 'N' . '6nx' . $pad;
        ($rid, $st_low, $st_high, $lo_low, $lo_high, $ro_low, $ro_high, $seq_num) = unpack($fmtstr, $val);
        $sampled_time = convert_from_32bit($st_high, $st_low);
        $local_offset = convert_from_32bit($lo_high, $lo_low);
        $remote_offset = convert_from_32bit($ro_high, $ro_low);
    }
    # short - padbytes - 3 timevals - short - padbytes
    my $hex = unpack('H*', $val);
    my $now = time;
    my $tdiff = abs($now - $sampled_time);
    my $maxdiff = 86400*365*10; # 10 years
    if ($tdiff > $maxdiff) { # try big endian
        if ($len <= 20) {
            ($rid, $sampled_time, $local_offset, $remote_offset, $seq_num) = unpack($bigfmtstr, $val);
        } else {
            ($rid, $st_high, $st_low, $lo_high, $lo_low, $ro_high, $ro_low, $seq_num) = unpack($bigfmtstr, $val);
            $sampled_time = convert_from_32bit($st_high, $st_low);
            $local_offset = convert_from_32bit($lo_high, $lo_low);
            $remote_offset = convert_from_32bit($ro_high, $ro_low);
        }
        my $tdiff = abs($now - $sampled_time);
        if ($tdiff > $maxdiff) { # error
            debug(0, "Error: could not parse nsstate $hex - tdiff is $tdiff seconds or", ($tdiff/86400), " days\n");
            return (0, 0, 'error_could_not_parse_nsstate', $ent->getDN(), $hex);
        }
    }
    # format for the target system
    my $packfmt;
    my @packargs;
    if ($is64) {
        my $packfmt = "vx" . $pad . "V6vx" . $pad;
        if ($is_big_endian) {
            $packfmt = "nx" . $pad . "N6nx" . $pad;
        }
        $st_high = $st >> 32;
        ($st_high, $st_low) = convert_to_32bit($sampled_time);
        ($lo_high, $lo_low) = convert_to_32bit($local_offset);
        ($ro_high, $ro_low) = convert_to_32bit($remote_offset);
        @packargs = ($rid, $st_low, $st_high, $lo_low, $lo_high, $ro_low, $ro_high, $seq_num);
    } else {
        my $packfmt = "vx" . $pad . "V3vx" . $pad;
        if ($is_big_endian) {
            $packfmt = "nx" . $pad . "N3nx" . $pad;
        }
        @packargs = ($rid, $sampled_time, $local_offset, $remote_offset, $seq_num);
    }
    my $newval = pack($fmtstr, @packargs);
    my $rc = 0;
    if ($val ne $newval) { # changed
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
#my $val = 'ABI3gdIdsgH3TJWpAACGIgEAAAA=';
#my $testval = "00a43cb4d11db2018b7912fd0000a42e01000000";
#my $testval = "0029B605D21DB201FEB70FFC00007EB301000000";
#my $testval = "00E8FEB72B80E001EC359ED2F4D9F1670100000000000000";
#my $testval = "00123781D21DB201F74C95A90000862201000000";
my $testval = '01E0D2DA53198600A12C2D6BADF15D630100000000000000';
my $testreplval = "\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00N\\\x8b5\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\x00\x01\x00\x00\x00\x00\x00\x00";
my $testdecval = $testval;
# base16 decode
$testdecval =~ s/(..)/chr(hex($1))/eg;
my $ent = new Mozilla::LDAP::Entry;
$ent->setDN("cn=uniqueid generator");
my ($rc, $newval) = convert_uniqueid($ent, $testdecval);
$ent->setDN('cn=replica');
my ($rc, $newval2) = convert_replica($ent, $testreplval);
}

testit() unless caller();

1;
