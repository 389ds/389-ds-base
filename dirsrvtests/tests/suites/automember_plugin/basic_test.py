# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

"""
Will test AutoMememer Plugin with AotoMember Task and Retro Changelog
"""

import os
import pytest
import time
import re
from lib389.topologies import topology_m1 as topo
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.idm.domain import Domain
from lib389.idm.posixgroup import PosixGroups
from lib389.plugins import AutoMembershipPlugin, AutoMembershipDefinitions, \
    MemberOfPlugin, AutoMembershipRegexRules, AutoMembershipDefinition, RetroChangelogPlugin
from lib389.backend import Backends
from lib389.config import Config
from lib389._constants import DEFAULT_SUFFIX
from lib389.idm.user import UserAccounts
from lib389.idm.group import Groups, Group, UniqueGroup, nsAdminGroups, nsAdminGroup
from lib389.tasks import Tasks, AutomemberRebuildMembershipTask, ExportTask
from lib389.utils import ds_is_older
from lib389.paths import Paths
import ldap

pytestmark = pytest.mark.tier1

BASE_SUFF = "dc=autoMembers,dc=com"
TEST_BASE = "dc=testAutoMembers,dc=com"
BASE_REPL = "dc=replAutoMembers,dc=com"
SUBSUFFIX = f'dc=SubSuffix,{BASE_SUFF}'
PLUGIN_AUTO = "cn=Auto Membership Plugin,cn=plugins,cn=config"
REPMANDN = "cn=ReplManager"
CACHE_SIZE = '-1'
CACHEMEM_SIZE = '10485760'
AUTO_MEM_SCOPE_TEST = f'ou=Employees,{TEST_BASE}'
AUTO_MEM_SCOPE_BASE = f'ou=Employees,{BASE_SUFF}'


def add_base_entries(topo):
    """
    Will create suffix
    """
    for suffix, backend_name in [(BASE_SUFF, 'AutoMembers'), (SUBSUFFIX, 'SubAutoMembers'),
                                 (TEST_BASE, 'testAutoMembers'), (BASE_REPL, 'ReplAutoMembers'),
                                 ("dc=SubSuffix,{}".format(BASE_REPL), 'ReplSubAutoMembers')]:
        Backends(topo.ms["supplier1"]).create(properties={
            'cn': backend_name,
            'nsslapd-suffix': suffix,
            'nsslapd-CACHE_SIZE': CACHE_SIZE,
            'nsslapd-CACHEMEM_SIZE': CACHEMEM_SIZE})
        Domain(topo.ms["supplier1"], suffix).create(properties={
            'dc': suffix.split('=')[1].split(',')[0],
            'aci': [
                f'(targetattr="userPassword")(version 3.0;aci  "Replication Manager '
                f'Access";allow (write,compare) userdn="ldap:///{REPMANDN},cn=config";)',
                f'(target ="ldap:///{suffix}")(targetattr !="cn||sn||uid") (version 3.0;'
                f'acl "Group Permission";allow (write) '
                f'(groupdn = "ldap:///cn=GroupMgr,{suffix}");)',
                f'(target ="ldap:///{suffix}")(targetattr !="userPassword")(version 3.0;acl '
                f'"Anonym-read access"; allow (read,search,compare)(userdn="ldap:///anyone");)'
            ]
        })
    for suffix, ou_cn in [(BASE_SUFF, 'userGroups'),
                          (BASE_SUFF, 'Employees'),
                          (BASE_SUFF, 'TaskEmployees'),
                          (TEST_BASE, 'Employees')]:
        OrganizationalUnits(topo.ms["supplier1"], suffix).create(properties={'ou': ou_cn})


def add_user(topo, user_id, suffix, uid_no, gid_no, role_usr):
    """
    Will create entries with nsAdminGroup objectclass
    """
    objectclasses = ['top', 'person', 'posixaccount', 'inetuser',
                     'nsMemberOf', 'nsAccount', 'nsAdminGroup']
    if ds_is_older('1.4.0'):
        objectclasses.remove('nsAccount')

    user = nsAdminGroups(topo.ms["supplier1"], suffix, rdn=None).create(properties={
        'cn': user_id,
        'sn': user_id,
        'uid': user_id,
        'homeDirectory': '/home/{}'.format(user_id),
        'loginShell': '/bin/bash',
        'uidNumber': uid_no,
        'gidNumber': gid_no,
        'objectclass': objectclasses,
        'nsAdminGroupName': role_usr,
        'seeAlso': 'uid={},{}'.format(user_id, suffix),
        'entrydn': 'uid={},{}'.format(user_id, suffix)
    })
    return user


def check_groups(topo, group_dn, user_dn, member):
    """
    Will check MEMBATTR
    """
    return bool(Group(topo.ms["supplier1"], group_dn).present(member, user_dn))


def add_group(topo, suffix, group_id):
    """
    Will create groups
    """
    Groups(topo.ms["supplier1"], suffix, rdn=None).create(properties={
        'cn': group_id
    })


def number_memberof(topo, user, number):
    """
    Function to check if the memberOf attribute is present.
    """
    return len(nsAdminGroup(topo.ms["supplier1"], user).get_attr_vals_utf8('memberOf')) == number


def add_group_entries(topo):
    """
    Will create multiple entries needed for this test script
    """
    for suffix, group in [(SUBSUFFIX, 'subsuffGroups'),
                          (SUBSUFFIX, 'Employees'),
                          (TEST_BASE, 'testuserGroups'),
                          ("dc=SubSuffix,{}".format(BASE_REPL), 'replsubGroups'),
                          (BASE_REPL, 'replsubGroups')]:
        add_group(topo, suffix, group)
    for group_cn in ['SubDef1', 'SubDef2', 'SubDef3', 'SubDef4', 'SubDef5']:
        add_group(topo, BASE_REPL, group_cn)
    for user in ['Managers', 'Contractors', 'Interns', 'Visitors']:
        add_group(topo, "cn=replsubGroups,{}".format(BASE_REPL), user)
    for ou_ou, group_cn in [("ou=userGroups,{}".format(BASE_SUFF), 'SuffDef1'),
                            ("ou=userGroups,{}".format(BASE_SUFF), 'SuffDef2'),
                            ("ou=userGroups,{}".format(BASE_SUFF), 'SuffDef3'),
                            ("ou=userGroups,{}".format(BASE_SUFF), 'SuffDef4'),
                            ("ou=userGroups,{}".format(BASE_SUFF), 'SuffDef5'),
                            ("ou=userGroups,{}".format(BASE_SUFF), 'Contractors'),
                            ("ou=userGroups,{}".format(BASE_SUFF), 'Managers'),
                            ("CN=testuserGroups,{}".format(TEST_BASE), 'TestDef1'),
                            ("CN=testuserGroups,{}".format(TEST_BASE), 'TestDef2'),
                            ("CN=testuserGroups,{}".format(TEST_BASE), 'TestDef3'),
                            ("CN=testuserGroups,{}".format(TEST_BASE), 'TestDef4'),
                            ("CN=testuserGroups,{}".format(TEST_BASE), 'TestDef5')]:
        add_group(topo, ou_ou, group_cn)
    for ou_ou, group_cn, grp_no in [(SUBSUFFIX, 'SubDef1', '111'),
                                    (SUBSUFFIX, 'SubDef2', '222'),
                                    (SUBSUFFIX, 'SubDef3', '333'),
                                    (SUBSUFFIX, 'SubDef4', '444'),
                                    (SUBSUFFIX, 'SubDef5', '555'),
                                    ('cn=subsuffGroups,{}'.format(SUBSUFFIX),
                                     'Managers', '666'),
                                    ('cn=subsuffGroups,{}'.format(SUBSUFFIX),
                                     'Contractors', '999')]:
        PosixGroups(topo.ms["supplier1"], ou_ou, rdn=None).create(properties={
            'cn': group_cn,
            'gidNumber': grp_no
        })


def add_member_attr(topo, group_dn, user_dn, member):
    """
    Will add members to groups
    """
    Group(topo.ms["supplier1"], group_dn).add(member, user_dn)


def change_grp_objclass(new_object, member, type_of):
    """
    Will change objectClass
    """
    try:
        type_of.remove(member, None)
    except ldap.NO_SUCH_ATTRIBUTE:
        pass
    type_of.ensure_state(properties={
        'cn': type_of.get_attr_val_utf8('cn'),
        'objectClass': ['top', 'nsMemberOf', new_object]
    })


@pytest.fixture(scope="module")
def _create_all_entries(topo):
    """
    Fixture module that will create required entries for test cases.
    """
    add_base_entries(topo)
    add_group_entries(topo)
    auto = AutoMembershipPlugin(topo.ms["supplier1"])
    auto.add("nsslapd-pluginConfigArea", "cn=autoMembersPlugin,{}".format(BASE_REPL))
    MemberOfPlugin(topo.ms["supplier1"]).enable()
    automembers_definitions = AutoMembershipDefinitions(topo.ms["supplier1"])
    automembers_definitions.create(properties={
        'cn': 'userGroups',
        'autoMemberScope': f'ou=Employees,{BASE_SUFF}',
        'autoMemberFilter': "objectclass=posixAccount",
        'autoMemberDefaultGroup': [
            f'cn=SuffDef1,ou=userGroups,{BASE_SUFF}',
            f'cn=SuffDef2,ou=userGroups,{BASE_SUFF}',
            f'cn=SuffDef3,ou=userGroups,{BASE_SUFF}',
            f'cn=SuffDef4,ou=userGroups,{BASE_SUFF}',
            f'cn=SuffDef5,ou=userGroups,{BASE_SUFF}'
        ],
        'autoMemberGroupingAttr': 'member:dn',
    })

    automembers_definitions.create(properties={
        'cn': 'subsuffGroups',
        'autoMemberScope': f'ou=Employees,{BASE_SUFF}',
        'autoMemberFilter': "objectclass=posixAccount",
        'autoMemberDefaultGroup': [
            f'cn=SubDef1,dc=subSuffix,{BASE_SUFF}',
            f'cn=SubDef2,dc=subSuffix,{BASE_SUFF}',
            f'cn=SubDef3,dc=subSuffix,{BASE_SUFF}',
            f'cn=SubDef4,dc=subSuffix,{BASE_SUFF}',
            f'cn=SubDef5,dc=subSuffix,{BASE_SUFF}',
        ],
        'autoMemberGroupingAttr': 'memberuid:dn',
    })

    automembers_regex_usergroup = AutoMembershipRegexRules(topo.ms["supplier1"],
                                                           f'cn=userGroups,{auto.dn}')
    automembers_regex_usergroup.create(properties={
        'cn': 'Managers',
        'description': f'Group placement for Managers',
        'autoMemberTargetGroup': [f'cn=Managers,ou=userGroups,{BASE_SUFF}'],
        'autoMemberInclusiveRegex': [
            "gidNumber=^9",
            "nsAdminGroupName=^Manager",
        ],
        "autoMemberExclusiveRegex": [
            "gidNumber=^[6-8]",
            "nsAdminGroupName=^Junior$",
        ],
    })

    automembers_regex_usergroup.create(properties={
        'cn': 'Contractors',
        'description': f'Group placement for Contractors',
        'autoMemberTargetGroup': [f'cn=Contractors,ou=userGroups,{BASE_SUFF}'],
        'autoMemberInclusiveRegex': [
            "gidNumber=^1",
            "nsAdminGroupName=Contractor",
        ],
        "autoMemberExclusiveRegex": [
            "gidNumber=^[2-4]",
            "nsAdminGroupName=^Employee$",
        ],
    })

    automembers_regex_sub = AutoMembershipRegexRules(topo.ms["supplier1"],
                                                     f'cn=subsuffGroups,{auto.dn}')
    automembers_regex_sub.create(properties={
        'cn': 'Managers',
        'description': f'Group placement for Managers',
        'autoMemberTargetGroup': [f'cn=Managers,cn=subsuffGroups,dc=subSuffix,{BASE_SUFF}'],
        'autoMemberInclusiveRegex': [
            "gidNumber=^[1-4]..3$",
            "uidNumber=^5.5$",
            "nsAdminGroupName=^Manager$|^Supervisor$",
        ],
        "autoMemberExclusiveRegex": [
            "gidNumber=^[6-8].0$",
            "uidNumber=^999$",
            "nsAdminGroupName=^Junior$",
        ],
    })

    automembers_regex_sub.create(properties={
        'cn': 'Contractors',
        'description': f'Group placement for Contractors',
        'autoMemberTargetGroup': [f'cn=Contractors,cn=subsuffGroups,dc=SubSuffix,{BASE_SUFF}'],
        'autoMemberInclusiveRegex': [
            "gidNumber=^[5-9].3$",
            "uidNumber=^8..5$",
            "nsAdminGroupName=^Contract|^Temporary$",
        ],
        "autoMemberExclusiveRegex": [
            "gidNumber=^[2-4]00$",
            "uidNumber=^[1,3,8]99$",
            "nsAdminGroupName=^Employee$",
        ],
    })
    for cn_name, ou_name in [('testuserGroups', 'Employees'), ('hostGroups', 'HostEntries')]:
        automembers_definitions.create(properties={
            'cn': cn_name,
            'autoMemberScope': f'ou={ou_name},dc=testautoMembers,dc=com',
            'autoMemberFilter': "objectclass=posixAccount",
            'autoMemberDefaultGroup': [
                f'cn=TestDef1,cn={cn_name},dc=testautoMembers,dc=com',
                f'cn=TestDef2,cn={cn_name},dc=testautoMembers,dc=com',
                f'cn=TestDef3,cn={cn_name},dc=testautoMembers,dc=com',
                f'cn=TestDef4,cn={cn_name},dc=testautoMembers,dc=com',
                f'cn=TestDef5,cn={cn_name},dc=testautoMembers,dc=com',
            ],
            'autoMemberGroupingAttr': 'member:dn',
        })

    topo.ms["supplier1"].restart()


def test_disable_the_plug_in(topo, _create_all_entries):
    """Plug-in and check the status

    :id: 4feee76c-e7ff-11e8-836e-8c16451d917b
    :setup: Instance with replication
    :steps:
        1. Disable the plug-in and check the status
        2. Enable the plug-in and check the status
    :expected results:
        1. Should success
        2. Should success
    """
    instance_auto = AutoMembershipPlugin(topo.ms["supplier1"])
    instance_auto.disable()
    assert not instance_auto.status()
    instance_auto.enable()
    assert instance_auto.status()


def test_custom_config_area(topo, _create_all_entries):
    """Custom config area

    :id: 4fefb8cc-e7ff-11e8-92fd-8c16451d917b
    :setup: Instance with replication
    :steps:
        1. Check whether the plugin can be configured for custom config area
        2. After adding custom config area can be removed
    :expected results:
        1. Should success
        2. Should success
    """
    instance_auto = AutoMembershipPlugin(topo.ms["supplier1"])
    instance_auto.replace("nsslapd-pluginConfigArea", DEFAULT_SUFFIX)
    assert instance_auto.get_attr_val_utf8("nsslapd-pluginConfigArea")
    instance_auto.remove("nsslapd-pluginConfigArea", DEFAULT_SUFFIX)
    assert not instance_auto.get_attr_val_utf8("nsslapd-pluginConfigArea")


@pytest.mark.bz834053
def test_ability_to_control_behavior_of_modifiers_name(topo, _create_all_entries):
    """Control behaviour of modifier's name

    :id: 4ff16370-e7ff-11e8-838d-8c16451d917b
    :setup: Instance with replication
    :steps:
        1. Turn on 'nsslapd-plugin-binddn-tracking'
        2. Add an user
        3. Check the creatorsname in the user entry
        4. Check the internalCreatorsname in the user entry
        5. Check the modifiersname in the user entry
        6. Check the internalModifiersname in the user entry
        7. Unset nsslapd-plugin-binddn-tracking attribute under
           cn=config and delete the test enteries
    :expected results:
        1. Should success
        2. Should success
        3. Should success
        4. Should success
        5. Should success
        6. Should success
        7. Should success
    """
    instance1 = topo.ms["supplier1"]
    memberof = MemberOfPlugin(instance1)
    if (memberof.get_memberofdeferredupdate() and memberof.get_memberofdeferredupdate().lower() == "on"):
        delay = 3
    else:
        delay = 0
    configure = Config(instance1)
    configure.replace('nsslapd-plugin-binddn-tracking', 'on')
    instance1.restart()
    assert configure.get_attr_val_utf8('nsslapd-plugin-binddn-tracking') == 'on'
    user = add_user(topo, "User_autoMembers_05", "ou=Employees,{}".format(TEST_BASE),
                    "19", "18", "Supervisor")
    time.sleep(delay)
    # search the User DN name for the creatorsname in user entry
    assert user.get_attr_val_utf8('creatorsname') == 'cn=directory manager'
    # search the User DN name for the internalCreatorsname in user entry
    assert user.get_attr_val_utf8('internalCreatorsname') == \
           'cn=ldbm database,cn=plugins,cn=config'
    # search the internalModifiersname in the user entry
    assert user.get_attr_val_utf8('internalModifiersname') == \
           'cn=MemberOf Plugin,cn=plugins,cn=config'
    # unset nsslapd-plugin-binddn-tracking attribute
    configure.replace('nsslapd-plugin-binddn-tracking', 'off')
    instance1.restart()
    # deleting test enteries of automember05 test case
    user.delete()


def test_posixaccount_objectclass_automemberdefaultgroup(topo, _create_all_entries):
    """Verify the PosixAccount user

    :id: 4ff0f642-e7ff-11e8-ac88-8c16451d917b
    :setup: Instance with replication
    :steps:
        1. Add users with PosixAccount ObjectClass
        2. Verify the same user added as a member to autoMemberDefaultGroup
    :expected results:
        1. Should success
        2. Should success
    """
    test_id = "autoMembers_05"
    instance = topo.ms["supplier1"]
    memberof = MemberOfPlugin(instance)
    if (memberof.get_memberofdeferredupdate() and memberof.get_memberofdeferredupdate().lower() == "on"):
        delay = 3
    else:
        delay = 0
    default_group = "cn=TestDef1,CN=testuserGroups,{}".format(TEST_BASE)
    user = add_user(topo, "User_{}".format(test_id), AUTO_MEM_SCOPE_TEST, "19", "18", "Supervisor")
    time.sleep(delay)
    assert check_groups(topo, default_group, user.dn, "member")
    user.delete()
    time.sleep(delay)
    with pytest.raises(AssertionError):
        assert check_groups(topo, default_group, user.dn, "member")


def test_duplicated_member_attributes_added_when_the_entry_is_re_created(topo, _create_all_entries):
    """Checking whether duplicated member attributes added when the entry is re-created

    :id: 4ff2afaa-e7ff-11e8-8a92-8c16451d917b
    :setup: Instance with replication
    :steps:
        1. Create a user
        2. It should present as member in all automember groups
        3. Delete use
        4. It should not present as member in all automember groups
        5. Recreate same user
        6. It should present as member in all automember groups
    :expected results:
        1. Should success
        2. Should success
        3. Should success
        4. Should success
        5. Should success
        6. Should success
    """
    test_id = "autoMembers_06"
    instance = topo.ms["supplier1"]
    memberof = MemberOfPlugin(instance)
    if (memberof.get_memberofdeferredupdate() and memberof.get_memberofdeferredupdate().lower() == "on"):
        delay = 3
    else:
        delay = 0
    default_group = "cn=TestDef1,CN=testuserGroups,{}".format(TEST_BASE)
    user = add_user(topo, "User_{}".format(test_id), AUTO_MEM_SCOPE_TEST, "19", "16", "Supervisor")
    time.sleep(delay)
    assert check_groups(topo, default_group, user.dn, "member")
    user.delete()
    time.sleep(delay)
    with pytest.raises(AssertionError):
        assert check_groups(topo, default_group, user.dn, "member")
    user = add_user(topo, "User_{}".format(test_id), AUTO_MEM_SCOPE_TEST, "19", "15", "Supervisor")
    time.sleep(delay)
    assert check_groups(topo, default_group, user.dn, "member")
    user.delete()


def test_multi_valued_automemberdefaultgroup_for_hostgroups(topo, _create_all_entries):
    """Multi-valued autoMemberDefaultGroup

    :id: 4ff32a02-e7ff-11e8-99a1-8c16451d917b
    :setup: Instance with replication
    :steps:
        1. Create a user
        2. Check user is present in all Automember Groups as member
        3. Delete the user
        4. Check user is not present in all Automember Groups
    :expected results:
        1. Should success
        2. Should success
        3. Should success
        4. Should success
    """
    test_id = "autoMembers_07"
    instance = topo.ms["supplier1"]
    memberof = MemberOfPlugin(instance)
    if (memberof.get_memberofdeferredupdate() and memberof.get_memberofdeferredupdate().lower() == "on"):
        delay = 3
    else:
        delay = 0
    default_group1 = "cn=TestDef1,CN=testuserGroups,{}".format(TEST_BASE)
    default_group2 = "cn=TestDef2,CN=testuserGroups,{}".format(TEST_BASE)
    default_group3 = "cn=TestDef3,CN=testuserGroups,{}".format(TEST_BASE)
    user = add_user(topo, "User_{}".format(test_id), AUTO_MEM_SCOPE_TEST, "19", "14", "TestEngr")
    time.sleep(delay)
    for grp in [default_group1, default_group2, default_group3]:
        assert check_groups(topo, grp, user.dn, "member")
    user.delete()
    time.sleep(delay)
    with pytest.raises(AssertionError):
        assert check_groups(topo, default_group1, user.dn, "member")


def test_plugin_creates_member_attributes_of_the_automemberdefaultgroup(topo, _create_all_entries):
    """Checking whether plugin creates member attributes if it already
    exists for some of the autoMemberDefaultGroup

    :id: 4ff3ba76-e7ff-11e8-9846-8c16451d917b
    :setup: Instance with replication
    :steps:
        1. Add a non existing user to some groups as member
        2. Then Create the user
        3. Check the same user is present to other groups also as member
    :expected results:
        1. Should success
        2. Should success
        3. Should success
    """
    test_id = "autoMembers_08"
    instance = topo.ms["supplier1"]
    memberof = MemberOfPlugin(instance)
    if (memberof.get_memberofdeferredupdate() and memberof.get_memberofdeferredupdate().lower() == "on"):
        delay = 3
    else:
        delay = 0
    default_group1 = "cn=TestDef1,CN=testuserGroups,{}".format(TEST_BASE)
    default_group2 = "cn=TestDef5,CN=testuserGroups,{}".format(TEST_BASE)
    default_group3 = "cn=TestDef3,CN=testuserGroups,{}".format(TEST_BASE)
    add_member_attr(topo,
                    "cn=TestDef2,CN=testuserGroups,{}".format(TEST_BASE),
                    "uid=User_{},{}".format(test_id, AUTO_MEM_SCOPE_TEST), "member")
    add_member_attr(topo,
                    "cn=TestDef4,CN=testuserGroups,{}".format(TEST_BASE),
                    "uid=User_{},{}".format(test_id, AUTO_MEM_SCOPE_TEST), "member")
    user = add_user(topo, "User_{}".format(test_id), AUTO_MEM_SCOPE_TEST, "19", "14", "TestEngr")
    time.sleep(delay)
    for grp in [default_group1, default_group2, default_group3]:
        assert check_groups(topo, grp, user.dn, "member")
    user.delete()


def test_multi_valued_automemberdefaultgroup_with_uniquemember(topo, _create_all_entries):
    """Multi-valued autoMemberDefaultGroup with uniquemember attributes

    :id: 4ff4461c-e7ff-11e8-8124-8c16451d917b
    :setup: Instance with replication
    :steps:
        1. Modify automember config entry to use uniquemember
        2. Change object class for all groups which is used for  automember grouping
        3. Add user uniquemember attributes
        4. Check uniqueMember attribute in groups
        5. Revert the changes done above
    :expected results:
        1. Should success
        2. Should success
        3. Should success
        4. Should success
        5. Should success
    """
    test_id = "autoMembers_09"
    instance = topo.ms["supplier1"]
    memberof = MemberOfPlugin(instance)
    if (memberof.get_memberofdeferredupdate() and memberof.get_memberofdeferredupdate().lower() == "on"):
        delay = 3
    else:
        delay = 0
    auto = AutoMembershipPlugin(topo.ms["supplier1"])
    # Modify automember config entry to use uniquemember: cn=testuserGroups,PLUGIN_AUTO
    AutoMembershipDefinition(
        instance, "cn=testuserGroups,{}".format(auto.dn)).replace('autoMemberGroupingAttr',
                                                                  "uniquemember: dn")
    instance.restart()
    default_group1 = "cn=TestDef1,CN=testuserGroups,{}".format(TEST_BASE)
    default_group2 = "cn=TestDef2,CN=testuserGroups,{}".format(TEST_BASE)
    default_group3 = "cn=TestDef3,CN=testuserGroups,{}".format(TEST_BASE)
    default_group4 = "cn=TestDef4,CN=testuserGroups,{}".format(TEST_BASE)
    default_group5 = "cn=TestDef5,CN=testuserGroups,{}".format(TEST_BASE)
    for grp in (default_group1, default_group2, default_group3, default_group4, default_group5):
        instance_of_group = Group(topo.ms["supplier1"], grp)
        change_grp_objclass("groupOfUniqueNames", "member", instance_of_group)
    # Add user: uid=User_{test_id}, AutoMemScope
    user = add_user(topo, "User_{}".format(test_id), AUTO_MEM_SCOPE_TEST, "19", "14", "New")
    # Checking groups...
    assert user.dn.lower() in UniqueGroup(topo.ms["supplier1"],
                                          default_group1).get_attr_val_utf8("uniqueMember")
    # Delete user uid=User_{test_id},AutoMemScope
    user.delete()
    # Change the automember config back to using \"member\"
    AutoMembershipDefinition(
        instance, "cn=testuserGroups,{}".format(auto.dn)).replace('autoMemberGroupingAttr',
                                                                  "member: dn")
    for grp in [default_group1, default_group2, default_group3, default_group4, default_group5]:
        instance_of_group = UniqueGroup(topo.ms["supplier1"], grp)
        change_grp_objclass("groupOfNames", "uniquemember", instance_of_group)
    topo.ms["supplier1"].restart()


def test_invalid_automembergroupingattr_member(topo, _create_all_entries):
    """Invalid autoMemberGroupingAttr-member

    :id: 4ff4b598-e7ff-11e8-a3a3-8c16451d917b
    :setup: Instance with replication
    :steps:
        1. Change object class for one group which is used for  automember grouping
        2. Try to add user with invalid parameter
        3. Check member attribute on other groups
        4. Check member attribute on group where object class was changed
        5. Revert the object class where it was changed
    :expected results:
        1. Should success
        2. Should fail (ldap.UNWILLING_TO_PERFORM)
        3. Should success
        4. Should fail (AssertionError)
        5. Should success
    """
    test_id = "autoMembers_10"
    instance = topo.ms["supplier1"]
    memberof = MemberOfPlugin(instance)
    if (memberof.get_memberofdeferredupdate() and memberof.get_memberofdeferredupdate().lower() == "on"):
        delay = 3
    else:
        delay = 0
    default_group = "cn=TestDef1,CN=testuserGroups,{}".format(TEST_BASE)
    instance_of_group = Group(topo.ms["supplier1"], default_group)
    change_grp_objclass("groupOfUniqueNames", "member", instance_of_group)
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        add_user(topo, "User_{}".format(test_id), AUTO_MEM_SCOPE_TEST, "19", "20", "Invalid")
    time.sleep(delay)
    with pytest.raises(AssertionError):
        assert check_groups(topo, default_group,
                            "uid=User_{},{}".format(test_id, AUTO_MEM_SCOPE_TEST), "member")
    change_grp_objclass("groupOfNames", "uniquemember", instance_of_group)


def test_valid_and_invalid_automembergroupingattr(topo, _create_all_entries):
    """Valid and invalid autoMemberGroupingAttr

    :id: 4ff4fad0-e7ff-11e8-9cbd-8c16451d917b
    :setup: Instance with replication
    :steps:
        1. Change object class for some groups which is used for  automember grouping
        2. Try to add user with invalid parameter
        3. Check member attribute on other groups
        4. Check member attribute on groups where object class was changed
        5. Revert the object class where it was changed
    :expected results:
        1. Should success
        2. Should fail (ldap.UNWILLING_TO_PERFORM)
        3. Should success
        4. Should fail (AssertionError)
        5. Should success
    """
    test_id = "autoMembers_11"
    instance = topo.ms["supplier1"]
    memberof = MemberOfPlugin(instance)
    if (memberof.get_memberofdeferredupdate() and memberof.get_memberofdeferredupdate().lower() == "on"):
        singleTXN = False
        delay = 3
    else:
        singleTXN = True
        delay = 0
    default_group_1 = "cn=TestDef1,CN=testuserGroups,{}".format(TEST_BASE)
    default_group_2 = "cn=TestDef2,CN=testuserGroups,{}".format(TEST_BASE)
    default_group_3 = "cn=TestDef3,CN=testuserGroups,{}".format(TEST_BASE)
    default_group_4 = "cn=TestDef4,CN=testuserGroups,{}".format(TEST_BASE)
    default_group_5 = "cn=TestDef5,CN=testuserGroups,{}".format(TEST_BASE)
    grp_4_5 = [default_group_4, default_group_5]
    for grp in grp_4_5:
        instance_of_group = Group(topo.ms["supplier1"], grp)
        change_grp_objclass("groupOfUniqueNames", "member", instance_of_group)
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        add_user(topo, "User_{}".format(test_id), AUTO_MEM_SCOPE_TEST, "19", "24", "MixUsers")
    time.sleep(delay)
    for grp in [default_group_1, default_group_2, default_group_3]:
        assert not check_groups(topo, grp, "cn=User_{},{}".format(test_id,
                                                                  AUTO_MEM_SCOPE_TEST), "member")
    for grp in grp_4_5:
        with pytest.raises(AssertionError):
            assert check_groups(topo, grp, "cn=User_{},{}".format(test_id,
                                                                  AUTO_MEM_SCOPE_TEST), "member")
    for grp in grp_4_5:
        instance_of_group = Group(topo.ms["supplier1"], grp)
        change_grp_objclass("groupOfNames", "uniquemember", instance_of_group)


def test_add_regular_expressions_for_user_groups_and_check_for_member_attribute_after_adding_users(
        topo, _create_all_entries):
    """Regular expressions for user groups

    :id: 4ff53fc2-e7ff-11e8-9a18-8c16451d917b
    :setup: Instance with replication
    :steps:
        1. Add user with a match with regular expressions for user groups
        2. check for member attribute after adding users
    :expected results:
        1. Should success
        2. Should success
    """
    test_id = "autoMembers_12"
    instance = topo.ms["supplier1"]
    memberof = MemberOfPlugin(instance)
    if (memberof.get_memberofdeferredupdate() and memberof.get_memberofdeferredupdate().lower() == "on"):
        delay = 3
    else:
        delay = 0
    default_group = f'cn=SuffDef1,ou=userGroups,{BASE_SUFF}'
    user = add_user(topo, "User_{}".format(test_id), AUTO_MEM_SCOPE_BASE, "19", "0", "HR")
    time.sleep(delay)
    assert check_groups(topo, default_group, user.dn, "member")
    assert number_memberof(topo, user.dn, 5)
    user.delete()


LIST_FOR_PARAMETERIZATION = [
    ("autoMembers_22", "5288", "5289", "Contractor", "5291", "5292", "Contractors"),
    ("autoMembers_21", "1161", "1162", "Contractor", "1162", "1163", "Contractors"),
    ("autoMembers_20", "1188", "1189", "CEO", "1191", "1192", "Contractors"),
    ("autoMembers_15", "9288", "9289", "Manager", "9291", "9292", "Managers"),
    ("autoMembers_14", "561", "562", "Manager", "562", "563", "Managers"),
    ("autoMembers_13", "9788", "9789", "VPEngg", "9392", "9393", "Managers")]


@pytest.mark.parametrize("testid, uid, gid, role, uid2, gid2, m_grp", LIST_FOR_PARAMETERIZATION)
def test_matching_gid_role_inclusive_regular_expression(topo, _create_all_entries,
                                                        testid, uid, gid, role, uid2, gid2, m_grp):
    """Matching gid nos and Role for the Inclusive regular expression

    :id: 4ff71ce8-e7ff-11e8-b69b-8c16451d917b
    :parametrized: yes
    :setup: Instance with replication
    :steps:
        1. Create users with matching gid nos and Role for the Inclusive regular expression
        2. It will be filtered with gidNumber, uidNumber and nsAdminGroupName
        3. It will a match for contract_grp
    :expected results:
        1. Should success
        2. Should success
        3. Should success
    """
    contract_grp = f'cn={m_grp},ou=userGroups,{BASE_SUFF}'
    user1 = add_user(topo, "User_{}".format(testid), AUTO_MEM_SCOPE_BASE, uid, gid, role)
    user2 = add_user(topo, "SecondUser_{}".format(testid), AUTO_MEM_SCOPE_BASE,
                     uid2, gid2, role)
    instance = topo.ms["supplier1"]
    memberof = MemberOfPlugin(instance)
    if (memberof.get_memberofdeferredupdate() and memberof.get_memberofdeferredupdate().lower() == "on"):
        delay = 3
    else:
        delay = 0
    time.sleep(delay)
    for user_dn in [user1.dn, user2.dn]:
        assert check_groups(topo, contract_grp, user_dn, "member")
    assert number_memberof(topo, user1.dn, 1)
    for user in [user1, user2]:
        user.delete()


LIST_FOR_PARAMETERIZATION = [
    ("autoMembers_26", "5788", "5789", "Intern", "Contractors", "SuffDef1", 5),
    ("autoMembers_25", "9788", "9789", "Employee", "Contractors", "Managers", 1),
    ("autoMembers_24", "1110", "1111", "Employee", "Contractors", "SuffDef1", 5),
    ("autoMembers_23", "2788", "2789", "Contractor", "Contractors", "SuffDef1", 5),
    ("autoMembers_19", "5788", "5789", "HRManager", "Managers", "SuffDef1", 5),
    ("autoMembers_18", "6788", "6789", "Junior", "Managers", "SuffDef1", 5),
    ("autoMembers_17", "562", "563", "Junior", "Managers", "SuffDef1", 5),
    ("autoMembers_16", "6788", "6789", "Manager", "Managers", "SuffDef1", 5)]


@pytest.mark.parametrize("testid, uid, gid, role, c_grp, m_grp, number", LIST_FOR_PARAMETERIZATION)
def test_gid_and_role_inclusive_exclusive_regular_expression(topo, _create_all_entries,
                                                             testid, uid, gid, role,
                                                             c_grp, m_grp, number):
    """Matching gid nos and Role for the Inclusive and Exclusive regular expression

    :id: 4ff7d160-e7ff-11e8-8fbc-8c16451d917b
    :parametrized: yes
    :setup: Instance with replication
    :steps:
        1. Create user with not matching gid nos and Role for
        the Inclusive and Exclusive regular expression
        2. It will be filtered with gidNumber, uidNumber and nsAdminGroupName
        3. It will not match for contract_grp(Exclusive regular expression)
        4. It will match for default_group(Inclusive regular expression)
    :expected results:
        1. Should success
        2. Should success
        3. Should success
        4. Should success
    """
    instance = topo.ms["supplier1"]
    memberof = MemberOfPlugin(instance)
    if (memberof.get_memberofdeferredupdate() and memberof.get_memberofdeferredupdate().lower() == "on"):
        delay = 3
    else:
        delay = 0
    contract_grp = f'cn={c_grp},ou=userGroups,{BASE_SUFF}'
    default_group = f'cn={m_grp},ou=userGroups,{BASE_SUFF}'
    user = add_user(topo, "User_{}".format(testid), AUTO_MEM_SCOPE_BASE, uid, gid, role)
    time.sleep(delay)
    with pytest.raises(AssertionError):
        assert check_groups(topo, contract_grp, user.dn, "member")
    check_groups(topo, default_group, user.dn, "member")
    assert number_memberof(topo, user.dn, number)
    user.delete()


LIST_FOR_PARAMETERIZATION = [
    ("autoMembers_32", "555", "720", "Employee", "SubDef1", "SubDef3"),
    ("autoMembers_31", "515", "200", "Junior", "SubDef1", "SubDef5"),
    ("autoMembers_30", "999", "400", "Supervisor", "SubDef1", "SubDef2"),
    ("autoMembers_28", "555", "3663", "ContractHR", "Contractors,cn=subsuffGroups",
     "Managers,cn=subsuffGroups")]


@pytest.mark.parametrize("testid, uid, gid, role, c_grp, m_grp", LIST_FOR_PARAMETERIZATION)
def test_managers_contractors_exclusive_regex_rules_member_uid(topo, _create_all_entries,
                                                               testid, uid, gid, role,
                                                               c_grp, m_grp):
    """Match both managers and contractors exclusive regex rules

    :id: 4ff8be18-e7ff-11e8-94aa-8c16451d917b
    :parametrized: yes
    :setup: Instance with replication
    :steps:
        1. Add Users to match both managers and contractors exclusive regex rules,
        memberUid created in Default grp
        2. It will be filtered with gidNumber, uidNumber and nsAdminGroupName
        3. It will match for default_group1 and default_group2(Inclusive regular expression)
    :expected results:
        1. Should success
        2. Should success
        3. Should success
    """
    default_group1 = f'cn={c_grp},{SUBSUFFIX}'
    default_group2 = f'cn={m_grp},{SUBSUFFIX}'
    instance = topo.ms["supplier1"]
    memberof = MemberOfPlugin(instance)
    if (memberof.get_memberofdeferredupdate() and memberof.get_memberofdeferredupdate().lower() == "on"):
        delay = 3
    else:
        delay = 0
    user = add_user(topo, "User_{}".format(testid), AUTO_MEM_SCOPE_BASE, uid, gid, role)
    time.sleep(delay)
    for group in [default_group1, default_group2]:
        assert check_groups(topo, group, user.dn, "memberuid")
    user.delete()


LIST_FOR_PARAMETERIZATION = [
    ("autoMembers_27", "595", "690", "ContractHR", "Managers", "Contractors"),
    ("autoMembers_29", "8195", "2753", "Employee", "Contractors", "Managers"),
    ("autoMembers_33", "545", "3333", "Supervisor", "Contractors", "Managers"),
    ("autoMembers_34", "8195", "693", "Temporary", "Managers", "Contractors")]


@pytest.mark.parametrize("testid, uid, gid, role, c_grp, m_grp", LIST_FOR_PARAMETERIZATION)
def test_managers_inclusive_regex_rule(topo, _create_all_entries,
                                       testid, uid, gid, role, c_grp, m_grp):
    """Match managers inclusive regex rule, and no
    inclusive/exclusive Contractors regex rules

    :id: 4ff8d862-e7ff-11e8-b688-8c16451d917b
    :parametrized: yes
    :setup: Instance with replication
    :steps:
        1. Add User to match managers inclusive regex rule, and no
        inclusive/exclusive Contractors regex rules
        2. It will be filtered with gidNumber, uidNumber and nsAdminGroupName(Supervisor)
        3. It will match for managers_grp(Inclusive regular expression)
        4. It will not match for contract_grp(Exclusive regular expression)
    :expected results:
        1. Should success
        2. Should success
        3. Should success
        4. Should success
    """
    contract_grp = f'cn={c_grp},cn=subsuffGroups,{SUBSUFFIX}'
    managers_grp = f'cn={m_grp},cn=subsuffGroups,{SUBSUFFIX}'
    user = add_user(topo, "User_{}".format(testid), AUTO_MEM_SCOPE_BASE, uid, gid, role)
    check_groups(topo, managers_grp, user.dn, "memberuid")
    with pytest.raises(AssertionError):
        assert check_groups(topo, contract_grp, user.dn, "memberuid")
    user.delete()


def test_reject_invalid_config_and_we_donot_deadlock_the_server(topo, _create_all_entries):
    """Verify DS reject invalid config, and we don't deadlock the server

    :id: 4ff90c38-e7ff-11e8-b72a-8c16451d917b
    :setup: Instance with replication
    :steps:
        1. Verify DS reject invalid config,
        2. This operation don't deadlock the server
    :expected results:
        1. Should success
        2. Should success
    """
    # Changing config area to dc=automembers,dc=com
    instance = AutoMembershipPlugin(topo.ms["supplier1"])
    instance.replace("nsslapd-pluginConfigArea", BASE_SUFF)
    topo.ms["supplier1"] .restart()
    # Attempting to add invalid config...
    automembers = AutoMembershipDefinitions(topo.ms["supplier1"], BASE_SUFF)
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        automembers.create(properties={
            'cn': 'userGroups',
            "autoMemberScope": BASE_SUFF,
            "autoMemberFilter": "objectclass=posixAccount",
            "autoMemberDefaultGroup": f'cn=SuffDef1,ou=userGroups,{BASE_SUFF}',
            "autoMemberGroupingAttr": "member: dn"
        })
    # Verify server is still working
    automembers = AutoMembershipRegexRules(topo.ms["supplier1"],
                                           f'cn=userGroups,cn=Auto Membership Plugin,'
                                           f'cn=plugins,cn=config')
    with pytest.raises(ldap.ALREADY_EXISTS):
        automembers.create(properties={
            'cn': 'Managers',
            'description': f'Group placement for Managers',
            'autoMemberTargetGroup': [f'cn=Managers,ou=userGroups,{BASE_SUFF}'],
            'autoMemberInclusiveRegex': [
                "gidNumber=^9",
                "nsAdminGroupName=^Manager",
            ],
        })

    # Adding first user...
    for uid in range(300, 302):
        UserAccounts(topo.ms["supplier1"], BASE_SUFF, rdn=None).create_test_user(uid=uid, gid=uid)
    # Adding this line code to remove the automembers plugin configuration.
    instance.remove("nsslapd-pluginConfigArea", BASE_SUFF)
    topo.ms["supplier1"] .restart()


@pytest.fixture(scope="module")
def _startuptask(topo):
    """
    Fixture module that will change required entries for test cases.
    """
    for Configs in ["cn=Managers,cn=subsuffGroups",
                    "cn=Contractors,cn=subsuffGroups",
                    "cn=testuserGroups",
                    "cn=subsuffGroups",
                    "cn=hostGroups"]:
        AutoMembershipDefinition(topo.ms["supplier1"], f'{Configs},{PLUGIN_AUTO}').delete()
    AutoMembershipDefinition(topo.ms["supplier1"], "cn=userGroups,{}".format(PLUGIN_AUTO)).replace(
        'autoMemberScope', 'ou=TaskEmployees,dc=autoMembers,dc=com')
    topo.ms['supplier1'].restart()


@pytest.fixture(scope="function")
def _fixture_for_build_task(request, topo):
    def finof():
        supplier = topo.ms['supplier1']
        auto_mem_scope = "ou=TaskEmployees,{}".format(BASE_SUFF)
        for user in nsAdminGroups(supplier, auto_mem_scope, rdn=None).list():
            user.delete()

    request.addfinalizer(finof)


def bulk_check_groups(topo, GROUP_DN, MEMBATTR, TOTAL_MEM):
    assert len(nsAdminGroup(topo, GROUP_DN).get_attr_vals_utf8(MEMBATTR)) == TOTAL_MEM


def test_automemtask_re_build_task(topo, _create_all_entries, _startuptask, _fixture_for_build_task):
    """
    :id: 4ff973a8-e7ff-11e8-a89b-8c16451d917b
    :setup: 4 Instances with replication
    :steps:
        1. Add 10 users and enable autoMembers plug-in
        2. Run automembers re-build task to create the member attributes
        3. Search for any error logs
    :expected results:
        1. Success
        2. Success
        3. Success
    """
    supplier = topo.ms['supplier1']
    testid = "autoMemTask_01"
    auto_mem_scope = "ou=TaskEmployees,{}".format(BASE_SUFF)
    managers_grp = "cn=Managers,ou=userGroups,{}".format(BASE_SUFF)
    contract_grp = "cn=Contractors,ou=userGroups,{}".format(BASE_SUFF)
    user_rdn = "User_{}".format(testid)
    # make sure the retro changelog is disabled
    RetroChangelogPlugin(supplier).disable()
    AutoMembershipPlugin(supplier).disable()
    supplier.restart()
    for i in range(10):
        add_user(topo, "{}{}".format(user_rdn, str(i)), auto_mem_scope, str(1188), str(1189), "Manager")
    for grp in (managers_grp, contract_grp):
        with pytest.raises(AssertionError):
            assert check_groups(topo, grp, f'uid=User_autoMemTask_010,{auto_mem_scope}', 'member')
    AutoMembershipPlugin(supplier).enable()
    supplier.restart()
    error_string = "automember_rebuild_task_thread"
    AutomemberRebuildMembershipTask(supplier).create(properties={
        'basedn': auto_mem_scope,
        'filter': "objectClass=posixAccount"
    })
    # Search for any error logs
    assert not supplier.searchErrorsLog(error_string)
    for grp in (managers_grp, contract_grp):
        bulk_check_groups(supplier, grp, "member", 10)


def ldif_check_groups(USERS_DN, MEMBATTR, TOTAL_MEM, LDIF_FILE):
    study = open('{}'.format(LDIF_FILE), 'r')
    study_ready = study.read()
    assert len(re.findall("{}: {}".format(MEMBATTR, USERS_DN.lower()), study_ready)) == TOTAL_MEM


def check_file_exists(export_ldif):
    count = 0
    while not os.path.exists(export_ldif) and count < 3:
        time.sleep(1)
        count += 1

    count = 0
    while (os.stat(export_ldif).st_size == 0) and count < 3:
        time.sleep(1)
        count += 1

    if os.path.exists(export_ldif) and os.stat(export_ldif).st_size != 0:
        return True
    else:
        return False


def test_automemtask_export_task(topo, _create_all_entries, _startuptask, _fixture_for_build_task):
    """
    :id: 4ff98b18-e7ff-11e8-872a-8c16451d917b
    :setup: 4 Instances with replication
    :steps:
        1. Add 10 users and enable autoMembers plug-in
        2. Run automembers export task to create an ldif file with member attributes
    :expected results:
        1. Success
        2. Success
    """
    supplier = topo.ms['supplier1']
    p = Paths('supplier1')
    testid = "autoMemTask_02"
    auto_mem_scope = "ou=TaskEmployees,{}".format(BASE_SUFF)
    managers_grp = "cn=Managers,ou=userGroups,{}".format(BASE_SUFF)
    user_rdn = "User_{}".format(testid)
    # Disabling plugin
    AutoMembershipPlugin(supplier).disable()
    supplier.restart()
    for i in range(10):
        add_user(topo, "{}{}".format(user_rdn, str(i)), auto_mem_scope, str(2788), str(2789), "Manager")
    with pytest.raises(AssertionError):
        bulk_check_groups(supplier, managers_grp, "member", 10)
    AutoMembershipPlugin(supplier).enable()
    supplier.restart()
    export_ldif = p.backup_dir + "/Out_Export_02.ldif"
    if os.path.exists(export_ldif):
        os.remove(export_ldif)
    exp_task = Tasks(supplier)
    exp_task.automemberExport(suffix=auto_mem_scope, fstr='objectclass=posixAccount', ldif_out=export_ldif)
    check_file_exists(export_ldif)
    ldif_check_groups("cn={}".format(user_rdn), "member", 10, export_ldif)
    os.remove(export_ldif)


def test_automemtask_mapping(topo, _create_all_entries, _startuptask, _fixture_for_build_task):
    """
    :id: 4ff9a206-e7ff-11e8-bf59-8c16451d917b
    :setup: 4 Instances with replication
    :steps:
        1. Add 10 users and enable autoMembers plug-in
        2. Run automembers Mapping task with input/output ldif files
    :expected results:
        1. Should success
        2. Should success
    """
    supplier = topo.ms['supplier1']
    p = Paths('supplier1')
    testid = "autoMemTask_02"
    auto_mem_scope = "ou=TaskEmployees,{}".format(BASE_SUFF)
    user_rdn = "User_{}".format(testid)
    export_ldif = p.backup_dir+"/Out_Export_02.ldif"
    output_ldif3 = p.backup_dir+"/Output_03.ldif"
    for file in [export_ldif, output_ldif3]:
        if os.path.exists(file):
            os.remove(file)
    for i in range(10):
        add_user(topo, "{}{}".format(user_rdn, str(i)), auto_mem_scope, str(2788), str(2789), "Manager")
    ExportTask(supplier).export_suffix_to_ldif(ldiffile=export_ldif, suffix=BASE_SUFF)
    check_file_exists(export_ldif)
    map_task = Tasks(supplier)
    map_task.automemberMap(ldif_in=export_ldif, ldif_out=output_ldif3)
    check_file_exists(output_ldif3)
    ldif_check_groups("cn={}".format(user_rdn), "member", 10, output_ldif3)
    for file in [export_ldif, output_ldif3]:
        os.remove(file)


def test_automemtask_re_build(topo, _create_all_entries, _startuptask, _fixture_for_build_task):
    """
    :id: 4ff9b944-e7ff-11e8-ad35-8c16451d917b
    :setup: 4 Instances with replication
    :steps:
        1. Add 10 users with inetOrgPerson object class
        2. Run automembers re-build task to create the member attributes, exp to FAIL
    :expected results:
        1. Should success
        2. Should not success
    """
    supplier = topo.ms['supplier1']
    testid = "autoMemTask_04"
    auto_mem_scope = "ou=TaskEmployees,{}".format(BASE_SUFF)
    managers_grp = "cn=Managers,ou=userGroups,{}".format(BASE_SUFF)
    user_rdn = "User_{}".format(testid)
    # Disabling plugin
    AutoMembershipPlugin(supplier).disable()
    supplier.restart()
    for number in range(10):
        add_user(topo, f'{user_rdn}{number}', auto_mem_scope, str(number), str(number), "Manager")
    with pytest.raises(AssertionError):
        bulk_check_groups(supplier, managers_grp, "member", 10)
    # Enabling plugin
    AutoMembershipPlugin(supplier).enable()
    supplier.restart()
    AutomemberRebuildMembershipTask(supplier).create(properties={
        'basedn': auto_mem_scope,
        'filter': "objectClass=inetOrgPerson"
    })
    with pytest.raises(AssertionError):
        bulk_check_groups(supplier, managers_grp, "member", 10)


def test_automemtask_export(topo, _create_all_entries, _startuptask, _fixture_for_build_task):
    """
    :id: 4ff9cf74-e7ff-11e8-b712-8c16451d917b
    :setup: 4 Instances with replication
    :steps:
        1. Add 10 users with inetOrgPerson objectClass
        2. Run automembers export task to create an ldif file with member attributes, exp to FAIL
    :expected results:
        1. Should success
        2. Should not success
    """
    supplier = topo.ms['supplier1']
    p = Paths('supplier1')
    testid = "autoMemTask_05"
    auto_mem_scope = "ou=TaskEmployees,{}".format(BASE_SUFF)
    managers_grp = "cn=Managers,ou=userGroups,{}".format(BASE_SUFF)
    user_rdn = "User_{}".format(testid)
    # Disabling plugin
    AutoMembershipPlugin(supplier).disable()
    supplier.restart()
    for number in range(10):
        add_user(topo, f'{user_rdn}{number}', auto_mem_scope, str(number), str(number), "Manager")
    with pytest.raises(AssertionError):
        bulk_check_groups(supplier, managers_grp, "member", 10)
    # Enabling plugin
    AutoMembershipPlugin(supplier).enable()
    supplier.restart()
    export_ldif = p.backup_dir + "/Out_Export_02.ldif"
    if os.path.exists(export_ldif):
        os.remove(export_ldif)
    exp_task = Tasks(supplier)
    exp_task.automemberExport(suffix=auto_mem_scope, fstr='objectclass=inetOrgPerson', ldif_out=export_ldif)
    check_file_exists(export_ldif)
    with pytest.raises(AssertionError):
        ldif_check_groups("uid={}".format(user_rdn), "member", 10, export_ldif)
    os.remove(export_ldif)


def test_automemtask_run_re_build(topo, _create_all_entries, _startuptask, _fixture_for_build_task):
    """
    :id: 4ff9e5c2-e7ff-11e8-943e-8c16451d917b
    :setup: 4 Instances with replication
    :steps:
        1. Add 10 users with inetOrgPerson obj class
        2. Change plugin config
        3. Enable plug-in and run re-build task to create the member attributes
    :expected results:
        1. Should success
        2. Should success
        3. Should success
    """
    supplier = topo.ms['supplier1']
    p = Paths('supplier1')
    testid = "autoMemTask_06"
    auto_mem_scope = "ou=TaskEmployees,{}".format(BASE_SUFF)
    managers_grp = "cn=Managers,ou=userGroups,{}".format(BASE_SUFF)
    user_rdn = "User_{}".format(testid)
    # Disabling plugin
    AutoMembershipPlugin(supplier).disable()
    supplier.restart()
    for number in range(10):
        add_user(topo, f'{user_rdn}{number}', auto_mem_scope, '111', '111', "Manager")
    for user in nsAdminGroups(supplier, auto_mem_scope, rdn=None).list():
        user.add('objectclass', 'inetOrgPerson')
    AutoMembershipDefinition(supplier,
                             f'cn=userGroups,{PLUGIN_AUTO}').replace('autoMemberFilter',
                                                                     "objectclass=inetOrgPerson")
    supplier.restart()
    with pytest.raises(AssertionError):
        bulk_check_groups(supplier, managers_grp, "member", 10)
    AutoMembershipPlugin(supplier).enable()
    supplier.restart()
    AutomemberRebuildMembershipTask(supplier).create(properties={
        'basedn': auto_mem_scope,
        'filter': "objectClass=inetOrgPerson"})
    time.sleep(2)
    bulk_check_groups(supplier, managers_grp, "member", 10)
    AutoMembershipDefinition(supplier,
                             f'cn=userGroups,{PLUGIN_AUTO}').replace('autoMemberFilter',
                                                                     "objectclass=posixAccount")
    supplier.restart()


def test_automemtask_run_export(topo, _create_all_entries, _startuptask, _fixture_for_build_task):
    """
    :id: 4ff9fba2-e7ff-11e8-a5ec-8c16451d917b
    :setup: 4 Instances with replication
    :steps:
        1. Add 10 users with inetOrgPerson objectClass
        2. change plugin config
        3. Run export task to create an ldif file with member attributes
    :expected results:
        1. Should success
        2. Should success
        3. Should success
    """
    supplier = topo.ms['supplier1']
    p = Paths('supplier1')
    testid = "autoMemTask_07"
    auto_mem_scope = "ou=TaskEmployees,{}".format(BASE_SUFF)
    managers_grp = "cn=Managers,ou=userGroups,{}".format(BASE_SUFF)
    user_rdn = "User_{}".format(testid)
    # Disabling plugin
    AutoMembershipPlugin(supplier).disable()
    supplier.restart()
    for number in range(10):
        add_user(topo, f'{user_rdn}{number}', auto_mem_scope, '222', '222', "Manager")
    for user in nsAdminGroups(supplier, auto_mem_scope, rdn=None).list():
        user.add('objectclass', 'inetOrgPerson')
    AutoMembershipDefinition(supplier, f'cn=userGroups,{PLUGIN_AUTO}').replace('autoMemberFilter',
                                                                             "objectclass=inetOrgPerson")
    supplier.restart()
    # Enabling plugin
    AutoMembershipPlugin(supplier).enable()
    supplier.restart()
    with pytest.raises(AssertionError):
        bulk_check_groups(supplier, managers_grp, "member", 10)
    export_ldif = p.backup_dir + "/Out_Export_02.ldif"
    if os.path.exists(export_ldif):
        os.remove(export_ldif)
    exp_task = Tasks(supplier)
    exp_task.automemberExport(suffix=auto_mem_scope, fstr='objectclass=inetOrgPerson', ldif_out=export_ldif)
    check_file_exists(export_ldif)
    ldif_check_groups("cn={}".format(user_rdn), "member", 10, export_ldif)
    AutoMembershipDefinition(supplier, f'cn=userGroups,{PLUGIN_AUTO}').\
        replace('autoMemberFilter', "objectclass=posixAccount")


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
