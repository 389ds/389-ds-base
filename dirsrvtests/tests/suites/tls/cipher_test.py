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
from lib389.config import Encryption
from lib389.topologies import topology_st as topo


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


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
