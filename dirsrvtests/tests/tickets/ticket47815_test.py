# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import time

import ldap
import pytest
from lib389 import Entry
from lib389._constants import *
from lib389.topologies import topology_st

log = logging.getLogger(__name__)
from lib389.utils import *

# Skip on older versions
pytestmark = [pytest.mark.tier2,
              pytest.mark.skipif(ds_is_older('1.3.3') or ds_is_newer('1.3.7'), 
              reason="Not implemented, or invalid by nsMemberOf")]

def test_ticket47815(topology_st):
    """
        Test betxn plugins reject an invalid option, and make sure that the rejected entry
        is not in the entry cache.

        Enable memberOf, automember, and retrocl plugins
        Add the automember config entry
        Add the automember group
        Add a user that will be rejected by a betxn plugin - result error 53
        Attempt the same add again, and it should result in another error 53 (not error 68)
    """
    result = 0
    result2 = 0

    log.info(
        'Testing Ticket 47815 - Add entries that should be rejected by the betxn plugins, and are not left in the entry cache')

    # Enabled the plugins
    topology_st.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)
    topology_st.standalone.plugins.enable(name=PLUGIN_AUTOMEMBER)
    topology_st.standalone.plugins.enable(name=PLUGIN_RETRO_CHANGELOG)

    # configure automember config entry
    log.info('Adding automember config')
    try:
        topology_st.standalone.add_s(Entry(('cn=group cfg,cn=Auto Membership Plugin,cn=plugins,cn=config', {
            'objectclass': 'top autoMemberDefinition'.split(),
            'autoMemberScope': 'dc=example,dc=com',
            'autoMemberFilter': 'cn=user',
            'autoMemberDefaultGroup': 'cn=group,dc=example,dc=com',
            'autoMemberGroupingAttr': 'member:dn',
            'cn': 'group cfg'})))
    except:
        log.error('Failed to add automember config')
        exit(1)

    topology_st.standalone.restart()

    # need to reopen a connection toward the instance
    topology_st.standalone.open()

    # add automember group
    log.info('Adding automember group')
    try:
        topology_st.standalone.add_s(Entry(('cn=group,dc=example,dc=com', {
            'objectclass': 'top groupOfNames'.split(),
            'cn': 'group'})))
    except:
        log.error('Failed to add automember group')
        exit(1)

    # add user that should result in an error 53
    log.info('Adding invalid entry')

    try:
        topology_st.standalone.add_s(Entry(('cn=user,dc=example,dc=com', {
            'objectclass': 'top person'.split(),
            'sn': 'user',
            'cn': 'user'})))
    except ldap.UNWILLING_TO_PERFORM:
        log.debug('Adding invalid entry failed as expected')
        result = 53
    except ldap.LDAPError as e:
        log.error('Unexpected result ' + e.message['desc'])
        assert False
    if result == 0:
        log.error('Add operation unexpectedly succeeded')
        assert False

    # Attempt to add user again, should result in error 53 again
    try:
        topology_st.standalone.add_s(Entry(('cn=user,dc=example,dc=com', {
            'objectclass': 'top person'.split(),
            'sn': 'user',
            'cn': 'user'})))
    except ldap.UNWILLING_TO_PERFORM:
        log.debug('2nd add of invalid entry failed as expected')
        result2 = 53
    except ldap.LDAPError as e:
        log.error('Unexpected result ' + e.message['desc'])
        assert False
    if result2 == 0:
        log.error('2nd Add operation unexpectedly succeeded')
        assert False


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
