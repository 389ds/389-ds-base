# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 William Brown <william@blackhats.net.au
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
import ldap
from lib389.topologies import topology_st
from lib389.utils import socket_check_open

pytestmark = pytest.mark.tier1

def test_tls_ldaps_only(topology_st):
    """Test that the server can run with ldaps only.

    :id: 812d806b-9368-4534-a291-cbc60ac92a23
    :steps:
        1. Enable TLS
        2. Set the server to ldaps only and restart
        3. Set the server to accept both and restart
    :expectedresults:
        1. TlS is setup
        2. The server only works on ldaps
        3. The server accepts both.
    """
    standalone = topology_st.standalone
    # Enable TLS
    standalone.enable_tls()
    # Remember the existing port for later.
    plain_port = standalone.config.get_attr_val_utf8('nsslapd-port')
    tls_port = standalone.config.get_attr_val_utf8('nsslapd-securePort')
    # Disable the plaintext port
    standalone.config.disable_plaintext_port()
    standalone.restart()
    # Check we only have the tls port
    nport = standalone.config.get_attr_val_utf8('nsslapd-port')
    assert(nport == '0');
    # Setup the plain again.
    standalone.config.enable_plaintext_port(plain_port)
    standalone.restart()
    nport = standalone.config.get_attr_val_utf8('nsslapd-port')
    assert(nport == plain_port);

