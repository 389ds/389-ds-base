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

log = logging.getLogger(__name__)

installation_prefix = None

MEMBEROF_PLUGIN_DN = ('cn=' + PLUGIN_MEMBER_OF + ',cn=plugins,cn=config')
GROUP_DN = ("cn=group," + DEFAULT_SUFFIX)
MEMBER_DN_COMP = "uid=member"

class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):
    '''
        This fixture is used to standalone topology for the 'module'.
    '''
    global installation_prefix

    if installation_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation_prefix

    standalone = DirSrv(verbose=False)

    # Args for the standalone instance
    args_instance[SER_HOST] = HOST_STANDALONE
    args_instance[SER_PORT] = PORT_STANDALONE
    args_instance[SER_SERVERID_PROP] = SERVERID_STANDALONE
    args_standalone = args_instance.copy()
    standalone.allocate(args_standalone)

    # Get the status of the instance and restart it if it exists
    instance_standalone = standalone.exists()

    # Remove the instance
    if instance_standalone:
        standalone.delete()

    # Create the instance
    standalone.create()

    # Used to retrieve configuration information (dbdir, confdir...)
    standalone.open()

    def fin():
        standalone.delete()
    request.addfinalizer(fin)

    # Here we have standalone instance up and running
    return TopologyStandalone(standalone)

def _add_group_with_members(topology):
    # Create group
    try:
        topology.standalone.add_s(Entry((GROUP_DN,
                                         {'objectclass': 'top groupofnames'.split(),
                                          'cn': 'group'})))
    except ldap.LDAPError as e:
        log.fatal('Failed to add group: error ' + e.message['desc'])
        assert False

    # Add members to the group - set timeout
    log.info('Adding members to the group...')
    for idx in range(1, 5):
        try:
            MEMBER_VAL = ("uid=member%d,%s" % (idx, DEFAULT_SUFFIX))
            topology.standalone.modify_s(GROUP_DN,
                                         [(ldap.MOD_ADD,
                                           'member',
                                           MEMBER_VAL)])
        except ldap.LDAPError as e:
            log.fatal('Failed to update group: member (%s) - error: %s' %
                      (MEMBER_VAL, e.message['desc']))
            assert False

def _find_retrocl_changes(topology, user_dn=None):
    ents = topology.standalone.search_s('cn=changelog', ldap.SCOPE_SUBTREE, '(targetDn=%s)' %user_dn)
    return len(ents)

def _find_memberof(topology, user_dn=None, group_dn=None, find_result=True):
    ent = topology.standalone.getEntry(user_dn, ldap.SCOPE_BASE, "(objectclass=*)", ['memberof'])
    found = False
    if ent.hasAttr('memberof'):

        for val in ent.getValues('memberof'):
            topology.standalone.log.info("!!!!!!! %s: memberof->%s" % (user_dn, val))
            if val == group_dn:
                found = True
                break

    if find_result:
        assert(found)
    else:
        assert(not found)

def test_ticket48759(topology):
    """
    The fix for ticket 48759 has to prevent plugin calls for tombstone purging

    The test uses the memberof and retrocl plugins to verify this.
    In tombstone purging without the fix the mmeberof plugin is called,
        if the tombstone entry is a group,
        it  modifies the user entries for the group
        and if retrocl is enabled this mod is written to the retrocl

    The test sequence is:
    - enable replication
    - enable memberof and retro cl plugin
    - add user entries
    - add a group and add the users as members
    - verify memberof is set to users
    - delete the group
    - verify memberof is removed from users
    - add group again
    - verify memberof is set to users
    - get number of changes in retro cl for one user
    - configure tombstone purging
    - wait for purge interval to pass
    - add a dummy entry to increase maxcsn
    - wait for purge interval to pass two times
    - get number of changes in retro cl for user again
    - assert there was no additional change
    """

    log.info('Testing Ticket 48759 - no plugin calls for tombstone purging')

    #
    # Setup Replication
    #
    log.info('Setting up replication...')
    topology.standalone.replica.enableReplication(suffix=DEFAULT_SUFFIX, role=REPLICAROLE_MASTER,
                                                  replicaId=REPLICAID_MASTER_1)

    #
    # enable dynamic plugins, memberof and retro cl plugin
    #
    log.info('Enable plugins...')
    try:
        topology.standalone.modify_s(DN_CONFIG,
                                     [(ldap.MOD_REPLACE,
                                       'nsslapd-dynamic-plugins',
                                       'on')])
    except ldap.LDAPError as e:
        ldap.error('Failed to enable dynamic plugins! ' + e.message['desc'])
        assert False

    topology.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)
    topology.standalone.plugins.enable(name=PLUGIN_RETRO_CHANGELOG)
    # Configure memberOf group attribute
    try:
        topology.standalone.modify_s(MEMBEROF_PLUGIN_DN,
                                     [(ldap.MOD_REPLACE,
                                       'memberofgroupattr',
                                       'member')])
    except ldap.LDAPError as e:
        log.fatal('Failed to configure memberOf plugin: error ' + e.message['desc'])
        assert False


    #
    #  create some users and a group
    #
    log.info('create users and group...')
    for idx in range(1, 5):
        try:
            USER_DN = ("uid=member%d,%s" % (idx, DEFAULT_SUFFIX))
            topology.standalone.add_s(Entry((USER_DN,
                                             {'objectclass': 'top extensibleObject'.split(),
                                              'uid': 'member%d' % (idx)})))
        except ldap.LDAPError as e:
            log.fatal('Failed to add user (%s): error %s' % (USER_DN, e.message['desc']))
            assert False

    _add_group_with_members(topology)

    MEMBER_VAL = ("uid=member2,%s" % DEFAULT_SUFFIX)
    time.sleep(1)
    _find_memberof(topology, MEMBER_VAL, GROUP_DN, True)

    # delete group
    log.info('delete group...')
    try:
        topology.standalone.delete_s(GROUP_DN)
    except ldap.LDAPError as e:
        log.error('Failed to delete entry: ' + e.message['desc'])
        assert False

    time.sleep(1)
    _find_memberof(topology, MEMBER_VAL, GROUP_DN, False)

    # add group again
    log.info('add group again')
    _add_group_with_members(topology)
    time.sleep(1)
    _find_memberof(topology, MEMBER_VAL, GROUP_DN, True)

    #
    # get number of changelog records for one user entry
    log.info('get number of changes for %s before tombstone purging' % MEMBER_VAL)
    changes_pre = _find_retrocl_changes(topology, MEMBER_VAL)

    # configure tombstone purging
    args = {REPLICA_PRECISE_PURGING: 'on',
            REPLICA_PURGE_DELAY: '5',
            REPLICA_PURGE_INTERVAL: '5'}
    try:
        topology.standalone.replica.setProperties(DEFAULT_SUFFIX, None, None, args)
    except:
        log.fatal('Failed to configure replica')
        assert False

    # Wait for the interval to pass
    log.info('Wait for tombstone purge interval to pass ...')
    time.sleep(6)

    # Add an entry to trigger replication
    log.info('add dummy entry')
    try:
        topology.standalone.add_s(Entry(('cn=test_entry,dc=example,dc=com', {
                                  'objectclass': 'top person'.split(),
                                  'sn': 'user',
                                  'cn': 'entry1'})))
    except ldap.LDAPError as e:
        log.error('Failed to add entry: ' + e.message['desc'])
        assert False

    # check memberof is still correct
    time.sleep(1)
    _find_memberof(topology, MEMBER_VAL, GROUP_DN, True)

    # Wait for the interval to pass again
    log.info('Wait for tombstone purge interval to pass again...')
    time.sleep(10)

    #
    # get number of changelog records for one user entry
    log.info('get number of changes for %s before tombstone purging' % MEMBER_VAL)
    changes_post = _find_retrocl_changes(topology, MEMBER_VAL)

    assert (changes_pre == changes_post)


def test_ticket48759_final(topology):
    log.info('Testcase PASSED')


def run_isolated():
    '''
        run_isolated is used to run these test cases independently of a test scheduler (xunit, py.test..)
        To run isolated without py.test, you need to
            - edit this file and comment '@pytest.fixture' line before 'topology' function.
            - set the installation prefix
            - run this program
    '''
    global installation_prefix
    installation_prefix = None

    topo = topology(True)
    test_ticket48759(topo)
    test_ticket48759_final(topo)

if __name__ == '__main__':
    run_isolated()
