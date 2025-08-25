# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import sys
import pytest
import ldap
import ldapurl
import logging
import secrets
import socket
import subprocess
import textwrap
import time
from contextlib import contextmanager
from lib389.cli_base import FakeArgs
from lib389.cli_ctl.tls import import_key_cert_pair
from lib389.dseldif import DSEldif
from lib389.utils import ds_is_older, ensure_str
from lib389._constants import DN_DM, PW_DM
from lib389.topologies import topology_st as topo
from tempfile import TemporaryDirectory, NamedTemporaryFile

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

tls_enabled = False


@contextmanager
def redirect_stdio(msg):
    # Used to log stdin/stdout in pytest logs
    with NamedTemporaryFile('w+t') as f:
        oldstdout = os.dup(1)
        oldstderr = os.dup(2)
        os.dup2(f.fileno(), 1)
        os.dup2(f.fileno(), 2)
        try:
            yield f
        finally:
            os.dup2(oldstdout, 1)
            os.dup2(oldstderr, 2)
            os.close(oldstdout)
            os.close(oldstderr)
            f.seek(0)
            log.info(f'STDIO TRACE: start of {msg} traces')
            for line in f:
                log.info(f'STDIO TRACE: {line.rstrip()}')
            log.info(f'STDIO TRACE: end of {msg} traces')


@contextmanager
def traced_ldap_connection(url, msg):
    # Open ldap connection with traces
    with redirect_stdio(msg) as f:
        ldap.set_option(ldap.OPT_DEBUG_LEVEL, -1)
        ldap.set_option(ldap.OPT_PROTOCOL_VERSION, 3)
        l = ldap.initialize(url, trace_level=0, trace_file=f, trace_stack_limit=1)
        try:
            yield l
        finally:
            l.unbind()


################################
###### GENERATE CA CERT ########
################################
class ECDSA_Certificate:
    # Generate ecdsa certificate

    PEM_PASSWORD_LEN = 15

    # For the CA policy
    CONF_CA_HEADER = textwrap.dedent("""\
        [ req ]
        distinguished_name = req_distinguished_name
        policy             = policy_match
        x509_extensions     = v3_ca

        # For the CA policy""")

    CONF_CA_TRAILER = textwrap.dedent("""\
        [ v3_ca ]
        subjectKeyIdentifier = hash
        authorityKeyIdentifier = keyid:always,issuer
        basicConstraints = critical,CA:true
        #nsComment = "OpenSSL Generated Certificate"
        keyUsage=critical, keyCertSign
        """)

    # For the other certificates policy
    CONF_CERT_HEADER = textwrap.dedent("""\
        distinguished_name = req_distinguished_name
        policy             = policy_match
        x509_extensions     = v3_cert

        # For the cert policy""")

    CONF_CERT_TRAILER = textwrap.dedent("""\
        [ v3_cert ]
        basicConstraints = critical,CA:false
        subjectAltName=DNS:progier-thinkpadt14gen5.rmtfr.csb
        keyUsage=digitalSignature, nonRepudiation, keyEncipherment, dataEncipherment
        #nsComment = OpenSSL Generated Certificate
        extendedKeyUsage=clientAuth, serverAuth
        nsCertType=client, server
        """)

    # Shared by all certificates (CA included)
    CONF_COMMOM = textwrap.dedent("""\
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
        0.organizationName_default	= {organizationName}

        organizationalUnitName		= Organizational Unit Name (eg, section)
        #organizationalUnitName_default	=

        commonName			= Common Name (e.g. server FQDN or YOUR name)
        commonName_max			= 64

        """)


    VDEF = {
        "countryName": "FR",
        "stateOrProvinceName": "test",
        "organizationName": "test-ECDSA",
        "organizationalUnitName": None,
        "localityName": None,
        "commonName": socket.gethostname(),
        "maxAge": "3",
    }


    SUBJECT_ATTR = ( "commonName", "organizationName", "countryName" )
    SUBJECT_MAP = {
        "countryName": "C",
        "commonName": "CN",
        "organizationName": "O",
    }


    def __init__(self, prefix, outdir, **kwargs):
        self.dir = outdir
        self.prefix = prefix
        self.args = { **ECDSA_Certificate.VDEF, **kwargs }
        if not 'organizationName' in kwargs:
            self.args['organizationName'] = f'{self.args["organizationName"]}-{prefix}'
        self.args['subject'] = self.get_subject()
        for name in ( 'conf', 'csr', 'key', 'p12', 'pem', 'pw1', 'pw2'):
            setattr(self, name, f'{outdir}/{prefix}.{name}')
        for path in ( self.pw1, self.pw2 ):
            with open(path, 'wt') as fd:
                fd.write(secrets.token_urlsafe(ECDSA_Certificate.PEM_PASSWORD_LEN))
                fd.write('\n')


    def get_subject(self, **kwargs):
        args = { **self.args, **kwargs }
        subject = ""
        for arg in ECDSA_Certificate.SUBJECT_ATTR:
            if arg in args and arg in ECDSA_Certificate.SUBJECT_MAP:
                subject += f"/{ECDSA_Certificate.SUBJECT_MAP[arg]}={args[arg]}"
        # return f"/CN={args['commonName']}/DC=example/DC=com"
        return subject

    def generate_conf(self, isCA):
        if isCA:
            confl = ( ECDSA_Certificate.CONF_CA_HEADER,
                      ECDSA_Certificate.CONF_COMMOM,
                      ECDSA_Certificate.CONF_CA_TRAILER )
        else:
            confl = ( ECDSA_Certificate.CONF_CERT_HEADER,
                      ECDSA_Certificate.CONF_COMMOM,
                      ECDSA_Certificate.CONF_CERT_TRAILER )
        conf_path = f'{self.dir}/{self.prefix}.conf'
        conf_data = "\n".join(confl).format(dir=self.dir, prefix=self.prefix, **self.args)
        with open(conf_path, "wt") as fd:
            for line in conf_data.split('\n'):
                sep = "#" if " = None" in line else ""
                fd.write(f'{sep}{line}\n')

    def run(self, cmd):
        log.info(f'Running: {" ".join(cmd)}')
        res = subprocess.run(cmd, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, encoding='utf-8')
        assert res
        log.info(f'Stdout+Stderr:{res.stdout}')
        res.check_returncode()

    def generate_CA(self):
        self.generate_conf(True)
        self.run(("openssl", "ecparam", "-genkey", "-name", "secp256r1", "-out", self.key))
        self.run(("openssl", "req", "-x509", "-new", "-sha256", "-key", self.key, "-days", self.args['maxAge'], "-config", self.conf, "-subj", self.get_subject(), "-out", self.pem, "-keyout", self.key, "-passout", f"file:{self.pw1}"))
        self.run(("openssl", "x509", "-text", "-in", self.pem))
        self.run(("openssl", "pkcs12", "-export", "-inkey", self.key, "-in", self.pem, "-name", f"ecdsaw-{self.prefix}", "-out", self.p12, "-passin", f"file:{self.pw1}", "-passout", f"file:{self.pw2}"))

    def generate_cert(self, ca):
        self.generate_conf(False)
        self.run(("openssl", "ecparam", "-genkey", "-name", "secp256r1", "-out", self.key))
        self.run(("openssl", "req", "-new", "-sha256", "-key", self.key, "-config", self.conf, "-subj", self.get_subject(), "-out", self.csr))
        self.run(("openssl", "x509", "-req", "-sha256", "-days", self.args['maxAge'], "-extensions", "v3_cert", "-extfile", self.conf, "-in", self.csr, "-CA", ca.pem, "-CAkey", ca.key, "-CAcreateserial", "-out", self.pem,  "-passin", f"file:{ca.pw1}"))
        self.run(("openssl", "x509", "-text", "-in", self.pem))
        self.run(("openssl", "pkcs12", "-export", "-inkey", self.key, "-in", self.pem, "-name", f"ecdsaw-{self.prefix}", "-out", self.p12, "-passin", f"file:{self.pw1}", "-passout", f"file:{self.pw2}"))

    def __repr__(self):
        return self.prefix


#############################
###### INSTALL CERTS ########
#############################
def install_certs(ca, cert, inst):
    prefix = os.environ.get('PREFIX', "/")
    certdbdir = f"{prefix}/etc/dirsrv/slapd-{inst.serverid}"
    pwfile = f'{certdbdir}/pwdfile.txt'
    for f in ('cert9.db', 'key4.db'):
        try:
            os.remove(f'{certdbdir}/{f}')
        except OSError:
            pass

    ca.run(('certutil', '-N', '-d', certdbdir, '-f', pwfile))
    ca.run(('certutil', '-A', '-n', 'Self-Signed-CA', '-t', 'CT,,', '-f', pwfile, '-d', certdbdir, '-a', '-i', ca.pem))
    args = FakeArgs()
    for k,v in { 'key_path': cert.key, 'cert_path': cert.pem }.items():
        setattr(args, k, v)
    import_key_cert_pair(inst, log, args)


#########################################################
###### Work around to debug libldap/liblber CERT ########
#########################################################

def open_ldap(url, logfile):
    if logfile is not None:
        lutil_debug_file(logfile)
        ldap.set_option(ldap.OPT_DEBUG_LEVEL, -1)
        loglv = 3
    else:
        lutil_debug_file(logfile)
        ldap.set_option(ldap.OPT_DEBUG_LEVEL, 0)
        loglv = 0
        logfile = sys.stderr
    ldap.set_option(ldap.OPT_PROTOCOL_VERSION, 3)
    return ldap.initialize(url, trace_level=loglv, trace_file=logfile)


#########################
###### TEST CERT ########
#########################

def open_ldaps_conn(inst, ca):
    url = inst.toLDAPURL()
    log.info(f'Attempt to connect to {url} using {ca.pem}')
    ld = ldap.initialize(url)
    #ld.set_option(ldap.OPT_X_TLS_REQUIRE_CERT,ldap.OPT_X_TLS_NEVER)
    ld.set_option(ldap.OPT_X_TLS_REQUIRE_CERT,ldap.OPT_X_TLS_DEMAND)
    ld.set_option(ldap.OPT_X_TLS_CACERTFILE, f'{ca.pem}')
    ld.set_option(ldap.OPT_X_TLS_NEWCTX, 0)
    return ld


def open_ldapi_conn(inst, logfile=None):
    dse = DSEldif(inst)
    ldapi_socket = dse.get('cn=config', 'nsslapd-ldapifilepath', single=True)
    url = "ldapi://%s" % (ldapurl.ldapUrlEscape(ensure_str(ldapi_socket)))
    ld = ldap.initialize(url)
    # Perform autobind
    sasl_auth = ldap.sasl.external()
    ld.sasl_interactive_bind_s("", sasl_auth)
    return ld


def tls_search(capsys, inst, ca):
    with traced_ldap_connection(inst.toLDAPURL(), f'ldaps bind using CA {ca}') as ld:
        #ld.set_option(ldap.OPT_X_TLS_REQUIRE_CERT,ldap.OPT_X_TLS_NEVER)
        ld.set_option(ldap.OPT_X_TLS_REQUIRE_CERT,ldap.OPT_X_TLS_DEMAND)
        ld.set_option(ldap.OPT_X_TLS_CACERTFILE, f'{ca.pem}')
        ld.set_option(ldap.OPT_X_TLS_NEWCTX, 0)
        ld.simple_bind_s(DN_DM, PW_DM)


#
#  Ideally There should be a dsctl/dsconf subcommand
#
def refresh_certs(inst, timeout=60):
    ld = open_ldapi_conn(inst)
    ld.modify_s('cn=config', [(ldap.MOD_REPLACE, 'nsslapd-refresh-certificates', b'on')])
    # Now lets wait until the config value is off again
    for i in range(timeout):
        result = ld.search_s('cn=config', ldap.SCOPE_BASE, attrlist=['nsslapd-refresh-certificates',])
        log.info(f'cn=config result: {result}')
        attrs = result[0][1]
        vals = attrs['nsslapd-refresh-certificates']
        if vals[0].decode("utf-8").lower() == "off":
            ld.unbind()
            return
        time.sleep(1)
    raise TimeoutError(f"Certificate not changed after {timeout} secopnds")


def test_ecdsa(topo):
    """Specify a test case purpose or name here

    :id: 7902f37c-01d3-11ed-b65c-482ae39447e5
    :setup: Standalone Instance
    :steps:
        1. Generate ECDSA CA and User Cert
        2. Install the certificates
        3. Restart the server
        4. Open ldaps connection with server CA certificate and search root entry
    :expectedresults:
        1. No error
        2. No error
        3. No error
        4. No error
    """

    inst=topo.standalone
    global tls_enabled
    if not tls_enabled:
        inst.enable_tls()
        tls_enabled = True
    with TemporaryDirectory() as dir:
        dir="/home/progier/sb/i1108/389-ds-base/dirsrvtests/tests/suites/tls/tmp"
        ca = ECDSA_Certificate("CA", dir)
        ca.generate_CA()
        cert = ECDSA_Certificate("Cert", dir)
        cert.generate_cert(ca)
        install_certs(ca, cert, inst)
        inst.restart(post_open=False)
        tls_search(inst, ca)


def test_refresh_ecdsa(topo, capsys):
    """Test dynamic refresh of certificate

    :id: 96039bce-5370-11f0-9de5-c85309d5c3e3
    :setup: Standalone Instance
    :steps:
        1. Generate ECDSA CA and User Cert
        2. Generate a second ECDSA CA and User Cert pair
        3. Install the first set of certificates
        4. Restart the server
        5. Open ldaps connection with server CA certificate and search root entry
        6. Open a second ldaps connection with server CA certificate and keep it open
        7. Open a third ldaps connection with server CA2 certificate and keep it open
        8. Install the second set of certificates
        9. Set the certificate refresh attribute to true (using ldapi)
        10. Wait a bit until certificates get replaced
        11. Open ldaps connection with new server CA certificate and search root entry
        12. Perform a search on the second open connection
        13. Perform a search on the third open connection
    :expectedresults:
        1. No error
        2. No error
        3. No error
        4. No error
        5. No error
        6. No error
        7. No error
        8. No error
        9. No error
        10. No error
        11. No error
        12. ldap.SERVER_DOWN because the old CA does not match the server one
        13. No error
    """

    inst=topo.standalone
    global tls_enabled
    if not tls_enabled:
        inst.enable_tls()
        tls_enabled = True
    with TemporaryDirectory() as dir:
        ca = ECDSA_Certificate("CA", dir)
        ca.generate_CA()
        cert = ECDSA_Certificate("Cert", dir)
        cert.generate_cert(ca)
        ca2 = ECDSA_Certificate("CA2", dir)
        ca2.generate_CA()
        cert2 = ECDSA_Certificate("Cert2", dir)
        cert2.generate_cert(ca2)

        install_certs(ca, cert, inst)
        inst.restart(post_open=False)
        tls_search(capsys, inst, ca)
        ld = open_ldaps_conn(inst, ca)
        ld2 = open_ldaps_conn(inst, ca2)

        install_certs(ca2, cert2, inst)
        refresh_certs(inst)
        time.sleep(1)

        # if we restart the next tls_search is OK and ld.search_s fails as expected
        # if we dont the tls_search fails ==> something is down
        tls_search(capsys, inst, ca2)
        # When trying to use an already open connection with the old CA.
        # the server renegotiate the SSL after server certificate change
        # So the ldap operation fails
        with pytest.raises(ldap.SERVER_DOWN):
            with redirect_stdio("Search using already open connection with CA 'CA'"):
                # Although connection is open, the certificate change triggers a renegotiation
                # That must be done with the new certificate
                results = ld.search_s('', ldap.SCOPE_BASE)
                assert len(results) == 1
                ld.unbind()

        with redirect_stdio("Search using already open connection with CA 'CA2'"):
            # Although connection is open, the certificate change triggers a renegotiation
            # That must be done with the new certificate
            results = ld2.search_s('', ldap.SCOPE_BASE)
            assert len(results) == 1
            ld2.unbind()

if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
