# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import datetime
import shutil
import logging
import pytest
import subprocess
import tempfile
import ssl
import os
import re
from pathlib import Path
from lib389.topologies import topology_st as topo
from lib389.nss_ssl import NssSsl
from cryptography import x509
from cryptography.x509.oid import NameOID
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.hazmat.primitives import serialization, hashes

log = logging.getLogger(__name__)

HERE = Path(__file__).parent
DATA = HERE / "data"
PEM_CA = DATA / "ca.pem"
PEM_SERVER = DATA / "server.pem"
DER_SERVER = DATA / "server.der"
P12_SERVER = DATA / "server.p12"
P12_PASSWORD = "p12password"
DB_PASSWORD = "dbpassword"

@pytest.fixture(scope='function')
# @pytest.fixture(scope='session')
def temp_nss_db():
    tmpdir = tempfile.mkdtemp()
    subprocess.check_call(["certutil", "-N", "-d", tmpdir, "--empty-password"])
    test_files = []
    try:
        yield tmpdir, test_files
    finally:
        for f in test_files:
            if os.path.exists(f):
                os.unlink(f)
        shutil.rmtree(tmpdir, ignore_errors=True)

def list_nicknames(db):
    out = subprocess.check_output(["certutil", "-L", "-d", db]).decode()
    lines = out.split("\n")[4:-1]
    nicks = []
    for line in lines:
        if not line.strip():
            continue
        parts = line.split()
        nicks.append(" ".join(parts[:-1]))
    return nicks

def generate_test_cert(nickname="TEST_CERT", ca=False, p12_password=b"password"):
    """Generate a test certificate in PEM, DER, and PKCS#12 formats."""

    # Generate key
    key = rsa.generate_private_key(public_exponent=65537, key_size=2048)

    # Generate certificate
    subject = issuer = x509.Name([
        x509.NameAttribute(NameOID.COUNTRY_NAME, "US"),
        x509.NameAttribute(NameOID.ORGANIZATION_NAME, "Test Org"),
        x509.NameAttribute(NameOID.COMMON_NAME, nickname),
    ])
    cert = x509.CertificateBuilder()\
        .subject_name(subject)\
        .issuer_name(issuer)\
        .public_key(key.public_key())\
        .serial_number(x509.random_serial_number())\
        .not_valid_before(datetime.datetime.utcnow())\
        .not_valid_after(datetime.datetime.utcnow() + datetime.timedelta(days=365))\
        .add_extension(x509.BasicConstraints(ca=ca, path_length=None), critical=True)\
        .sign(key, hashes.SHA256())

    # PEM
    pem_file = tempfile.NamedTemporaryFile(delete=False, suffix=".pem")
    pem_file.write(cert.public_bytes(serialization.Encoding.PEM))
    pem_file.close()

    # DER
    der_file = tempfile.NamedTemporaryFile(delete=False, suffix=".der")
    der_file.write(cert.public_bytes(serialization.Encoding.DER))
    der_file.close()

    # PKCS#12
    p12_file = tempfile.NamedTemporaryFile(delete=False, suffix=".p12")
    p12_data = serialization.pkcs12.serialize_key_and_certificates(
        name=nickname.encode(),
        key=key,
        cert=cert,
        cas=None,
        encryption_algorithm=serialization.BestAvailableEncryption(p12_password)
    )
    p12_file.write(p12_data)
    p12_file.close()

    return {
        "pem": pem_file.name,
        "der": der_file.name,
        "p12": p12_file.name,
        "nickname": nickname,
        "password": p12_password.decode()
    }

def test_tls_command_returns_error_text(topo):
    """CLI commands that called certutil should return the error text from
    certutil when something goes wrong, and not the system error code number.

    :id: 7f0c28d0-6e13-4ca4-bec2-4586d56b73f6
    :setup: Standalone Instance
    :steps:
        1. Issue invalid "generate key and cert" command, and error text is returned
        2. Issue invalid "delete cert" command, and error text is returned
        3. Issue invalid "import ca cert" command, and error text is returned
        4. Issue invalid "import server cert" command, and error text is returned
        5. Issue invalid "import key and server cert" command, and error text is returned
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
    """

    # dsctl localhost tls generate-server-cert-csr -s "bad"
    tls = NssSsl(dirsrv=topo.standalone)
    try:
        tls.create_rsa_key_and_csr([], "bad")
        assert False
    except ValueError as e:
        assert '255' not in str(e)
        assert 'improperly formatted name' in str(e)

    # dsctl localhost tls remove-cert
    try:
        tls.del_cert("bad")
        assert False
    except ValueError as e:
        assert '255' not in str(e)
        assert 'could not find certificate named' in str(e)

    # dsctl localhost tls import-ca
    try:
        invalid_file = topo.standalone.confdir + '/dse.ldif'
        tls.add_cert(nickname="bad", input_file=invalid_file)
        assert False
    except ValueError as e:
        assert '255' not in str(e)
        assert 'Unsupported certificate format' in str(e)

    # dsctl localhost tls import-server-cert
    try:
        invalid_file = topo.standalone.confdir + '/dse.ldif'
        tls.import_rsa_crt(crt=invalid_file)
        assert False
    except ValueError as e:
        assert '255' not in str(e)
        assert 'error converting ascii to binary' in str(e)

    # dsctl localhost tls import-server-key-cert
    try:
        invalid_file = topo.standalone.confdir + '/dse.ldif'
        tls.add_server_key_and_cert(invalid_file,  invalid_file)
        assert False
    except ValueError as e:
        assert '255' not in str(e)
        if 'OpenSSL 3' in ssl.OPENSSL_VERSION:
            error_message = r"Could not (read|find) private key from"
        else:
            error_message = r"unable to load private key"
        assert re.search(error_message, str(e))

def test_add_pem_ca_cert(temp_nss_db):
    """
    Test importing a CA certificate from a PEM file.

    :id: fb00a9bc-acdb-4516-90c7-00c6504d887f
    :setup: Temporary NSS database
    :steps:
        1. Generate a CA certificate in PEM format.
        2. Import it into the NSS database using add_cert().
        3. Verify the certificate now appears in the database.
    :expectedresults:
        1. PEM CA certificate is successfully added.
    """
    db, test_files = temp_nss_db
    nickname = "ca-cert-pem"
    nss = NssSsl(dbpath=db, dbpassword=DB_PASSWORD)

    assert nickname not in list_nicknames(db)
    certs = generate_test_cert(nickname, ca=True)
    nss.add_cert(nickname, certs["pem"], ca=True)
    assert nickname in list_nicknames(db)

    # capture file paths for fixture cleanup
    test_files.extend(certs.values())

def test_add_pem_server_cert(temp_nss_db):
    """
    Test importing a server certificate from a PEM file.

    :id: e103ff74-6ad9-4260-b3f2-3f3668b3412b
    :setup: Temporary NSS database
    :steps:
        1. Generate a server certificate in PEM format.
        2. Import it into the NSS database.
        3. Verify the server certificate appears in the database.
    :expectedresults:
        1. Server PEM certificate is successfully added.
    """
    db, test_files = temp_nss_db
    nickname = "server-cert-pem"
    nss = NssSsl(dbpath=db, dbpassword=DB_PASSWORD)

    assert nickname not in list_nicknames(db)
    certs = generate_test_cert(nickname, ca=False)
    nss.add_cert(nickname, certs["pem"], ca=False)
    assert nickname in list_nicknames(db)

    # capture file paths for fixture cleanup
    test_files.extend(certs.values())

def test_add_der_server_cert(temp_nss_db):
    """
    Test importing a server certificate from a DER file.

    :id: 004bac8e-b44d-49d6-9d5d-c85e277a62cf
    :setup: Temporary NSS database
    :steps:
        1. Generate a server certificate in DER format.
        2. Import it into the NSS database.
        3. Verify the server certificate appears in the database.
    :expectedresults:
        1. Server DER certificate is successfully added.
    """
    db, test_files = temp_nss_db
    nickname = "server-cert-der"
    nss = NssSsl(dbpath=db, dbpassword=DB_PASSWORD)

    assert nickname not in list_nicknames(db)
    certs = generate_test_cert(nickname, ca=False)
    nss.add_cert(nickname, certs["der"], ca=False)
    assert nickname in list_nicknames(db)

    # capture file paths for fixture cleanup
    test_files.extend(certs.values())

def test_add_pkcs12_with_pwd_text(temp_nss_db):
    """
    Test importing a PKCS#12 file using a plaintext password argument.

    :id: 8c80ab31-5acc-4290-84d2-67aea0b6d912
    :setup: Temporary NSS database
    :steps:
        1. Generate a PKCS#12 server cert bundle.
        2. Import it using add_cert() with pkcs12_pin_text.
        3. Verify the certificate appears in the NSS database.
    :expectedresults:
        1. PKCS#12 file imports successfully when password text is provided.
    """
    db, test_files = temp_nss_db
    nickname = "server-cert-p12-txt"
    nss = NssSsl(dbpath=db, dbpassword=DB_PASSWORD)

    assert nickname not in list_nicknames(db)
    certs = generate_test_cert(nickname, ca=False)
    nss.add_cert(nickname, certs["p12"], pkcs12_pin_text=certs["password"], ca=False)
    assert nickname in list_nicknames(db)

    # capture file paths for fixture cleanup
    test_files.extend(certs.values())

def test_add_pkcs12_with_pwd_path(temp_nss_db):
    """
    Test importing a PKCS#12 file using a password stored in a file.

    :id: 28faf5df-b402-44eb-ae9d-6a6aa27f6903
    :setup: Temporary NSS database
    :steps:
        1. Generate a PKCS#12 server certificate.
        2. Store the password in a temporary file.
        3. Import via add_cert() using pkcs12_pin_path.
        4. Verify the certificate appears in the database.
    :expectedresults:
        1. PKCS#12 file imports successfully when password path is supplied.
    """
    db, test_files = temp_nss_db
    nickname = "server-cert-p12-path"
    nss = NssSsl(dbpath=db, dbpassword=DB_PASSWORD)

    assert nickname not in list_nicknames(db)
    certs = generate_test_cert(nickname, ca=False)
    pwd_file = tempfile.NamedTemporaryFile(delete=False, suffix=".txt")
    pwd_file.write(certs["password"].encode())
    pwd_file.close()
    nss.add_cert(nickname, certs["p12"], pkcs12_pin_path=pwd_file.name)
    assert nickname in list_nicknames(db)

    # capture file paths for fixture cleanup
    test_files.extend(certs.values())
    os.unlink(pwd_file.name)

def test_pkcs12_requires_password(temp_nss_db):
    """
    Test that importing a PKCS#12 file without a password fails.

    :id: df3480ec-563b-4c4f-92a3-9d16028c088f
    :setup: Temporary NSS database
    :steps:
        1. Generate a PKCS#12 server certificate.
        2. Attempt to import it without providing a password.
    :expectedresults:
        1. add_cert() raises ValueError due to missing PKCS#12 password.
    """
    db, test_files = temp_nss_db
    nickname = "server-cert-p12-pwd"
    nss = NssSsl(dbpath=db, dbpassword=DB_PASSWORD)

    assert nickname not in list_nicknames(db)
    certs = generate_test_cert(nickname, ca=False)
    with pytest.raises(ValueError):
        nss.add_cert(nickname, certs["p12"])

    # capture file paths for fixture cleanup
    test_files.extend(certs.values())

def test_add_pkcs12_no_nickname(temp_nss_db):
    """
    Test importing a PKCS#12 file without specifying a nickname.

    :id: c1299e43-1218-47d1-9311-f575ed0c4758
    :setup: Temporary NSS database
    :steps:
        1. Generate a PKCS#12 server certificate.
        2. Import using add_cert() with nickname=None.
        3. Verify the autogenerated nickname is present.
    :expectedresults:
        1. PKCS#12 file imports successfully.
        2. A nickname is generated automatically.
    """
    db, test_files = temp_nss_db
    nickname = "server-cert-p12-no-nick"
    nss = NssSsl(dbpath=db, dbpassword=DB_PASSWORD)

    assert nickname not in list_nicknames(db)
    certs = generate_test_cert(nickname, ca=False)
    nss.add_cert(None, certs["p12"], pkcs12_pin_text=certs["password"], ca=False)
    nicks = list_nicknames(db)
    assert len(nicks) == 1
    assert nicks[0]

    # capture file paths for fixture cleanup
    test_files.extend(certs.values())

def test_add_pem_no_nickname(temp_nss_db):
    """
    Test that importing a PEM file with no nickname is rejected.

    :id: 09e4c801-4b40-4007-a94c-7d83d9a715d4
    :setup: Temporary NSS database
    :steps:
        1. Generate a server PEM certificate.
        2. Attempt to import it with nickname=None.
    :expectedresults:
        1. add_cert() raises ValueError due to missing nickname.
    """
    db, test_files = temp_nss_db
    nickname = "server-cert-pem-no-nick"
    nss = NssSsl(dbpath=db, dbpassword=DB_PASSWORD)

    assert nickname not in list_nicknames(db)
    certs = generate_test_cert(nickname, ca=False)

    # Adding a PEM cert with no nickname should raise ValueError
    with pytest.raises(ValueError):
        nss.add_cert(None, certs["pem"], ca=False)

    # capture file paths for fixture cleanup
    test_files.extend(certs.values())

def test_add_der_no_nickname(temp_nss_db):
    """
    Test that importing a DER certificate with no nickname is rejected.

    :id: e82048e1-2e1a-48ac-887c-67fed47a0874
    :setup: Temporary NSS database
    :steps:
        1. Generate a server DER certificate.
        2. Attempt to import it with nickname=None.
    :expectedresults:
        1. add_cert() raises ValueError due to missing nickname.
    """
    db, test_files = temp_nss_db
    nickname = "server-cert-der-no-nick"
    nss = NssSsl(dbpath=db, dbpassword=DB_PASSWORD)

    assert nickname not in list_nicknames(db)
    certs = generate_test_cert(nickname, ca=False)

    # Adding a PEM cert with no nickname should raise ValueError
    with pytest.raises(ValueError):
        nss.add_cert(None, certs["der"], ca=False)

    # capture file paths for fixture cleanup
    test_files.extend(certs.values())

def test_duplicate_nickname_fails(temp_nss_db):
    """
    Test that adding a certificate with a nickname already in the database fails.

    :id: b1ef9d3b-7bfb-4873-87f6-cc0d7fceb82e
    :setup: Temporary NSS database
    :steps:
        1. Import a certificate using nickname X.
        2. Attempt to import another certificate using the same nickname X.
    :expectedresults:
        1. add_cert() raises ValueError due to duplicate nickname.
    """
    db, test_files = temp_nss_db
    nickname = "server-cert-dup-nicks"
    nss = NssSsl(dbpath=db, dbpassword=DB_PASSWORD)

    certs = generate_test_cert(nickname, ca=False)

    # Add cert
    nss.add_cert(nickname, certs["pem"], ca=False)

    # Attempt to add it again
    with pytest.raises(ValueError):
        nss.add_cert(nickname, certs["pem"], ca=False)

    # capture file paths for fixture cleanup
    test_files.extend(certs.values())

def test_no_cert_input_file(temp_nss_db):
    """
    Test that importing a certificate from a non-existent file fails.

    :id: 64d97b83-6a48-442b-9028-1f556eaef737
    :setup: Temporary NSS database
    :steps:
        1. Call add_cert() with an invalid file path.
    :expectedresults:
        1. add_cert() raises ValueError due to missing file.
    """
    db, _ = temp_nss_db
    nickname = "server-cert-no-file"
    nss = NssSsl(dbpath=db, dbpassword=DB_PASSWORD)

    with pytest.raises(ValueError):
        nss.add_cert("Missing", "/does/not/exist.pem")

def test_add_pem_ca_bundle(temp_nss_db):
    """
    Test importing a PEM bundle containing multiple CA certificates.

    :id: 2da2c170-f2ca-4ba9-adc0-18d708659065
    :setup: Temporary NSS database
    :steps:
        1. Generate multiple CA certificates.
        2. Combine them into a single PEM bundle.
        3. Import the bundle using add_cert().
        4. Verify all CA certificates appear in the database.
    :expectedresults:
        1. CA bundle is accepted.
        2. Each CA certificate is imported with the correct nickname.
    """
    db, test_files = temp_nss_db
    nss = NssSsl(dirsrv=None, dbpath=db, dbpassword=DB_PASSWORD)

    # Generate multiple CA certificates
    certs1 = generate_test_cert("CA_CERT_1", ca=True)
    certs2 = generate_test_cert("CA_CERT_2", ca=True)

    # Create a PEM bundle file
    bundle_file = tempfile.NamedTemporaryFile(delete=False, suffix=".pem")
    bundle_file.write(open(certs1["pem"], "rb").read())
    bundle_file.write(b"\n")
    bundle_file.write(open(certs2["pem"], "rb").read())
    bundle_file.close()

    # Provide nicknames for each certificate in the bundle
    nicknames = ["CA_CERT_1", "CA_CERT_2"]

    # Add the PEM bundle
    nss.add_cert(nicknames, bundle_file.name, ca=True)

    # Assert that both certificates were added
    nicks_in_db = list_nicknames(db)
    assert "CA_CERT_1" in nicks_in_db
    assert "CA_CERT_2" in nicks_in_db

    # capture file paths for fixture cleanup
    test_files.extend([certs1["pem"], certs1["der"], certs1["p12"],
                       certs2["pem"], certs2["der"], certs2["p12"],
                       bundle_file.name])

    # add any split files created by the bundle
    for path in Path("/tmp").glob(f"{Path(bundle_file.name).name}-*.pem"):
        test_files.append(str(path))

def test_add_pem_server_bundle(temp_nss_db):
    """
    Test that importing a PEM bundle containing multiple *server* certificates is rejected.

    :id: 06840392-7815-4945-9a84-fd65c0e7d21e
    :setup: Temporary NSS database
    :steps:
        1. Generate two server certificates.
        2. Combine them into a single PEM bundle.
        3. Attempt to import bundle as a server cert.
    :expectedresults:
        1. add_cert() raises ValueError because server cert bundles are not supported.
    """
    db, test_files = temp_nss_db
    nss = NssSsl(dirsrv=None, dbpath=db, dbpassword=DB_PASSWORD)

    # Generate two server certificates
    cert1 = generate_test_cert("SERVER1", ca=False)
    cert2 = generate_test_cert("SERVER2", ca=False)

    # Create a PEM bundle
    bundle_file = tempfile.NamedTemporaryFile(delete=False, suffix=".pem")
    bundle_file.write(open(cert1["pem"], "rb").read())
    bundle_file.write(b"\n")
    bundle_file.write(open(cert2["pem"], "rb").read())
    bundle_file.close()

    # Expect add_cert to raise ValueError for multiple certs in a server PEM
    with pytest.raises(ValueError):
        nss.add_cert("SERVER_BUNDLE", bundle_file.name, ca=False)

    # capture file paths for fixture cleanup
    test_files.extend([cert1["pem"], cert1["der"], cert1["p12"],
                       cert2["pem"], cert2["der"], cert2["p12"],
                       bundle_file.name])

    # add any split files created by the bundle
    for path in Path("/tmp").glob(f"{Path(bundle_file.name).name}-*.pem"):
        test_files.append(str(path))

def test_add_ca_cert_with_do_it(temp_nss_db):
    db, test_files = temp_nss_db
    nss = NssSsl(dbpath=db, dbpassword=DB_PASSWORD)

    # Generate a "bad" CA cert (e.g., server cert masquerading as CA)
    bad_ca_cert = generate_test_cert("BAD_CA", ca=False)["pem"]

    # Without do_it, should raise
    with pytest.raises(ValueError):
        nss.add_cert("bad_ca", bad_ca_cert, ca=True, do_it=False)

    # With do_it, should succeed
    nss.add_cert("bad_ca", bad_ca_cert, ca=True, do_it=True)
    assert "bad_ca" in [n for n, _ in nss._rsa_cert_list()]

    # capture for cleanup
    test_files.append(bad_ca_cert)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])

