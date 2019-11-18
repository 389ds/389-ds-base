# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2018 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#


import pytest
import ldap
from lib389.topologies import topology_st

pytestmark = pytest.mark.tier1

def test_tls_check_crl(topology_st):
    """Test that TLS check_crl configurations work as expected.

    :id: 9dfc6c62-dcae-44a9-83e8-b15c8e61c609
    :steps:
        1. Enable TLS
        2. Set invalid value
        3. Set valid values
        4. Check config reset
    :expectedresults:
        1. TlS is setup
        2. The invalid value is rejected
        3. The valid values are used
        4. The value can be reset
    """
    standalone = topology_st.standalone
    # Enable TLS
    standalone.enable_tls()
    # Check all the valid values.
    assert(standalone.config.get_attr_val_utf8('nsslapd-tls-check-crl') == 'none')
    with pytest.raises(ldap.OPERATIONS_ERROR):
        standalone.config.set('nsslapd-tls-check-crl', 'tnhoeutnoeutn')
    assert(standalone.config.get_attr_val_utf8('nsslapd-tls-check-crl') == 'none')

    standalone.config.set('nsslapd-tls-check-crl', 'peer')
    assert(standalone.config.get_attr_val_utf8('nsslapd-tls-check-crl') == 'peer')

    standalone.config.set('nsslapd-tls-check-crl', 'none')
    assert(standalone.config.get_attr_val_utf8('nsslapd-tls-check-crl') == 'none')

    standalone.config.set('nsslapd-tls-check-crl', 'all')
    assert(standalone.config.get_attr_val_utf8('nsslapd-tls-check-crl') == 'all')

    standalone.config.remove_all('nsslapd-tls-check-crl')
    assert(standalone.config.get_attr_val_utf8('nsslapd-tls-check-crl') == 'none')



