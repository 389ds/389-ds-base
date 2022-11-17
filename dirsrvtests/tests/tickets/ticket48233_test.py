# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
from lib389.utils import *
from lib389.topologies import topology_st

from lib389._constants import DEFAULT_SUFFIX

pytestmark = pytest.mark.tier2

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


def test_ticket48233(topology_st):
    """Test that ACI's that use IP restrictions do not crash the server at
       shutdown
    """

    # Add aci to restrict access my ip
    aci_text = ('(targetattr != "userPassword")(version 3.0;acl ' +
                '"Enable anonymous access - IP"; allow (read,compare,search)' +
                '(userdn = "ldap:///anyone") and (ip="127.0.0.1");)')

    try:
        topology_st.standalone.modify_s(DEFAULT_SUFFIX, [(ldap.MOD_ADD, 'aci', ensure_bytes(aci_text))])
    except ldap.LDAPError as e:
        log.error('Failed to add aci: ({}) error {}'.format(aci_text,e.args[0]['desc']))
        assert False
    time.sleep(1)

    # Anonymous search to engage the aci
    try:
        topology_st.standalone.simple_bind_s("", "")
    except ldap.LDAPError as e:
        log.error('Failed to anonymously bind -error {}'.format(e.args[0]['desc']))
        assert False

    try:
        entries = topology_st.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 'objectclass=*')
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
    pytest.main("-s %s" % CURRENT_FILE)
