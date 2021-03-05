# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging

import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st
from lib389.replica import ReplicationManager,Replicas

from lib389._constants import (PLUGIN_MEMBER_OF, DEFAULT_SUFFIX, ReplicaRole, REPLICAID_SUPPLIER_1,
                               PLUGIN_RETRO_CHANGELOG, REPLICA_PRECISE_PURGING, REPLICA_PURGE_DELAY,
                               REPLICA_PURGE_INTERVAL)

pytestmark = pytest.mark.tier2

log = logging.getLogger(__name__)

MEMBEROF_PLUGIN_DN = ('cn=' + PLUGIN_MEMBER_OF + ',cn=plugins,cn=config')
GROUP_DN = ("cn=group," + DEFAULT_SUFFIX)
MEMBER_DN_COMP = "uid=member"


def _add_group_with_members(topology_st):
    # Create group
    try:
        topology_st.standalone.add_s(Entry((GROUP_DN,
                                            {'objectclass': 'top groupofnames'.split(),
                                             'cn': 'group'})))
    except ldap.LDAPError as e:
        log.fatal('Failed to add group: error ' + e.args[0]['desc'])
        assert False

    # Add members to the group - set timeout
    log.info('Adding members to the group...')
    for idx in range(1, 5):
        try:
            MEMBER_VAL = ("uid=member%d,%s" % (idx, DEFAULT_SUFFIX))
            topology_st.standalone.modify_s(GROUP_DN,
                                            [(ldap.MOD_ADD,
                                              'member',
                                              ensure_bytes(MEMBER_VAL))])
        except ldap.LDAPError as e:
            log.fatal('Failed to update group: member (%s) - error: %s' %
                      (MEMBER_VAL, e.args[0]['desc']))
            assert False


def _find_retrocl_changes(topology_st, user_dn=None):
    ents = topology_st.standalone.search_s('cn=changelog', ldap.SCOPE_SUBTREE, '(targetDn=%s)' % user_dn)
    return len(ents)


def _find_memberof(topology_st, user_dn=None, group_dn=None, find_result=True):
    ent = topology_st.standalone.getEntry(user_dn, ldap.SCOPE_BASE, "(objectclass=*)", ['memberof'])
    found = False
    if ent.hasAttr('memberof'):

        for val in ent.getValues('memberof'):
            topology_st.standalone.log.info("!!!!!!! %s: memberof->%s" % (user_dn, val))
            if ensure_str(val) == group_dn:
                found = True
                break

    if find_result:
        assert (found)
    else:
        assert (not found)


def test_ticket48759(topology_st):
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
    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.create_first_supplier(topology_st.standalone)
    #
    # enable dynamic plugins, memberof and retro cl plugin
    #
    log.info('Enable plugins...')
    try:
        topology_st.standalone.config.set('nsslapd-dynamic-plugins', 'on')
    except ldap.LDAPError as e:
        ldap.error('Failed to enable dynamic plugins! ' + e.args[0]['desc'])
        assert False

    topology_st.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)
    topology_st.standalone.plugins.enable(name=PLUGIN_RETRO_CHANGELOG)
    # Configure memberOf group attribute
    try:
        topology_st.standalone.modify_s(MEMBEROF_PLUGIN_DN,
                                        [(ldap.MOD_REPLACE,
                                          'memberofgroupattr',
                                          b'member')])
    except ldap.LDAPError as e:
        log.fatal('Failed to configure memberOf plugin: error ' + e.args[0]['desc'])
        assert False

    #
    #  create some users and a group
    #
    log.info('create users and group...')
    for idx in range(1, 5):
        try:
            USER_DN = ("uid=member%d,%s" % (idx, DEFAULT_SUFFIX))
            topology_st.standalone.add_s(Entry((USER_DN,
                                                {'objectclass': 'top extensibleObject'.split(),
                                                 'uid': 'member%d' % (idx)})))
        except ldap.LDAPError as e:
            log.fatal('Failed to add user (%s): error %s' % (USER_DN, e.args[0]['desc']))
            assert False

    _add_group_with_members(topology_st)

    MEMBER_VAL = ("uid=member2,%s" % DEFAULT_SUFFIX)
    time.sleep(1)
    _find_memberof(topology_st, MEMBER_VAL, GROUP_DN, True)

    # delete group
    log.info('delete group...')
    try:
        topology_st.standalone.delete_s(GROUP_DN)
    except ldap.LDAPError as e:
        log.error('Failed to delete entry: ' + e.args[0]['desc'])
        assert False

    time.sleep(1)
    _find_memberof(topology_st, MEMBER_VAL, GROUP_DN, False)

    # add group again
    log.info('add group again')
    _add_group_with_members(topology_st)
    time.sleep(1)
    _find_memberof(topology_st, MEMBER_VAL, GROUP_DN, True)

    #
    # get number of changelog records for one user entry
    log.info('get number of changes for %s before tombstone purging' % MEMBER_VAL)
    changes_pre = _find_retrocl_changes(topology_st, MEMBER_VAL)

    # configure tombstone purging
    args = {REPLICA_PRECISE_PURGING: 'on',
             REPLICA_PURGE_DELAY: '5',
             REPLICA_PURGE_INTERVAL: '5'}
    try:
        Repl_DN = 'cn=replica,cn=dc\\3Dexample\\2Cdc\\3Dcom,cn=mapping tree,cn=config'
        topology_st.standalone.modify_s(Repl_DN,
                                        [(ldap.MOD_ADD, 'nsDS5ReplicaPreciseTombstonePurging', b'on'),
                                         (ldap.MOD_ADD, 'nsDS5ReplicaPurgeDelay', b'5'),
                                         (ldap.MOD_ADD, 'nsDS5ReplicaTombstonePurgeInterval', b'5')])
    except:
        log.fatal('Failed to configure replica')
        assert False

    # Wait for the interval to pass
    log.info('Wait for tombstone purge interval to pass ...')
    time.sleep(6)

    # Add an entry to trigger replication
    log.info('add dummy entry')
    try:
        topology_st.standalone.add_s(Entry(('cn=test_entry,dc=example,dc=com', {
            'objectclass': 'top person'.split(),
            'sn': 'user',
            'cn': 'entry1'})))
    except ldap.LDAPError as e:
        log.error('Failed to add entry: ' + e.args[0]['desc'])
        assert False

    # check memberof is still correct
    time.sleep(1)
    _find_memberof(topology_st, MEMBER_VAL, GROUP_DN, True)

    # Wait for the interval to pass again
    log.info('Wait for tombstone purge interval to pass again...')
    time.sleep(10)

    #
    # get number of changelog records for one user entry
    log.info('get number of changes for %s before tombstone purging' % MEMBER_VAL)
    changes_post = _find_retrocl_changes(topology_st, MEMBER_VAL)

    assert (changes_pre == changes_post)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
