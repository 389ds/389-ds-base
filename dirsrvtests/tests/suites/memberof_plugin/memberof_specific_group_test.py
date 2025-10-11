# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import pytest
import os
import time
import ldap
from lib389._constants import *
from lib389.topologies import topology_st as topo
from lib389.utils import ensure_str
from lib389.plugins import MemberOfPlugin
from lib389.idm.user import UserAccounts, UserAccount
from lib389.idm.group import Group, Groups, UniqueGroup, UniqueGroups

log = logging.getLogger(__name__)

# Constants
PLUGIN_MEMBER_OF = 'MemberOf Plugin'
MEMBEROF_PLUGIN_DN = ('cn=' + PLUGIN_MEMBER_OF + ',cn=plugins,cn=config')
PLUGIN_ENABLED = 'nsslapd-pluginEnabled'

# Test data
USER1_DN = 'uid=testuser1,ou=people,' + DEFAULT_SUFFIX
USER2_DN = 'uid=testuser2,ou=people,' + DEFAULT_SUFFIX
USER3_DN = 'uid=testuser3,ou=people,' + DEFAULT_SUFFIX
UNIQGROUP1_DN = 'cn=testugroup1,ou=groups,' + DEFAULT_SUFFIX
UNIQGROUP2_DN = 'cn=testugroup2,ou=groups,' + DEFAULT_SUFFIX
GROUP1_DN = 'cn=testgroup1,ou=groups,' + DEFAULT_SUFFIX
GROUP2_DN = 'cn=testgroup2,ou=groups,' + DEFAULT_SUFFIX
GROUP3_DN = 'cn=testgroup3,ou=groups,' + DEFAULT_SUFFIX


@pytest.fixture(scope="function")
def users_and_groups(request, topo):
    """
    Add users and groups to the default suffix
    """

    log.info('Adding users and groups')
    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    for user_name in ["testuser1", "testuser2", "testuser3"]:
        users.create(properties={
            'uid':  user_name,
            'cn': user_name,
            'sn': user_name,
            'uidNumber': '1234',
            'gidNumber': '1234',
            'homeDirectory': '/home/test',
        })

    groups = Groups(topo.standalone, DEFAULT_SUFFIX)
    for group_name in ["testgroup1", "testgroup2", "testgroup3"]:
        groups.create(properties={'cn': group_name})

    uniquegroups = UniqueGroups(topo.standalone, DEFAULT_SUFFIX)
    uniquegroups.create(properties={'cn': 'testugroup1'})
    uniquegroups.create(properties={'cn': 'testugroup2'})

    def fin():
        log.info('Cleanup')
        UserAccount(topo.standalone, USER1_DN).delete()
        UserAccount(topo.standalone, USER2_DN).delete()
        UserAccount(topo.standalone, USER3_DN).delete()
        Group(topo.standalone, GROUP1_DN).delete()
        Group(topo.standalone, GROUP2_DN).delete()
        Group(topo.standalone, GROUP3_DN).delete()
        UniqueGroup(topo.standalone, UNIQGROUP1_DN).delete()
        UniqueGroup(topo.standalone, UNIQGROUP2_DN).delete()

    request.addfinalizer(fin)


def _memberof_checking_delay(inst):
    """Get appropriate delay for memberof checking based on plugin configuration"""
    memberof = MemberOfPlugin(inst)
    if (memberof.get_memberofdeferredupdate() and memberof.get_memberofdeferredupdate().lower() == "on"):
        # In case of deferred update then a safe delay to let the deferred thread processing is 3 sec
        delay = 3
    else:
        # Else it is the same TXN, no reason to wait
        delay = 0
    return delay


def _check_memberof(inst, member, group):
    """Check if member has memberof attribute pointing to group"""
    log.info("Lookup memberof from %s" % member)
    entry = inst.getEntry(ensure_str(member), ldap.SCOPE_BASE, '(objectclass=*)', ['memberof'])
    if not entry.hasAttr('memberof'):
        return False

    found = False
    for val in entry.getValues('memberof'):
        log.info("memberof: %s" % ensure_str(val))
        if ensure_str(group.lower()) == ensure_str(val.lower()):
            found = True
            log.info("--> membership verified")
            break
    return found


def test_user_to_group_membership(topo, users_and_groups):
    """Test that including and excluding groups works as expected with users

    :id: 1561e082-b2cf-4863-b3b3-6af2398c545d
    :setup: Standalone Instance
    :steps:
        1. Enable and configure memberof plugin
        2. Add users to various groups
        3. Check that users have, or do not have the memberof attribute
    :expectedresults:
        1. Plugin should be enabled successfully
        2. Success
        3. Success
    """

    inst = topo.standalone

    # Enable memberof plugin
    log.info("Enable MemberOf plugin")
    memberof = MemberOfPlugin(inst)
    memberof.enable()
    memberof.remove_all_specific_group_filters()
    memberof.remove_all_exclude_specific_group_filters()
    memberof.add_specific_group_filter("(entrydn=" + GROUP1_DN + ")")
    memberof.add_exclude_specific_group_filter("(entrydn=" + GROUP3_DN + ")")
    inst.restart()

    # Add user to group
    log.info("Add user to group 1")
    group1 = Group(inst, GROUP1_DN)
    group1.add_member(USER1_DN)
    group2 = Group(inst, GROUP2_DN)
    group2.add_member(USER2_DN)
    group3 = Group(inst, GROUP3_DN)
    group3.add_member(USER3_DN)

    # Wait for memberof processing
    delay = _memberof_checking_delay(inst)
    if delay > 0:
        time.sleep(delay)

    # Check memberof attribute
    log.info("Check memberof attribute")
    assert _check_memberof(inst, USER1_DN, GROUP1_DN)
    assert not _check_memberof(inst, USER2_DN, GROUP2_DN)
    assert not _check_memberof(inst, USER3_DN, GROUP3_DN)


def test_group_to_group_membership(topo, users_and_groups):
    """Test that including and excluding groups works as expected with nested
    groups

    :id: 1561e082-b2cf-4863-b3b3-6af2398c545e
    :setup: Standalone Instance
    :steps:
        1. Enable and configure memberof plugin
        2. Add groups to other groups
        3. Check that users have, or do not have the memberof attribute
    :expectedresults:
        1. Plugin should be enabled successfully
        2. Success
        3. Success
    """

    inst = topo.standalone

    # Enable memberof plugin
    log.info("Enable MemberOf plugin")
    memberof = MemberOfPlugin(inst)
    memberof.enable()
    memberof.remove_all_specific_group_filters()
    memberof.remove_all_exclude_specific_group_filters()
    memberof.add_exclude_specific_group_filter("(entrydn=" + GROUP3_DN + ")")
    inst.restart()

    # Add user to group
    log.info("Add user to group 1")
    group1 = Group(inst, GROUP1_DN)
    group1.add_member(USER1_DN)
    group2 = Group(inst, GROUP2_DN)
    group2.add_member(GROUP2_DN)
    group2.add_member(GROUP3_DN)
    group3 = Group(inst, GROUP3_DN)
    group3.add_member(GROUP2_DN)

    # _check_memberof (member, group)
    assert _check_memberof(inst, USER1_DN, GROUP1_DN)
    assert not _check_memberof(inst, GROUP3_DN, GROUP2_DN)
    assert not _check_memberof(inst, GROUP2_DN, GROUP3_DN)


def test_group_oc_membership(topo, users_and_groups):
    """Test that including and excluding groups works as expected with custom
    objectclasses. The specific objectclasses are used to distinguish if entries
    are "groups" or "not groups"

    :id: 42714d25-be6d-4aa0-831d-0f4cf91fac86
    :setup: Standalone Instance
    :steps:
        1. Enable and configure memberof plugin (exclude groups only and
        objectclass groupofuniquenames)
        2. Add user to an excluded group but the group has objectclass groupofnames
        3. Add user to an excluded group but the group has objectclass groupofuniquenames
        4. Memberof config remove exclude rules and add include ones
        5. Add user to a group that is in the include list
        6. Add group to a group that is not in the include list
        7. Add group that is not in the include list to a group that is in the include list
        8. Add group with unknown objectclass to a group that is in the include list
        9. Add group with known objectclass to a group that is in the include list
    :expectedresults:
        1. Plugin should be enabled successfully
        2. Membership is not updated
        3. Membership is not updated
        4. Success
        5. Success
        6. Membership is not updated
        7. Membership is not updated
        8. Success
        9. Membership is not updated
    """

    inst = topo.standalone

    # Enable memberof plugin
    log.info("Enable MemberOf plugin")
    memberof = MemberOfPlugin(inst)
    memberof.enable()
    memberof.add_groupattr('uniquemember')
    memberof.remove_all_specific_group_filters()
    memberof.remove_all_exclude_specific_group_filters()
    memberof.add_exclude_specific_group_filter("(entrydn=" + UNIQGROUP1_DN + ")")
    memberof.add_exclude_specific_group_filter("(entrydn=" + GROUP1_DN + ")")
    memberof.add_specific_group_oc("groupofuniquenames")
    inst.restart()

    # Add user to an excluded group but the group has objectclass groupofnames
    # but since we are excluding specifc DN's we don't care about the entry's
    # objectclasses
    log.info("Add user to group")
    group = Group(inst, GROUP1_DN)
    group.add_member(USER1_DN)
    assert not _check_memberof(inst, USER1_DN, GROUP1_DN)

    # Add user to an excluded group that does match specifc group objectclass
    unique_group = UniqueGroup(inst, UNIQGROUP1_DN)
    unique_group.add_member(USER1_DN)
    assert not _check_memberof(inst, USER1_DN, UNIQGROUP1_DN)

    #
    # Switch over to just allowing specific groups
    #
    memberof.remove_all_exclude_specific_group_filters()
    memberof.add_specific_group_filter("(entrydn=" + UNIQGROUP1_DN + ")")
    memberof.add_specific_group_filter("(entrydn=" + GROUP1_DN + ")")
    time.sleep(1)
    # Update group that should allow the update
    unique_group.add_member(USER2_DN)
    assert _check_memberof(inst, USER2_DN, UNIQGROUP1_DN)

    # Update a valid "group" but that is not in the specified list
    unique_group2 = UniqueGroup(inst, UNIQGROUP2_DN)
    unique_group2.add_member(USER3_DN)
    assert not _check_memberof(inst, USER3_DN, GROUP1_DN)

    # uniquegroup2 should not be listed as memberof uniquegroup1 since its not
    # in the allowed group list
    unique_group.add_member(UNIQGROUP2_DN)
    assert not _check_memberof(inst, UNIQGROUP2_DN, UNIQGROUP1_DN)

    # uniquegroup1 is an allowed group, and group2 is not seen a group because
    # of the specific group objectclass setting. So the MO update should be
    # allowed in this case
    unique_group.add_member(GROUP2_DN)
    assert _check_memberof(inst, GROUP2_DN, UNIQGROUP1_DN)

    # Now add groupOfNames oc to the MO config, and try adding a group as a
    # member that is not in the specific group list
    memberof.add_specific_group_oc("groupofnames")
    unique_group.add_member(GROUP3_DN)
    assert not _check_memberof(inst, GROUP3_DN, UNIQGROUP1_DN)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
