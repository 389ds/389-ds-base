# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import time
import ldap
import logging
import pytest
from lib389 import DirSrv, Entry
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from lib389.utils import *

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)
USER1_DN = 'uid=user1,' + DEFAULT_SUFFIX
USER2_DN = 'uid=user2,' + DEFAULT_SUFFIX
installation1_prefix = None


class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):
    global installation1_prefix
    if installation1_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation1_prefix

    # Creating standalone instance ...
    standalone = DirSrv(verbose=False)
    args_instance[SER_HOST] = HOST_STANDALONE
    args_instance[SER_PORT] = PORT_STANDALONE
    args_instance[SER_SERVERID_PROP] = SERVERID_STANDALONE
    args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
    args_standalone = args_instance.copy()
    standalone.allocate(args_standalone)
    instance_standalone = standalone.exists()
    if instance_standalone:
        standalone.delete()
    standalone.create()
    standalone.open()

    # Clear out the tmp dir
    standalone.clearTmpDir(__file__)

    def fin():
        standalone.delete()
    request.addfinalizer(fin)

    return TopologyStandalone(standalone)


def test_attr_uniqueness_init(topology):
    '''
    Enable dynamic plugins - makes things easier
    '''
    try:
        topology.standalone.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE, 'nsslapd-dynamic-plugins', 'on')])
    except ldap.LDAPError as e:
        ldap.fatal('Failed to enable dynamic plugin!' + e.message['desc'])
        assert False

    topology.standalone.plugins.enable(name=PLUGIN_ATTR_UNIQUENESS)


def test_attr_uniqueness(topology):
    log.info('Running test_attr_uniqueness...')

    #
    # Configure plugin
    #
    try:
        topology.standalone.modify_s('cn=' + PLUGIN_ATTR_UNIQUENESS + ',cn=plugins,cn=config',
                      [(ldap.MOD_REPLACE, 'uniqueness-attribute-name', 'uid')])

    except ldap.LDAPError as e:
        log.fatal('test_attr_uniqueness: Failed to configure plugin for "uid": error ' + e.message['desc'])
        assert False

    # Add an entry
    try:
        topology.standalone.add_s(Entry((USER1_DN, {'objectclass': "top extensibleObject".split(),
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
        topology.standalone.add_s(Entry((USER2_DN, {'objectclass': "top extensibleObject".split(),
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
        topology.standalone.modify_s('cn=' + PLUGIN_ATTR_UNIQUENESS + ',cn=plugins,cn=config',
                      [(ldap.MOD_REPLACE, 'uniqueness-attribute-name', 'mail')])

    except ldap.LDAPError as e:
        log.fatal('test_attr_uniqueness: Failed to configure plugin for "mail": error ' + e.message['desc'])
        assert False

    #
    # Test plugin - Add an entry, that has a duplicate "mail" value
    #
    try:
        topology.standalone.add_s(Entry((USER2_DN, {'objectclass': "top extensibleObject".split(),
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
        topology.standalone.modify_s('cn=' + PLUGIN_ATTR_UNIQUENESS + ',cn=plugins,cn=config',
                      [(ldap.MOD_REPLACE, 'uniqueness-attribute-name', 'mail'),
                       (ldap.MOD_ADD, 'uniqueness-attribute-name',
                        'mailAlternateAddress')])
    except ldap.LDAPError as e:
        log.error('test_attr_uniqueness: Failed to reconfigure plugin for "mail mailAlternateAddress": error ' +
                  e.message['desc'])
        assert False

    #
    # Test plugin - Add an entry, that has a duplicate "mail" value
    #
    try:
        topology.standalone.add_s(Entry((USER2_DN, {'objectclass': "top extensibleObject".split(),
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
        topology.standalone.add_s(Entry((USER2_DN, {'objectclass': "top extensibleObject".split(),
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
        topology.standalone.add_s(Entry((USER2_DN, {'objectclass': "top extensibleObject".split(),
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
        topology.standalone.add_s(Entry((USER2_DN, {'objectclass': "top extensibleObject".split(),
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
        topology.standalone.delete_s(USER1_DN)
    except ldap.LDAPError as e:
        log.fatal('test_attr_uniqueness: Failed to delete test entry: ' + e.message['desc'])
        assert False

    log.info('test_attr_uniqueness: PASS\n')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
