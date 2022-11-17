# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
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
from lib389 import DirSrv, Entry, tools, tasks
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st as topo

pytestmark = pytest.mark.tier2

DEBUGGING = os.getenv("DEBUGGING", default=False)
GROUP_DN_1 = ("cn=group1," + DEFAULT_SUFFIX)
GROUP_DN_2 = ("cn=group2," + DEFAULT_SUFFIX)
SUPER_GRP1 = ("cn=super_grp1,"  + DEFAULT_SUFFIX)
SUPER_GRP2 = ("cn=super_grp2,"  + DEFAULT_SUFFIX)
SUPER_GRP3 = ("cn=super_grp3,"  + DEFAULT_SUFFIX)

if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

def _add_group_with_members(topo, group_dn):
    # Create group
    try:
        topo.standalone.add_s(Entry((group_dn,
                                      {'objectclass': 'top groupofnames extensibleObject'.split(),
                                       'cn': 'group'})))
    except ldap.LDAPError as e:
        log.fatal('Failed to add group: error ' + e.args[0]['desc'])
        assert False

    # Add members to the group - set timeout
    log.info('Adding members to the group...')
    for idx in range(1, 5):
        try:
            MEMBER_VAL = ("uid=member%d,%s" % (idx, DEFAULT_SUFFIX))
            topo.standalone.modify_s(group_dn,
                                      [(ldap.MOD_ADD,
                                        'member',
                                        ensure_bytes(MEMBER_VAL))])
        except ldap.LDAPError as e:
            log.fatal('Failed to update group: member (%s) - error: %s' %
                      (MEMBER_VAL, e.args[0]['desc']))
            assert False

def _check_memberof(topo, member=None, memberof=True, group_dn=None):
    # Check that members have memberof attribute on M1
    for idx in range(1, 5):
        try:
            USER_DN = ("uid=member%d,%s" % (idx, DEFAULT_SUFFIX))
            ent = topo.standalone.getEntry(USER_DN, ldap.SCOPE_BASE, "(objectclass=*)")
            if presence_flag:
                assert ent.hasAttr('memberof') and ent.getValue('memberof') == ensure_bytes(group_dn)
            else:
                assert not ent.hasAttr('memberof')
        except ldap.LDAPError as e:
            log.fatal('Failed to retrieve user (%s): error %s' % (USER_DN, e.args[0]['desc']))
            assert False
            
def _check_memberof(topo, member=None, memberof=True, group_dn=None):
    ent = topo.standalone.getEntry(member, ldap.SCOPE_BASE, "(objectclass=*)")
    if memberof:
        assert group_dn
        assert ent.hasAttr('memberof') and ensure_bytes(group_dn) in ent.getValues('memberof')
    else:
        if ent.hasAttr('memberof'):
            assert ensure_bytes(group_dn) not in ent.getValues('memberof')

            
def test_ticket49184(topo):
    """Write your testcase here...

    Also, if you need any testcase initialization,
    please, write additional fixture for that(include finalizer).
    """
    
    topo.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)
    topo.standalone.restart(timeout=10)

    #
    #  create some users and a group
    #
    log.info('create users and group...')
    for idx in range(1, 5):
        try:
            USER_DN = ("uid=member%d,%s" % (idx, DEFAULT_SUFFIX))
            topo.standalone.add_s(Entry((USER_DN,
                                          {'objectclass': 'top extensibleObject'.split(),
                                           'uid': 'member%d' % (idx)})))
        except ldap.LDAPError as e:
            log.fatal('Failed to add user (%s): error %s' % (USER_DN, e.args[0]['desc']))
            assert False

    # add all users in GROUP_DN_1 and checks each users is memberof GROUP_DN_1
    _add_group_with_members(topo, GROUP_DN_1)
    for idx in range(1, 5):
        USER_DN = ("uid=member%d,%s" % (idx, DEFAULT_SUFFIX))
        _check_memberof(topo, member=USER_DN, memberof=True, group_dn=GROUP_DN_1 )
        
    # add all users in GROUP_DN_2 and checks each users is memberof GROUP_DN_2
    _add_group_with_members(topo, GROUP_DN_2)
    for idx in range(1, 5):
        USER_DN = ("uid=member%d,%s" % (idx, DEFAULT_SUFFIX))
        _check_memberof(topo, member=USER_DN, memberof=True, group_dn=GROUP_DN_2 )
    
    # add the level 2, 3 and 4 group
    for super_grp in (SUPER_GRP1, SUPER_GRP2, SUPER_GRP3):
        topo.standalone.add_s(Entry((super_grp,
                                          {'objectclass': 'top groupofnames extensibleObject'.split(),
                                           'cn': 'super_grp'})))
    topo.standalone.modify_s(SUPER_GRP1,
                                      [(ldap.MOD_ADD,
                                        'member',
                                        ensure_bytes(GROUP_DN_1)),
                                        (ldap.MOD_ADD,
                                        'member',
                                        ensure_bytes(GROUP_DN_2))])
    topo.standalone.modify_s(SUPER_GRP2,
                                      [(ldap.MOD_ADD,
                                        'member',
                                        ensure_bytes(GROUP_DN_1)),
                                        (ldap.MOD_ADD,
                                        'member',
                                        ensure_bytes(GROUP_DN_2))])
    return
    topo.standalone.delete_s(GROUP_DN_2)
    for idx in range(1, 5):
        USER_DN = ("uid=member%d,%s" % (idx, DEFAULT_SUFFIX))
        _check_memberof(topo, member=USER_DN, memberof=True, group_dn=GROUP_DN_1 )
        _check_memberof(topo, member=USER_DN, memberof=False, group_dn=GROUP_DN_2 )
    
    if DEBUGGING:
        # Add debugging steps(if any)...
        pass


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

