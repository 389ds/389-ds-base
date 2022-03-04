# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
import os
import ldap
from lib389.utils import ensure_str
from lib389.topologies import topology_st as topo
from lib389._constants import *
from lib389.plugins import MemberOfPlugin
from lib389.idm.user import UserAccount, UserAccounts
from lib389.idm.group import Group, Groups
from lib389.idm.nscontainer import nsContainers

SUBTREE_1 = 'cn=sub1,%s' % SUFFIX
SUBTREE_2 = 'cn=sub2,%s' % SUFFIX
SUBTREE_3 = 'cn=sub3,%s' % SUFFIX

def add_container(inst, dn, name):
    """Creates container entry"""
    conts = nsContainers(inst, dn)
    cont = conts.create(properties={'cn': name})
    return cont

def add_member_and_group(server, cn, group_cn, subtree):
    users = UserAccounts(server, subtree, rdn=None)
    users.create(properties={'uid': f'test_{cn}',
                             'cn': f'test_{cn}',
                             'sn': f'test_{cn}',
                             'description': 'member',
                             'uidNumber': '1000',
                             'gidNumber': '2000',
                             'homeDirectory': '/home/testuser'})
    group = Groups(server, subtree, rdn=None)
    group.create(properties={'cn': group_cn,
                             'member': f'uid=test_{cn},{subtree}',
                             'description': 'group'})

def check_membership(server, user_dn=None, group_dn=None, find_result=True):
    ent = server.getEntry(user_dn, ldap.SCOPE_BASE, "(objectclass=*)", ['memberof'])
    found = False
    if ent.hasAttr('memberof'):
        for val in ent.getValues('memberof'):
            if ensure_str(val) == group_dn:
                found = True
                break

    if find_result:
        assert found
    else:
        assert (not found)

def test_multiple_scopes(topo):
    """Specify memberOf works when multiple include scopes are defined

    :id: fbcd70cc-c83d-4c79-bd5b-2d8f017545ae
    :setup: Standalone Instance
    :steps:
        1. Set multiple include scopes
        2. Test members added to both scopes are correctly updated
        3. Test user outside of scope was not updated
        4. Set exclude scope
        5. Move user into excluded subtree and check the membership is correct
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
    """

    inst = topo.standalone

    # configure plugin
    memberof = MemberOfPlugin(inst)
    memberof.enable()
    memberof.add('memberOfEntryScope', SUBTREE_1)
    memberof.add('memberOfEntryScope', SUBTREE_2)
    inst.restart()

    # Add setup entries
    add_container(inst, SUFFIX, 'sub1')
    add_container(inst, SUFFIX, 'sub2')
    add_container(inst, SUFFIX, 'sub3')
    add_member_and_group(inst, 'm1', 'g1', SUBTREE_1)
    add_member_and_group(inst, 'm2', 'g2', SUBTREE_2)
    add_member_and_group(inst, 'm3', 'g3', SUBTREE_3)

    # Check users 1 and 2 were correctly updated
    check_membership(inst, f'uid=test_m1,{SUBTREE_1}', f'cn=g1,{SUBTREE_1}', True)
    check_membership(inst, f'uid=test_m2,{SUBTREE_2}', f'cn=g2,{SUBTREE_2}', True)

    # Check that user3, which is out of scope, was not updated
    check_membership(inst, f'uid=test_m3,{SUBTREE_3}', f'cn=g1,{SUBTREE_1}', False)
    check_membership(inst, f'uid=test_m3,{SUBTREE_3}', f'cn=g2,{SUBTREE_2}', False)
    check_membership(inst, f'uid=test_m3,{SUBTREE_3}', f'cn=g3,{SUBTREE_3}', False)

    # Set exclude scope
    EXCLUDED_SUBTREE = 'cn=exclude,%s' % SUFFIX
    EXCLUDED_USER = f"uid=test_m1,{EXCLUDED_SUBTREE}"
    INCLUDED_USER = f"uid=test_m1,{SUBTREE_1}"
    GROUP_DN = f'cn=g1,{SUBTREE_1}'

    add_container(inst, SUFFIX, 'exclude')
    memberof.add('memberOfEntryScopeExcludeSubtree', EXCLUDED_SUBTREE)

    # Move user to excluded scope
    user = UserAccount(topo.standalone, dn=INCLUDED_USER)
    user.rename("uid=test_m1", newsuperior=EXCLUDED_SUBTREE)

    # Check memberOf and group are cleaned up
    check_membership(inst, EXCLUDED_USER, GROUP_DN, False)
    group = Group(topo.standalone,  dn=GROUP_DN)
    assert not group.present("member", EXCLUDED_USER)
    assert not group.present("member", INCLUDED_USER)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
