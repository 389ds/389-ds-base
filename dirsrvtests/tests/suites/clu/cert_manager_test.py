# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import os
import io
import sys
import logging
import tempfile
from contextlib import suppress
from subprocess import run
import pytest
import ldap
import ldapurl

from lib389._constants import DEFAULT_SUFFIX
from lib389.config import CertmapLegacy
from lib389.nss_ssl import NssSsl
from lib389.dyncerts import DynamicCerts
from lib389.cert_manager import CertManager
from lib389.topologies import topology_st as topo
from lib389.dseldif import DSEldif
from lib389.utils import ensure_str, RSACertificate
from lib389.cli_base import LogCapture, FakeArgs
from lib389.cli_conf.security import cert_add, cert_list, cert_del, cert_edit

log = logging.getLogger(__name__)

def _open_ldapi_conn(inst):
    dse = DSEldif(inst)
    ldapi_socket = dse.get('cn=config', 'nsslapd-ldapifilepath', single=True)
    url = f"ldapi://{ldapurl.ldapUrlEscape(ensure_str(ldapi_socket))}"
    ld = ldap.initialize(url)
    sasl_auth = ldap.sasl.external()
    log.debug(f'Connecting to {url} using sasl external authentication')
    ld.sasl_interactive_bind_s("", sasl_auth)
    return ld

def setSslPersonality(inst, nickname):
    ldc = _open_ldapi_conn(inst)
    dn = 'cn=RSA,cn=encryption,cn=config'
    mods = [(2, 'nsSSLPersonalitySSL', [nickname.encode()])]
    ldc.modify_s(dn, mods)

def clear_nss_db(inst):
    db_path = inst.get_cert_dir()
    pw_file = os.path.join(db_path, "pwdfile.txt")

    inst.stop()
    for f in ('cert9.db', 'key4.db'):
        with suppress(OSError):
            os.remove(f'{db_path}/{f}')

    run(['certutil', '-N', '-d', db_path, '-f', pw_file, '-@', pw_file], check=True)

def offline_install(inst, nickname, pem=None, p12=None, p12_password=None, is_ca=False):
    db_path = inst.get_cert_dir()
    pw_file = os.path.join(db_path, "pwdfile.txt")

    if is_ca:
        if pem is None:
            raise ValueError("PEM file must be provided for CA certificate installation")
        run([
            'certutil',
            '-A',
            '-n', nickname,
            '-t', 'CT,,',
            '-f', pw_file,
            '-d', db_path,
            '-a',
            '-i', pem
        ], check=True)
    else:
        if p12 is None or p12_password is None:
            raise ValueError("PKCS#12 file and password must be provided for server certificate installation")
        run([
            'pk12util',
            '-v',
            '-i', p12,
            '-d', db_path,
            '-k', pw_file,
            '-W', p12_password
        ], check=True)

@pytest.fixture(params=["DynamicCerts", "NssSsl"])
def cert_manager(request, topo):
    inst = topo.standalone
    if request.param == "DynamicCerts":
        backend = DynamicCerts(inst)
    else:
        backend = NssSsl(inst)
    return CertManager(inst, backend=backend)

@pytest.fixture(scope="module")
def cert_setup(topo, request):
    """
    Generate Root CA
    Create Server RSA cert and sign with CA cert
    Set SSL personality
    Clears NSS DB
    Do an offline install of CA and server certs
    Restarts instance
    """
    tmpdir = tempfile.mkdtemp(prefix="certs_")
    inst = topo.standalone

    inst.enable_tls()

    # Create Root CA
    ca = RSACertificate.generateRootCA("ROOT_CA")
    ca.save(tmpdir)

    # Create server cert signed by CA
    server_cert = ca.generateServerCert("SERVER_CERT", ca=ca)
    server_cert.save(tmpdir)

    # Set SSL personality
    setSslPersonality(inst, server_cert.nickname)

    # Clear NSS DB
    clear_nss_db(inst)

    # Offline install
    offline_install(inst, nickname=ca.nickname, pem=ca.pem, is_ca=True)
    offline_install(inst, nickname=server_cert.nickname,p12=server_cert.p12,
                    p12_password=server_cert.P12_PASSWORD, is_ca=False)

    # Restart instance
    inst.restart(post_open=False)

    # Cleanup
    def fin():
        with suppress(FileNotFoundError):
            import shutil
            shutil.rmtree(tmpdir)
    request.addfinalizer(fin)

    return {
        "ca": ca,
        "server_cert": server_cert,
        "dir": tmpdir
    }

def test_add_delete_pem_certmanager(cert_setup, cert_manager, tmp_path):
    """
    Add and delete a PEM cert.

    This test validates that both backends supported by the
    CertManager abstraction, DynamicCerts and NssSsl, correctly
    import, list and delete a PEM cert.

    The cert_manager fixture parametrises the test, running the
    same logic against each backend through the CertManager layer.

    :id: 0704d89e-da60-4f14-8261-ba49e7a5d1cc
    :setup: Standalone instance with RSACertificate cert class
    :steps:
        1. Generate a new PEM cert signed by the test CA.
        2. Add the cert.
        3. Verify the cert can be retrieved.
        4. Delete the cert.
        5. Verify the cert has been removed.
    :expectedresults:
        1. Success.
        2. Success.
        3. Success.
        4. Success.
        5. Success.
    """
    ca = cert_setup["ca"]
    manager = cert_manager
    nickname = "TEST_SERVER_PEM"

    # Generate a new server cert signed by the CA
    server_cert = ca.generateServerCert(nickname, ca=ca)
    server_cert.save(tmp_path)

    # Add certificate
    manager.add_cert(str(server_cert.pem), nickname=nickname, pkcs12_file=False)

    # Verify the cert exists
    cert_details = manager.get_cert_details(nickname)
    assert cert_details is not None

    # Delete and verify removal
    manager.del_cert(nickname)
    cert_details = manager.get_cert_details(nickname)
    assert cert_details is None

def test_add_delete_der_certmanager(cert_setup, cert_manager, tmp_path):
    """
    Add and delete a DER cert.

    This test validates that both backends supported by the
    CertManager abstraction, DynamicCerts and NssSsl, correctly
    import, list and delete a DER cert.

    The cert_manager fixture parametrises the test, running the
    same logic against each backend through the CertManager layer.

    :id: e95d3cd9-baab-4a0f-9f88-e9f3286c3f04
    :setup: Standalone instance with RSACertificate cert class
    :steps:
        1. Generate a new DER cert signed by the test CA.
        2. Add the cert.
        3. Verify the cert can be retrieved.
        4. Delete the cert.
        5. Verify the cert has been removed.
    :expectedresults:
        1. Success.
        2. Success.
        3. Success.
        4. Success.
        5. Success.
    """
    ca = cert_setup["ca"]
    manager = cert_manager
    nickname = "TEST_SERVER_DER"

    # Generate a new server cert signed by the CA
    server_cert = ca.generateServerCert(nickname, ca=ca)
    server_cert.save(tmp_path)

    # Add certificate
    manager.add_cert(str(server_cert.der), nickname=nickname, pkcs12_file=False)

    # Verify the cert exists
    cert_details = manager.get_cert_details(nickname)
    assert cert_details is not None

    # Delete and verify removal
    manager.del_cert(nickname)
    cert_details = manager.get_cert_details(nickname)
    assert cert_details is None

def test_add_delete_pkcs12_certmanager(cert_setup, cert_manager, tmp_path):
    """
    Add and delete a PKCS#12 container with cert and private key.

    This test validates that both backends supported by the
    CertManager abstraction, DynamicCerts and NssSsl, correctly
    import a PKCS#12 container, list and delete the cert.

    The cert_manager fixture parametrises the test, running the
    same logic against each backend through the CertManager layer.

    :id: b12dbf9b-7506-4040-b47c-b2b7c4e30ed4
    :setup: Standalone instance with RSACertificate cert class
    :steps:
        1. Generate a new server cert signed by the test CA.
        2. Add the cert from a PKCS#12 container.
        3. Verify the cert can be retrieved.
        4. Delete the cert.
        5. Verify the cert has been removed.
    :expectedresults:
        1. Success.
        2. Success.
        3. Success.
        4. Success.
        5. Success.
    """

    ca = cert_setup["ca"]
    manager = cert_manager
    nickname = "TEST_SERVER_P12"

    # Generate a new server cert signed by the CA
    server_cert = ca.generateServerCert(nickname, ca=ca)
    server_cert.save(tmp_path)  # This writes PEM, DER, and P12 files

    # Add certificate from PKCS#12 container
    manager.add_cert(
        str(server_cert.p12),
        nickname=nickname,
        pkcs12_file=True,
        pkcs12_password=server_cert.P12_PASSWORD
    )

    # Verify the cert exists
    cert_details = manager.get_cert_details(nickname)
    assert cert_details is not None

    # Delete the cert and verify removal
    manager.del_cert(nickname)
    cert_details = manager.get_cert_details(nickname)
    assert cert_details is None


def test_add_delete_pkcs12_security_text(topo, cert_setup, tmp_path):
    """
    Add and delete a PKCS#12 container, password supplied via plaintext.

    Validates the user facing cert CLI (cert_add, cert_list, etc).
    This test verifies that a PKCS#12 container containing a cert
    and private key can be imported when the password is supplied
    directly as text, and that the cert can be listed and deleted.

    Under the hood, the test goes through the CertManager abstraction
    layer and interacts with the NSS and DynamicCerts backends.

    :id: 6e17cc36-ce9b-4506-b5f7-cc94db35a076
    :setup: Standalone instance with RSACertificate cert class
    :steps:
        1. Generate a new server certificate signed by the test CA.
        2. Add the cert from a PKCS#12 container using plaintext
           password.
        3. Verify the cert can be listed.
        4. Delete the cert.
        5. Verify the cert can not be listed.
    :expectedresults:
        1. Success.
        2. Success.
        3. Success.
        4. Success.
        5. Success.
    """
    inst = topo.standalone
    lc = LogCapture()
    ca = cert_setup["ca"]
    nickname = "TEST_SERVER_P12_TEXT"

    # Generate server cert signed by CA
    server_cert = ca.generateServerCert(nickname, ca=ca)
    server_cert.save(str(tmp_path))

    args = FakeArgs()
    args.name = server_cert.nickname
    args.file = server_cert.p12
    args.pkcs12_pin_text = server_cert.P12_PASSWORD  # Provide password as text
    args.pkcs12_pin_stdin = False
    args.pkcs12_pin_path = None
    args.primary_cert = False
    args.force = False

    # Add certificate
    cert_add(inst, DEFAULT_SUFFIX, lc.log, args)

    # Verify certificate exists
    lc.flush()
    args = FakeArgs()
    args.json = False
    cert_list(inst, DEFAULT_SUFFIX, lc.log, args)
    assert lc.contains(nickname)

    # Delete the certificate
    lc.flush()
    args = FakeArgs()
    args.name = server_cert.nickname
    cert_del(inst, DEFAULT_SUFFIX, lc.log, args)

    # Verify deletion
    lc.flush()
    args = FakeArgs()
    args.json = False
    cert_list(inst, DEFAULT_SUFFIX, lc.log, args)
    assert not lc.contains(nickname)

def test_add_delete_pkcs12_security_stdin(topo, cert_setup, tmp_path):
    """
    Add and delete a PKCS#12 container, password supplied via stdin.

    Validates the user facing cert CLI (cert_add, cert_list, etc).
    This test verifies that a PKCS#12 container containing a cert
    and private key can be imported when the password is supplied
    through standard input, and that the cert can be listed and deleted.

    Under the hood, the test goes through the CertManager abstraction
    layer and interacts with the NSS and DynamicCerts backends.

    :id: 490e8ccc-a711-4ea9-b115-734d0c2c931d
    :setup: Standalone instance with RSACertificate cert class
    :steps:
        1. Generate a new server certificate signed by the test CA.
        2. Add the cert from a PKCS#12 container using a password
           read from stdin.
        3. Verify the cert can be listed.
        4. Delete the cert.
        5. Verify the cert can not be listed.
    :expectedresults:
        1. Success.
        2. Success.
        3. Success.
        4. Success.
        5. Success.
    """
    inst = topo.standalone
    lc = LogCapture()
    ca = cert_setup["ca"]
    nickname = "TEST_SERVER_P12_STDIN"

    # Generate server cert signed by CA
    server_cert = ca.generateServerCert(nickname, ca=ca)
    server_cert.save(str(tmp_path))

    args = FakeArgs()
    args.name = server_cert.nickname
    args.file = server_cert.p12
    args.pkcs12_pin_text = None
    args.pkcs12_pin_stdin = True
    args.pkcs12_pin_path = None
    args.primary_cert = False
    args.force = False

    stdin_backup = sys.stdin
    try:
        sys.stdin = io.StringIO(server_cert.P12_PASSWORD + "\n")
        cert_add(inst, DEFAULT_SUFFIX, lc.log, args)
    finally:
        sys.stdin = stdin_backup

    # Verify certificate exists
    lc.flush()
    args = FakeArgs()
    args.json = False
    cert_list(inst, DEFAULT_SUFFIX, lc.log, args)
    assert lc.contains(nickname)

    # Delete the certificate
    lc.flush()
    args = FakeArgs()
    args.name = server_cert.nickname
    cert_del(inst, DEFAULT_SUFFIX, lc.log, args)

    # Verify deletion
    lc.flush()
    args = FakeArgs()
    args.json = False
    cert_list(inst, DEFAULT_SUFFIX, lc.log, args)
    assert not lc.contains(nickname)

def test_add_delete_pkcs12_security_file(topo, cert_setup, tmp_path):
    """
    Add and delete a PKCS#12 container, password supplied via file.

    Validates the user facing cert CLI (cert_add, cert_list, etc).
    This test verifies that a PKCS#12 container containing a cert
    and private key can be imported when the password is supplied
    via a file, and that the cert can be listed and deleted.

    Under the hood, the test goes through the CertManager abstraction
    layer and interacts with the NSS and DynamicCerts backends.

    :id: 1784c258-db09-4d12-ad96-a6ff0643860a
    :setup: Standalone instance with RSACertificate cert class
    :steps:
        1. Generate a new server certificate signed by the test CA.
        2. Add the cert from a PKCS#12 container using a password
           read from a file.
        3. Verify the cert can be listed.
        4. Delete the cert.
        5. Verify the cert can not be listed.
    :expectedresults:
        1. Success.
        2. Success.
        3. Success.
        4. Success.
        5. Success.
    """
    inst = topo.standalone
    lc = LogCapture()
    ca = cert_setup["ca"]
    nickname = "TEST_SERVER_P12_FILE"

    # Generate server cert signed by CA
    server_cert = ca.generateServerCert(nickname, ca=ca)
    server_cert.save(str(tmp_path))

    # Write password to a file
    pwd_file = tmp_path / "p12_password.txt"
    pwd_file.write_text(server_cert.P12_PASSWORD)

    args = FakeArgs()
    args.name = server_cert.nickname
    args.file = server_cert.p12
    args.pkcs12_pin_text = None
    args.pkcs12_pin_stdin = False
    args.pkcs12_pin_path = str(pwd_file)  # Path to password file
    args.primary_cert = False
    args.force = False

    # Add certificate
    cert_add(inst, DEFAULT_SUFFIX, lc.log, args)

    # Verify certificate exists
    lc.flush()
    args = FakeArgs()
    args.json = False
    cert_list(inst, DEFAULT_SUFFIX, lc.log, args)
    assert lc.contains(nickname)

    # Delete the certificate
    lc.flush()
    args = FakeArgs()
    args.name = server_cert.nickname
    cert_del(inst, DEFAULT_SUFFIX, lc.log, args)

    # Verify deletion
    lc.flush()
    args = FakeArgs()
    args.json = False
    cert_list(inst, DEFAULT_SUFFIX, lc.log, args)
    assert not lc.contains(nickname)

def test_add_delete_pem_force(topo, cert_setup, tmp_path):
    """
    Test adding and deleting a server certificate in PEM format using the
    --do-it (force) option.

    Validates the user facing cert CLI (cert_add, cert_list, etc)
    for importing an unsigned PEM certificate, bypassing chain
    verification via the '--do-it' flag. The test ensures that the
    unsigned cert can be added, and that the cert can be listed and deleted.

    Under the hood, the test goes through the CertManager abstraction
    layer and interacts with the NSS and DynamicCerts backends.

    :id: 07104ff3-86b7-4821-b055-954dd527fe48
    :setup: Standalone instance with RSACertificate cert class
    :steps:
        1. Generate a new server certificate, NOT signed by the test CA.
        2. Add the cert without force flag
        3. Add the cert with force flag
        4. Verify the cert can be listed.
        5. Delete the cert.
        6. Verify the cert can not be listed.
    :expectedresults:
        1. Success.
        2. Returns UNWILLING_TO_PERFORM.
        3. Success.
        4. Success.
        5. Success.
        6. Success.
    """
    inst = topo.standalone
    lc = LogCapture()
    ca = cert_setup["ca"]
    nickname = "TEST_SERVER_PEM_FORCE"

    # Generate server cert, NOT signed by CA
    server_cert = ca.generateServerCert(nickname)
    server_cert.save(str(tmp_path))

    args = FakeArgs()
    args.name = server_cert.nickname
    args.file = server_cert.pem
    args.pkcs12_pin_text = None
    args.pkcs12_pin_stdin = None
    args.pkcs12_pin_path = None
    args.primary_cert = False
    args.force = False

    # Ensure the adding of an unsigned cert returns error
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        cert_add(inst, DEFAULT_SUFFIX, lc.log, args)

    # Force add an unsigned cert
    lc.flush()
    args.force = True
    cert_add(inst, DEFAULT_SUFFIX, lc.log, args)

    # Verify certificate exists
    lc.flush()
    args = FakeArgs()
    args.json = False
    cert_list(inst, DEFAULT_SUFFIX, lc.log, args)
    assert lc.contains(nickname)

    # Delete the certificate
    lc.flush()
    args = FakeArgs()
    args.name = server_cert.nickname
    cert_del(inst, DEFAULT_SUFFIX, lc.log, args)

    # Verify deletion
    lc.flush()
    args = FakeArgs()
    args.json = False
    cert_list(inst, DEFAULT_SUFFIX, lc.log, args)
    assert not lc.contains(nickname)

def test_edit_cert_trust_flags(topo, cert_setup, tmp_path):
    """
    Edit trust flags on an existing certificate.

    Validates the user facing cert CLI (cert_add, cert_edit_trust, cert_list)
    via CertManager. Ensures trust flags can be set and removed.

    Validates the user facing cert CLI (cert_add, cert_list, etc)
    for editing an existing certifcates trust flags.

    Under the hood, the test goes through the CertManager abstraction
    layer and interacts with the NSS and DynamicCerts backends.

    :id: TODO
    :setup: Standalone instance with RSACertificate cert class
    :steps:
        1. Generate a new server certificate and add it.
        2. Edit the trust flags to 'CT,,'
        3. Verify the certificate listing shows updated trust.
        4. Edit the trust flags to ',,'
        5. Verify the certificate listing shows cleared trust.
        6. Delete the certificate.
    :expectedresults:
        1. Certificate added successfully
        2. Trust flags updated successfully
        3. Trust flags match 'CT,,'
        4. Trust flags cleared successfully
        5. Trust flags are empty
        6. Certificate deleted successfully
    """
    inst = topo.standalone
    lc = LogCapture()
    ca = cert_setup["ca"]
    nickname = "TEST_SERVER_TRUST"

    # Generate server cert signed by CA
    server_cert = ca.generateServerCert(nickname, ca=ca)
    server_cert.save(str(tmp_path))

    args = FakeArgs()
    args.name = server_cert.nickname
    args.file = server_cert.pem
    args.pkcs12_pin_text = None
    args.pkcs12_pin_stdin = None
    args.pkcs12_pin_path = None
    args.primary_cert = False
    args.force = False

    cert_add(inst, DEFAULT_SUFFIX, lc.log, args)

    # Edit trust flags to 'CT,,' (trusted CA/SSL)
    lc.flush()
    args = FakeArgs()
    args.name = nickname
    args.flags = "CT,,"
    cert_edit(inst, DEFAULT_SUFFIX, lc.log, args)

    # Verify certificate shows updated trust
    lc.flush()
    args = FakeArgs()
    args.json = False
    cert_list(inst, DEFAULT_SUFFIX, lc.log, args)
    assert lc.contains(nickname)
    assert lc.contains("CT,,")

    # Edit trust flags to ',,' (untrustworthy)
    lc.flush()
    args = FakeArgs()
    args.name = nickname
    args.flags = ",,"
    cert_edit(inst, DEFAULT_SUFFIX, lc.log, args)

    # Verify certificate shows updated trust
    lc.flush()
    args = FakeArgs()
    args.json = False
    cert_list(inst, DEFAULT_SUFFIX, lc.log, args)
    assert lc.contains(nickname)
    assert lc.contains(",,")

    # Step 6: Delete the certificate
    lc.flush()
    args = FakeArgs()
    args.name = nickname
    cert_del(inst, DEFAULT_SUFFIX, lc.log, args)

    # Verify deletion
    lc.flush()
    args = FakeArgs()
    args.json = False
    cert_list(inst, DEFAULT_SUFFIX, lc.log, args)
    assert not lc.contains(nickname)