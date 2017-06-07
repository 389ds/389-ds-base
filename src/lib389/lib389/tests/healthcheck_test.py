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
    # Check the HR timestamp
    topology_st.standalone.config.set('nsslapd-logging-hr-timestamps-enabled', 'off')
    result = topology_st.standalone.config._lint_hr_timestamp()
    assert result == DSCLE0001

    # Check the password scheme check.
    topology_st.standalone.config.set('passwordStorageScheme', 'SSHA')
    result = topology_st.standalone.config._lint_passwordscheme()
    assert result == DSCLE0002

