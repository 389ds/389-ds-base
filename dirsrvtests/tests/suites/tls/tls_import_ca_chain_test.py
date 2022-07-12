# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022, William Brown <william@blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import pytest
import ldap
import os

from lib389.nss_ssl import NssSsl
from lib389.topologies import topology_st

pytestmark = pytest.mark.tier1

CA_CHAIN_FILE = os.path.join(os.path.dirname(__file__), '../../data/tls/tls_import_ca_chain.pem')
CRT_CHAIN_FILE = os.path.join(os.path.dirname(__file__), '../../data/tls/tls_import_crt_chain.pem')
KEY_CHAIN_FILE = os.path.join(os.path.dirname(__file__), '../../data/tls/tls_import_key_chain.pem')
KEY_FILE = os.path.join(os.path.dirname(__file__), '../../data/tls/tls_import_key.pem')

def test_tls_import_chain(topology_st):
    """Test that TLS import will correct report errors when there are multiple
    files in a chain.

    :id: b7ba71bd-112a-44a1-8a7e-8968249da419

    :steps:
        1. Attempt to import a ca chain

    :expectedresults:
        1. The chain is rejected
    """
    topology_st.standalone.stop()
    tls = NssSsl(dirsrv=topology_st.standalone)
    tls.reinit()

    with pytest.raises(ValueError):
        tls.add_cert(nickname='CA_CHAIN_1', input_file=CA_CHAIN_FILE)

    with pytest.raises(ValueError):
        tls.import_rsa_crt(crt=CRT_CHAIN_FILE)
    with pytest.raises(ValueError):
        tls.import_rsa_crt(ca=CA_CHAIN_FILE)

def test_tls_import_chain_pk12util(topology_st):
    """Test that importing certificate chain files via pk12util does not report
    any errors

    :id: c38b2cf9-93f0-4168-ab23-c74ac21ad59f

    :steps:
        1. Attempt to import a ca chain

    :expectedresults:
        1. Chain is successfully imported, no errors raised
    """

    topology_st.standalone.stop()
    tls = NssSsl(dirsrv=topology_st.standalone)
    tls.reinit()

    tls.add_server_key_and_cert(KEY_FILE, CRT_CHAIN_FILE)
    tls.add_server_key_and_cert(KEY_CHAIN_FILE, CRT_CHAIN_FILE)
    tls.add_server_key_and_cert(KEY_FILE, KEY_CHAIN_FILE)
