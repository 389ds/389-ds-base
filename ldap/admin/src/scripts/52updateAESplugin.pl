use Mozilla::LDAP::Conn;
use Mozilla::LDAP::Entry;
use Mozilla::LDAP::Utils qw(normalizeDN);
use Mozilla::LDAP::API qw(:constant ldap_url_parse ldap_explode_dn);
use File::Basename;
use File::Copy;
use DSUtil qw(debug serverIsRunning);

# no warnings 'experimental::smartmatch';
no if $] >= 5.017011, warnings => 'experimental::smartmatch';

#
# Check if there is a DES plugin and make sure the AES plugin contains the same attributes
#
sub runinst {
    my ($inf, $inst, $dseldif, $conn) = @_;
    my @attrs;
    my @attrs_to_add;
    my $aes_count = 0;
    my $des_count = 0;
    my $i = 0;

    my $aes_dn = "cn=AES,cn=Password Storage Schemes,cn=plugins,cn=config";
    my $aes_entry = $conn->search($aes_dn, "base", "(cn=*)");
    if (!$aes_entry) {
        # No AES plugin - nothing to do
        return ();
    }

    # We need to grab the AES plugin args...
    while(1){
        my $argattr = "nsslapd-pluginarg" . $i;
        my $val = $aes_entry->getValues($argattr);
        if($val ne ""){
            $attrs[$aes_count] = $val;
            $aes_count++;
        } else {
            last;
        }
        $i++;
    }

    # Grab the DES plugin
    my $des_dn = "cn=DES,cn=Password Storage Schemes,cn=plugins,cn=config";
    my $des_entry = $conn->search($des_dn, "base", "(cn=*)");
    if (!$des_entry) {
        # No DES plugin - nothing to do
        return ();
    }

    # We need to check the DES plugin args against the AES args.
    $i = 0;
    while(1){
        my $argattr = "nsslapd-pluginarg" . $i;
        my $val = $des_entry->getValues($argattr);
        if($val eq ""){
            last;
        }
        if(!($val ~~ @attrs) ){  # smartmatch
            $attrs_to_add[$des_count] = $val;
            $des_count++;
        }
        $i++;
    }

    # Add the missing attributes to the AES plugin
    if($#attrs_to_add >= 0){
        foreach $val (@attrs_to_add){
            $aes_entry->addValue("nsslapd-pluginarg" . $aes_count, $val);
	    $aes_count++;
        }
        $conn->update($aes_entry);
    }

    # Change replication plugin dependency from DES to AES
    my $mmr_entry = $conn->search("cn=Multimaster Replication Plugin,cn=plugins,cn=config", "base", "(cn=*)");
    $mmr_entry->removeValue("nsslapd-plugin-depends-on-named", "DES");
    $mmr_entry->addValue("nsslapd-plugin-depends-on-named", "AES");
    $conn->update($mmr_entry);

    # Change the des plugin to use the new libpbe-plugin library
    $des_entry->{"nsslapd-pluginPath"} = [ "libpbe-plugin" ];
    $conn->update($des_entry);

    return ();
}

