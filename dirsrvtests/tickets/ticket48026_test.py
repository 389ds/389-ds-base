# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details. 
# --- END COPYRIGHT BLOCK ---
#
import os
import sys
import time
import ldap
import logging
import pytest
from lib389 import DirSrv, Entry, tools, tasks
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from lib389.utils import *

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

installation1_prefix = None

USER1_DN = 'uid=user1,' + DEFAULT_SUFFIX
USER2_DN = 'uid=user2,' + DEFAULT_SUFFIX


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

    return TopologyStandalone(standalone)


def test_ticket48026(topology):
    '''
    Test that multiple attribute uniqueness works correctly.
    '''
    # Configure the plugin
    inst = topology.standalone
    inst.plugins.enable(name=PLUGIN_ATTR_UNIQUENESS)

    try:
        # This plugin enable / disable doesn't seem to create the nsslapd-pluginId correctly?
        inst.modify_s('cn=' + PLUGIN_ATTR_UNIQUENESS + ',cn=plugins,cn=config',
                      [(ldap.MOD_REPLACE, 'uniqueness-attribute-name', 'mail'),
                       (ldap.MOD_ADD, 'uniqueness-attribute-name',
                        'mailAlternateAddress'),
                      ])
    except ldap.LDAPError, e:
        log.fatal('test_ticket48026: Failed to configure plugin for "mail": error ' + e.message['desc'])
        assert False

    inst.restart(timeout=30)

    # Add an entry
    try:
        inst.add_s(Entry((USER1_DN, {'objectclass': "top extensibleObject".split(),
                                     'sn': '1',
                                     'cn': 'user 1',
                                     'uid': 'user1',
                                     'mail': 'user1@example.com',
                                     'mailAlternateAddress' : 'user1@alt.example.com',
                                     'userpassword': 'password'})))
    except ldap.LDAPError, e:
        log.fatal('test_ticket48026: Failed to add test user' + USER1_DN + ': error ' + e.message['desc'])
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
        log.error('test_ticket48026: Adding of 2nd entry(mailAlternateAddress v mailAlternateAddress) incorrectly succeeded')
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


def test_ticket48026_final(topology):
    topology.standalone.delete()
    log.info('Testcase PASSED')


def run_isolated():
    global installation1_prefix
    installation1_prefix = None

    topo = topology(True)
    test_ticket48026(topo)
    test_ticket48026_final(topo)


if __name__ == '__main__':
    run_isolated()

