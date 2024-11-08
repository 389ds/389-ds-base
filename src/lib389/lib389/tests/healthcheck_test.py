# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import pytest
import ldap

from lib389.topologies import topology_st
from lib389.plugins import ReferentialIntegrityPlugin

from lib389.lint import *

def test_hc_backend_mt(topology_st):
    mt = topology_st.standalone.mappingtrees.get('userRoot')
    # We have to remove the MT from a backend.
    mt.delete()
    be = topology_st.standalone.backends.get('userRoot')
    result = be._lint_mappingtree()
    # We have to check this by name, not object, as the result changes
    # the affected dn in the result.
    assert result['dsle'] == 'DSBLE0001'

def test_hc_encryption(topology_st):
    topology_st.standalone.encryption.set('sslVersionMin', 'TLS1.0')
    result = topology_st.standalone.encryption._lint_check_tls_version()
    assert result == DSELE0001

def test_hc_config(topology_st):
    # Check the password scheme check.
    topology_st.standalone.config.set('passwordStorageScheme', 'SSHA')
    result = topology_st.standalone.config._lint_passwordscheme()
    assert result == DSCLE0002

def test_hc_referint(topology_st):
    plugin = ReferentialIntegrityPlugin(topology_st.standalone)
    plugin.enable()

    # Assert we don't get an error when delay is 0.
    plugin.set('referint-update-delay', '0')
    result = plugin._lint_update_delay()
    assert result is None

    # Assert we get an error when delay is not 0.
    plugin.set('referint-update-delay', '10')
    result = plugin._lint_update_delay()
    assert result == DSRILE0001

    # Assert we don't get an error when plugin is disabled.
    plugin.disable()
    result = plugin._lint_update_delay()
    assert result is None
