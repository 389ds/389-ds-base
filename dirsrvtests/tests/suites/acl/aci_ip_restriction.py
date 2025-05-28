# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
import logging
import time
import ldap
from lib389.topologies import topology_st
from lib389._constants import DEFAULT_SUFFIX
from lib389.idm.domain import Domain


pytestmark = pytest.mark.tier2

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


def test_aci_with_ip_restrictions_does_not_crash_server(topology_st):
    """ Test that ACI's that use IP restrictions do not crash the server at
       shutdown

    :id: 2ca3e3c9-8ac4-4f73-8604-c9133c6c7956
    :setup: Standalone instance
    :steps:
        1. Create an ACI with IP restriction
        2. Anonymous bind
        3. Search for the ACI to engage it
        4. Restart the server
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Server should not crash
    """

    # Add aci to restrict access my ip
    ACI_TARGET = ('(targetattr != "userPassword")')
    ACI_ALLOW = ('(version 3.0;acl "Enable anonymous access - IP"; allow (read,compare,search)')
    ACI_SUBJECT = ('(userdn = "ldap:///anyone") and (ip="127.0.0.1");)')
    ACI = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT
    suffix = Domain(topology_st.standalone, DEFAULT_SUFFIX)

    try:
        suffix.add('aci', ACI)
    except ldap.LDAPError as e:
        log.error('Failed to add aci: ({}) error {}'.format(ACI,e.args[0]['desc']))
        assert False
    except ldap.TYPE_OR_VALUE_EXISTS:
        pass
    time.sleep(1)

    # Anonymous search to engage the aci
    try:
        topology_st.standalone.simple_bind_s("", "", escapehatch="i am sure")
    except ldap.LDAPError as e:
        log.error('Failed to anonymously bind -error {}'.format(e.args[0]['desc']))
        assert False

    try:
        entries = suffix.search(filter='objectclass=*')#topology_st.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 'objectclass=*')
        if not entries:
            log.fatal('Failed return an entries from search')
            assert False
    except ldap.LDAPError as e:
        log.fatal('Search failed: ' + e.message['desc'])
        assert False

    # Restart the server
    topology_st.standalone.restart(timeout=10)

    # Check for crash
    if topology_st.standalone.detectDisorderlyShutdown():
        log.fatal('Server crashed!')
        assert False

    log.info('Test complete')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s {}".format(CURRENT_FILE))
