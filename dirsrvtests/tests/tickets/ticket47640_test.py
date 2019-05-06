# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st

from lib389._constants import PLUGIN_LINKED_ATTRS, DEFAULT_SUFFIX

# Skip on older versions
pytestmark = [pytest.mark.tier2,
              pytest.mark.skipif(ds_is_older('1.3.4'), reason="Not implemented")]

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


def test_ticket47640(topology_st):
    '''
    Linked Attrs Plugins - verify that if the plugin fails to update the link entry
    that the entire operation is aborted
    '''

    # Enable Dynamic plugins, and the linked Attrs plugin
    try:
        topology_st.standalone.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE, 'nsslapd-dynamic-plugins', b'on')])
    except ldap.LDAPError as e:
        log.fatal('Failed to enable dynamic plugin!' + e.message['desc'])
        assert False

    try:
        topology_st.standalone.plugins.enable(name=PLUGIN_LINKED_ATTRS)
    except ValueError as e:
        log.fatal('Failed to enable linked attributes plugin!' + e.message['desc'])
        assert False

    # Add the plugin config entry
    try:
        topology_st.standalone.add_s(Entry(('cn=manager link,cn=Linked Attributes,cn=plugins,cn=config', {
            'objectclass': 'top extensibleObject'.split(),
            'cn': 'Manager Link',
            'linkType': 'seeAlso',
            'managedType': 'seeAlso'
        })))
    except ldap.LDAPError as e:
        log.fatal('Failed to add linked attr config entry: error ' + e.message['desc'])
        assert False

    # Add an entry who has a link to an entry that does not exist
    OP_REJECTED = False
    try:
        topology_st.standalone.add_s(Entry(('uid=manager,' + DEFAULT_SUFFIX, {
            'objectclass': 'top extensibleObject'.split(),
            'uid': 'manager',
            'seeAlso': 'uid=user,dc=example,dc=com'
        })))
    except ldap.UNWILLING_TO_PERFORM:
        # Success
        log.info('Add operation correctly rejected.')
        OP_REJECTED = True
    except ldap.LDAPError as e:
        log.fatal('Add operation incorrectly rejected: error %s - ' +
                  'expected "unwilling to perform"' % e.message['desc'])
        assert False
    if not OP_REJECTED:
        log.fatal('Add operation incorrectly allowed')
        assert False

    log.info('Test complete')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
