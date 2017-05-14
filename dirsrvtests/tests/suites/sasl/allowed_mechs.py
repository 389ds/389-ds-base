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

import time

from lib389.topologies import topology_st

def test_sasl_allowed_mechs(topology_st):
    standalone = topology_st.standalone

    # Get the supported mechs. This should contain PLAIN, GSSAPI, EXTERNAL at least
    orig_mechs = standalone.rootdse.supported_sasl()
    print(orig_mechs)
    assert('GSSAPI' in orig_mechs)
    assert('PLAIN' in orig_mechs)
    assert('EXTERNAL' in orig_mechs)

    # Now edit the supported mechs. CHeck them again.
    standalone.config.set('nsslapd-allowed-sasl-mechanisms', 'PLAIN')

    limit_mechs = standalone.rootdse.supported_sasl()
    assert('PLAIN' in limit_mechs)
    # Should always be in the allowed list, even if not set.
    assert('EXTERNAL' in limit_mechs)
    # Should not be there!
    assert('GSSAPI' not in limit_mechs)

    standalone.config.set('nsslapd-allowed-sasl-mechanisms', 'PLAIN, EXTERNAL')

    limit_mechs = standalone.rootdse.supported_sasl()
    assert('PLAIN' in limit_mechs)
    assert('EXTERNAL' in limit_mechs)
    # Should not be there!
    assert('GSSAPI' not in limit_mechs)

    # Do a config reset
    standalone.config.reset('nsslapd-allowed-sasl-mechanisms')

    # check the supported list is the same as our first check.
    final_mechs = standalone.rootdse.supported_sasl()
    print(final_mechs)
    assert(set(final_mechs) == set(orig_mechs))

