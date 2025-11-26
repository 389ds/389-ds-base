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
<<<<<<< HEAD
<<<<<<< HEAD
import copy
import datetime
import ipaddress
import ldap
import ldapurl
import logging
import pytest
import secrets
import shutil
=======
import pytest
=======
import datetime
import ipaddress
>>>>>>> 1236487850 (Phase 3 - suite)
import ldap
import ldapurl
import logging
import pytest
import secrets
<<<<<<< HEAD
>>>>>>> 87c7465ad8 (Dynamic Certificate - phase 3 - switching tls cert)
=======
import shutil
>>>>>>> 1236487850 (Phase 3 - suite)
import socket
import subprocess
import textwrap
import time
<<<<<<< HEAD
<<<<<<< HEAD
=======
>>>>>>> 1236487850 (Phase 3 - suite)
from contextlib import contextmanager, suppress
from cryptography.hazmat.backends import default_backend
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.serialization import pkcs12
from cryptography import x509
from cryptography.x509.oid import NameOID, ExtensionOID
<<<<<<< HEAD
from lib389.cli_base import FakeArgs
=======
from contextlib import contextmanager
from lib389.cli_base import FakeArgs
from lib389.cli_ctl.tls import import_key_cert_pair
from lib389.dseldif import DSEldif
from lib389.utils import ds_is_older, ensure_str
>>>>>>> 87c7465ad8 (Dynamic Certificate - phase 3 - switching tls cert)
from lib389._constants import DN_DM, PW_DM
from lib389.dseldif import DSEldif
from lib389.topologies import topology_st as topo
<<<<<<< HEAD
from lib389.utils import ds_is_older, ensure_str
from tempfile import NamedTemporaryFile
=======
from tempfile import TemporaryDirectory, NamedTemporaryFile
>>>>>>> 87c7465ad8 (Dynamic Certificate - phase 3 - switching tls cert)
=======
from lib389.cli_base import FakeArgs
from lib389._constants import DN_DM, PW_DM
from lib389.dseldif import DSEldif
from lib389.topologies import topology_st as topo
from lib389.utils import ds_is_older, ensure_str
from tempfile import NamedTemporaryFile
>>>>>>> 1236487850 (Phase 3 - suite)

pytestmark = pytest.mark.tier1

DYNCERT_SUFFIX = "cn=dynamiccertificates"
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
<<<<<<< HEAD

@contextmanager
def ldapi(inst):
    l = open_ldapi_conn(inst)
    try:
        yield l
    finally:
        l.unbind()
=======
>>>>>>> 87c7465ad8 (Dynamic Certificate - phase 3 - switching tls cert)

@contextmanager
def ldapi(inst):
    l = open_ldapi_conn(inst)
    try:
        yield l
    finally:
        l.unbind()


################################
###### GENERATE CA CERT ########
################################
class ECDSA_Certificate:
<<<<<<< HEAD
<<<<<<< HEAD
    """Elliptic Curve Certificate Generator"""

    PKCS12_PASSWORD = "a+password"

    def __init__(self):
        self.nickname = None
        self.trust = None
        self.namingAttrs = {}
        self.subject = None
        self.validity_days = 365
        self.isCA = False
        self.isRoot = False
        self.caChain = None

    @staticmethod
    def generateRootCA(nickname, namingAttributes={}, validity_days=3650):
        """Generate a self-signed Root CA certificate using elliptic curve"""
        ca = ECDSA_Certificate()
        ca.namingAttrs = namingAttributes
        ca.validity_days = validity_days
        ca.isCA = True
        ca.isRoot = True
        ca.nickname = nickname
        ca.fixNamingAttributes()
        ca.subject = x509.Name([x509.NameAttribute(k,v) for k,v in ca.namingAttrs.items()])
        ca.issuer = ca.subject
        ca.trust = 'CT,,'

        # Generate prime256v1 private key
        ca.pkey = ec.generate_private_key( ec.SECP256R1(), default_backend())
        ca.cert = (
            x509.CertificateBuilder()
            .subject_name(ca.subject)
            .issuer_name(ca.issuer)
            .public_key(ca.pkey.public_key())
            .serial_number(x509.random_serial_number())
            .not_valid_before(datetime.datetime.now(datetime.UTC))
            .not_valid_after(datetime.datetime.now(datetime.UTC) + datetime.timedelta(days=validity_days))
            # CA certificate extensions
            .add_extension(
                x509.BasicConstraints(ca=True, path_length=0),
                critical=True,
            )
            .add_extension(
                x509.KeyUsage(
                    digital_signature=False,
                    key_cert_sign=True,
                    crl_sign=False,
                    key_encipherment=False,
                    content_commitment=False,
                    data_encipherment=False,
                    key_agreement=False,
                    encipher_only=False,
                    decipher_only=False,
                ),
                critical=True,
            )
            .add_extension(
                x509.SubjectKeyIdentifier.from_public_key(ca.pkey.public_key()),
                critical=False,
            )
            .sign(ca.pkey, hashes.SHA256(), default_backend())
        )
        return ca

    @staticmethod
    def save_pem_file(filename, *items):
        """Save PEM formatted items to file"""
        with open(filename, "wb") as f:
            for item in items:
                if isinstance(item, ec.EllipticCurvePrivateKey):
                    pem = item.private_bytes(
                        encoding=serialization.Encoding.PEM,
                        format=serialization.PrivateFormat.TraditionalOpenSSL,
                        encryption_algorithm=serialization.NoEncryption()
                    )
                elif isinstance(item, x509.Certificate):
                    pem = item.public_bytes(serialization.Encoding.PEM)
                else:
                    raise ValueError(f"Unknown item type: {type(item)}")
                f.write(pem)

    @staticmethod
    def save_der_file(filename, item):
        """Save DER formatted item to file"""
        with open(filename, "wb") as f:
            if isinstance(item, ec.EllipticCurvePrivateKey):
                der = item.private_bytes(
                    encoding=serialization.Encoding.DER,
                    format=serialization.PrivateFormat.PKCS8, #format=serialization.PrivateFormat.TraditionalOpenSSL,
                    encryption_algorithm=serialization.NoEncryption()
                )
            elif isinstance(item, x509.Certificate):
                der = item.public_bytes(serialization.Encoding.DER)
            else:
                raise ValueError(f"Unknown item type: {type(item)}")
            f.write(der)

    def fixNamingAttribute(self, name, vdef):
        """Set value for naming attribute if it is missing"""
        if name not in self.namingAttrs:
            self.namingAttrs[name] = vdef

    def fixNamingAttributes(self):
        """Set default value for mandatory naming attributes"""
        self.fixNamingAttribute(NameOID.COMMON_NAME, self.nickname)
        self.fixNamingAttribute(NameOID.COUNTRY_NAME, 'US')
        self.fixNamingAttribute(NameOID.ORGANIZATION_NAME, 'Example Organization')

    def generateCA(self, nickname, namingAttributes={}, validity_days=3650):
        """Generate an intermediary CA certificate using elliptic curve"""
        ca = ECDSA_Certificate()
        ca.namingAttrs = namingAttributes
        ca.validity_days = validity_days
        ca.isCA = True
        ca.isRoot = False
        ca.nickname = nickname
        ca.fixNamingAttributes()
        ca.subject = x509.Name([x509.NameAttribute(k,v) for k,v in ca.namingAttrs.items()])
        ca.issuer = self.subject
        ca.trust = 'CT,,'

        # Generate prime256v1 private key
        ca.pkey = ec.generate_private_key( ec.SECP256R1(), default_backend())
        ca.cert = (
            x509.CertificateBuilder()
            .subject_name(ca.subject)
            .issuer_name(ca.issuer)
            .public_key(ca.pkey.public_key())
            .serial_number(x509.random_serial_number())
            .not_valid_before(datetime.datetime.now(datetime.UTC))
            .not_valid_after(datetime.datetime.now(datetime.UTC) + datetime.timedelta(days=validity_days))
            # CA certificate extensions
            .add_extension(
                x509.BasicConstraints(ca=True, path_length=0),
                critical=True,
            )
            .add_extension(
                x509.KeyUsage(
                    digital_signature=False,
                    key_cert_sign=True,
                    crl_sign=False,
                    key_encipherment=False,
                    content_commitment=False,
                    data_encipherment=False,
                    key_agreement=False,
                    encipher_only=False,
                    decipher_only=False,
                ),
                critical=True,
            )
            .add_extension(
                x509.SubjectKeyIdentifier.from_public_key(ca.pkey.public_key()),
                critical=False,
            )
            .add_extension(
                x509.AuthorityKeyIdentifier.from_issuer_public_key(self.pkey.public_key()),
                critical=False,
            )
            .sign(self.pkey, hashes.SHA256(), default_backend())
        )
        return ca

    def generateCertificate(self, nickname, namingAttributes={}, hostname=None, validity_days=3650):
        """Generate an user certificate using elliptic curve"""
        cert = ECDSA_Certificate()
        cert.namingAttrs = namingAttributes
        cert.validity_days = validity_days
        cert.isCA = False
        cert.isRoot = False
        cert.nickname = nickname
        cert.fixNamingAttributes()
        cert.subject = x509.Name([x509.NameAttribute(k,v) for k,v in cert.namingAttrs.items()])
        cert.issuer = self.subject
        cert.trust = 'u,u,u'
        cert.caChain = self

        if hostname is None:
            hostname = socket.gethostname()

        san_list = [x509.DNSName(hostname),]
        if hostname != 'localhost':
            san_list.append(x509.DNSName(hostname))
        san_list.append(x509.IPAddress(ipaddress.ip_address("127.0.0.1")))
        san_list.append(x509.IPAddress(ipaddress.ip_address("::1")))

        # Generate prime256v1 private key
        cert.pkey = ec.generate_private_key( ec.SECP256R1(), default_backend())
        cert.cert = (
            x509.CertificateBuilder()
            .subject_name(cert.subject)
            .issuer_name(cert.issuer)
            .public_key(cert.pkey.public_key())
            .serial_number(x509.random_serial_number())
            .not_valid_before(datetime.datetime.now(datetime.UTC))
            .not_valid_after(datetime.datetime.now(datetime.UTC) + datetime.timedelta(days=validity_days))
            # Server certificate extensions
            .add_extension(
                x509.BasicConstraints(ca=False, path_length=None),
                critical=True,
            )
            .add_extension(
                x509.KeyUsage(
                    digital_signature=True,
                    key_encipherment=True,
                    key_cert_sign=False,
                    crl_sign=False,
                    content_commitment=False,
                    data_encipherment=False,
                    key_agreement=False,
                    encipher_only=False,
                    decipher_only=False,
                ),
                critical=True,
            )
            .add_extension(
                x509.ExtendedKeyUsage([
                    x509.oid.ExtendedKeyUsageOID.SERVER_AUTH,
                    x509.oid.ExtendedKeyUsageOID.CLIENT_AUTH,
                ]),
                critical=True,
            )
            .add_extension(
                x509.SubjectAlternativeName(san_list),
                critical=False,
            )
            .add_extension(
                x509.SubjectKeyIdentifier.from_public_key(cert.pkey.public_key()),
                critical=False,
            )
            .add_extension(
                x509.AuthorityKeyIdentifier.from_issuer_public_key(self.pkey.public_key()),
                critical=False,
            )
            .sign(self.pkey, hashes.SHA256(), default_backend())
        )
        return cert

    def write_pkcs12_file(self, filename, pw=PKCS12_PASSWORD):
        """Save PKCS12 formatede certificate and private key"""
        if isinstance(pw, str):
            pw = pw.encode()
        if pw is None or pw == b"":
            enc = serialization.NoEncryption()
        else:
            enc = serialization.BestAvailableEncryption(pw)
        with open(filename, "wb") as f:
            f.write(pkcs12.serialize_key_and_certificates(
                self.nickname.encode(),
                self.pkey,
                self.cert,
                [],
                enc
            ))

    def save(self, dirname, pk12pw=PKCS12_PASSWORD):
        print(f'Writing files for {self.nickname}')
        if self.isCA:
            name = f'{dirname}/{self.nickname}-ca'
        else:
            name = f'{dirname}/{self.nickname}-cert'
        self.pem = f"{name}-cert.pem"
        self.der = f"{name}-cert.der"
        self.key = f"{name}-key.pem"
        self.kder = f"{name}-key.der"
        self.p12 = f"{name}.p12"
        ECDSA_Certificate.save_pem_file(self.pem, self.cert)
        ECDSA_Certificate.save_der_file(self.der, self.cert)
        ECDSA_Certificate.save_pem_file(self.key, self.pkey)
        ECDSA_Certificate.save_der_file(self.kder, self.pkey)
        self.write_pkcs12_file(self.p12, pk12pw)

    def run(self, cmd):
        log.info(f'Running: {" ".join(cmd)}')
        res = subprocess.run(cmd, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, encoding='utf-8')
        assert res
        log.info(f'Stdout+Stderr:{res.stdout}')
        res.check_returncode()

    def __repr__(self):
        return self.nickname

    @staticmethod
    def nss_db_paths(inst):
        prefix = os.environ.get('PREFIX', "/")
        certdbdir = f"{prefix}/etc/dirsrv/slapd-{inst.serverid}"
        pwfile = f'{certdbdir}/pwdfile.txt'
        return ( prefix, certdbdir, pwfile )

    def clear_nss_db(self, inst):
        prefix, certdbdir, pwfile = ECDSA_Certificate.nss_db_paths(inst)
        inst.stop()
        for f in ('cert9.db', 'key4.db'):
            try:
                os.remove(f'{certdbdir}/{f}')
            except OSError:
                pass
        self.run(('certutil', '-N', '-d', certdbdir, '-f', pwfile, '-@', pwfile))

    def offline_install(self, inst):
        prefix, certdbdir, pwfile = ECDSA_Certificate.nss_db_paths(inst)
        if self.isCA:
            self.run(('certutil', '-A', '-n', self.nickname, '-t', 'CT,,', '-f', pwfile, '-d', certdbdir, '-a', '-i', self.pem))
        else:
            self.run(('pk12util', '-v', '-i', self.p12, '-d', certdbdir, '-k', pwfile, '-W', ECDSA_Certificate.PKCS12_PASSWORD))

    def show(self, inst):
        prefix, certdbdir, pwfile = ECDSA_Certificate.nss_db_paths(inst)
        self.run(('certutil', '-L', '-n', self.nickname,  '-d', certdbdir))

    def rename(self, inst, newname):
        olddn = f'cn={self.nickname},{DYNCERT_SUFFIX}'
        newdn = f'cn={newname}'
        with ldapi(inst) as ldc:
            ldc.modrdn_s(olddn, newdn)
        newobj = copy.copy(self)
        newobj.nickname = newname
        return newobj

    def delete(self, inst):
        dn = f'cn={self.nickname},{DYNCERT_SUFFIX}'
        with ldapi(inst) as ldc:
            ldc.delete_s(dn)

    def showAll(self, inst):
        prefix, certdbdir, pwfile = ECDSA_Certificate.nss_db_paths(inst)
        self.run(('certutil', '-L', '-d', certdbdir))

    def read_cert(self, inst):
        with ldapi(inst) as ldc:
            with suppress(ldap.NO_SUCH_OBJECT):
                dn = f'cn={self.nickname},{DYNCERT_SUFFIX}'
                res = ldc.search_s(dn, ldap.SCOPE_BASE)
                log.info(f'Certificate {self.nickname} is {res}')
                return res
        log.info(f'Certificate {self.nickname} is not found')
        return None

    def online_install(self, inst):
        old_cert = self.read_cert(inst)
        log.info(f'Before online_install: cert={old_cert}')
        dn = f'cn={self.nickname},{DYNCERT_SUFFIX}'
        if self.isCA:
            # Read DER file
            with open(self.der, "rb") as f:
                dercert = f.read()
                derpkey = None
        else:
            # Read PKCS12 file
            with open(self.p12, "rb") as f:
                p12_data = f.read()
                p12pw = ECDSA_Certificate.PKCS12_PASSWORD.encode()
            privkey, cert, cas = pkcs12.load_key_and_certificates(p12_data, p12pw)
            dercert = cert.public_bytes(serialization.Encoding.DER)
            derpkey = privkey.private_bytes(encoding=serialization.Encoding.DER,
                                            format=serialization.PrivateFormat.PKCS8,
                                            encryption_algorithm=serialization.NoEncryption())
        with ldapi(inst) as ldc:
            if old_cert:
                log.info(f'+++ Trying to replace {dn} +++')
                mods = [(ldap.MOD_REPLACE, 'nsDynamicCertificateDER', [ dercert, ]),]
                if derpkey:
                    mods.append((ldap.MOD_REPLACE, 'nsDynamicCertificatePrivateKeyDER', [ derpkey, ]))
                ldc.modify_s(dn, mods)
                self.show(inst)
            else:
                log.info(f'+++ Trying to add {dn} +++')
                e = [
                     ('cn', [ self.nickname.encode(encoding="utf-8"), ] ),
                     ('objectclass', [ b'top', b'extensibleobject' ] ),
                     ('dsDynamicCertificateDER', [ dercert, ] ),
                    ]
                if derpkey:
                    e.append(('dsDynamicCertificatePrivateKeyDER', [derpkey,]))
                ldc.add_s(dn, e)
        new_cert = self.read_cert(inst)
        log.info(f'After online_install: cert={new_cert}')
        assert new_cert != old_cert

    def setSslPersonality(self, inst):
        ldc = open_ldapi_conn(inst)
        dn='cn=RSA,cn=encryption,cn=config'
        mods = [ (ldap.MOD_REPLACE, 'nsSSLPersonalitySSL', [ self.nickname.encode(), ]), ]
        ldc.modify_s(dn, mods)


#########################################################
###### Work around to debug libldap/liblber CERT ########
#########################################################

=======
    # Generate ecdsa certificate
=======
    """Elliptic Curve Certificate Generator"""
>>>>>>> 1236487850 (Phase 3 - suite)

    PKCS12_PASSWORD = "a+password"

    def __init__(self):
        self.nickname = None
        self.trust = None
        self.namingAttrs = {}
        self.subject = None
        self.validity_days = 365
        self.isCA = False
        self.isRoot = False
        self.caChain = None

    @staticmethod
    def generateRootCA(nickname, namingAttributes={}, validity_days=3650):
        """Generate a self-signed Root CA certificate using elliptic curve"""
        ca = ECDSA_Certificate()
        ca.namingAttrs = namingAttributes
        ca.validity_days = validity_days
        ca.isCA = True
        ca.isRoot = True
        ca.nickname = nickname
        ca.fixNamingAttributes()
        ca.subject = x509.Name([x509.NameAttribute(k,v) for k,v in ca.namingAttrs.items()])
        ca.issuer = ca.subject
        ca.trust = 'CT,,'

        # Generate prime256v1 private key 
        ca.pkey = ec.generate_private_key( ec.SECP256R1(), default_backend())
        ca.cert = (
            x509.CertificateBuilder()
            .subject_name(ca.subject)
            .issuer_name(ca.issuer)
            .public_key(ca.pkey.public_key())
            .serial_number(x509.random_serial_number())
            .not_valid_before(datetime.datetime.now(datetime.UTC))
            .not_valid_after(datetime.datetime.now(datetime.UTC) + datetime.timedelta(days=validity_days))
            # CA certificate extensions
            .add_extension(
                x509.BasicConstraints(ca=True, path_length=0),
                critical=True,
            )
            .add_extension(
                x509.KeyUsage(
                    digital_signature=False,
                    key_cert_sign=True,
                    crl_sign=False,
                    key_encipherment=False,
                    content_commitment=False,
                    data_encipherment=False,
                    key_agreement=False,
                    encipher_only=False,
                    decipher_only=False,
                ),
                critical=True,
            )
            .add_extension(
                x509.SubjectKeyIdentifier.from_public_key(ca.pkey.public_key()),
                critical=False,
            )
            .sign(ca.pkey, hashes.SHA256(), default_backend())
        )
        return ca

    @staticmethod
    def save_pem_file(filename, *items):
        """Save PEM formatted items to file"""
        with open(filename, "wb") as f:
            for item in items:
                if isinstance(item, ec.EllipticCurvePrivateKey):
                    pem = item.private_bytes(
                        encoding=serialization.Encoding.PEM,
                        format=serialization.PrivateFormat.TraditionalOpenSSL,
                        encryption_algorithm=serialization.NoEncryption()
                    )
                elif isinstance(item, x509.Certificate):
                    pem = item.public_bytes(serialization.Encoding.PEM)
                else:
                    raise ValueError(f"Unknown item type: {type(item)}")
                f.write(pem)

    @staticmethod
    def save_der_file(filename, item):
        """Save DER formatted item to file"""
        with open(filename, "wb") as f:
            if isinstance(item, ec.EllipticCurvePrivateKey):
                der = item.private_bytes(
                    encoding=serialization.Encoding.DER,
                    format=serialization.PrivateFormat.PKCS8, #format=serialization.PrivateFormat.TraditionalOpenSSL,
                    encryption_algorithm=serialization.NoEncryption()
                )
            elif isinstance(item, x509.Certificate):
                der = item.public_bytes(serialization.Encoding.DER)
            else:
                raise ValueError(f"Unknown item type: {type(item)}")
            f.write(der)

    def fixNamingAttribute(self, name, vdef):
        """Set value for naming attribute if it is missing"""
        if name not in self.namingAttrs:
            self.namingAttrs[name] = vdef
    
    def fixNamingAttributes(self):
        """Set default value for mandatory naming attributes"""
        self.fixNamingAttribute(NameOID.COMMON_NAME, self.nickname)
        self.fixNamingAttribute(NameOID.COUNTRY_NAME, 'US')
        self.fixNamingAttribute(NameOID.ORGANIZATION_NAME, 'Example Organization')

    def generateCA(self, namingAttributes, validity_days=3650):
        """Generate an intermediary CA certificate using elliptic curve"""
        ca = ECDSA_Certificate()
        ca.namingAttrs = namingAttributes
        ca.validity_days = validity_days
        ca.isCA = True
        ca.isRoot = False
        ca.nickname = nickname
        ca.fixNamingAttributes()
        ca.subject = x509.Name([x509.NameAttribute(k,v) for k,v in ca.namingAttrs.items()])
        ca.issuer = self.subject
        ca.trust = 'CT,,'

        # Generate prime256v1 private key 
        ca.pkey = ec.generate_private_key( ec.SECP256R1(), default_backend())
        ca.cert = (
            x509.CertificateBuilder()
            .subject_name(ca.subject)
            .issuer_name(ca.issuer)
            .public_key(ca.pkey.public_key())
            .serial_number(x509.random_serial_number())
            .not_valid_before(datetime.datetime.now(datetime.UTC))
            .not_valid_after(datetime.datetime.now(datetime.UTC) + datetime.timedelta(days=validity_days))
            # CA certificate extensions
            .add_extension(
                x509.BasicConstraints(ca=True, path_length=0),
                critical=True,
            )
            .add_extension(
                x509.KeyUsage(
                    digital_signature=False,
                    key_cert_sign=True,
                    crl_sign=False,
                    key_encipherment=False,
                    content_commitment=False,
                    data_encipherment=False,
                    key_agreement=False,
                    encipher_only=False,
                    decipher_only=False,
                ),
                critical=True,
            )
            .add_extension(
                x509.SubjectKeyIdentifier.from_public_key(ca.pkey.public_key()),
                critical=False,
            )
            .add_extension(
                x509.AuthorityKeyIdentifier.from_issuer_public_key(self.pkey.public_key()),
                critical=False,
            )
            .sign(self.pkey, hashes.SHA256(), default_backend())
        )
        return ca

    def generateCertificate(self, nickname, namingAttributes={}, hostname=None, validity_days=3650):
        """Generate an user certificate using elliptic curve"""
        cert = ECDSA_Certificate()
        cert.namingAttrs = namingAttributes
        cert.validity_days = validity_days
        cert.isCA = False
        cert.isRoot = False
        cert.nickname = nickname
        cert.fixNamingAttributes()
        cert.subject = x509.Name([x509.NameAttribute(k,v) for k,v in cert.namingAttrs.items()])
        cert.issuer = self.subject
        cert.trust = 'u,u,u'
        cert.caChain = self

        if hostname is None:
            hostname = socket.gethostname()

        san_list = [x509.DNSName(hostname),]
        if hostname != 'localhost':
            san_list.append(x509.DNSName(hostname))
        san_list.append(x509.IPAddress(ipaddress.ip_address("127.0.0.1")))
        san_list.append(x509.IPAddress(ipaddress.ip_address("::1")))

        # Generate prime256v1 private key 
        cert.pkey = ec.generate_private_key( ec.SECP256R1(), default_backend())
        cert.cert = (
            x509.CertificateBuilder()
            .subject_name(cert.subject)
            .issuer_name(cert.issuer)
            .public_key(cert.pkey.public_key())
            .serial_number(x509.random_serial_number())
            .not_valid_before(datetime.datetime.now(datetime.UTC))
            .not_valid_after(datetime.datetime.now(datetime.UTC) + datetime.timedelta(days=validity_days))
            # Server certificate extensions
            .add_extension(
                x509.BasicConstraints(ca=False, path_length=None),
                critical=True,
            )
            .add_extension(
                x509.KeyUsage(
                    digital_signature=True,
                    key_encipherment=True,
                    key_cert_sign=False,
                    crl_sign=False,
                    content_commitment=False,
                    data_encipherment=False,
                    key_agreement=False,
                    encipher_only=False,
                    decipher_only=False,
                ),
                critical=True,
            )
            .add_extension(
                x509.ExtendedKeyUsage([
                    x509.oid.ExtendedKeyUsageOID.SERVER_AUTH,
                    x509.oid.ExtendedKeyUsageOID.CLIENT_AUTH,
                ]),
                critical=True,
            )
            .add_extension(
                x509.SubjectAlternativeName(san_list),
                critical=False,
            )
            .add_extension(
                x509.SubjectKeyIdentifier.from_public_key(cert.pkey.public_key()),
                critical=False,
            )
            .add_extension(
                x509.AuthorityKeyIdentifier.from_issuer_public_key(self.pkey.public_key()),
                critical=False,
            )
            .sign(self.pkey, hashes.SHA256(), default_backend())
        )
        return cert

    def write_pkcs12_file(self, filename, pw=PKCS12_PASSWORD):
        """Save PKCS12 formatede certificate and private key"""
        if isinstance(pw, str):
            pw = pw.encode()
        if pw is None or pw == b"":
            enc = serialization.NoEncryption()
        else:
            enc = serialization.BestAvailableEncryption(pw)
        with open(filename, "wb") as f:
            f.write(pkcs12.serialize_key_and_certificates(
                self.nickname.encode(),
                self.pkey,
                self.cert,
                [],
                enc
            ))

    def save(self, dirname, pk12pw=PKCS12_PASSWORD):
        print(f'Writing files for {self.nickname}')
        if self.isCA:
            name = f'{dirname}/{self.nickname}-ca'
        else:
            name = f'{dirname}/{self.nickname}-cert'
        self.pem = f"{name}-cert.pem"
        self.der = f"{name}-cert.der"
        self.key = f"{name}-key.pem"
        self.kder = f"{name}-key.der"
        self.p12 = f"{name}.p12"
        ECDSA_Certificate.save_pem_file(self.pem, self.cert)
        ECDSA_Certificate.save_der_file(self.der, self.cert)
        ECDSA_Certificate.save_pem_file(self.key, self.pkey)
        ECDSA_Certificate.save_der_file(self.kder, self.pkey)
        self.write_pkcs12_file(self.p12, pk12pw)

    def run(self, cmd):
        log.info(f'Running: {" ".join(cmd)}')
        res = subprocess.run(cmd, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, encoding='utf-8')
        assert res
        log.info(f'Stdout+Stderr:{res.stdout}')
        res.check_returncode()

    def __repr__(self):
        return self.nickname

    @staticmethod
    def nss_db_paths(inst):
        prefix = os.environ.get('PREFIX', "/")
        certdbdir = f"{prefix}/etc/dirsrv/slapd-{inst.serverid}"
        pwfile = f'{certdbdir}/pwdfile.txt'
        return ( prefix, certdbdir, pwfile )

    def clear_nss_db(self, inst):
        prefix, certdbdir, pwfile = ECDSA_Certificate.nss_db_paths(inst)        
        inst.stop()
        for f in ('cert9.db', 'key4.db'):
            try:
                os.remove(f'{certdbdir}/{f}')
            except OSError:
                pass
        self.run(('certutil', '-N', '-d', certdbdir, '-f', pwfile, '-@', pwfile))

    def offline_install(self, inst):
        prefix, certdbdir, pwfile = ECDSA_Certificate.nss_db_paths(inst)        
        if self.isCA:
            self.run(('certutil', '-A', '-n', self.nickname, '-t', 'CT,,', '-f', pwfile, '-d', certdbdir, '-a', '-i', self.pem))
        else:
            self.run(('pk12util', '-v', '-i', self.p12, '-d', certdbdir, '-k', pwfile, '-W', ECDSA_Certificate.PKCS12_PASSWORD))

    def show(self, inst):
        prefix, certdbdir, pwfile = ECDSA_Certificate.nss_db_paths(inst)        
        self.run(('certutil', '-L', '-n', self.nickname,  '-d', certdbdir))

    def showAll(self, inst):
        prefix, certdbdir, pwfile = ECDSA_Certificate.nss_db_paths(inst)        
        self.run(('certutil', '-L', '-d', certdbdir))

    def read_cert(self, inst):
        with ldapi(inst) as ldc:
            with suppress(ldap.NO_SUCH_OBJECT):
                dn = f'cn={self.nickname},{DYNCERT_SUFFIX}'
                res = ldc.search_s(dn, ldap.SCOPE_BASE)
                return res
        return None

    def online_install(self, inst):
        old_cert = self.read_cert(inst)
        log.info(f'Before online_install: cert={old_cert}')
        dn = f'cn={self.nickname},{DYNCERT_SUFFIX}'
        if self.isCA:
            # Read DER file
            with open(self.der, "rb") as f:
                dercert = f.read()
                derpkey = None
        else:
            # Read PKCS12 file
            with open(self.p12, "rb") as f:
                p12_data = f.read()
                p12pw = ECDSA_Certificate.PKCS12_PASSWORD.encode()
            privkey, cert, cas = pkcs12.load_key_and_certificates(p12_data, p12pw)
            dercert = cert.public_bytes(serialization.Encoding.DER)
            derpkey = privkey.private_bytes(encoding=serialization.Encoding.DER,
                                            format=serialization.PrivateFormat.PKCS8,
                                            encryption_algorithm=serialization.NoEncryption())
        with ldapi(inst) as ldc:
            if old_cert:
                log.info(f'+++ Trying to replace {dn} +++')
                mods = [(ldap.MOD_REPLACE, 'nsDynamicCertificateDER', [ dercert, ]),]
                if derpkey:
                    mods.append((ldap.MOD_REPLACE, 'nsDynamicCertificatePrivateKeyDER', [ derpkey, ]))
                ldc.modify_s(dn, mods)
                self.show(inst)
            else:
                log.info(f'+++ Trying to add {dn} +++')
                e = [
                     ('cn', [ self.nickname.encode(encoding="utf-8"), ] ),
                     ('objectclass', [ b'top', b'extensibleobject' ] ),
                     ('nsDynamicCertificateDER', [ dercert, ] ),
                    ]
                if derpkey:
                    e.append(('nsDynamicCertificatePrivateKeyDER', [derpkey,]))
                ldc.add_s(dn, e)
        new_cert = self.read_cert(inst)
        log.info(f'After online_install: cert={new_cert}')
        assert new_cert != old_cert

    def setSslPersonality(self, inst):
        ldc = open_ldapi_conn(inst)
        dn='cn=RSA,cn=encryption,cn=config'
        mods = [ (ldap.MOD_REPLACE, 'nsSSLPersonalitySSL', [ self.nickname.encode(), ]), ]
        ldc.modify_s(dn, mods)


#########################################################
###### Work around to debug libldap/liblber CERT ########
#########################################################

>>>>>>> 87c7465ad8 (Dynamic Certificate - phase 3 - switching tls cert)
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
<<<<<<< HEAD
<<<<<<< HEAD
    url = f'ldaps://localhost:{inst.sslport}'
=======
    url = inst.toLDAPURL()
>>>>>>> 87c7465ad8 (Dynamic Certificate - phase 3 - switching tls cert)
=======
    url = f'ldaps://localhost:{inst.sslport}'
>>>>>>> 1236487850 (Phase 3 - suite)
    log.info(f'Attempt to connect to {url} using {ca.pem}')
    ld = ldap.initialize(url)
    #ld.set_option(ldap.OPT_X_TLS_REQUIRE_CERT,ldap.OPT_X_TLS_NEVER)
    ld.set_option(ldap.OPT_X_TLS_REQUIRE_CERT,ldap.OPT_X_TLS_DEMAND)
    ld.set_option(ldap.OPT_X_TLS_CACERTFILE, f'{ca.pem}')
    ld.set_option(ldap.OPT_X_TLS_NEWCTX, 0)
    return ld
<<<<<<< HEAD
=======


def open_ldapi_conn(inst, logfile=None):
    dse = DSEldif(inst)
    ldapi_socket = dse.get('cn=config', 'nsslapd-ldapifilepath', single=True)
    url = f"ldapi://{ldapurl.ldapUrlEscape(ensure_str(ldapi_socket))}"
    log.info(f'Attempt to connect to {url} using sasl external authentication')
    ld = ldap.initialize(url)
    # Perform autobind
    sasl_auth = ldap.sasl.external()
    ld.sasl_interactive_bind_s("", sasl_auth)
    return ld


def tls_search(inst, ca):
    url = f'ldaps://localhost:{inst.sslport}'
    log.info(f'Attempt to connect to {url} using {ca.pem}')
    with traced_ldap_connection(url, f'ldaps bind using CA {ca}') as ld:
        #ld.set_option(ldap.OPT_X_TLS_REQUIRE_CERT,ldap.OPT_X_TLS_NEVER)
        ld.set_option(ldap.OPT_X_TLS_REQUIRE_CERT,ldap.OPT_X_TLS_DEMAND)
        ld.set_option(ldap.OPT_X_TLS_CACERTFILE, f'{ca.pem}')
        ld.set_option(ldap.OPT_X_TLS_NEWCTX, 0)
        ld.simple_bind_s(DN_DM, PW_DM)
        results = ld.search_s('', ldap.SCOPE_BASE)
        assert len(results) == 1


#
#  Ideally There should be a dsctl/dsconf subcommand
#
def refresh_certs(inst, timeout=60):
    ld = open_ldapi_conn(inst)
    ld.modify_s('cn=config', [(ldap.MOD_REPLACE, 'nsslapd-refresh-certificates', b'on')])
    # Now lets wait until the config value is off again
    for _ in range(timeout):
        result = ld.search_s('cn=config', ldap.SCOPE_BASE, attrlist=['nsslapd-refresh-certificates',])
        log.info(f'cn=config result: {result}')
        attrs = result[0][1]
        vals = attrs['nsslapd-refresh-certificates']
        if vals[0].decode("utf-8").lower() == "off":
            ld.unbind()
            return
        time.sleep(1)
    raise TimeoutError(f"Certificate not changed after {timeout} secopnds")
>>>>>>> 87c7465ad8 (Dynamic Certificate - phase 3 - switching tls cert)

<<<<<<< HEAD

def open_ldapi_conn(inst, logfile=None):
    dse = DSEldif(inst)
    ldapi_socket = dse.get('cn=config', 'nsslapd-ldapifilepath', single=True)
    url = f"ldapi://{ldapurl.ldapUrlEscape(ensure_str(ldapi_socket))}"
    log.info(f'Attempt to connect to {url} using sasl external authentication')
    ld = ldap.initialize(url)
    # Perform autobind
    sasl_auth = ldap.sasl.external()
    ld.sasl_interactive_bind_s("", sasl_auth)
    return ld


def tls_search(inst, ca):
    url = f'ldaps://localhost:{inst.sslport}'
    log.info(f'Attempt to connect to {url} using {ca.pem}')
    with traced_ldap_connection(url, f'ldaps bind using CA {ca}') as ld:
        #ld.set_option(ldap.OPT_X_TLS_REQUIRE_CERT,ldap.OPT_X_TLS_NEVER)
        ld.set_option(ldap.OPT_X_TLS_REQUIRE_CERT,ldap.OPT_X_TLS_DEMAND)
        ld.set_option(ldap.OPT_X_TLS_CACERTFILE, f'{ca.pem}')
        ld.set_option(ldap.OPT_X_TLS_NEWCTX, 0)
        ld.simple_bind_s(DN_DM, PW_DM)
        results = ld.search_s('', ldap.SCOPE_BASE)
        assert len(results) == 1


#
#  Ideally There should be a dsctl/dsconf subcommand
#
def refresh_certs(inst, timeout=60):
    ld = open_ldapi_conn(inst)
    ld.modify_s('cn=config', [(ldap.MOD_REPLACE, 'nsslapd-refresh-certificates', b'on')])
    # Now lets wait until the config value is off again
    for _ in range(timeout):
        result = ld.search_s('cn=config', ldap.SCOPE_BASE, attrlist=['nsslapd-refresh-certificates',])
        log.info(f'cn=config result: {result}')
        attrs = result[0][1]
        vals = attrs['nsslapd-refresh-certificates']
        if vals[0].decode("utf-8").lower() == "off":
            ld.unbind()
            return
        time.sleep(1)
    raise TimeoutError(f"Certificate not changed after {timeout} secopnds")

=======
>>>>>>> 1236487850 (Phase 3 - suite)
# Generate Server Certifcate and Root CA and install them
@pytest.fixture(scope="module")
def ecdsa_certs(topo, request):
    dir = '/tmp/ecdsa_certs'
    def fin():
        if not DEBUGGING:
            with suppress(FileNotFoundError):
                shutil.rmtree(dir)
    request.addfinalizer(fin)
    with suppress(FileNotFoundError):
        shutil.rmtree(dir)
    os.makedirs(dir, 0o700)
<<<<<<< HEAD
    inst=topo.standalone
    global tls_enabled
    if not tls_enabled:
        inst.enable_tls()
        tls_enabled = True
    ca = ECDSA_Certificate.generateRootCA("CA")
    cert = ca.generateCertificate("Cert")
    cert.setSslPersonality(inst)
    ca.clear_nss_db(inst)
    ca.showAll(inst)
    ca.save(dir)
    cert.save(dir)
    cert.offline_install(inst)
    cert.show(inst)
    ca.offline_install(inst)
    ca.show(inst)
    ca.showAll(inst)
    inst.restart(post_open=False)
    return (cert, ca, dir)


def test_ecdsa(topo, ecdsa_certs):
    """Setup instance with ecdsa certificates

    :id: 7902f37c-01d3-11ed-b65c-482ae39447e5
    :setup: Standalone Instance with ecdsa certificates
    :steps:
<<<<<<< HEAD
        1. Open ldaps connection with server CA certificate and search root entry
    :expectedresults:
        1. No error
    """

    inst=topo.standalone
    cert, ca, dir = ecdsa_certs
    tls_search(inst, ca)


def test_dynamic(topo, ecdsa_certs):
    """Check that certificates can be managed dynamically

    :id: 7902f37c-01d3-11ed-b65c-482ae39447e5
    :setup: Standalone Instance with ecdsa certificates
    :steps:
        1. Create Test-DynCert-1 certificate
        2. Add it dynamically
        3. Search for Test-DynCert-1
        4. Rename Test-DynCert-1 as Test-DynCert-2
        5. Search for Test-DynCert-1
        6. Search for Test-DynCert-2
        7. Delete dynamically Test-DynCert-2
        8. Search for Test-DynCert-2
    :expectedresults:
        1. No error
        2. No error
        3. Entry should exists
        4. No error
        5. Entry should not exists
        6. Entry should exists
        7. No error
        8. Entry should not exists
    """

    inst=topo.standalone
    cert, ca, dir = ecdsa_certs
    cert1 = ca.generateCertificate("Test-DynCert-1")
    cert1.save(dir)
    cert1.online_install(inst)
    assert cert1.read_cert(inst)
    cert2 = cert1.rename(inst, "Test-DynCert-2")
    assert not cert1.read_cert(inst)
    assert cert2.read_cert(inst)
    cert2.delete(inst)
    assert not cert2.read_cert(inst)
=======
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

=======
>>>>>>> 1236487850 (Phase 3 - suite)
    inst=topo.standalone
    global tls_enabled
    if not tls_enabled:
        inst.enable_tls()
        tls_enabled = True
    ca = ECDSA_Certificate.generateRootCA("CA")
    cert = ca.generateCertificate("Cert")
    cert.setSslPersonality(inst)
    ca.clear_nss_db(inst)
    ca.showAll(inst)
    ca.save(dir)
    cert.save(dir)
    cert.offline_install(inst)
    cert.show(inst)
    ca.offline_install(inst)
    ca.show(inst)
    ca.showAll(inst)
    inst.restart(post_open=False)
    return (cert, ca, dir)


def test_ecdsa(topo, ecdsa_certs):
    """Specify a test case purpose or name here

    :id: 7902f37c-01d3-11ed-b65c-482ae39447e5
    :setup: Standalone Instance with ecdsa certificates
    :steps:
        1. Open ldaps connection with server CA certificate and search root entry
    :expectedresults:
        1. No error
    """

    inst=topo.standalone
    cert, ca, dir = ecdsa_certs
    tls_search(inst, ca)


def test_refresh_ecdsa_1ca(topo, ecdsa_certs):
    """Test dynamic refresh of server certificate
>>>>>>> 87c7465ad8 (Dynamic Certificate - phase 3 - switching tls cert)

    :id: 96039bce-5370-11f0-9de5-c85309d5c3e3
    :setup: Standalone Instance with ecdsa certificates
    :steps:
        1. Open ldaps connection with server CA certificate and search root entry
        2. Open a second ldaps connection with server CA certificate and keep it open
        3. Generate a new server certificate (signed by original CA)
        4. Change nsSSLPersonalitySSL to the new certificate nickname
        5. Add the new server cert (using add operation on dynamic certificates)
        6. Open ldaps connection with server CA certificate and search root entry
        7. Perform a search on the second open ldaps connection
    :expectedresults:
        1. No error
        2. No error
        3. No error
        4. No error
        5. No error
        6. No error
        7. No error
    """

    inst=topo.standalone
    cert, ca, dir = ecdsa_certs
    tls_search(inst, ca)
    ld = open_ldaps_conn(inst, ca)
    cert2 = ca.generateCertificate("New-Cert-1CA")
    cert2.save(dir)
    cert2.setSslPersonality(inst)
    cert2.online_install(inst)
    tls_search(inst, ca)
    with redirect_stdio(f"Search using already open connection with CA: {ca}"):
        results = ld.search_s('', ldap.SCOPE_BASE)
        assert len(results) == 1
        ld.unbind()


def test_refresh_ecdsa_2ca(topo, ecdsa_certs):
    """Test dynamic refresh of server certificate and CA

    :id: 96039bce-5370-11f0-9de5-c85309d5c3e3
    :setup: Standalone Instance
    :steps:
        1. Open ldaps connection with server CA certificate and search root entry
        2. Open a second ldaps connection with server CA certificate and keep it open
        3. Generate a new Self Signed CA
        4. Generate a new server certificate (signed by the new CA)
        5. Add the new CA (using add operation on dynamic certificates)
        6. Add the new server cert (using add operation on dynamic certificates)
        7. Open ldaps connection with old server CA certificate and search root entry
        8. Open ldaps connection with new server CA certificate and search root entry
        9. Perform a search on the second open ldaps connection
    :expectedresults:
        1. No error
        2. No error
        3. No error
        4. No error
        5. No error
        6. No error
        7. ldap.SERVER_DOWN because the old CA does not match the server one
        8. No error
        9. ldap.SERVER_DOWN because the old CA does not match the server one
    """

    inst=topo.standalone
    cert, ca, dir = ecdsa_certs
    tls_search(inst, ca)
    ld = open_ldaps_conn(inst, ca)
    ca2 = ECDSA_Certificate.generateRootCA("New-CA-2CA")
    cert2 = ca2.generateCertificate("New-Cert-2CA")
    ca2.save(dir)
    cert2.save(dir)
    cert2.setSslPersonality(inst)
    ca2.online_install(inst)
    cert2.online_install(inst)
    with pytest.raises(ldap.SERVER_DOWN):
        tls_search(inst, ca)
    tls_search(inst, ca2)
    with redirect_stdio(f"Search using already open connection with CA: {ca}"):
        with pytest.raises(ldap.SERVER_DOWN):
            results = ld.search_s('', ldap.SCOPE_BASE)
            assert len(results) == 1
        ld.unbind()


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
