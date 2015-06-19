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


def test_ticket47921(topology):
    '''
    Test that indirect cos reflects the current value of the indirect entry
    '''

    INDIRECT_COS_DN = 'cn=cos definition,' + DEFAULT_SUFFIX
    MANAGER_DN = 'uid=my manager,ou=people,' + DEFAULT_SUFFIX
    USER_DN = 'uid=user,ou=people,' + DEFAULT_SUFFIX

    # Add COS definition
    try:
        topology.standalone.add_s(Entry((INDIRECT_COS_DN,
            {'objectclass': 'top cosSuperDefinition cosIndirectDefinition ldapSubEntry'.split(),
             'cosIndirectSpecifier': 'manager',
             'cosAttribute': 'roomnumber'
            })))
    except ldap.LDAPError, e:
        log.fatal('Failed to add cos defintion, error: ' + e.message['desc'])
        assert False

    # Add manager entry
    try:
        topology.standalone.add_s(Entry((MANAGER_DN,
            {'objectclass': 'top extensibleObject'.split(),
             'uid': 'my manager',
             'roomnumber': '1'
            })))
    except ldap.LDAPError, e:
        log.fatal('Failed to add manager entry, error: ' + e.message['desc'])
        assert False

    # Add user entry
    try:
        topology.standalone.add_s(Entry((USER_DN,
            {'objectclass': 'top person organizationalPerson inetorgperson'.split(),
             'sn': 'last',
             'cn': 'full',
             'givenname': 'mark',
             'uid': 'user',
             'manager': MANAGER_DN
            })))
    except ldap.LDAPError, e:
        log.fatal('Failed to add manager entry, error: ' + e.message['desc'])
        assert False

    # Test COS is working
    try:
        entry = topology.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE,
                                             "uid=user",
                                             ['roomnumber'])
        if entry:
            if entry[0].getValue('roomnumber') != '1':
                log.fatal('COS is not working.')
                assert False
        else:
            log.fatal('Failed to find user entry')
            assert False
    except ldap.LDAPError, e:
        log.error('Failed to search for user entry: ' + e.message['desc'])
        assert False

    # Modify manager entry
    try:
        topology.standalone.modify_s(MANAGER_DN, [(ldap.MOD_REPLACE, 'roomnumber', '2')])
    except ldap.LDAPError, e:
        log.error('Failed to modify manager entry: ' + e.message['desc'])
        assert False

    # Confirm COS is returning the new value
    try:
        entry = topology.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE,
                                             "uid=user",
                                             ['roomnumber'])
        if entry:
            if entry[0].getValue('roomnumber') != '2':
                log.fatal('COS is not working after manager update.')
                assert False
        else:
            log.fatal('Failed to find user entry')
            assert False
    except ldap.LDAPError, e:
        log.error('Failed to search for user entry: ' + e.message['desc'])
        assert False

    log.info('Test complete')


def test_ticket47921_final(topology):
    topology.standalone.delete()
    log.info('Testcase PASSED')


def run_isolated():
    global installation1_prefix
    installation1_prefix = None

    topo = topology(True)
    test_ticket47921(topo)
    test_ticket47921_final(topo)


if __name__ == '__main__':
    run_isolated()

