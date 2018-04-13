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
from lib389._constants import DEFAULT_SUFFIX, PLUGIN_ATTR_UNIQUENESS

USER1_DN = 'uid=user1,' + DEFAULT_SUFFIX
USER2_DN = 'uid=user2,' + DEFAULT_SUFFIX

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


def test_attr_uniqueness_init(topology_st):
    '''
    Enable dynamic plugins - makes things easier
    '''
    try:
        topology_st.standalone.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE, 'nsslapd-dynamic-plugins', b'on')])
    except ldap.LDAPError as e:
        log.fatal('Failed to enable dynamic plugin!' + e.message['desc'])
        assert False

    topology_st.standalone.plugins.enable(name=PLUGIN_ATTR_UNIQUENESS)


def test_attr_uniqueness(topology_st):
    log.info('Running test_attr_uniqueness...')

    #
    # Configure plugin
    #
    try:
        topology_st.standalone.modify_s('cn=' + PLUGIN_ATTR_UNIQUENESS + ',cn=plugins,cn=config',
                                        [(ldap.MOD_REPLACE, 'uniqueness-attribute-name', b'uid')])

    except ldap.LDAPError as e:
        log.fatal('test_attr_uniqueness: Failed to configure plugin for "uid": error ' + e.message['desc'])
        assert False

    # Add an entry
    try:
        topology_st.standalone.add_s(Entry((USER1_DN, {'objectclass': "top extensibleObject".split(),
                                                       'sn': '1',
                                                       'cn': 'user 1',
                                                       'uid': 'user1',
                                                       'mail': 'user1@example.com',
                                                       'mailAlternateAddress': 'user1@alt.example.com',
                                                       'userpassword': 'password'})))
    except ldap.LDAPError as e:
        log.fatal('test_attr_uniqueness: Failed to add test user' + USER1_DN + ': error ' + e.message['desc'])
        assert False

    # Add an entry with a duplicate "uid"
    try:
        topology_st.standalone.add_s(Entry((USER2_DN, {'objectclass': "top extensibleObject".split(),
                                                       'sn': '2',
                                                       'cn': 'user 2',
                                                       'uid': 'user2',
                                                       'uid': 'user1',
                                                       'userpassword': 'password'})))
    except ldap.CONSTRAINT_VIOLATION:
        pass
    else:
        log.fatal('test_attr_uniqueness: Adding of 2nd entry(uid) incorrectly succeeded')
        assert False

    #
    # Change config to use "mail" instead of "uid"
    #
    try:
        topology_st.standalone.modify_s('cn=' + PLUGIN_ATTR_UNIQUENESS + ',cn=plugins,cn=config',
                                        [(ldap.MOD_REPLACE, 'uniqueness-attribute-name', b'mail')])

    except ldap.LDAPError as e:
        log.fatal('test_attr_uniqueness: Failed to configure plugin for "mail": error ' + e.message['desc'])
        assert False

    #
    # Test plugin - Add an entry, that has a duplicate "mail" value
    #
    try:
        topology_st.standalone.add_s(Entry((USER2_DN, {'objectclass': "top extensibleObject".split(),
                                                       'sn': '2',
                                                       'cn': 'user 2',
                                                       'uid': 'user2',
                                                       'mail': 'user1@example.com',
                                                       'userpassword': 'password'})))
    except ldap.CONSTRAINT_VIOLATION:
        pass
    else:
        log.fatal('test_attr_uniqueness: Adding of 2nd entry(mail) incorrectly succeeded')
        assert False

    #
    # Reconfigure plugin for mail and mailAlternateAddress
    #
    try:
        topology_st.standalone.modify_s('cn=' + PLUGIN_ATTR_UNIQUENESS + ',cn=plugins,cn=config',
                                        [(ldap.MOD_REPLACE, 'uniqueness-attribute-name', b'mail'),
                                         (ldap.MOD_ADD, 'uniqueness-attribute-name',
                                          b'mailAlternateAddress')])
    except ldap.LDAPError as e:
        log.error('test_attr_uniqueness: Failed to reconfigure plugin for "mail mailAlternateAddress": error ' +
                  e.message['desc'])
        assert False

    #
    # Test plugin - Add an entry, that has a duplicate "mail" value
    #
    try:
        topology_st.standalone.add_s(Entry((USER2_DN, {'objectclass': "top extensibleObject".split(),
                                                       'sn': '2',
                                                       'cn': 'user 2',
                                                       'uid': 'user2',
                                                       'mail': 'user1@example.com',
                                                       'userpassword': 'password'})))
    except ldap.CONSTRAINT_VIOLATION:
        pass
    else:
        log.error('test_attr_uniqueness: Adding of 3rd entry(mail) incorrectly succeeded')
        assert False

    #
    # Test plugin - Add an entry, that has a duplicate "mailAlternateAddress" value
    #
    try:
        topology_st.standalone.add_s(Entry((USER2_DN, {'objectclass': "top extensibleObject".split(),
                                                       'sn': '2',
                                                       'cn': 'user 2',
                                                       'uid': 'user2',
                                                       'mailAlternateAddress': 'user1@alt.example.com',
                                                       'userpassword': 'password'})))
    except ldap.CONSTRAINT_VIOLATION:
        pass
    else:
        log.error('test_attr_uniqueness: Adding of 4th entry(mailAlternateAddress) incorrectly succeeded')
        assert False

    #
    # Test plugin - Add an entry, that has a duplicate "mail" value conflicting mailAlternateAddress
    #
    try:
        topology_st.standalone.add_s(Entry((USER2_DN, {'objectclass': "top extensibleObject".split(),
                                                       'sn': '2',
                                                       'cn': 'user 2',
                                                       'uid': 'user2',
                                                       'mail': 'user1@alt.example.com',
                                                       'userpassword': 'password'})))
    except ldap.CONSTRAINT_VIOLATION:
        pass
    else:
        log.error('test_attr_uniqueness: Adding of 5th entry(mailAlternateAddress) incorrectly succeeded')
        assert False

    #
    # Test plugin - Add an entry, that has a duplicate "mailAlternateAddress" conflicting mail
    #
    try:
        topology_st.standalone.add_s(Entry((USER2_DN, {'objectclass': "top extensibleObject".split(),
                                                       'sn': '2',
                                                       'cn': 'user 2',
                                                       'uid': 'user2',
                                                       'mailAlternateAddress': 'user1@example.com',
                                                       'userpassword': 'password'})))
    except ldap.CONSTRAINT_VIOLATION:
        pass
    else:
        log.error('test_attr_uniqueness: Adding of 6th entry(mail) incorrectly succeeded')
        assert False

    #
    # Cleanup
    #
    try:
        topology_st.standalone.delete_s(USER1_DN)
    except ldap.LDAPError as e:
        log.fatal('test_attr_uniqueness: Failed to delete test entry: ' + e.message['desc'])
        assert False

    log.info('test_attr_uniqueness: PASS\n')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
