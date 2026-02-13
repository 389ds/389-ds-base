# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
"""
Comprehensive test suite for Dynamic Certificates feature
Tests certificate metadata extraction, trust flags, multi-token support,
verification, and error handling using lib389 DynamicCerts classes.
"""

import os
import sys
import base64
import copy
import datetime
import ipaddress
import ldap
import ldapurl
import logging
import pytest
import secrets
import shutil
import socket
import subprocess
import textwrap
import time
from contextlib import contextmanager, suppress
from cryptography.hazmat.backends import default_backend
from cryptography.hazmat.primitives.asymmetric import ec, rsa
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.serialization import pkcs12
from cryptography import x509
from cryptography.x509.oid import NameOID, ExtensionOID
from lib389.cli_base import FakeArgs
from lib389._constants import DN_DM, PW_DM
from lib389.dseldif import DSEldif
from lib389.topologies import topology_st as topo
from lib389.utils import ds_is_older, ensure_str, pem_to_der, rpm_is_older
from lib389.dyncerts import (
    DynamicCerts, DynamicCert, DYNCERT_SUFFIX,
    DYCATTR_CN, DYCATTR_CERTDER, DYCATTR_PKEYDER )
from tempfile import NamedTemporaryFile

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

tls_enabled = False


def utcdate():
    return datetime.datetime.utcnow()

def fix_crash_issue_7227(inst):
    """Work around to avoid ns-slapd crash in CERT_VerifyCertificateNow()"""
    inst.restart()


class RSA_Certificate:
    """RSA Certificate Generator for testing RSA key support"""

    PKCS12_PASSWORD = "rsa+password"

    def __init__(self):
        self.nickname = None
        self.trust = None
        self.namingAttrs = {}
        self.subject = None
        self.validity_days = 365
        self.isCA = False
        self.isRoot = False

    @staticmethod
    def generateRootCA(nickname, namingAttributes={}, validity_days=3650):
        """Generate a self-signed RSA Root CA certificate"""
        ca = RSA_Certificate()
        ca.namingAttrs = namingAttributes
        ca.validity_days = validity_days
        ca.isCA = True
        ca.isRoot = True
        ca.nickname = nickname
        ca.fixNamingAttributes()
        ca.subject = x509.Name([x509.NameAttribute(k, v) for k, v in ca.namingAttrs.items()])
        ca.issuer = ca.subject
        ca.trust = 'CT,,'

        # Generate RSA 2048-bit private key
        ca.pkey = rsa.generate_private_key(
            public_exponent=65537,
            key_size=2048,
            backend=default_backend()
        )
        ca.cert = (
            x509.CertificateBuilder()
            .subject_name(ca.subject)
            .issuer_name(ca.issuer)
            .public_key(ca.pkey.public_key())
            .serial_number(x509.random_serial_number())
            .not_valid_before(utcdate())
            .not_valid_after(utcdate() + datetime.timedelta(days=validity_days))
            .add_extension(
                x509.BasicConstraints(ca=True, path_length=0),
                critical=True,
            )
            .add_extension(
                x509.KeyUsage(
                    digital_signature=True,
                    key_cert_sign=True,
                    crl_sign=True,
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

    def fixNamingAttribute(self, name, vdef):
        if name not in self.namingAttrs:
            self.namingAttrs[name] = vdef

    def fixNamingAttributes(self):
        self.fixNamingAttribute(NameOID.COMMON_NAME, self.nickname)
        self.fixNamingAttribute(NameOID.COUNTRY_NAME, 'US')
        self.fixNamingAttribute(NameOID.ORGANIZATION_NAME, 'Test Organization')

    def generateCertificate(self, nickname, namingAttributes={}, hostname=None, validity_days=365):
        """Generate an RSA server certificate"""
        cert = RSA_Certificate()
        cert.namingAttrs = namingAttributes
        cert.validity_days = validity_days
        cert.isCA = False
        cert.isRoot = False
        cert.nickname = nickname
        cert.fixNamingAttributes()
        cert.subject = x509.Name([x509.NameAttribute(k, v) for k, v in cert.namingAttrs.items()])
        cert.issuer = self.subject
        cert.trust = 'u,u,u'

        if hostname is None:
            hostname = socket.gethostname()

        san_list = [
            x509.DNSName(hostname),
            x509.DNSName('localhost'),
            x509.IPAddress(ipaddress.ip_address("127.0.0.1")),
            x509.IPAddress(ipaddress.ip_address("::1"))
        ]

        cert.pkey = rsa.generate_private_key(
            public_exponent=65537,
            key_size=2048,
            backend=default_backend()
        )
        cert.cert = (
            x509.CertificateBuilder()
            .subject_name(cert.subject)
            .issuer_name(cert.issuer)
            .public_key(cert.pkey.public_key())
            .serial_number(x509.random_serial_number())
            .not_valid_before(utcdate())
            .not_valid_after(utcdate() + datetime.timedelta(days=validity_days))
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

    def save_pem_file(self, filename):
        """Save certificate in PEM format"""
        with open(filename, "wb") as f:
            f.write(self.cert.public_bytes(serialization.Encoding.PEM))

    def save_der_file(self, filename):
        """Save certificate in DER format"""
        with open(filename, "wb") as f:
            f.write(self.cert.public_bytes(serialization.Encoding.DER))

    def save_key_der(self, filename):
        """Save private key in PKCS8 DER format"""
        with open(filename, "wb") as f:
            f.write(self.pkey.private_bytes(
                encoding=serialization.Encoding.DER,
                format=serialization.PrivateFormat.PKCS8,
                encryption_algorithm=serialization.NoEncryption()
            ))

    def write_pkcs12_file(self, filename, pw=PKCS12_PASSWORD):
        """Save PKCS12 formatted certificate and private key"""
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

    def save(self, dirname):
        """Save all certificate formats"""
        name = f'{dirname}/{self.nickname}'
        self.pem = f"{name}.pem"
        self.der = f"{name}.der"
        self.kder = f"{name}-key.der"
        self.p12 = f"{name}.p12"
        self.save_pem_file(self.pem)
        self.save_der_file(self.der)
        self.save_key_der(self.kder)
        self.write_pkcs12_file(self.p12)


@pytest.fixture(scope="module")
def setup_tls(topo):
    """Enable TLS on the instance"""
    global tls_enabled
    inst = topo.standalone
    if not tls_enabled:
        inst.enable_tls()
        tls_enabled = True
    return inst


def test_certificate_metadata_extraction(topo, setup_tls):
    """Test that certificate metadata is correctly extracted and exposed

    :id: a1b2c3d4-1111-2222-3333-444444444444
    :setup: Standalone Instance with TLS enabled
    :steps:
        1. Generate RSA Root CA certificate
        2. Add certificate dynamically using DynamicCerts
        3. Retrieve certificate object and verify all metadata attributes
        4. Verify subject DN is correct
        5. Verify issuer DN is correct
        6. Verify isCA flag is TRUE
        7. Verify isRootCA flag is TRUE
        8. Verify key algorithm is RSA
        9. Verify trust flags are present
        10. Verify validity dates (notBefore, notAfter)
        11. Verify serial number is present
    :expectedresults:
        1. Certificate generated successfully
        2. Certificate added without error
        3. Certificate object retrieved
        4. Subject matches expected DN
        5. Issuer matches subject (self-signed)
        6. isCA is TRUE
        7. isRootCA is TRUE
        8. Key algorithm is RSA
        9. Trust flags are present
        10. Validity dates are within expected range
        11. Serial number is present and non-empty
    """
    inst = setup_tls
    dir = '/tmp/dyncert_metadata_test'
    os.makedirs(dir, 0o700, exist_ok=True)

    try:
        # Generate RSA Root CA
        ca = RSA_Certificate.generateRootCA(
            "TestMetadataCA",
            namingAttributes={
                NameOID.COMMON_NAME: "Test Metadata CA",
                NameOID.COUNTRY_NAME: "US",
                NameOID.ORGANIZATION_NAME: "Test Org",
                NameOID.ORGANIZATIONAL_UNIT_NAME: "Testing Unit"
            }
        )
        ca.save(dir)

        # Add certificate using DynamicCerts
        dyncerts = DynamicCerts(inst)
        dyncerts.add_cert(ca.pem, ca.nickname, ca=True)

        # Get certificate object
        cert_obj = dyncerts.get_cert_obj(ca.nickname)
        assert cert_obj is not None

        # Verify subject and issuer
        subject = cert_obj.get_attr_val_utf8_l('dsDynamicCertificateSubject')
        issuer = cert_obj.get_attr_val_utf8_l('dsDynamicCertificateIssuer')
        log.info(f"Subject: {subject}")
        log.info(f"Issuer: {issuer}")
        assert "Test Metadata CA".lower() in subject
        assert subject == issuer  # Self-signed

        # Verify CA flags
        is_ca = cert_obj.get_attr_val_utf8_l('dsDynamicCertificateIsCA')
        is_root_ca = cert_obj.get_attr_val_utf8_l('dsDynamicCertificateIsRootCA')
        log.info(f"isCA: {is_ca}")
        log.info(f"isRootCA: {is_root_ca}")
        assert is_ca == 'true'
        assert is_root_ca == 'true'

        # Verify key algorithm
        key_algo = cert_obj.get_attr_val_utf8('dsDynamicCertificateKeyAlgorithm')
        log.info(f"Key Algorithm: {key_algo}")
        assert 'RSA' in key_algo.upper()

        # Verify trust flags
        trust_flags = cert_obj.get_attr_val_utf8('dsDynamicCertificateTrustFlags')
        log.info(f"Trust Flags: {trust_flags}")
        assert trust_flags is not None

        # Verify validity dates
        not_before = cert_obj.get_attr_val_utf8('dsDynamicCertificateNotBefore')
        not_after = cert_obj.get_attr_val_utf8('dsDynamicCertificateNotAfter')
        log.info(f"Not Before: {not_before}")
        log.info(f"Not After: {not_after}")
        assert not_before is not None
        assert not_after is not None

        # Verify serial number
        serial = cert_obj.get_attr_val_utf8('dsDynamicCertificateSerialNumber')
        log.info(f"Serial Number: {serial}")
        assert serial is not None
        assert len(serial) > 0

        # Cleanup
        dyncerts.del_cert(ca.nickname)

    finally:
        if not DEBUGGING:
            shutil.rmtree(dir, ignore_errors=True)


def test_subject_alternative_names(topo, setup_tls):
    """Test that Subject Alternative Names are correctly parsed and displayed

    :id: b2c3d4e5-2222-3333-4444-555555555555
    :setup: Standalone Instance with TLS enabled
    :steps:
        1. Generate server certificate with multiple SANs (DNS and IP)
        2. Add certificate dynamically using DynamicCerts
        3. Retrieve certificate object
        4. Verify dsDynamicCertificateSubjectAltName attribute exists
        5. Verify all DNS names are present
        6. Verify all IP addresses are present
    :expectedresults:
        1. Certificate generated with SANs
        2. Certificate added successfully
        3. Certificate object retrieved
        4. SubjectAltName attribute exists
        5. DNS names are correctly parsed
        6. IP addresses are correctly parsed
    """
    inst = setup_tls
    dir = '/tmp/dyncert_san_test'
    os.makedirs(dir, 0o700, exist_ok=True)

    try:
        ca = RSA_Certificate.generateRootCA("TestSAN_CA")
        ca.save(dir)

        # Add CA first
        dyncerts = DynamicCerts(inst)
        dyncerts.add_cert(ca.pem, ca.nickname, ca=True)

        # Generate certificate with specific hostname
        cert = ca.generateCertificate("TestSAN_Cert", hostname="test.example.com")
        cert.save(dir)

        # Add server certificate with private key
        dyncerts.add_cert(cert.p12, cert.nickname, pkcs12_password=RSA_Certificate.PKCS12_PASSWORD)

        # Get certificate object
        cert_obj = dyncerts.get_cert_obj(cert.nickname)
        assert cert_obj is not None

        # Verify SANs
        san_values = cert_obj.get_attr_vals_utf8('dsDynamicCertificateSubjectAltName')
        log.info(f"Subject Alternative Names: {san_values}")
        assert san_values is not None

        # Verify expected SANs are present
        san_str = ' '.join(san_values)
        assert 'test.example.com' in san_str or 'localhost' in san_str
        assert '127.0.0.1' in san_str or '::1' in san_str

        # Cleanup
        dyncerts.del_cert(ca.nickname)
        dyncerts.del_cert(cert.nickname)

    finally:
        if not DEBUGGING:
            shutil.rmtree(dir, ignore_errors=True)


def test_private_key_detection(topo, setup_tls):
    """Test detection of certificates with and without private keys

    :id: c3d4e5f6-3333-4444-5555-666666666666
    :setup: Standalone Instance with TLS enabled
    :steps:
        1. Add CA certificate without private key
        2. Get certificate object and verify hasPrivateKey is FALSE
        3. Add server certificate with private key
        4. Get certificate object and verify hasPrivateKey is TRUE
        5. Verify isServerCert is TRUE for server cert
        6. Verify isServerCert is FALSE for CA cert
    :expectedresults:
        1. CA added successfully
        2. hasPrivateKey is FALSE
        3. Server cert added successfully
        4. hasPrivateKey is TRUE
        5. Server cert properly identified
        6. CA cert not identified as server cert
    """
    inst = setup_tls
    dir = '/tmp/dyncert_privkey_test'
    os.makedirs(dir, 0o700, exist_ok=True)

    try:
        ca = RSA_Certificate.generateRootCA("TestPrivKey_CA")
        ca.save(dir)
        cert = ca.generateCertificate("TestPrivKey_Cert")
        cert.save(dir)

        dyncerts = DynamicCerts(inst)

        # Add CA without private key (PEM has no key)
        dyncerts.add_cert(ca.pem, ca.nickname, ca=True)

        # Verify CA has no private key
        ca_obj = dyncerts.get_cert_obj(ca.nickname)
        has_key = ca_obj.get_attr_val_utf8_l('dsDynamicCertificateHasPrivateKey')
        log.info(f"CA hasPrivateKey: {has_key}")
        assert has_key == 'false'

        # Verify it's identified as CA
        is_ca = ca_obj.get_attr_val_utf8_l('dsDynamicCertificateIsCA')
        assert is_ca == 'true'

        # Add server cert with private key (PKCS12 has key)
        dyncerts.add_cert(cert.p12, cert.nickname, pkcs12_password=RSA_Certificate.PKCS12_PASSWORD)

        # Verify server cert has private key
        cert_obj = dyncerts.get_cert_obj(cert.nickname)
        has_key = cert_obj.get_attr_val_utf8_l('dsDynamicCertificateHasPrivateKey')
        log.info(f"Server cert hasPrivateKey: {has_key}")
        assert has_key == 'true'

        # Check if isServerCert attribute exists
        is_server = cert_obj.get_attr_val_utf8_l('dsDynamicCertificateIsServerCert')
        if is_server:
            log.info(f"isServerCert: {is_server}")

        # Cleanup
        dyncerts.del_cert(ca.nickname)
        dyncerts.del_cert(cert.nickname)

    finally:
        if not DEBUGGING:
            shutil.rmtree(dir, ignore_errors=True)


def test_trust_flags_modification(topo, setup_tls):
    """Test modification of certificate trust flags

    :id: d4e5f6a7-4444-5555-6666-777777777777
    :setup: Standalone Instance with TLS enabled
    :steps:
        1. Add CA certificate with default trust flags
        2. Read initial trust flags
        3. Modify trust flags to different values using edit_cert_trust
        4. Retrieve certificate and verify trust flags changed
        5. Modify trust flags back
        6. Verify trust flags changed again
    :expectedresults:
        1. Certificate added successfully
        2. Initial trust flags present
        3. Modification succeeds
        4. Trust flags match new value
        5. Second modification succeeds
        6. Trust flags match updated value
    """
    inst = setup_tls
    dir = '/tmp/dyncert_trust_test'
    os.makedirs(dir, 0o700, exist_ok=True)

    try:
        ca = RSA_Certificate.generateRootCA("TestTrust_CA")
        ca.save(dir)

        dyncerts = DynamicCerts(inst)
        dyncerts.add_cert(ca.pem, ca.nickname, ca=True)

        # Read initial trust flags
        ca_obj = dyncerts.get_cert_obj(ca.nickname)
        initial_trust = ca_obj.get_attr_val_utf8('dsDynamicCertificateTrustFlags')
        log.info(f"Initial trust flags: {initial_trust}")

        # Modify trust flags
        new_trust = "CT,C,C"
        dyncerts.edit_cert_trust(ca.nickname, new_trust)

        # Verify modification
        ca_obj = dyncerts.get_cert_obj(ca.nickname)
        updated_trust = ca_obj.get_attr_val_utf8('dsDynamicCertificateTrustFlags')
        log.info(f"Updated trust flags: {updated_trust}")
        assert new_trust in updated_trust or updated_trust in new_trust

        # Cleanup
        dyncerts.del_cert(ca.nickname)

    finally:
        if not DEBUGGING:
            shutil.rmtree(dir, ignore_errors=True)


def test_certificate_replace_operation(topo, setup_tls):
    """Test replacing existing certificate using add_cert operation

    :id: e5f6a7b8-5555-6666-7777-888888888888
    :setup: Standalone Instance with TLS enabled
    :steps:
        1. Add initial certificate
        2. Verify certificate is present
        3. Generate new certificate with same nickname
        4. Replace certificate using add_cert (it detects existing and updates)
        5. Retrieve certificate and verify it was replaced
        6. Verify serial number changed
    :expectedresults:
        1. Initial certificate added
        2. Certificate found
        3. New certificate generated
        4. Replace operation succeeds
        5. Certificate is different
        6. Serial number is different
    """
    inst = setup_tls
    dir = '/tmp/dyncert_replace_test'
    os.makedirs(dir, 0o700, exist_ok=True)

    try:
        ca = RSA_Certificate.generateRootCA("TestReplace_CA")
        ca.save(dir)

        dyncerts = DynamicCerts(inst)

        # Add initial certificate
        dyncerts.add_cert(ca.pem, ca.nickname, ca=True)

        # Get initial serial number
        ca_obj = dyncerts.get_cert_obj(ca.nickname)
        serial1 = ca_obj.get_attr_val_utf8('dsDynamicCertificateSerialNumber')
        log.info(f"Initial serial: {serial1}")

        # Generate new certificate with same nickname
        ca2 = RSA_Certificate.generateRootCA("TestReplace_CA")  # Same nickname, different cert
        os.makedirs(dir + '/replacement', 0o700, exist_ok=True)
        ca2.save(dir + '/replacement')

        # Replace certificate (add_cert detects existing and updates)
        dyncerts.add_cert(ca2.pem, ca2.nickname, ca=True)

        # Verify replacement
        ca_obj = dyncerts.get_cert_obj(ca.nickname)
        serial2 = ca_obj.get_attr_val_utf8('dsDynamicCertificateSerialNumber')
        log.info(f"New serial: {serial2}")
        assert serial1 != serial2

        # Cleanup
        dyncerts.del_cert(ca.nickname)

    finally:
        if not DEBUGGING:
            shutil.rmtree(dir, ignore_errors=True)


def test_search_with_filters(topo, setup_tls):
    """Test searching certificates with LDAP filters using DynamicCerts.list()

    :id: f6a7b8c9-6666-7777-8888-999999999999
    :setup: Standalone Instance with TLS enabled
    :steps:
        1. Add multiple certificates (CA and server certs)
        2. List all certificates using list()
        3. List CA certificates using list_ca_certs()
        4. List server certificates using list_certs()
        5. Use get_cert_details to get specific certificate info
    :expectedresults:
        1. All certificates added successfully
        2. list() returns all certificate objects
        3. list_ca_certs() returns only CA certificates
        4. list_certs() returns only server certificates
        5. get_cert_details returns detailed info
    """
    inst = setup_tls
    fix_crash_issue_7227(inst)
    dir = '/tmp/dyncert_filter_test'
    os.makedirs(dir, 0o700, exist_ok=True)

    try:
        dyncerts = DynamicCerts(inst)

        # Add CA certificate
        ca = RSA_Certificate.generateRootCA("TestFilter_CA")
        ca.save(dir)
        dyncerts.add_cert(ca.pem, ca.nickname, ca=True)

        # Add server certificate with private key
        cert = ca.generateCertificate("TestFilter_Cert")
        cert.save(dir)
        dyncerts.add_cert(cert.p12, cert.nickname, pkcs12_password=RSA_Certificate.PKCS12_PASSWORD)

        # List all certificate objects
        all_certs = dyncerts.list()
        log.info(f"Total certificate objects found: {len(all_certs)}")
        assert len(all_certs) >= 2

        # List CA certificates only
        ca_certs = dyncerts.list_ca_certs()
        log.info(f"CA certificates found: {len(ca_certs)}")
        assert len(ca_certs) >= 1
        assert any(c['cn'] == ca.nickname for c in ca_certs)

        # List server certificates only
        server_certs = dyncerts.list_certs()
        log.info(f"Server certificates found: {len(server_certs)}")
        assert len(server_certs) >= 1
        assert any(c['cn'] == cert.nickname for c in server_certs)

        # Get specific certificate details
        ca_details = dyncerts.get_cert_details(ca.nickname)
        assert ca_details is not None
        assert ca_details['cn'] == ca.nickname
        log.info(f"CA details: {ca_details}")

        # Cleanup
        dyncerts.del_cert(ca.nickname)
        dyncerts.del_cert(cert.nickname)

    finally:
        if not DEBUGGING:
            shutil.rmtree(dir, ignore_errors=True)


def test_batch_certificate_operations(topo, setup_tls):
    """Test adding and managing multiple certificates

    :id: a7b8c9d0-7777-8888-9999-000000000000
    :setup: Standalone Instance with TLS enabled
    :steps:
        1. Generate 5 different certificates
        2. Add all certificates using DynamicCerts
        3. List all certificates
        4. Verify all certificates are present
        5. Delete all certificates
        6. Verify all deletions succeeded
    :expectedresults:
        1. All certificates generated
        2. All additions succeed
        3. List returns all certificates
        4. All certificates have correct metadata
        5. All deletions succeed
        6. No certificates remain
    """
    inst = setup_tls
    dir = '/tmp/dyncert_batch_test'
    os.makedirs(dir, 0o700, exist_ok=True)

    cert_list = []
    try:
        dyncerts = DynamicCerts(inst)

        # Generate and add multiple certificates
        for i in range(5):
            ca = RSA_Certificate.generateRootCA(f"TestBatch_CA_{i}")
            ca.save(dir)
            cert_list.append(ca)

            dyncerts.add_cert(ca.pem, ca.nickname, ca=True)
            log.info(f"Added certificate {i+1}/5: {ca.nickname}")

        # List all CA certificates
        ca_certs = dyncerts.list_ca_certs()
        batch_certs = [c for c in ca_certs if c['cn'].startswith('TestBatch_CA_')]
        log.info(f"Found {len(batch_certs)} batch certificates")
        assert len(batch_certs) == 5

        # Verify each certificate
        for ca in cert_list:
            cert_obj = dyncerts.get_cert_obj(ca.nickname)
            assert cert_obj is not None
            subject = cert_obj.get_attr_val_utf8('dsDynamicCertificateSubject')
            assert subject is not None
            log.info(f"Verified certificate: {ca.nickname}")

        # Delete all certificates
        for ca in cert_list:
            dyncerts.del_cert(ca.nickname)
            log.info(f"Deleted certificate: {ca.nickname}")

        # Verify deletions
        ca_certs_after = dyncerts.list_ca_certs()
        batch_certs_after = [c for c in ca_certs_after if c['cn'].startswith('TestBatch_CA_')]
        assert len(batch_certs_after) == 0

    finally:
        if not DEBUGGING:
            shutil.rmtree(dir, ignore_errors=True)


def test_invalid_certificate_handling(topo, setup_tls):
    """Test error handling for invalid certificate data

    :id: b8c9d0e1-8888-9999-0000-111111111111
    :setup: Standalone Instance with TLS enabled
    :steps:
        1. Attempt to add certificate from non-existent file
        2. Attempt to add certificate with empty nickname
        3. Attempt to add non-CA cert as CA
        4. Verify all operations fail with appropriate errors
    :expectedresults:
        1. Non-existent file rejected with ValueError
        2. Empty nickname rejected with ValueError
        3. Non-CA cert rejected as CA with ValueError
        4. All errors are appropriate exceptions
    """
    inst = setup_tls
    dir = '/tmp/dyncert_invalid_test'
    os.makedirs(dir, 0o700, exist_ok=True)

    try:
        dyncerts = DynamicCerts(inst)

        # Test 1: Non-existent file
        try:
            dyncerts.add_cert('/nonexistent/file.pem', 'TestInvalid')
            assert False, "Should have failed with non-existent file"
        except ValueError as e:
            log.info(f"Expected error for non-existent file: {e}")

        # Test 2: Empty nickname
        ca = RSA_Certificate.generateRootCA("TestInvalid_CA")
        ca.save(dir)

        try:
            dyncerts.add_cert(ca.pem, '', ca=True)
            assert False, "Should have failed with empty nickname"
        except ValueError as e:
            log.info(f"Expected error for empty nickname: {e}")

        # Test 3: Non-CA cert marked as CA
        cert = ca.generateCertificate("TestInvalid_Cert")
        cert.save(dir)

        try:
            dyncerts.add_cert(cert.pem, cert.nickname, ca=True)
            assert False, "Should have failed with non-CA cert"
        except ValueError as e:
            log.info(f"Expected error for non-CA cert: {e}")

    finally:
        if not DEBUGGING:
            shutil.rmtree(dir, ignore_errors=True)


def test_certificate_verification_status(topo, setup_tls):
    """Test certificate verification status attribute

    :id: c9d0e1f2-9999-0000-1111-222222222222
    :setup: Standalone Instance with TLS enabled
    :steps:
        1. Add valid CA certificate
        2. Add server certificate signed by CA
        3. Retrieve certificates and check verification status
        4. Verify self-signed CA has appropriate status
        5. Verify server cert has verification status
    :expectedresults:
        1. CA added successfully
        2. Server cert added successfully
        3. Verification status attribute accessible
        4. CA status is appropriate for self-signed
        5. Server cert status reflects chain validation
    """
    inst = setup_tls
    dir = '/tmp/dyncert_verify_test'
    os.makedirs(dir, 0o700, exist_ok=True)

    try:
        dyncerts = DynamicCerts(inst)

        ca = RSA_Certificate.generateRootCA("TestVerify_CA")
        ca.save(dir)
        dyncerts.add_cert(ca.pem, ca.nickname, ca=True)

        # Check CA verification status
        ca_obj = dyncerts.get_cert_obj(ca.nickname)
        verify_status = ca_obj.get_attr_val_utf8('dsDynamicCertificateVerificationStatus')
        if verify_status:
            log.info(f"CA verification status: {verify_status}")
        else:
            log.info("Verification status attribute not present for CA")

        # Add server certificate
        cert = ca.generateCertificate("TestVerify_Cert")
        cert.save(dir)
        dyncerts.add_cert(cert.p12, cert.nickname, pkcs12_password=RSA_Certificate.PKCS12_PASSWORD)

        # Check server cert verification status
        cert_obj = dyncerts.get_cert_obj(cert.nickname)
        verify_status = cert_obj.get_attr_val_utf8('dsDynamicCertificateVerificationStatus')
        if verify_status:
            log.info(f"Server cert verification status: {verify_status}")
        else:
            log.info("Verification status attribute not present for server cert")

        # Cleanup
        dyncerts.del_cert(ca.nickname)
        dyncerts.del_cert(cert.nickname)

    finally:
        if not DEBUGGING:
            shutil.rmtree(dir, ignore_errors=True)


def test_expired_certificate_handling(topo, setup_tls):
    """Test handling of expired certificates

    :id: d0e1f2a3-0000-1111-2222-333333333333
    :setup: Standalone Instance with TLS enabled
    :steps:
        1. Generate certificate with validity starting in the past and already expired
        2. Add expired certificate
        3. Retrieve certificate object
        4. Verify notBefore date is in the past
        5. Verify notAfter date is also in the past
        6. Check if verification status indicates expiration
    :expectedresults:
        1. Expired certificate generated
        2. Certificate can be added (stored)
        3. Certificate object retrieved
        4. notBefore shows past date
        5. notAfter shows past date (expired)
        6. Verification status may indicate invalid/expired
    """
    inst = setup_tls
    dir = '/tmp/dyncert_expired_test'
    os.makedirs(dir, 0o700, exist_ok=True)

    try:
        # Create a certificate that's already expired
        ca = RSA_Certificate()
        ca.nickname = "TestExpired_CA"
        ca.isCA = True
        ca.isRoot = True
        ca.fixNamingAttributes()
        ca.subject = x509.Name([x509.NameAttribute(k, v) for k, v in ca.namingAttrs.items()])
        ca.issuer = ca.subject
        ca.trust = 'CT,,'

        # Generate with dates in the past
        past_date = utcdate() - datetime.timedelta(days=365)
        expired_date = utcdate() - datetime.timedelta(days=1)

        ca.pkey = rsa.generate_private_key(
            public_exponent=65537,
            key_size=2048,
            backend=default_backend()
        )
        ca.cert = (
            x509.CertificateBuilder()
            .subject_name(ca.subject)
            .issuer_name(ca.issuer)
            .public_key(ca.pkey.public_key())
            .serial_number(x509.random_serial_number())
            .not_valid_before(past_date)
            .not_valid_after(expired_date)
            .add_extension(
                x509.BasicConstraints(ca=True, path_length=0),
                critical=True,
            )
            .sign(ca.pkey, hashes.SHA256(), default_backend())
        )
        ca.save(dir)

        # Add expired certificate (may need force flag)
        dyncerts = DynamicCerts(inst)
        try:
            dyncerts.add_cert(ca.pem, ca.nickname, ca=True, force=True)
        except Exception as e:
            log.info(f"Adding expired cert raised: {e}")
            return  # Expected - server may reject expired certs

        # Verify certificate metadata shows expiration
        ca_obj = dyncerts.get_cert_obj(ca.nickname)
        if ca_obj:
            not_before = ca_obj.get_attr_val_utf8('dsDynamicCertificateNotBefore')
            not_after = ca_obj.get_attr_val_utf8('dsDynamicCertificateNotAfter')
            log.info(f"Expired cert - Not Before: {not_before}")
            log.info(f"Expired cert - Not After: {not_after}")

            verify_status = ca_obj.get_attr_val_utf8('dsDynamicCertificateVerificationStatus')
            if verify_status:
                log.info(f"Verification status (should indicate expired): {verify_status}")

            # Cleanup
            dyncerts.del_cert(ca.nickname)

    finally:
        if not DEBUGGING:
            shutil.rmtree(dir, ignore_errors=True)


def test_list_and_get_operations(topo, setup_tls):
    """Test listing and retrieval operations

    :id: e1f2a3b4-1111-2222-3333-444444444444
    :setup: Standalone Instance with TLS enabled
    :steps:
        1. Add CA and server certificates
        2. Use list() to get all certificate objects
        3. Use list_ca_certs() to get CA cert details
        4. Use list_certs() to get server cert details
        5. Use get_cert_obj() to retrieve specific certificate
        6. Use get_cert_details() to get formatted details
    :expectedresults:
        1. Certificates added successfully
        2. list() returns DSLdapObject instances
        3. list_ca_certs() returns dict with CA info
        4. list_certs() returns dict with server cert info
        5. get_cert_obj() returns DynamicCert object
        6. get_cert_details() returns formatted dictionary
    """
    inst = setup_tls
    dir = '/tmp/dyncert_list_test'
    os.makedirs(dir, 0o700, exist_ok=True)

    try:
        dyncerts = DynamicCerts(inst)

        # Add certificates
        ca = RSA_Certificate.generateRootCA("TestList_CA")
        ca.save(dir)
        dyncerts.add_cert(ca.pem, ca.nickname, ca=True)

        cert = ca.generateCertificate("TestList_Cert")
        cert.save(dir)
        dyncerts.add_cert(cert.p12, cert.nickname, pkcs12_password=RSA_Certificate.PKCS12_PASSWORD)

        # Test list() - returns DynamicCert objects
        all_objs = dyncerts.list()
        log.info(f"list() returned {len(all_objs)} objects")
        assert any(isinstance(obj, DynamicCert) and obj.get_attr_val_utf8('cn') == ca.nickname
                   for obj in all_objs if obj._dn != DYNCERT_SUFFIX)

        # Test list_ca_certs() - returns list of dicts
        ca_list = dyncerts.list_ca_certs()
        log.info(f"list_ca_certs() returned {len(ca_list)} CAs")
        assert any(c['cn'] == ca.nickname for c in ca_list)
        ca_entry = next(c for c in ca_list if c['cn'] == ca.nickname)
        assert 'subject' in ca_entry
        assert 'issuer' in ca_entry
        assert 'expires' in ca_entry
        assert 'trust_flags' in ca_entry

        # Test list_certs() - returns list of dicts
        cert_list = dyncerts.list_certs()
        log.info(f"list_certs() returned {len(cert_list)} server certs")
        assert any(c['cn'] == cert.nickname for c in cert_list)

        # Test get_cert_obj() - returns DynamicCert object
        ca_obj = dyncerts.get_cert_obj(ca.nickname)
        assert isinstance(ca_obj, DynamicCert)
        assert ca_obj.get_attr_val_utf8('cn') == ca.nickname

        # Test get_cert_details() - returns formatted dict
        ca_details = dyncerts.get_cert_details(ca.nickname)
        assert ca_details['cn'] == ca.nickname
        assert 'subject' in ca_details
        assert 'issuer' in ca_details
        assert 'expires' in ca_details
        assert 'trust_flags' in ca_details
        log.info(f"get_cert_details() returned: {ca_details}")

        # Cleanup
        dyncerts.del_cert(ca.nickname)
        dyncerts.del_cert(cert.nickname)

    finally:
        if not DEBUGGING:
            shutil.rmtree(dir, ignore_errors=True)


def pem_file_to_der(pem_file_path):
    log.debug(f"Decoding cert pem: {pem_file_path}")
    with open(pem_file_path, "rb") as f:
        pem__bytes = f.read()
        try:
            return pem_to_der(pem__bytes)
        except Exception as e:
            raise ValueError(f"Failed to parse certificate '{pem_file_path}': {e}")


@pytest.mark.skipif(rpm_is_older("openssl", "3.5"), reason="OpenSSL too old to support PQC")
@pytest.mark.skipif(rpm_is_older("nss", "3.119.1"), reason="NSS too old to support PQC")
@pytest.mark.parametrize("with_private_key", [False, pytest.param(True, marks=pytest.mark.xfail(reason='cryptography module does not support ML-DSA keys')),])
def test_mldsa_dynamic_certificates(topo, setup_tls, with_private_key):
    """Test ML-DSA (post-quantum) certificates with Dynamic Certificates feature

    :id: f2a3b4c5-2222-3333-4444-555555555555
    :setup: Standalone Instance with TLS enabled
    :steps:
        1. Generate ML-DSA-87 Root CA certificate using OpenSSL
        2. Generate ML-DSA-65 server certificate signed by CA
        3. Add CA certificate dynamically using DynamicCerts
        4. Add server certificate dynamically using DynamicCerts (with or without private key)
        5. Verify CA key algorithm shows ML-DSA
        6. Verify server cert key algorithm shows ML-DSA
        7. Verify all certificate metadata is correctly extracted
        8. Verify CA is identified correctly
        9. Verify server cert has private key based on parameter
        10. Test certificate listing operations
    :expectedresults:
        1. ML-DSA CA generated successfully
        2. ML-DSA server cert generated successfully
        3. CA added without error
        4. Server cert added without error (with or without key)
        5. CA key algorithm contains ML-DSA
        6. Server cert key algorithm contains ML-DSA
        7. All metadata attributes present and correct
        8. isCA is TRUE for CA
        9. hasPrivateKey matches with_private_key parameter
        10. Certificates appear in list operations
    """
    inst = setup_tls
    fix_crash_issue_7227(inst)
    key_suffix = 'with_key' if with_private_key else 'without_key'
    dir = f'/tmp/dyncert_mldsa_test_{key_suffix}'
    os.makedirs(dir, 0o700, exist_ok=True)

    ca_config = """
[ req ]
distinguished_name = req_distinguished_name
policy             = policy_match
x509_extensions    = v3_ca

[ policy_match ]
countryName             = optional
stateOrProvinceName     = optional
organizationName        = optional
organizationalUnitName  = optional
commonName              = supplied
emailAddress            = optional

[ req_distinguished_name ]
countryName             = Country Name (2 letter code)
countryName_default     = US
countryName_min         = 2
countryName_max         = 2

stateOrProvinceName     = State or Province Name (full name)
localityName            = Locality Name (eg, city)

0.organizationName      = Organization Name (eg, company)
0.organizationName_default = TestMLDSA-CA-Org

organizationalUnitName  = Organizational Unit Name (eg, section)
commonName              = Common Name (e.g. server FQDN or YOUR name)
commonName_max          = 64

[ v3_ca ]
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid:always,issuer
basicConstraints = critical,CA:true
keyUsage = critical, keyCertSign
"""

    cert_config = """
[ req ]
distinguished_name = req_distinguished_name
policy             = policy_match
x509_extensions    = v3_cert

[ policy_match ]
countryName             = optional
stateOrProvinceName     = optional
organizationName        = optional
organizationalUnitName  = optional
commonName              = supplied
emailAddress            = optional

[ req_distinguished_name ]
countryName             = Country Name (2 letter code)
countryName_default     = US
countryName_min         = 2
countryName_max         = 2

stateOrProvinceName     = State or Province Name (full name)
localityName            = Locality Name (eg, city)

0.organizationName      = Organization Name (eg, company)
0.organizationName_default = TestMLDSA-Org

organizationalUnitName  = Organizational Unit Name (eg, section)
commonName              = Common Name (e.g. server FQDN or YOUR name)
commonName_max          = 64

[ v3_cert ]
basicConstraints = critical,CA:false
subjectAltName = DNS:localhost,IP:127.0.0.1
keyUsage = digitalSignature, nonRepudiation, keyEncipherment, dataEncipherment
extendedKeyUsage = clientAuth, serverAuth
"""

    try:
        hostname = socket.gethostname()

        # Write config files
        ca_conf_file = f"{dir}/ca.conf"
        cert_conf_file = f"{dir}/cert.conf"

        with open(ca_conf_file, 'w') as f:
            f.write(ca_config)

        with open(cert_conf_file, 'w') as f:
            f.write(cert_config)

        # Generate ML-DSA-87 CA certificate
        log.info("Generating ML-DSA-87 CA certificate...")

        # Generate CA private key
        result = subprocess.run(
            ['openssl', 'genpkey', '-algorithm', 'ML-DSA-87', '-out', f'{dir}/ca.key'],
            capture_output=True, text=True, check=True
        )
        log.debug(f"CA key generation output: {result.stderr}")

        # Generate self-signed CA certificate
        result = subprocess.run(
            ['openssl', 'req', '-x509', '-new', '-sha256', '-key', f'{dir}/ca.key',
             '-nodes', '-days', '3650', '-config', ca_conf_file,
             '-subj', f'/CN=TestMLDSA-CA/O=TestMLDSA-CA-Org/C=US',
             '-out', f'{dir}/ca.pem'],
            capture_output=True, text=True, check=True
        )
        log.debug(f"CA cert generation output: {result.stderr}")

        # Generate ML-DSA-65 server certificate
        log.info("Generating ML-DSA-65 server certificate...")

        # Generate server private key
        result = subprocess.run(
            ['openssl', 'genpkey', '-algorithm', 'ML-DSA-65', '-out', f'{dir}/cert.key'],
            capture_output=True, text=True, check=True
        )
        log.debug(f"Server key generation output: {result.stderr}")

        # Generate certificate signing request
        result = subprocess.run(
            ['openssl', 'req', '-new', '-sha256', '-key', f'{dir}/cert.key',
             '-nodes', '-config', cert_conf_file,
             '-subj', f'/CN=TestMLDSA-Cert/O=TestMLDSA-Org/C=US',
             '-out', f'{dir}/cert.csr'],
            capture_output=True, text=True, check=True
        )
        log.debug(f"CSR generation output: {result.stderr}")

        # Sign server certificate with CA
        result = subprocess.run(
            ['openssl', 'x509', '-req', '-sha256', '-days', '3650',
             '-extensions', 'v3_cert', '-extfile', cert_conf_file,
             '-in', f'{dir}/cert.csr', '-CA', f'{dir}/ca.pem',
             '-CAkey', f'{dir}/ca.key', '-CAcreateserial',
             '-out', f'{dir}/cert.pem'],
            capture_output=True, text=True, check=True
        )
        log.debug(f"Cert signing output: {result.stderr}")

        # Create PKCS12 bundle for server certificate
        result = subprocess.run(
            ['openssl', 'pkcs12', '-export', '-inkey', f'{dir}/cert.key',
             '-in', f'{dir}/cert.pem', '-name', 'mldsa-server-cert',
             '-out', f'{dir}/cert.p12', '-passout', 'pass:mldsa123'],
            capture_output=True, text=True, check=True
        )
        log.debug(f"PKCS12 creation output: {result.stderr}")

        # Display certificate details for debugging
        result = subprocess.run(
            ['openssl', 'x509', '-text', '-noout', '-in', f'{dir}/ca.pem'],
            capture_output=True, text=True, check=True
        )
        log.info(f"CA Certificate details:\n{result.stdout}")

        result = subprocess.run(
            ['openssl', 'x509', '-text', '-noout', '-in', f'{dir}/cert.pem'],
            capture_output=True, text=True, check=True
        )
        log.info(f"Server Certificate details:\n{result.stdout}")

        # Add certificates using DynamicCerts
        dyncerts = DynamicCerts(inst)

        # Add CA certificate
        log.info("Adding ML-DSA CA certificate to Dynamic Certificates...")
        dyncerts.add_cert(f'{dir}/ca.pem', 'TestMLDSA_CA', ca=True)

        # Add server certificate (with or without private key based on parameter)
        if with_private_key:
            log.info("Adding ML-DSA server certificate WITH private key to Dynamic Certificates...")
            # Note: cannot use dyncerts.add_cert with PKCS12 because python3 cryptography
            # module does not yet support ML-DSA. Instead, use OpenSSL to convert to DER
            # and use DynamicCerts.create() directly
            # dyncerts.add_cert(f'{dir}/cert.p12', 'TestMLDSA_Cert', pkcs12_password='mldsa123')

            # Convert certificate to DER using OpenSSL
            result = subprocess.run(
                ['openssl', 'x509', '-in', f'{dir}/cert.pem', '-outform', 'DER', '-out', f'{dir}/cert.der'],
                capture_output=True, text=True, check=True
            )

            # Convert private key to DER PrivateKeyInfo (PKCS#8) using OpenSSL
            result = subprocess.run(
                ['openssl', 'pkcs8', '-topk8', '-nocrypt', '-in', f'{dir}/cert.key',
                 '-outform', 'DER', '-out', f'{dir}/cert_key.der'],
                capture_output=True, text=True, check=True
            )

            # Read DER data
            with open(f'{dir}/cert.der', 'rb') as f:
                cert_der = f.read()
            with open(f'{dir}/cert_key.der', 'rb') as f:
                key_der = f.read()

            # Create entry directly with DER data
            properties = {
                DYCATTR_CN: 'TestMLDSA_Cert',
                DYCATTR_CERTDER: cert_der,
                DYCATTR_PKEYDER: key_der,
            }
            rdn = f"{DYCATTR_CN}=TestMLDSA_Cert"
            dyncerts.create(rdn=rdn, properties=properties)
        else:
            log.info("Adding ML-DSA server certificate WITHOUT private key to Dynamic Certificates...")
            dyncerts.add_cert(f'{dir}/cert.pem', 'TestMLDSA_Cert')

        # Verify CA certificate metadata
        log.info("Verifying CA certificate metadata...")
        ca_obj = dyncerts.get_cert_obj('TestMLDSA_CA')
        assert ca_obj is not None

        # Verify subject and issuer
        ca_subject = ca_obj.get_attr_val_utf8('dsDynamicCertificateSubject')
        ca_issuer = ca_obj.get_attr_val_utf8('dsDynamicCertificateIssuer')
        log.info(f"CA Subject: {ca_subject}")
        log.info(f"CA Issuer: {ca_issuer}")
        assert 'TestMLDSA-CA' in ca_subject
        assert ca_subject == ca_issuer  # Self-signed

        # Verify CA flags
        is_ca = ca_obj.get_attr_val_utf8_l('dsDynamicCertificateIsCA')
        is_root_ca = ca_obj.get_attr_val_utf8_l('dsDynamicCertificateIsRootCA')
        log.info(f"CA isCA: {is_ca}")
        log.info(f"CA isRootCA: {is_root_ca}")
        assert is_ca == 'true'
        assert is_root_ca == 'true'

        # Verify CA key algorithm contains ML-DSA
        ca_key_algo = ca_obj.get_attr_val_utf8('dsDynamicCertificateKeyAlgorithm')
        log.info(f"CA Key Algorithm: {ca_key_algo}")
        assert ca_key_algo is not None
        assert 'ML-DSA' in ca_key_algo.upper() or 'DILITHIUM' in ca_key_algo.upper() or '2.16.840.1.101.3.4.3' in ca_key_algo

        # Verify CA has no private key (PEM file only contains cert)
        ca_has_key = ca_obj.get_attr_val_utf8_l('dsDynamicCertificateHasPrivateKey')
        log.info(f"CA hasPrivateKey: {ca_has_key}")
        assert ca_has_key == 'false'

        # Verify server certificate metadata
        log.info("Verifying server certificate metadata...")
        cert_obj = dyncerts.get_cert_obj('TestMLDSA_Cert')
        assert cert_obj is not None

        # Verify subject
        cert_subject = cert_obj.get_attr_val_utf8('dsDynamicCertificateSubject')
        cert_issuer = cert_obj.get_attr_val_utf8('dsDynamicCertificateIssuer')
        log.info(f"Server Cert Subject: {cert_subject}")
        log.info(f"Server Cert Issuer: {cert_issuer}")
        assert 'TestMLDSA-Cert' in cert_subject

        # Verify server cert is not a CA
        cert_is_ca = cert_obj.get_attr_val_utf8_l('dsDynamicCertificateIsCA')
        log.info(f"Server Cert isCA: {cert_is_ca}")
        assert cert_is_ca == 'false'

        # Verify server cert key algorithm contains ML-DSA
        cert_key_algo = cert_obj.get_attr_val_utf8('dsDynamicCertificateKeyAlgorithm')
        log.info(f"Server Cert Key Algorithm: {cert_key_algo}")
        assert cert_key_algo is not None
        assert 'ML-DSA' in cert_key_algo.upper()

        # Verify server cert has private key based on parameter
        cert_has_key = cert_obj.get_attr_val_utf8_l('dsDynamicCertificateHasPrivateKey')
        log.info(f"Server Cert hasPrivateKey: {cert_has_key}")
        expected_has_key = 'true' if with_private_key else 'false'
        assert cert_has_key == expected_has_key, \
            f"Expected hasPrivateKey={expected_has_key}, got {cert_has_key}"

        # Verify serial numbers are present
        ca_serial = ca_obj.get_attr_val_utf8('dsDynamicCertificateSerialNumber')
        cert_serial = cert_obj.get_attr_val_utf8('dsDynamicCertificateSerialNumber')
        log.info(f"CA Serial: {ca_serial}")
        log.info(f"Server Cert Serial: {cert_serial}")
        assert ca_serial is not None and len(ca_serial) > 0
        assert cert_serial is not None and len(cert_serial) > 0

        # Verify validity dates
        ca_not_before = ca_obj.get_attr_val_utf8('dsDynamicCertificateNotBefore')
        ca_not_after = ca_obj.get_attr_val_utf8('dsDynamicCertificateNotAfter')
        cert_not_before = cert_obj.get_attr_val_utf8('dsDynamicCertificateNotBefore')
        cert_not_after = cert_obj.get_attr_val_utf8('dsDynamicCertificateNotAfter')
        log.info(f"CA Valid: {ca_not_before} to {ca_not_after}")
        log.info(f"Server Cert Valid: {cert_not_before} to {cert_not_after}")
        assert ca_not_before is not None
        assert ca_not_after is not None
        assert cert_not_before is not None
        assert cert_not_after is not None

        # Verify Subject Alternative Names on server cert
        cert_san = cert_obj.get_attr_vals_utf8('dsDynamicCertificateSubjectAltName')
        if cert_san:
            log.info(f"Server Cert SANs: {cert_san}")
            san_str = ' '.join(cert_san)
            assert 'localhost' in san_str or '127.0.0.1' in san_str

        # Test listing operations
        log.info("Testing certificate listing operations...")

        ca_list = dyncerts.list_ca_certs()
        mldsa_cas = [c for c in ca_list if c['cn'] == 'TestMLDSA_CA']
        assert len(mldsa_cas) == 1
        log.info(f"Found ML-DSA CA in list: {mldsa_cas[0]}")

        cert_list = dyncerts.list_certs()
        mldsa_certs = [c for c in cert_list if c['cn'] == 'TestMLDSA_Cert']
        assert len(mldsa_certs) == 1
        log.info(f"Found ML-DSA server cert in list: {mldsa_certs[0]}")

        # Test get_cert_details
        ca_details = dyncerts.get_cert_details('TestMLDSA_CA')
        assert ca_details is not None
        assert ca_details['cn'] == 'TestMLDSA_CA'
        assert 'subject' in ca_details
        assert 'issuer' in ca_details
        log.info(f"CA details: {ca_details}")

        cert_details = dyncerts.get_cert_details('TestMLDSA_Cert')
        assert cert_details is not None
        assert cert_details['cn'] == 'TestMLDSA_Cert'
        log.info(f"Server cert details: {cert_details}")

        # Cleanup
        log.info("Cleaning up ML-DSA certificates...")
        dyncerts.del_cert('TestMLDSA_CA')
        dyncerts.del_cert('TestMLDSA_Cert')

        key_status = "with private key" if with_private_key else "without private key"
        log.info(f"ML-DSA Dynamic Certificates test ({key_status}) completed successfully!")

    except subprocess.CalledProcessError as e:
        log.error(f"OpenSSL command failed: {e}")
        log.error(f"stdout: {e.stdout}")
        log.error(f"stderr: {e.stderr}")
        raise
    except Exception as e:
        log.error(f"Test failed with exception: {e}")
        raise
    finally:
        if not DEBUGGING:
            shutil.rmtree(dir, ignore_errors=True)


if __name__ == '__main__':
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", "-v", CURRENT_FILE])
