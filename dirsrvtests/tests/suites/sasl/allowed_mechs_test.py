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
from lib389.utils import ds_is_older

pytestmark = pytest.mark.tier1

def test_basic_feature(topology_st):
    """Test the alloweed sasl mechanism feature

    :id: b0453b91-9955-4e8f-9d2f-a6bf440022b1
    :setup: Standalone instance
    :steps:
        1. Get the default list of mechanisms
        2. Set allowed mechanism PLAIN
        3. Verify the list
        4. Restart the server
        5. Verify that list is still correct
        6. Edit mechanisms to allow just PLAIN and EXTERNAL
        7. Verify the list
        8. Edit mechanisms to allow just PLAIN and GSSAPI
        9. Verify the list
        10. Restart the server
        11. Verify that list is still correct
        12. Edit mechanisms to allow just PLAIN, GSSAPI, and ANONYMOUS
        13. Verify the list
        14. Restart the server
        15. Verify that list is still correct
        16. Edit mechanisms to allow just PLAIN and ANONYMOUS
        17. Verify the list
        18. Restart the server
        19. Verify that list is still correct
        20. Reset the allowed list to nothing,
        21. Verify that the returned mechanisms are the default ones
        22. Restart the server
        23. Verify that list is still correct
    :expectedresults:
        1. GSSAPI, PLAIN and EXTERNAL mechanisms should be acquired
        2. Operation should be successful
        3. List should have - PLAIN, EXTERNAL; shouldn't have - GSSAPI
        4. Server should be restarted
        5. List should have - PLAIN, EXTERNAL; shouldn't have - GSSAPI
        6. Operation should be successful
        7. List should have - PLAIN, EXTERNAL; shouldn't have - GSSAPI
        8. Operation should be successful
        9. List should have - PLAIN, EXTERNAL, GSSAPI
        10. Server should be restarted
        11. List should have - PLAIN, EXTERNAL, GSSAPI
        12. Operation should be successful
        13. List should have - PLAIN, EXTERNAL, GSSAPI, ANONYMOUS
        14. Server should be restarted
        15. List should have - PLAIN, EXTERNAL, GSSAPI, ANONYMOUS
        16. Operation should be successful
        17. List should have - PLAIN, EXTERNAL, ANONYMOUS; shouldn't have - GSSAPI
        18. Server should be restarted
        19. List should have - PLAIN, EXTERNAL, ANONYMOUS; shouldn't have - GSSAPI
        20. Operation should be successful
        21. List should have - PLAIN, EXTERNAL, GSSAPI
        22. Server should be restarted
        23. List should have - PLAIN, EXTERNAL, GSSAPI
    """

    standalone = topology_st.standalone

    # Get the supported mechanisms. This should contain PLAIN, GSSAPI, EXTERNAL at least
    standalone.log.info("Test we have some of the default mechanisms")
    orig_mechs = standalone.rootdse.supported_sasl()
    print(orig_mechs)
    assert('GSSAPI' in orig_mechs)
    assert('PLAIN' in orig_mechs)
    assert('EXTERNAL' in orig_mechs)

    # Now edit the supported mechanisms. Check them again.
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

    # Now edit the supported mechanisms. Check them again.
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

    # Add ANONYMOUS to the supported mechanisms and test again.
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


@pytest.mark.bz1816854
@pytest.mark.ds50869
@pytest.mark.xfail(ds_is_older('1.3.11', '1.4.3.6'), reason="May fail because of bz1816854")
def test_config_set_few_mechs(topology_st):
    """Test that we can successfully set multiple values to nsslapd-allowed-sasl-mechanisms

    :id: d7c3c58b-4fbe-42ab-a8d4-9dd362916d5f
    :setup: Standalone instance
    :steps:
        1. Set nsslapd-allowed-sasl-mechanisms to "PLAIN GSSAPI"
        2. Verify nsslapd-allowed-sasl-mechanisms has the values
    :expectedresults:
        1. Operation should be successful
        2. Operation should be successful
    """

    standalone = topology_st.standalone

    standalone.log.info("Set nsslapd-allowed-sasl-mechanisms to 'PLAIN GSSAPI'")
    standalone.config.set('nsslapd-allowed-sasl-mechanisms', 'PLAIN GSSAPI')

    standalone.log.info("Verify nsslapd-allowed-sasl-mechanisms has the values")
    allowed_mechs = standalone.config.get_attr_val_utf8('nsslapd-allowed-sasl-mechanisms')
    assert('PLAIN' in allowed_mechs)
    assert('GSSAPI' in allowed_mechs)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
