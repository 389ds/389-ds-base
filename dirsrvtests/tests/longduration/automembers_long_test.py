# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

"""
Will do stress testing of automember plugin
"""

import os
import pytest

from lib389.tasks import DEFAULT_SUFFIX
from lib389.topologies import topology_m4 as topo_m4
from lib389.idm.nscontainer import nsContainers, nsContainer
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.idm.domain import Domain
from lib389.idm.posixgroup import PosixGroups
from lib389.plugins import AutoMembershipPlugin, AutoMembershipDefinitions, \
    MemberOfPlugin, AutoMembershipRegexRules
from lib389.backend import Backends
from lib389.config import Config
from lib389.replica import ReplicationManager
from lib389.tasks import AutomemberRebuildMembershipTask
from lib389.idm.group import Groups, Group, nsAdminGroups, nsAdminGroup


SUBSUFFIX = f'dc=SubSuffix,{DEFAULT_SUFFIX}'
REPMANDN = "cn=ReplManager"
REPMANSFX = "dc=replmangr,dc=com"
CACHE_SIZE = '-1'
CACHEMEM_SIZE = '10485760'


pytestmark = pytest.mark.tier3


@pytest.fixture(scope="module")
def _create_entries(topo_m4):
    """
    Will act as module .Will set up required user/entries for the test cases.
    """
    for instance in [topo_m4.ms['master1'], topo_m4.ms['master2'],
                     topo_m4.ms['master3'], topo_m4.ms['master4']]:
        assert instance.status()

    for org in ['autouserGroups', 'Employees', 'TaskEmployees']:
        OrganizationalUnits(topo_m4.ms['master1'], DEFAULT_SUFFIX).create(properties={'ou': org})

    Backends(topo_m4.ms['master1']).create(properties={
        'cn': 'SubAutoMembers',
        'nsslapd-suffix': SUBSUFFIX,
        'nsslapd-CACHE_SIZE': CACHE_SIZE,
        'nsslapd-CACHEMEM_SIZE': CACHEMEM_SIZE
    })

    Domain(topo_m4.ms['master1'], SUBSUFFIX).create(properties={
        'dc': SUBSUFFIX.split('=')[1].split(',')[0],
        'aci': [
            f'(targetattr="userPassword")(version 3.0;aci "Replication Manager Access";'
            f'allow (write,compare) userdn="ldap:///{REPMANDN},cn=config";)',
            f'(target ="ldap:///{SUBSUFFIX}")(targetattr !="cn||sn||uid")(version 3.0;'
            f'acl "Group Permission";allow (write)(groupdn = "ldap:///cn=GroupMgr,{SUBSUFFIX}");)',
            f'(target ="ldap:///{SUBSUFFIX}")(targetattr !="userPassword")(version 3.0;'
            f'acl "Anonym-read access"; allow (read,search,compare) (userdn="ldap:///anyone");)']
    })

    for suff, grp in [(DEFAULT_SUFFIX, 'SubDef1'),
                      (DEFAULT_SUFFIX, 'SubDef2'),
                      (DEFAULT_SUFFIX, 'SubDef3'),
                      (DEFAULT_SUFFIX, 'SubDef4'),
                      (DEFAULT_SUFFIX, 'SubDef5'),
                      (DEFAULT_SUFFIX, 'Employees'),
                      (DEFAULT_SUFFIX, 'NewEmployees'),
                      (DEFAULT_SUFFIX, 'testuserGroups'),
                      (SUBSUFFIX, 'subsuffGroups'),
                      (SUBSUFFIX, 'Employees'),
                      (DEFAULT_SUFFIX, 'autoMembersPlugin'),
                      (DEFAULT_SUFFIX, 'replsubGroups'),
                      ("cn=replsubGroups,{}".format(DEFAULT_SUFFIX), 'Managers'),
                      ("cn=replsubGroups,{}".format(DEFAULT_SUFFIX), 'Contractors'),
                      ("cn=replsubGroups,{}".format(DEFAULT_SUFFIX), 'Interns'),
                      ("cn=replsubGroups,{}".format(DEFAULT_SUFFIX), 'Visitors'),
                      ("ou=autouserGroups,{}".format(DEFAULT_SUFFIX), 'SuffDef1'),
                      ("ou=autouserGroups,{}".format(DEFAULT_SUFFIX), 'SuffDef2'),
                      ("ou=autouserGroups,{}".format(DEFAULT_SUFFIX), 'SuffDef3'),
                      ("ou=autouserGroups,{}".format(DEFAULT_SUFFIX), 'SuffDef4'),
                      ("ou=autouserGroups,{}".format(DEFAULT_SUFFIX), 'SuffDef5'),
                      ("ou=autouserGroups,{}".format(DEFAULT_SUFFIX), 'Contractors'),
                      ("ou=autouserGroups,{}".format(DEFAULT_SUFFIX), 'Managers'),
                      ("CN=testuserGroups,{}".format(DEFAULT_SUFFIX), 'TestDef1'),
                      ("CN=testuserGroups,{}".format(DEFAULT_SUFFIX), 'TestDef2'),
                      ("CN=testuserGroups,{}".format(DEFAULT_SUFFIX), 'TestDef3'),
                      ("CN=testuserGroups,{}".format(DEFAULT_SUFFIX), 'TestDef4'),
                      ("CN=testuserGroups,{}".format(DEFAULT_SUFFIX), 'TestDef5')]:
        Groups(topo_m4.ms['master1'], suff, rdn=None).create(properties={'cn': grp})

    for suff, grp, gid in [(SUBSUFFIX, 'SubDef1', '111'),
                           (SUBSUFFIX, 'SubDef2', '222'),
                           (SUBSUFFIX, 'SubDef3', '333'),
                           (SUBSUFFIX, 'SubDef4', '444'),
                           (SUBSUFFIX, 'SubDef5', '555'),
                           ('cn=subsuffGroups,{}'.format(SUBSUFFIX), 'Managers', '666'),
                           ('cn=subsuffGroups,{}'.format(SUBSUFFIX), 'Contractors', '999')]:
        PosixGroups(topo_m4.ms['master1'], suff, rdn=None).create(properties={
            'cn': grp,
            'gidNumber': gid})

    for master in [topo_m4.ms['master1'], topo_m4.ms['master2'],
                   topo_m4.ms['master3'], topo_m4.ms['master4']]:
        AutoMembershipPlugin(master).add("nsslapd-pluginConfigArea",
                                         "cn=autoMembersPlugin,{}".format(DEFAULT_SUFFIX))
        MemberOfPlugin(master).enable()

    automembers = AutoMembershipDefinitions(topo_m4.ms['master1'],
                                            f'cn=autoMembersPlugin,{DEFAULT_SUFFIX}')
    automember1 = automembers.create(properties={
        'cn': 'replsubGroups',
        'autoMemberScope': f'ou=Employees,{DEFAULT_SUFFIX}',
        'autoMemberFilter': "objectclass=posixAccount",
        'autoMemberDefaultGroup': [f'cn=SubDef1,{DEFAULT_SUFFIX}',
                                   f'cn=SubDef2,{DEFAULT_SUFFIX}',
                                   f'cn=SubDef3,{DEFAULT_SUFFIX}',
                                   f'cn=SubDef4,{DEFAULT_SUFFIX}',
                                   f'cn=SubDef5,{DEFAULT_SUFFIX}'],
        'autoMemberGroupingAttr': 'member:dn'
    })

    automembers = AutoMembershipRegexRules(topo_m4.ms['master1'], automember1.dn)
    automembers.create(properties={
        'cn': 'Managers',
        'description': f'Group placement for Managers',
        'autoMemberTargetGroup': [f'cn=Managers,cn=replsubGroups,{DEFAULT_SUFFIX}'],
        'autoMemberInclusiveRegex': ['uidNumber=^5..5$', 'gidNumber=^[1-4]..3$',
                                     'nsAdminGroupName=^Manager$|^Supervisor$'],
        "autoMemberExclusiveRegex": ['uidNumber=^999$',
                                     'gidNumber=^[6-8].0$',
                                     'nsAdminGroupName=^Junior$'],
    })
    automembers.create(properties={
        'cn': 'Contractors',
        'description': f'Group placement for Contractors',
        'autoMemberTargetGroup': [f'cn=Contractors,cn=replsubGroups,{DEFAULT_SUFFIX}'],
        'autoMemberInclusiveRegex': ['uidNumber=^8..5$',
                                     'gidNumber=^[5-9]..3$',
                                     'nsAdminGroupName=^Contract|^Temporary$'],
        "autoMemberExclusiveRegex": ['uidNumber=^[1,3,8]99$',
                                     'gidNumber=^[2-4]00$',
                                     'nsAdminGroupName=^Employee$'],
    })
    automembers.create(properties={
        'cn': 'Interns',
        'description': f'Group placement for Interns',
        'autoMemberTargetGroup': [f'cn=Interns,cn=replsubGroups,{DEFAULT_SUFFIX}'],
        'autoMemberInclusiveRegex': ['uidNumber=^1..6$',
                                     'gidNumber=^[1-9]..3$',
                                     'nsAdminGroupName=^Interns$|^Trainees$'],
        "autoMemberExclusiveRegex": ['uidNumber=^[1-9]99$',
                                     'gidNumber=^[1-9]00$',
                                     'nsAdminGroupName=^Students$'],})
    automembers.create(properties={
        'cn': 'Visitors',
        'description': f'Group placement for Visitors',
        'autoMemberTargetGroup': [f'cn=Visitors,cn=replsubGroups,{DEFAULT_SUFFIX}'],
        'autoMemberInclusiveRegex': ['uidNumber=^1..6$',
                                     'gidNumber=^[1-5]6.3$',
                                     'nsAdminGroupName=^Visitors$'],
        "autoMemberExclusiveRegex": ['uidNumber=^[7-9]99$',
                                     'gidNumber=^[7-9]00$',
                                     'nsAdminGroupName=^Inter'],
    })
    for instance in [topo_m4.ms['master1'], topo_m4.ms['master2'],
                     topo_m4.ms['master3'], topo_m4.ms['master4']]:
        instance.restart()


def delete_users_and_wait(topo_m4, automem_scope):
    """
    Deletes entries after test and waits for replication.
    """
    for user in nsAdminGroups(topo_m4.ms['master1'], automem_scope, rdn=None).list():
        user.delete()
    for master in [topo_m4.ms['master2'], topo_m4.ms['master3'], topo_m4.ms['master4']]:
        ReplicationManager(DEFAULT_SUFFIX).wait_for_replication(topo_m4.ms['master1'],
                                                                master, timeout=30000)


def create_entry(topo_m4, user_id, suffix, uid_no, gid_no, role_usr):
    """
    Will create entries with nsAdminGroup objectclass
    """
    user = nsAdminGroups(topo_m4.ms['master1'], suffix, rdn=None).create(properties={
        'cn': user_id,
        'sn': user_id,
        'uid': user_id,
        'homeDirectory': '/home/{}'.format(user_id),
        'loginShell': '/bin/bash',
        'uidNumber': uid_no,
        'gidNumber': gid_no,
        'objectclass': ['top', 'person', 'posixaccount', 'inetuser',
                        'nsMemberOf', 'nsAccount', 'nsAdminGroup'],
        'nsAdminGroupName': role_usr,
        'seeAlso': 'uid={},{}'.format(user_id, suffix),
        'entrydn': 'uid={},{}'.format(user_id, suffix)
    })
    return user


def test_adding_300_user(topo_m4, _create_entries):
    """
    Adding 300 user entries matching the inclusive regex rules for
    all targetted groups at M1 and checking the same created in M2 & M3
    :id: fcd867bc-be57-11e9-9842-8c16451d917b
    :setup: Instance with 4 masters
    :steps:
        1. Add 300 user entries matching the inclusive regex rules at topo_m4.ms['master1']
        2. Check the same created in rest masters
    :expected results:
        1. Pass
        2. Pass
    """
    user_rdn = "long01usr"
    automem_scope = "ou=Employees,{}".format(DEFAULT_SUFFIX)
    grp_container = "cn=replsubGroups,{}".format(DEFAULT_SUFFIX)
    default_group1 = "cn=SubDef1,{}".format(DEFAULT_SUFFIX)
    default_group2 = "cn=SubDef2,{}".format(DEFAULT_SUFFIX)
    # Adding BulkUsers
    for number in range(300):
        create_entry(topo_m4, f'{user_rdn}{number}', automem_scope, '5795', '5693', 'Contractor')
    try:
        # Check  to sync the entries
        for master in [topo_m4.ms['master2'], topo_m4.ms['master3'], topo_m4.ms['master4']]:
            ReplicationManager(DEFAULT_SUFFIX).wait_for_replication(topo_m4.ms['master1'],
                                                                    master, timeout=30000)
        for instance, grp in [(topo_m4.ms['master2'], 'Managers'),
                              (topo_m4.ms['master3'], 'Contractors'),
                              (topo_m4.ms['master4'], 'Interns')]:
            assert len(nsAdminGroup(
                instance, f'cn={grp},{grp_container}').get_attr_vals_utf8('member')) == 300
        for grp in [default_group1, default_group2]:
            assert not Group(topo_m4.ms['master4'], grp).get_attr_vals_utf8('member')
            assert not Group(topo_m4.ms['master3'], grp).get_attr_vals_utf8('member')

    finally:
        delete_users_and_wait(topo_m4, automem_scope)


def test_adding_1000_users(topo_m4, _create_entries):
    """
    Adding 1000 users matching inclusive regex for Managers/Contractors
    and exclusive regex for Interns/Visitors
    :id: f641e612-be57-11e9-94e6-8c16451d917b
    :setup: Instance with 4 masters
    :steps:
        1. Add 1000 user entries matching the inclusive/exclusive
        regex rules at topo_m4.ms['master1']
        2. Check the same created in rest masters
    :expected results:
        1. Pass
        2. Pass
    """
    automem_scope = "ou=Employees,{}".format(DEFAULT_SUFFIX)
    grp_container = "cn=replsubGroups,{}".format(DEFAULT_SUFFIX)
    default_group1 = "cn=SubDef1,{}".format(DEFAULT_SUFFIX)
    default_group2 = "cn=SubDef2,{}".format(DEFAULT_SUFFIX)
    # Adding 1000 users
    for number in range(1000):
        create_entry(topo_m4, f'automemusrs{number}', automem_scope, '799', '5693', 'Manager')
    try:
        # Check  to sync the entries
        for master in [topo_m4.ms['master2'], topo_m4.ms['master3'], topo_m4.ms['master4']]:
            ReplicationManager(DEFAULT_SUFFIX).wait_for_replication(topo_m4.ms['master1'],
                                                                    master, timeout=30000)
        for instance, grp in [(topo_m4.ms['master1'], 'Managers'),
                              (topo_m4.ms['master3'], 'Contractors')]:
            assert len(nsAdminGroup(
                instance, "cn={},{}".format(grp,
                                            grp_container)).get_attr_vals_utf8('member')) == 1000
        for instance, grp in [(topo_m4.ms['master2'], 'Interns'),
                              (topo_m4.ms['master4'], 'Visitors')]:
            assert not nsAdminGroup(
                instance, "cn={},{}".format(grp, grp_container)).get_attr_vals_utf8('member')
        for grp in [default_group1, default_group2]:
            assert not Group(topo_m4.ms['master2'], grp).get_attr_vals_utf8('member')
            assert not Group(topo_m4.ms['master3'], grp).get_attr_vals_utf8('member')
    finally:
        delete_users_and_wait(topo_m4, automem_scope)


def test_adding_3000_users(topo_m4, _create_entries):
    """
    Adding 3000 users matching all inclusive regex rules and no matching exclusive regex rules
    :id: ee54576e-be57-11e9-b536-8c16451d917b
    :setup: Instance with 4 masters
    :steps:
        1. Add 3000 user entries matching the inclusive/exclusive regex
        rules at topo_m4.ms['master1']
        2. Check the same created in rest masters
    :expected results:
        1. Pass
        2. Pass
    """
    automem_scope = "ou=Employees,{}".format(DEFAULT_SUFFIX)
    grp_container = "cn=replsubGroups,{}".format(DEFAULT_SUFFIX)
    default_group1 = "cn=SubDef3,{}".format(DEFAULT_SUFFIX)
    default_group2 = "cn=SubDef5,{}".format(DEFAULT_SUFFIX)
    # Adding 3000 users
    for number in range(3000):
        create_entry(topo_m4, f'automemusrs{number}', automem_scope, '5995', '5693', 'Manager')
    try:
        for master in [topo_m4.ms['master2'], topo_m4.ms['master3'], topo_m4.ms['master4']]:
            ReplicationManager(DEFAULT_SUFFIX).wait_for_replication(topo_m4.ms['master1'],
                                                                    master, timeout=30000)
        for instance, grp in [(topo_m4.ms['master1'], 'Managers'),
                              (topo_m4.ms['master3'], 'Contractors'),
                              (topo_m4.ms['master2'], 'Interns'),
                              (topo_m4.ms['master4'], 'Visitors')
                              ]:
            assert len(
                nsAdminGroup(instance,
                             "cn={},{}".format(grp,
                                               grp_container)).get_attr_vals_utf8('member')) == 3000
        for grp in [default_group1, default_group2]:
            assert not Group(topo_m4.ms['master2'], grp).get_attr_vals_utf8('member')
            assert not Group(topo_m4.ms['master3'], grp).get_attr_vals_utf8('member')
    finally:
        delete_users_and_wait(topo_m4, automem_scope)


def test_3000_users_matching_all_exclusive_regex(topo_m4, _create_entries):
    """
    Adding 3000 users matching all exclusive regex rules and no matching inclusive regex rules
    :id: e789331e-be57-11e9-b298-8c16451d917b
    :setup: Instance with 4 masters
    :steps:
        1. Add 3000 user entries matching the inclusive/exclusive regex
        rules at topo_m4.ms['master1']
        2. Check the same created in rest masters
    :expected results:
        1. Pass
        2. Pass
    """
    automem_scope = "ou=Employees,{}".format(DEFAULT_SUFFIX)
    grp_container = "cn=replsubGroups,{}".format(DEFAULT_SUFFIX)
    default_group1 = "cn=SubDef1,{}".format(DEFAULT_SUFFIX)
    default_group2 = "cn=SubDef2,{}".format(DEFAULT_SUFFIX)
    default_group4 = "cn=SubDef4,{}".format(DEFAULT_SUFFIX)
    # Adding 3000 users
    for number in range(3000):
        create_entry(topo_m4, f'automemusrs{number}', automem_scope, '399', '700', 'Manager')
    try:
        for master in [topo_m4.ms['master2'], topo_m4.ms['master3'], topo_m4.ms['master4']]:
            ReplicationManager(DEFAULT_SUFFIX).wait_for_replication(topo_m4.ms['master1'],
                                                                    master, timeout=30000)

        for instance, grp in [(topo_m4.ms['master1'], default_group4),
                              (topo_m4.ms['master2'], default_group1),
                              (topo_m4.ms['master3'], default_group2),
                              (topo_m4.ms['master4'], default_group2)]:
            assert len(nsAdminGroup(instance, grp).get_attr_vals_utf8('member')) == 3000
        for grp, instance in [('Managers', topo_m4.ms['master3']),
                              ('Contractors', topo_m4.ms['master2'])]:
            assert not nsAdminGroup(
                instance, "cn={},{}".format(grp, grp_container)).get_attr_vals_utf8('member')

    finally:
        delete_users_and_wait(topo_m4, automem_scope)


def test_no_matching_inclusive_regex_rules(topo_m4, _create_entries):
    """
    Adding 3000 users matching all exclusive regex rules and no matching inclusive regex rules
    :id: e0cc0e16-be57-11e9-9c0f-8c16451d917b
    :setup: Instance with 4 masters
    :steps:
        1. Add 3000 user entries matching the inclusive/exclusive regex
        rules at topo_m4.ms['master1']
        2. Check the same created in rest masters
    :expected results:
        1. Pass
        2. Pass
    """
    automem_scope = "ou=Employees,{}".format(DEFAULT_SUFFIX)
    grp_container = "cn=replsubGroups,{}".format(DEFAULT_SUFFIX)
    default_group1 = "cn=SubDef1,{}".format(DEFAULT_SUFFIX)
    # Adding 3000 users
    for number in range(3000):
        create_entry(topo_m4, f'automemusrs{number}', automem_scope, '399', '700', 'Manager')
    try:
        for master in [topo_m4.ms['master2'], topo_m4.ms['master3'], topo_m4.ms['master4']]:
            ReplicationManager(DEFAULT_SUFFIX).wait_for_replication(topo_m4.ms['master1'],
                                                                    master, timeout=30000)
        for instance, grp in [(topo_m4.ms['master1'], "cn=SubDef4,{}".format(DEFAULT_SUFFIX)),
                              (topo_m4.ms['master2'], default_group1),
                              (topo_m4.ms['master3'], "cn=SubDef2,{}".format(DEFAULT_SUFFIX)),
                              (topo_m4.ms['master4'], "cn=SubDef3,{}".format(DEFAULT_SUFFIX))]:
            assert len(nsAdminGroup(instance, grp).get_attr_vals_utf8('member')) == 3000
        for grp, instance in [('Managers', topo_m4.ms['master3']),
                              ('Contractors', topo_m4.ms['master2'])]:
            assert not nsAdminGroup(
                instance, "cn={},{}".format(grp, grp_container)).get_attr_vals_utf8('member')
    finally:
        delete_users_and_wait(topo_m4, automem_scope)


def test_adding_deleting_and_re_adding_the_same_3000(topo_m4, _create_entries):
    """
    Adding, Deleting and re-adding the same 3000 users matching all
    exclusive regex rules and no matching inclusive regex rules
    :id: d939247c-be57-11e9-825d-8c16451d917b
    :setup: Instance with 4 masters
    :steps:
        1. Add 3000 user entries matching the inclusive/exclusive regex
        rules at topo_m4.ms['master1']
        2. Check the same created in rest masters
        3. Delete 3000 users
        4. Again add 3000 users
        5. Check the same created in rest masters
    :expected results:
        1. Pass
        2. Pass
        3. Pass
        4. Pass
        5. Pass
    """
    automem_scope = "ou=Employees,{}".format(DEFAULT_SUFFIX)
    grp_container = "cn=replsubGroups,{}".format(DEFAULT_SUFFIX)
    default_group1 = "cn=SubDef1,{}".format(DEFAULT_SUFFIX)
    # Adding
    for number in range(3000):
        create_entry(topo_m4, f'automemusrs{number}', automem_scope, '399', '700', 'Manager')
    try:
        for master in [topo_m4.ms['master2'], topo_m4.ms['master3'], topo_m4.ms['master4']]:
            ReplicationManager(DEFAULT_SUFFIX).wait_for_replication(topo_m4.ms['master1'],
                                                                    master, timeout=30000)
        assert len(nsAdminGroup(topo_m4.ms['master2'],
                                default_group1).get_attr_vals_utf8('member')) == 3000
        # Deleting
        for user in nsAdminGroups(topo_m4.ms['master2'], automem_scope, rdn=None).list():
            user.delete()
        for master in [topo_m4.ms['master1'], topo_m4.ms['master3'], topo_m4.ms['master4']]:
            ReplicationManager(DEFAULT_SUFFIX).wait_for_replication(topo_m4.ms['master2'],
                                                                    master, timeout=30000)
        # Again adding
        for number in range(3000):
            create_entry(topo_m4, f'automemusrs{number}', automem_scope, '399', '700', 'Manager')
        for master in [topo_m4.ms['master2'], topo_m4.ms['master3'], topo_m4.ms['master4']]:
            ReplicationManager(DEFAULT_SUFFIX).wait_for_replication(topo_m4.ms['master1'],
                                                                    master, timeout=30000)
        for instance, grp in [(topo_m4.ms['master1'], "cn=SubDef4,{}".format(DEFAULT_SUFFIX)),
                              (topo_m4.ms['master3'], "cn=SubDef5,{}".format(DEFAULT_SUFFIX)),
                              (topo_m4.ms['master4'], "cn=SubDef3,{}".format(DEFAULT_SUFFIX))]:
            assert len(nsAdminGroup(instance, grp).get_attr_vals_utf8('member')) == 3000
        for grp, instance in [('Interns', topo_m4.ms['master3']),
                              ('Contractors', topo_m4.ms['master2'])]:
            assert not nsAdminGroup(
                instance, "cn={},{}".format(grp, grp_container)).get_attr_vals_utf8('member')
    finally:
        delete_users_and_wait(topo_m4, automem_scope)


def test_re_adding_the_same_3000_users(topo_m4, _create_entries):
    """
    Adding, Deleting and re-adding the same 3000 users matching all inclusive
    regex rules and no matching exclusive regex rules
    :id: d2f5f112-be57-11e9-b164-8c16451d917b
    :setup: Instance with 4 masters
    :steps:
        1. Add 3000 user entries matching the inclusive/exclusive regex
        rules at topo_m4.ms['master1']
        2. Check the same created in rest masters
        3. Delete 3000 users
        4. Again add 3000 users
        5. Check the same created in rest masters
    :expected results:
        1. Pass
        2. Pass
        3. Pass
        4. Pass
        5. Pass
    """
    automem_scope = "ou=Employees,{}".format(DEFAULT_SUFFIX)
    grp_container = "cn=replsubGroups,{}".format(DEFAULT_SUFFIX)
    default_group1 = "cn=SubDef3,{}".format(DEFAULT_SUFFIX)
    default_group2 = "cn=SubDef5,{}".format(DEFAULT_SUFFIX)
    # Adding
    for number in range(3000):
        create_entry(topo_m4, f'automemusrs{number}', automem_scope, '5995', '5693', 'Manager')
    try:
        for master in [topo_m4.ms['master1'], topo_m4.ms['master3'], topo_m4.ms['master4']]:
            ReplicationManager(DEFAULT_SUFFIX).wait_for_replication(topo_m4.ms['master2'],
                                                                    master, timeout=30000)
        assert len(nsAdminGroup(
            topo_m4.ms['master2'],
            f'cn=Contractors,{grp_container}').get_attr_vals_utf8('member')) == 3000
        # Deleting
        delete_users_and_wait(topo_m4, automem_scope)

        # re-adding
        for number in range(3000):
            create_entry(topo_m4, f'automemusrs{number}', automem_scope, '5995', '5693', 'Manager')
        for master in [topo_m4.ms['master2'], topo_m4.ms['master3'], topo_m4.ms['master4']]:
            ReplicationManager(DEFAULT_SUFFIX).wait_for_replication(topo_m4.ms['master1'],
                                                                    master, timeout=30000)
        for instance, grp in [(topo_m4.ms['master1'], "cn=Managers,{}".format(grp_container)),
                              (topo_m4.ms['master3'], "cn=Contractors,{}".format(grp_container)),
                              (topo_m4.ms['master4'], "cn=Visitors,{}".format(grp_container)),
                              (topo_m4.ms['master2'], "cn=Interns,{}".format(grp_container))]:
            assert len(nsAdminGroup(instance, grp).get_attr_vals_utf8('member')) == 3000
        for grp, instance in [(default_group2, topo_m4.ms['master4']),
                              (default_group1, topo_m4.ms['master3'])]:
            assert not nsAdminGroup(instance, grp).get_attr_vals_utf8('member')
    finally:
        delete_users_and_wait(topo_m4, automem_scope)


def test_users_with_different_uid_and_gid_nos(topo_m4, _create_entries):
    """
    Adding, Deleting and re-adding the same 3000 users with
    different uid and gid nos, with different inclusive/exclusive matching regex rules
    :id: cc595a1a-be57-11e9-b053-8c16451d917b
    :setup: Instance with 4 masters
    :steps:
        1. Add 3000 user entries matching the inclusive/exclusive regex
        rules at topo_m4.ms['master1']
        2. Check the same created in rest masters
        3. Delete 3000 users
        4. Again add 3000 users
        5. Check the same created in rest masters
    :expected results:
        1. Pass
        2. Pass
        3. Pass
        4. Pass
        5. Pass
    """
    automem_scope = "ou=Employees,{}".format(DEFAULT_SUFFIX)
    grp_container = "cn=replsubGroups,{}".format(DEFAULT_SUFFIX)
    default_group1 = "cn=SubDef3,{}".format(DEFAULT_SUFFIX)
    default_group2 = "cn=SubDef5,{}".format(DEFAULT_SUFFIX)
    # Adding
    for number in range(3000):
        create_entry(topo_m4, f'automemusrs{number}', automem_scope, '3994', '5695', 'OnDeputation')
    try:
        for master in [topo_m4.ms['master2'], topo_m4.ms['master3'], topo_m4.ms['master4']]:
            ReplicationManager(DEFAULT_SUFFIX).wait_for_replication(topo_m4.ms['master1'],
                                                                    master, timeout=30000)
        for intstance, grp in [(topo_m4.ms['master2'], default_group1),
                               (topo_m4.ms['master3'], default_group2)]:
            assert len(nsAdminGroup(intstance, grp).get_attr_vals_utf8('member')) == 3000
        for grp, instance in [('Contractors', topo_m4.ms['master3']),
                              ('Managers', topo_m4.ms['master1'])]:
            assert not nsAdminGroup(
                instance, "cn={},{}".format(grp, grp_container)).get_attr_vals_utf8('member')
        # Deleting
        for user in nsAdminGroups(topo_m4.ms['master1'], automem_scope, rdn=None).list():
            user.delete()
        for master in [topo_m4.ms['master2'], topo_m4.ms['master3'], topo_m4.ms['master4']]:
            ReplicationManager(DEFAULT_SUFFIX).wait_for_replication(topo_m4.ms['master1'],
                                                                    master, timeout=30000)
        # re-adding
        for number in range(3000):
            create_entry(topo_m4, f'automemusrs{number}', automem_scope,
                         '5995', '5693', 'OnDeputation')

        for master in [topo_m4.ms['master2'], topo_m4.ms['master3'], topo_m4.ms['master4']]:
            ReplicationManager(DEFAULT_SUFFIX).wait_for_replication(topo_m4.ms['master1'],
                                                                    master, timeout=30000)
        for grp, instance in [('Contractors', topo_m4.ms['master3']),
                              ('Managers', topo_m4.ms['master1']),
                              ('Interns', topo_m4.ms['master2']),
                              ('Visitors', topo_m4.ms['master4'])]:
            assert len(nsAdminGroup(
                instance, f'cn={grp},{grp_container}').get_attr_vals_utf8('member')) == 3000

        for instance, grp in [(topo_m4.ms['master2'], default_group1),
                              (topo_m4.ms['master3'], default_group2)]:
            assert not nsAdminGroup(instance, grp).get_attr_vals_utf8('member')
    finally:
        delete_users_and_wait(topo_m4, automem_scope)


def test_bulk_users_to_non_automemscope(topo_m4, _create_entries):
    """
    Adding bulk users to non-automem_scope and then running modrdn
    operation to change the ou to automem_scope
    :id: c532dc0c-be57-11e9-bcca-8c16451d917b
    :setup: Instance with 4 masters
    :steps:
        1. Running modrdn operation to change the ou to automem_scope
        2. Add 3000 user entries to non-automem_scope at topo_m4.ms['master1']
        3. Run AutomemberRebuildMembershipTask
        4. Check the same created in rest masters
    :expected results:
        1. Pass
        2. Pass
        3. Pass
        4. Pass
    """
    automem_scope = "cn=EmployeesNew,{}".format(DEFAULT_SUFFIX)
    grp_container = "cn=replsubGroups,{}".format(DEFAULT_SUFFIX)
    default_group1 = "cn=SubDef3,{}".format(DEFAULT_SUFFIX)
    default_group2 = "cn=SubDef5,{}".format(DEFAULT_SUFFIX)
    nsContainers(topo_m4.ms['master1'], DEFAULT_SUFFIX).create(properties={'cn': 'ChangeThisCN'})
    Group(topo_m4.ms['master1'],
          f'cn=replsubGroups,cn=autoMembersPlugin,{DEFAULT_SUFFIX}').replace('autoMemberScope',
                                                                             automem_scope)
    for instance in [topo_m4.ms['master1'], topo_m4.ms['master2'],
                     topo_m4.ms['master3'], topo_m4.ms['master4']]:
        instance.restart()
    # Adding BulkUsers
    for number in range(3000):
        create_entry(topo_m4, f'automemusrs{number}', f'cn=ChangeThisCN,{DEFAULT_SUFFIX}',
                     '5995', '5693', 'Supervisor')
    try:
        for master in [topo_m4.ms['master2'], topo_m4.ms['master3'], topo_m4.ms['master4']]:
            ReplicationManager(DEFAULT_SUFFIX).wait_for_replication(topo_m4.ms['master1'],
                                                                    master, timeout=30000)
        for instance, grp in [(topo_m4.ms['master2'], default_group1),
                              (topo_m4.ms['master1'], "cn=Managers,{}".format(grp_container))]:
            assert not nsAdminGroup(instance, grp).get_attr_vals_utf8('member')
        # Deleting BulkUsers "User_Name" Suffix "Nof_Users"
        topo_m4.ms['master3'].rename_s(f"CN=ChangeThisCN,{DEFAULT_SUFFIX}",
                                       f'cn=EmployeesNew', newsuperior=DEFAULT_SUFFIX, delold=1)
        for master in [topo_m4.ms['master2'], topo_m4.ms['master3'], topo_m4.ms['master4']]:
            ReplicationManager(DEFAULT_SUFFIX).wait_for_replication(topo_m4.ms['master1'],
                                                                    master, timeout=30000)
        AutomemberRebuildMembershipTask(topo_m4.ms['master1']).create(properties={
            'basedn': automem_scope,
            'filter': "objectClass=posixAccount"
        })
        for master in [topo_m4.ms['master2'], topo_m4.ms['master3'], topo_m4.ms['master4']]:
            ReplicationManager(DEFAULT_SUFFIX).wait_for_replication(topo_m4.ms['master1'],
                                                                    master, timeout=30000)
        for instance, grp in [(topo_m4.ms['master1'], 'Managers'),
                              (topo_m4.ms['master2'], 'Interns'),
                              (topo_m4.ms['master3'], 'Contractors'),
                              (topo_m4.ms['master4'], 'Visitors')]:
            assert len(nsAdminGroup(
                instance, f'cn={grp},{grp_container}').get_attr_vals_utf8('member')) == 3000
        for grp, instance in [(default_group1, topo_m4.ms['master2']),
                              (default_group2, topo_m4.ms['master3'])]:
            assert not nsAdminGroup(instance, grp).get_attr_vals_utf8('member')
    finally:
        delete_users_and_wait(topo_m4, automem_scope)
        nsContainer(topo_m4.ms['master1'], "CN=EmployeesNew,{}".format(DEFAULT_SUFFIX)).delete()


def test_automemscope_and_running_modrdn(topo_m4, _create_entries):
    """
    Adding bulk users to non-automem_scope and running modrdn operation
    with new superior to automem_scope
    :id: bf60f958-be57-11e9-945d-8c16451d917b
    :setup: Instance with 4 masters
    :steps:
        1. Running modrdn operation to change the ou to automem_scope
        2. Add 3000 user entries to non-automem_scope at topo_m4.ms['master1']
        3. Run AutomemberRebuildMembershipTask
        4. Check the same created in rest masters
    :expected results:
        1. Pass
        2. Pass
        3. Pass
        4. Pass
    """
    user_rdn = "long09usr"
    automem_scope1 = "ou=Employees,{}".format(DEFAULT_SUFFIX)
    automem_scope2 = "cn=NewEmployees,{}".format(DEFAULT_SUFFIX)
    grp_container = "cn=replsubGroups,{}".format(DEFAULT_SUFFIX)
    default_group1 = "cn=SubDef3,{}".format(DEFAULT_SUFFIX)
    default_group2 = "cn=SubDef5,{}".format(DEFAULT_SUFFIX)
    OrganizationalUnits(topo_m4.ms['master1'],
                        DEFAULT_SUFFIX).create(properties={'ou': 'NewEmployees'})
    Group(topo_m4.ms['master1'],
          f'cn=replsubGroups,cn=autoMembersPlugin,{DEFAULT_SUFFIX}').replace('autoMemberScope',
                                                                             automem_scope2)
    for instance in [topo_m4.ms['master1'], topo_m4.ms['master2'],
                     topo_m4.ms['master3'], topo_m4.ms['master4']]:
        Config(instance).replace('nsslapd-errorlog-level', '73728')
        instance.restart()
    # Adding bulk users
    for number in range(3000):
        create_entry(topo_m4, f'automemusrs{number}', automem_scope1,
                     '3994', '5695', 'OnDeputation')
    try:
        for master in [topo_m4.ms['master2'], topo_m4.ms['master3'], topo_m4.ms['master4']]:
            ReplicationManager(DEFAULT_SUFFIX).wait_for_replication(topo_m4.ms['master1'],
                                                                    master, timeout=30000)
        for grp, instance in [(default_group2, topo_m4.ms['master3']),
                              ("cn=Managers,{}".format(grp_container), topo_m4.ms['master1']),
                              ("cn=Contractors,{}".format(grp_container), topo_m4.ms['master3'])]:
            assert not nsAdminGroup(instance, grp).get_attr_vals_utf8('member')
        count = 0
        for user in nsAdminGroups(topo_m4.ms['master3'], automem_scope1, rdn=None).list():
            topo_m4.ms['master1'].rename_s(user.dn,
                                           f'cn=New{user_rdn}{count}',
                                           newsuperior=automem_scope2, delold=1)
            count += 1
        for master in [topo_m4.ms['master2'], topo_m4.ms['master3'], topo_m4.ms['master4']]:
            ReplicationManager(DEFAULT_SUFFIX).wait_for_replication(topo_m4.ms['master1'],
                                                                    master, timeout=30000)
        AutomemberRebuildMembershipTask(topo_m4.ms['master1']).create(properties={
            'basedn': automem_scope2,
            'filter': "objectClass=posixAccount"
        })
        for master in [topo_m4.ms['master2'], topo_m4.ms['master3'], topo_m4.ms['master4']]:
            ReplicationManager(DEFAULT_SUFFIX).wait_for_replication(topo_m4.ms['master1'],
                                                                    master, timeout=30000)
        for instance, grp in [(topo_m4.ms['master3'], default_group2),
                              (topo_m4.ms['master3'], default_group1)]:
            assert len(nsAdminGroup(instance, grp).get_attr_vals_utf8('member')) == 3000
        for instance, grp in [(topo_m4.ms['master1'], 'Managers'),
                              (topo_m4.ms['master3'], 'Contractors'),
                              (topo_m4.ms['master2'], 'Interns'),
                              (topo_m4.ms['master4'], 'Visitors')]:
            assert not nsAdminGroup(
                instance, "cn={},{}".format(grp, grp_container)).get_attr_vals_utf8('member')
    finally:
        for scope in [automem_scope1, automem_scope2]:
            delete_users_and_wait(topo_m4, scope)


if __name__ == '__main__':
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
