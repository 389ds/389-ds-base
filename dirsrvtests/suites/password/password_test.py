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

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

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

    return TopologyStandalone(standalone)


def test_password_init(topology):
    '''
    Do init, if necessary
    '''

    return


def test_password_delete_specific_password(topology):
    '''
    Delete a specific userpassword, and make sure it is actually deleted from the entry
    '''

    log.info('Running test_password_delete_specific_password...')

    USER_DN = 'uid=test_entry,' + DEFAULT_SUFFIX

    #
    # Add a test user with a password
    #
    try:
        topology.standalone.add_s(Entry((USER_DN, {'objectclass': "top extensibleObject".split(),
                                 'sn': '1',
                                 'cn': 'user 1',
                                 'uid': 'user1',
                                 'userpassword': PASSWORD})))
    except ldap.LDAPError, e:
        log.fatal('test_password_delete_specific_password: Failed to add test user ' +
                  USER_DN + ': error ' + e.message['desc'])
        assert False

    #
    # Delete the exact password
    #
    try:
        topology.standalone.modify_s(USER_DN, [(ldap.MOD_DELETE, 'userpassword', PASSWORD)])
    except ldap.LDAPError, e:
        log.fatal('test_password_delete_specific_password: Failed to delete userpassword: error ' +
                  e.message['desc'])
        assert False

    #
    # Check the password is actually deleted
    #
    try:
        entry = topology.standalone.search_s(USER_DN, ldap.SCOPE_BASE, 'objectclass=top')
        if entry[0].hasAttr('userpassword'):
            log.fatal('test_password_delete_specific_password: Entry incorrectly still have the userpassword attribute')
            assert False
    except ldap.LDAPError, e:
        log.fatal('test_password_delete_specific_password: Failed to search for user(%s), error: %s' %
                  (USER_DN, e.message('desc')))
        assert False

    #
    # Cleanup
    #
    try:
        topology.standalone.delete_s(USER_DN)
    except ldap.LDAPError, e:
        log.fatal('test_password_delete_specific_password: Failed to delete user(%s), error: %s' %
                  (USER_DN, e.message('desc')))
        assert False

    log.info('test_password_delete_specific_password: PASSED')


def test_password_final(topology):
    topology.standalone.delete()
    log.info('Password test suite PASSED')


def run_isolated():
    global installation1_prefix
    installation1_prefix = None

    topo = topology(True)
    test_password_init(topo)
    test_password_delete_specific_password(topo)
    test_password_final(topo)


if __name__ == '__main__':
    run_isolated()

