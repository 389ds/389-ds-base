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

from lib389._constants import PLUGIN_ATTR_UNIQUENESS, DEFAULT_SUFFIX

# Skip on older versions
pytestmark = [pytest.mark.tier2,
              pytest.mark.skipif(ds_is_older('1.3.4'), reason="Not implemented")]

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

USER1_DN = 'uid=user1,' + DEFAULT_SUFFIX
USER2_DN = 'uid=user2,' + DEFAULT_SUFFIX


def test_ticket48026(topology_st):
    '''
    Test that multiple attribute uniqueness works correctly.
    '''
    # Configure the plugin
    inst = topology_st.standalone
    inst.plugins.enable(name=PLUGIN_ATTR_UNIQUENESS)

    try:
        # This plugin enable / disable doesn't seem to create the nsslapd-pluginId correctly?
        inst.modify_s('cn=' + PLUGIN_ATTR_UNIQUENESS + ',cn=plugins,cn=config',
                      [(ldap.MOD_REPLACE, 'uniqueness-attribute-name', b'mail'),
                       (ldap.MOD_ADD, 'uniqueness-attribute-name',
                        b'mailAlternateAddress'),
                       ])
    except ldap.LDAPError as e:
        log.fatal('test_ticket48026: Failed to configure plugin for "mail": error {}'.format(e.args[0]['desc']))
        assert False

    inst.restart(timeout=30)

    # Add an entry
    try:
        inst.add_s(Entry((USER1_DN, {'objectclass': "top extensibleObject".split(),
                                     'sn': '1',
                                     'cn': 'user 1',
                                     'uid': 'user1',
                                     'mail': 'user1@example.com',
                                     'mailAlternateAddress': 'user1@alt.example.com',
                                     'userpassword': 'password'})))
    except ldap.LDAPError as e:
        log.fatal('test_ticket48026: Failed to add test user' + USER1_DN + ': error {}'.format(e.args[0]['desc']))
        assert False

    try:
        inst.add_s(Entry((USER2_DN, {'objectclass': "top extensibleObject".split(),
                                     'sn': '2',
                                     'cn': 'user 2',
                                     'uid': 'user2',
                                     'mail': 'user1@example.com',
                                     'userpassword': 'password'})))
    except ldap.CONSTRAINT_VIOLATION:
        pass
    else:
        log.error('test_ticket48026: Adding of 1st entry(mail v mail) incorrectly succeeded')
        assert False

    try:
        inst.add_s(Entry((USER2_DN, {'objectclass': "top extensibleObject".split(),
                                     'sn': '2',
                                     'cn': 'user 2',
                                     'uid': 'user2',
                                     'mailAlternateAddress': 'user1@alt.example.com',
                                     'userpassword': 'password'})))
    except ldap.CONSTRAINT_VIOLATION:
        pass
    else:
        log.error(
            'test_ticket48026: Adding of 2nd entry(mailAlternateAddress v mailAlternateAddress) incorrectly succeeded')
        assert False

    try:
        inst.add_s(Entry((USER2_DN, {'objectclass': "top extensibleObject".split(),
                                     'sn': '2',
                                     'cn': 'user 2',
                                     'uid': 'user2',
                                     'mail': 'user1@alt.example.com',
                                     'userpassword': 'password'})))
    except ldap.CONSTRAINT_VIOLATION:
        pass
    else:
        log.error('test_ticket48026: Adding of 3rd entry(mail v mailAlternateAddress) incorrectly succeeded')
        assert False

    try:
        inst.add_s(Entry((USER2_DN, {'objectclass': "top extensibleObject".split(),
                                     'sn': '2',
                                     'cn': 'user 2',
                                     'uid': 'user2',
                                     'mailAlternateAddress': 'user1@example.com',
                                     'userpassword': 'password'})))
    except ldap.CONSTRAINT_VIOLATION:
        pass
    else:
        log.error('test_ticket48026: Adding of 4th entry(mailAlternateAddress v mail) incorrectly succeeded')
        assert False

    log.info('Test complete')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
