# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
import os
import ldap
import time
from lib389.config import Encryption
from test389.topologies import topology_st as topo
from lib389.utils import ds_is_older
from lib389.nss_ssl import NssSsl
from lib389.config import Config
from lib389._constants import DN_DM, PASSWORD, SECUREPORT_STANDALONE
from lib389.dseldif import DSEldif
import subprocess
import logging

log = logging.getLogger(__name__)

pytestmark = pytest.mark.tier1

LDAPSPORT = str(SECUREPORT_STANDALONE)
ENC_DN = 'cn=encryption,cn=config'

def test_long_cipher_list(topo):
    """Test a long cipher list, and makre sure it is not truncated

    :id: bc400f54-3966-49c8-b640-abbf4fb2377d
    :setup: Standalone Instance
    :steps:
        1. Set nsSSL3Ciphers to a very long list of ciphers
        2. Ciphers are applied correctly
    :expectedresults:
        1. Success
        2. Success
    """
    ENABLED_CIPHER = "TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384::AES-GCM::AEAD::256"
    DISABLED_CIPHER = "TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256::AES-GCM::AEAD::128"
    CIPHER_LIST = (
            "-all,-SSL_CK_RC4_128_WITH_MD5,-SSL_CK_RC4_128_EXPORT40_WITH_MD5,-SSL_CK_RC2_128_CBC_WITH_MD5,"
            "-SSL_CK_RC2_128_CBC_EXPORT40_WITH_MD5,-SSL_CK_DES_64_CBC_WITH_MD5,-SSL_CK_DES_192_EDE3_CBC_WITH_MD5,"
            "-TLS_RSA_WITH_RC4_128_MD5,-TLS_RSA_WITH_RC4_128_SHA,-TLS_RSA_WITH_3DES_EDE_CBC_SHA,"
            "-TLS_RSA_WITH_DES_CBC_SHA,-SSL_RSA_FIPS_WITH_3DES_EDE_CBC_SHA,-SSL_RSA_FIPS_WITH_DES_CBC_SHA,"
            "-TLS_RSA_EXPORT_WITH_RC4_40_MD5,-TLS_RSA_EXPORT_WITH_RC2_CBC_40_MD5,-TLS_RSA_WITH_NULL_MD5,"
            "-TLS_RSA_WITH_NULL_SHA,-TLS_RSA_EXPORT1024_WITH_DES_CBC_SHA,-SSL_FORTEZZA_DMS_WITH_FORTEZZA_CBC_SHA,"
            "-SSL_FORTEZZA_DMS_WITH_RC4_128_SHA,-SSL_FORTEZZA_DMS_WITH_NULL_SHA,-TLS_DHE_DSS_WITH_DES_CBC_SHA,"
            "-TLS_DHE_DSS_WITH_3DES_EDE_CBC_SHA,-TLS_DHE_RSA_WITH_DES_CBC_SHA,-TLS_DHE_RSA_WITH_3DES_EDE_CBC_SHA,"
            "+TLS_RSA_WITH_AES_128_CBC_SHA,-TLS_DHE_DSS_WITH_AES_128_CBC_SHA,-TLS_DHE_RSA_WITH_AES_128_CBC_SHA,"
            "+TLS_RSA_WITH_AES_256_CBC_SHA,-TLS_DHE_DSS_WITH_AES_256_CBC_SHA,-TLS_DHE_RSA_WITH_AES_256_CBC_SHA,"
            "-TLS_RSA_EXPORT1024_WITH_RC4_56_SHA,-TLS_DHE_DSS_WITH_RC4_128_SHA,-TLS_ECDHE_RSA_WITH_RC4_128_SHA,"
            "-TLS_RSA_WITH_NULL_SHA,-TLS_RSA_EXPORT1024_WITH_DES_CBC_SHA,-SSL_CK_DES_192_EDE3_CBC_WITH_MD5,"
            "-TLS_RSA_WITH_RC4_128_MD5,-TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,-TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA,"
            "-TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,+TLS_AES_128_GCM_SHA256,+TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384"
        )

    topo.standalone.enable_tls()
    enc = Encryption(topo.standalone)
    enc.set('nsSSL3Ciphers', CIPHER_LIST)
    topo.standalone.restart()
    enabled_ciphers = enc.get_attr_vals_utf8('nssslenabledciphers')
    assert ENABLED_CIPHER in enabled_ciphers
    assert DISABLED_CIPHER not in enabled_ciphers


def connectWithOpenssl(topology_st, expect=True):
    """
    Connect with OpenSSL
    Condition:
    If expect is True, the handshake should be successful.
    If expect is False, the handshake should be refused with
       access log: "Cannot communicate securely with peer:
                   no common encryption algorithm(s)."
    """
    log.info(f'Testing connection -- expect to handshake {"successfully" if expect else "failed"}')

    myurl = f'localhost:{LDAPSPORT}'
    cmdline = ['/usr/bin/openssl', 's_client', '-connect', myurl]

    strcmdline = " ".join(cmdline)
    log.info(f"Running cmdline: {strcmdline}")

    with subprocess.Popen(cmdline, stdout=subprocess.PIPE, stdin=subprocess.PIPE, stderr=subprocess.STDOUT) as proc:
        for line in proc.stdout:
            if line == b"":
                break
            if b'Cipher is' in line:
                log.info(f"Found: {line}")
                if expect:
                    assert b'(NONE)' not in line
                    proc.stdin.close()
                else:
                    assert b'(NONE)' in line
                    proc.stdin.close()


@pytest.fixture(scope='function')
def setup_cipher_test(request, topo):
    topo.standalone.enable_tls()
    topo.standalone.restart()
    topo.standalone.simple_bind_s(DN_DM, PASSWORD)

    def fin():
        try:
            topo.standalone.encryption.set('nsSSL3Ciphers', b'default')
            topo.standalone.restart()
        except ValueError:
            dse = DSEldif(topo.standalone)
            topo.standalone.stop()
            dse.replace(ENC_DN, 'nsSSL3Ciphers', b'default')
            topo.standalone.use_ldap_uri()
            topo.standalone.start()
    request.addfinalizer(fin)


def test_cipher_policy_allowWeakCipher(topo, setup_cipher_test):
    """Verify negotiated TLS handshake succeeds when weak ciphers are explicitly allowed

    :id: dcea9436-b26c-44ed-ac0c-3437612bf15a
    :setup: Standalone Instance
    :steps:
        1. Set nsslapd-errorlog-level to 64 and allowWeakCipher to on, then restart
        2. Run openssl s_client (negotiated cipher) and expect a successful handshake
        3. Restore nsslapd-errorlog-level and set allowWeakCipher to off
    :expectedresults:
        1. Success
        2. Negotiated cipher is not (NONE)
        3. Success
    """
    topo.standalone.simple_bind_s(DN_DM, PASSWORD)
    errloglevel = topo.standalone.config.get_attr_val('nsslapd-errorlog-level')
    try:
        topo.standalone.config.set('nsslapd-errorlog-level', '64')
        topo.standalone.encryption.set('allowWeakCipher', 'on')
        topo.standalone.restart()

        connectWithOpenssl(topo)
    finally:
        topo.standalone.config.set('nsslapd-errorlog-level', errloglevel)
        topo.standalone.encryption.set('allowWeakCipher', 'off')


def test_cipher_policy_delete_allowWeakCipher(topo, setup_cipher_test):
    """Verify cipher behavior after removing allowWeakCipher so the default policy applies

    :id: ac8bc03d-5c1c-4c3a-9d34-61b116f0e7cd
    :setup: Standalone Instance
    :steps:
        1. Set nsslapd-errorlog-level to 64 and delete allowWeakCipher, then restart
        2. Run openssl s_client (negotiated cipher) and expect a successful handshake
        3. Restore nsslapd-errorlog-level and allowWeakCipher to its prior value
    :expectedresults:
        1. Success
        2. Negotiated cipher is not (NONE)
        3. Success
    """
    topo.standalone.simple_bind_s(DN_DM, PASSWORD)
    errloglevel = topo.standalone.config.get_attr_val('nsslapd-errorlog-level')
    allowWeakCipher = topo.standalone.encryption.get_attr_val('allowWeakCipher')
    try:
        topo.standalone.config.set('nsslapd-errorlog-level', '64')
        topo.standalone.encryption.delete('allowWeakCipher')
        topo.standalone.restart()

        connectWithOpenssl(topo)
    finally:
        topo.standalone.config.set('nsslapd-errorlog-level', errloglevel)
        topo.standalone.encryption.set('allowWeakCipher', allowWeakCipher)


def test_cipher_policy_all_ciphers_disabled(topo, setup_cipher_test):
    """Verify bind is not possible when TLS is configured but all TLS ciphers are disabled

    :id: e38b4e7d-8dda-4ac5-9013-25762715792b
    :setup: Standalone Instance
    :steps:
        1. Set nsSSL3Ciphers to -all
        2. Attempt to restart the standalone instance
    :expectedresults:
        1. Success
        2. Restart raises ldap.SERVER_DOWN because no ciphers are available
    """
    topo.standalone.simple_bind_s(DN_DM, PASSWORD)

    topo.standalone.encryption.set('nsSSL3Ciphers', b'-all')

    # Server fails to bind and restart because TLS is configured, but no ciphers are enabled
    with pytest.raises(ldap.SERVER_DOWN):
        topo.standalone.restart()


def test_cipher_policy_delete_ciphers(topo, setup_cipher_test):
    """Verify TLS handshake fails after deleting nsSSL3Ciphers from the encryption entry

    :id: aa264a5d-655d-4e15-91cb-e288a9ea9a8a
    :setup: Standalone Instance
    :steps:
        1. Delete nsSSL3Ciphers and restart the server
        2. Run openssl s_client (negotiated cipher) and expect handshake failure
    :expectedresults:
        1. Success
        2. Handshake fails
    """
    topo.standalone.simple_bind_s(DN_DM, PASSWORD)

    topo.standalone.encryption.delete('nsSSL3Ciphers')
    topo.standalone.restart()

    connectWithOpenssl(topo, False)


def test_cipher_policy_default_ciphers(topo, setup_cipher_test):
    """Verify TLS handshakes when nsSSL3Ciphers is set to the default cipher suite

    :id: f88fd3f6-4b97-4b3b-9d18-5a98591eaeb2
    :setup: Standalone Instance
    :steps:
        1. Set nsSSL3Ciphers to default and restart the server
        2. Attempt openssl handshake
    :expectedresults:
        1. Success
        2. Success
    """
    topo.standalone.simple_bind_s(DN_DM, PASSWORD)

    topo.standalone.encryption.set('nsSSL3Ciphers', b'default')
    topo.standalone.restart()

    connectWithOpenssl(topo)


def test_cipher_policy_all_with_excluded_cipher(topo, setup_cipher_test):
    """Verify TLS handshake when TLS_AES_128_GCM_SHA256 is excluded from an otherwise broad cipher list

    :id: 401a9f4f-e496-4120-958b-c43d8cd62275
    :setup: Standalone Instance
    :steps:
        1. Set nsSSL3Ciphers to +all,-TLS_AES_128_GCM_SHA256 and restart
        2. Run openssl s_client (negotiated cipher) and expect a successful handshake
    :expectedresults:
        1. Success
        2. Handshake succeeds
    """
    topo.standalone.simple_bind_s(DN_DM, PASSWORD)

    topo.standalone.encryption.set('nsSSL3Ciphers', b'+all,-TLS_AES_128_GCM_SHA256')
    topo.standalone.restart()

    connectWithOpenssl(topo)


def test_cipher_policy_default_ciphers_no_weak(topo, setup_cipher_test):
    """Verify negotiated TLS handshake with default ciphers and allowWeakCipher off

    :id: f5e87043-4660-4db6-9f83-5c0765076bce
    :setup: Standalone Instance
    :steps:
        1. Set nsSSL3Ciphers to default, set allowWeakCipher to off, and restart
        2. Run openssl s_client (negotiated cipher) and expect a successful handshake
    :expectedresults:
        1. Success
        2. Handshake succeeds
    """
    topo.standalone.simple_bind_s(DN_DM, PASSWORD)

    topo.standalone.encryption.set('nsSSL3Ciphers', b'default')
    topo.standalone.encryption.set('allowWeakCipher', 'off')
    topo.standalone.restart()

    connectWithOpenssl(topo)


def test_cipher_policy_clear_ciphers(topo, setup_cipher_test):
    """Verify negotiated TLS handshake when nsSSL3Ciphers is cleared and weak ciphers are allowed

    :id: c6117e7a-0da7-48e8-91b1-c7764acb192f
    :setup: Standalone Instance
    :steps:
        1. Set nsSSL3Ciphers to None, set allowWeakCipher to on, delete nsslapd-errorlog-level, and restart
        2. Run openssl s_client (negotiated cipher) and expect a successful handshake
        3. Set allowWeakCipher to off
    :expectedresults:
        1. Success
        2. Handshake succeeds
        3. Success
    """

    topo.standalone.simple_bind_s(DN_DM, PASSWORD)

    try:
        topo.standalone.encryption.set('nsSSL3Ciphers', None)
        topo.standalone.encryption.set('allowWeakCipher', b'on')
        topo.standalone.config.delete('nsslapd-errorlog-level')
        topo.standalone.restart()

        connectWithOpenssl(topo)
    finally:
        topo.standalone.encryption.set('allowWeakCipher', b'off')


def test_cipher_policy_unsupported_cipher(topo, setup_cipher_test):
    """Verify change_ciphers rejects an unsupported cipher and leaves nsSSL3Ciphers unchanged

    :id: ebb44bd2-b962-4847-958a-db955a09b7c1
    :setup: Standalone Instance
    :steps:
        1. Record the current nsSSL3Ciphers value
        2. Call change_ciphers to add the unsupported fortezza cipher suite
        3. Read nsSSL3Ciphers again
    :expectedresults:
        1. Success
        2. change_ciphers returns False
        3. nsSSL3Ciphers is unchanged from the value recorded in step 1
    """
    topo.standalone.simple_bind_s(DN_DM, PASSWORD)
    default_cipher = topo.standalone.encryption.get_attr_val('nsSSL3Ciphers')

    # change_ciphers should return False because 'fortezza' cipher is not supported
    assert not topo.standalone.encryption.change_ciphers('+', ['fortezza']), "Unsupported cipher 'fortezza' was not rejected"
    
    # Verify that nsSSL3Ciphers is still set to'default'
    assert topo.standalone.encryption.get_attr_val('nsSSL3Ciphers') == default_cipher, "nsSSL3Ciphers attribute should not be modified"


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
