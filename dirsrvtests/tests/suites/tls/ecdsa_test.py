import logging
import pytest
import os
import subprocess
from lib389.utils import ds_is_older
from lib389._constants import DN_DM, PW_DM
from lib389.topologies import topology_st as topo
from tempfile import TemporaryDirectory

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

script_content="""
#!/bin/bash
set -e  # Exit if a command fails
set -x  # Log the commands

cd {dir}
inst={instname}
url={url}
rootdn="{rootdn}"
rootpw="{rootpw}"

################################
###### GENERATE CA CERT ########
################################

echo "
[ req ]
distinguished_name = req_distinguished_name
policy             = policy_match
x509_extensions     = v3_ca

# For the CA policy
[ policy_match ]
countryName             = optional
stateOrProvinceName     = optional
organizationName        = optional
organizationalUnitName  = optional
commonName              = supplied
emailAddress            = optional

[ req_distinguished_name ]
countryName			= Country Name (2 letter code)
countryName_default		= FR
countryName_min			= 2
countryName_max			= 2

stateOrProvinceName		= State or Province Name (full name)
stateOrProvinceName_default	= test

localityName			= Locality Name (eg, city)

0.organizationName		= Organization Name (eg, company)
0.organizationName_default	= test-ECDSA-CA

organizationalUnitName		= Organizational Unit Name (eg, section)
#organizationalUnitName_default	=

commonName			= Common Name (e.g. server FQDN or YOUR name)
commonName_max			= 64


[ v3_ca ]
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid:always,issuer
basicConstraints = critical,CA:true
#nsComment = "OpenSSL Generated Certificate"
keyUsage=critical, keyCertSign
" >ca.conf


openssl ecparam -genkey -name prime256v1 -out ca.key
openssl req -x509 -new -sha256 -key ca.key -nodes -days 3650 -config ca.conf -subj "/CN=`hostname`/O=test-ECDSA-CA/C=FR"  -out ca.pem -keyout ca.key
openssl x509 -outform der -in ca.pem -out ca.crt

openssl x509 -text -in ca.pem

####################################
###### GENERATE SERVER CERT ########
####################################

echo "
[ req ]
distinguished_name = req_distinguished_name
policy             = policy_match
x509_extensions     = v3_cert

# For the cert policy
[ policy_match ]
countryName             = optional
stateOrProvinceName     = optional
organizationName        = optional
organizationalUnitName  = optional
commonName              = supplied
emailAddress            = optional

[ req_distinguished_name ]
countryName			= Country Name (2 letter code)
countryName_default		= FR
countryName_min			= 2
countryName_max			= 2

stateOrProvinceName		= State or Province Name (full name)

localityName			= Locality Name (eg, city)

0.organizationName		= Organization Name (eg, company)
0.organizationName_default	= test-ECDSA

organizationalUnitName		= Organizational Unit Name (eg, section)
#organizationalUnitName_default	=

commonName			= Common Name (e.g. server FQDN or YOUR name)
commonName_max			= 64


[ v3_cert ]
basicConstraints = critical,CA:false
subjectAltName=DNS:`hostname`
keyUsage=digitalSignature, nonRepudiation, keyEncipherment, dataEncipherment
#nsComment = "OpenSSL Generated Certificate"
extendedKeyUsage=clientAuth, serverAuth
nsCertType=client, server
" >cert.conf

openssl ecparam -genkey -name prime256v1 -out cert.key
openssl req -new -sha256 -key cert.key -nodes  -config cert.conf -subj "/CN=`hostname`/O=test-ECDSA/C=FR" -out cert.csr 
openssl x509 -req -sha256 -days 3650 -extensions v3_cert -extfile cert.conf -in cert.csr -CA ca.pem -CAkey ca.key -CAcreateserial -out cert.pem 
openssl pkcs12 -export -inkey cert.key -in cert.pem -name ecdsacert -out cert.p12 -passout pass:secret12

openssl x509 -text -in cert.pem


#############################
###### INSTALL CERTS ########
#############################

certdbdir=$PREFIX/etc/dirsrv/slapd-$inst
rm -f $certdbdir/cert9.db $certdbdir/key4.db
certutil -N -d $certdbdir -f $certdbdir/pwdfile.txt 

certutil -A -n Self-Signed-CA -t CT,, -f $certdbdir/pwdfile.txt -d $certdbdir -a -i ca.pem

dsctl $inst tls import-server-key-cert cert.pem cert.key

dsctl $inst restart


#########################
###### TEST CERT ########
#########################
LDAPTLS_CACERT=$PWD/ca.pem ldapsearch -x -H $url -D "$rootdn" -w "$rootpw" -b "" -s base
"""


def test_ecdsa(topo):
    """Specify a test case purpose or name here

    :id: 7902f37c-01d3-11ed-b65c-482ae39447e5
    :setup: Standalone Instance
    :steps:
        1. Generate the test script
        2. Run the test script
        3. Check that ldapsearch returned the namingcontext
    :expectedresults:
        1. No error
        2. No error and exit code should be 0
        3. namingcontext should be in the script output
    """

    inst=topo.standalone
    inst.enable_tls()
    with TemporaryDirectory() as dir:
        scriptname = f"{dir}/doit"
        scriptname = "/tmp/doit"
        d = {
            'dir': dir,
            'instname': inst.serverid,
            'url': f"ldaps://localhost:{inst.sslport}",
            'rootdn': DN_DM,
            'rootpw': PW_DM,
        }
        with open(scriptname, 'w') as f:
            f.write(script_content.format(**d))
        res = subprocess.run(('/bin/bash', scriptname), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, encoding='utf-8')
        assert res
        log.info(res.stdout)
        res.check_returncode()
        # If ldapsearch is successful then defaultnamingcontext should be in res.stdout
        assert "defaultnamingcontext" in res.stdout




if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
