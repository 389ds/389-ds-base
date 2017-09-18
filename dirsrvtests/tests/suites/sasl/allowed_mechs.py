# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import pytest
import os
from lib389.topologies import topology_st


def test_sasl_allowed_mechs(topology_st):
    """Test the alloweed sasl mechanism feature

    :ID: ab7d9f86-8cfe-48c3-8baa-739e599f006a
    :feature: Allowed sasl mechanisms
    :steps: 1.  Get the default list of mechanisms
            2.  Set allowed mechanism PLAIN, and verify it's correctly listed
            3.  Restart server, and verify list is still correct
            4.  Test EXTERNAL is properly listed
            5.  Add GSSAPI to the existing list, and verify it's correctly listed
            6.  Restart server and verify list is still correct
            7.  Add ANONYMOUS to the existing list, and veirfy it's correctly listed
            8.  Restart server and verify list is still correct
            9.  Remove GSSAPI and verify it's correctly listed
            10. Restart server and verify list is still correct
            11. Reset allowed list to nothing, verify "all" the mechanisms are returned
            12. Restart server and verify list is still correct

    :expectedresults: The supported mechanisms supported what is set for the allowed
                      mechanisms
    """
    standalone = topology_st.standalone

    # Get the supported mechs. This should contain PLAIN, GSSAPI, EXTERNAL at least
    standalone.log.info("Test we have some of the default mechanisms")
    orig_mechs = standalone.rootdse.supported_sasl()
    print(orig_mechs)
    assert('GSSAPI' in orig_mechs)
    assert('PLAIN' in orig_mechs)
    assert('EXTERNAL' in orig_mechs)

    # Now edit the supported mechs. Check them again.
    standalone.log.info("Edit mechanisms to allow just PLAIN")
    standalone.config.set('nsslapd-allowed-sasl-mechanisms', 'PLAIN')
    limit_mechs = standalone.rootdse.supported_sasl()
    assert('PLAIN' in limit_mechs)
    assert('EXTERNAL' in limit_mechs)  # Should always be in the allowed list, even if not set.
    assert('GSSAPI' not in limit_mechs)  # Should not be there!

    # Restart the server a few times and make sure nothing changes
    standalone.log.info("Restart server and make sure we still have correct allowed mechs")
    standalone.restart()
    standalone.restart()
    limit_mechs = standalone.rootdse.supported_sasl()
    assert('PLAIN' in limit_mechs)
    assert('EXTERNAL' in limit_mechs)
    assert('GSSAPI' not in limit_mechs)

    # Set EXTERNAL, even though its always supported
    standalone.log.info("Edit mechanisms to allow just PLAIN and EXTERNAL")
    standalone.config.set('nsslapd-allowed-sasl-mechanisms', 'PLAIN, EXTERNAL')
    limit_mechs = standalone.rootdse.supported_sasl()
    assert('PLAIN' in limit_mechs)
    assert('EXTERNAL' in limit_mechs)
    assert('GSSAPI' not in limit_mechs)

    # Now edit the supported mechs. Check them again.
    standalone.log.info("Edit mechanisms to allow just PLAIN and GSSAPI")
    standalone.config.set('nsslapd-allowed-sasl-mechanisms', 'PLAIN, GSSAPI')
    limit_mechs = standalone.rootdse.supported_sasl()
    assert('PLAIN' in limit_mechs)
    assert('EXTERNAL' in limit_mechs)
    assert('GSSAPI' in limit_mechs)
    assert(len(limit_mechs) == 3)

    # Restart server twice and make sure the allowed list is the same
    standalone.restart()
    standalone.restart()  # For ticket 49379 (test double restart)
    limit_mechs = standalone.rootdse.supported_sasl()
    assert('PLAIN' in limit_mechs)
    assert('EXTERNAL' in limit_mechs)
    assert('GSSAPI' in limit_mechs)
    assert(len(limit_mechs) == 3)

    # Add ANONYMOUS to the supported mechs and test again.
    standalone.log.info("Edit mechanisms to allow just PLAIN, GSSAPI, and ANONYMOUS")
    standalone.config.set('nsslapd-allowed-sasl-mechanisms', 'PLAIN, GSSAPI, ANONYMOUS')
    limit_mechs = standalone.rootdse.supported_sasl()
    assert('PLAIN' in limit_mechs)
    assert('EXTERNAL' in limit_mechs)
    assert('GSSAPI' in limit_mechs)
    assert('ANONYMOUS' in limit_mechs)
    assert(len(limit_mechs) == 4)

    # Restart server and make sure the allowed list is the same
    standalone.restart()
    standalone.restart()  # For ticket 49379 (test double restart)
    limit_mechs = standalone.rootdse.supported_sasl()
    assert('PLAIN' in limit_mechs)
    assert('EXTERNAL' in limit_mechs)
    assert('GSSAPI' in limit_mechs)
    assert('ANONYMOUS' in limit_mechs)
    assert(len(limit_mechs) == 4)

    # Remove GSSAPI
    standalone.log.info("Edit mechanisms to allow just PLAIN and ANONYMOUS")
    standalone.config.set('nsslapd-allowed-sasl-mechanisms', 'PLAIN, ANONYMOUS')
    limit_mechs = standalone.rootdse.supported_sasl()
    assert('PLAIN' in limit_mechs)
    assert('EXTERNAL' in limit_mechs)
    assert('GSSAPI' not in limit_mechs)
    assert('ANONYMOUS' in limit_mechs)
    assert(len(limit_mechs) == 3)

    # Restart server and make sure the allowed list is the same
    standalone.restart()
    limit_mechs = standalone.rootdse.supported_sasl()
    assert('PLAIN' in limit_mechs)
    assert('EXTERNAL' in limit_mechs)
    assert('GSSAPI' not in limit_mechs)
    assert('ANONYMOUS' in limit_mechs)
    assert(len(limit_mechs) == 3)

    # Do a config reset
    standalone.log.info("Reset allowed mechaisms")
    standalone.config.reset('nsslapd-allowed-sasl-mechanisms')

    # check the supported list is the same as our first check.
    standalone.log.info("Check that we have the original set of mechanisms")
    final_mechs = standalone.rootdse.supported_sasl()
    assert(set(final_mechs) == set(orig_mechs))

    # Check it after a restart
    standalone.log.info("Check that we have the original set of mechanisms after a restart")
    standalone.restart()
    final_mechs = standalone.rootdse.supported_sasl()
    assert(set(final_mechs) == set(orig_mechs))


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
