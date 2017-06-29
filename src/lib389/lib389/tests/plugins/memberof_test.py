# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016-2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import pytest
from time import sleep

from lib389.topologies import topology_st
from lib389.plugins import MemberOfPlugin
from lib389.properties import BACKEND_NAME, BACKEND_SUFFIX
from lib389._constants import DEFAULT_SUFFIX
from lib389.tests.plugins.utils import (
        create_test_user, create_test_group, create_test_ou, delete_objects)


@pytest.fixture(scope="module")
def plugin(request):
    topology = topology_st(request)
    plugin = MemberOfPlugin(topology.standalone)
    return plugin


def test_memberof_enable_disable(plugin):
    """
    Test that the plugin doesn't do anything while disabled, and functions
    properly when enabled.

    NOTICE: This test case leaves the plugin enabled for the following tests.
    """
    # assert plugin is disabled (by default)
    assert plugin.status() == False

    user = create_test_user(plugin._instance)
    group = create_test_group(plugin._instance)

    # add user to the group (which normally triggers the plugin)
    group.add_member(user.dn)

    memberofattr = plugin.get_attr()
    # assert no memberof attribute was created by the plugin
    assert not memberofattr in user.get_all_attrs()

    # enable the plugin and restart the server for the action to take effect
    plugin.enable()
    plugin._instance.restart()
    assert plugin.status() == True

    # trigger the plugin again
    group.remove_member(user.dn)
    group.add_member(user.dn)

    # assert that the memberof attribute was now properly set
    assert memberofattr in user.get_all_attrs()

    # clean up for subsequent test cases
    delete_objects([user, group])

def test_memberofattr(plugin):
    """
    Test that the plugin automatically creates an attribute on user entries
    declaring membership after adding them to a group. The attribute depends
    on the value of memberOfAttr.
    """
    memberofattr = "memberOf"
    plugin.set_attr(memberofattr)

    user = create_test_user(plugin._instance)
    group = create_test_group(plugin._instance)

    # memberof attribute should not yet appear
    assert not memberofattr in user.get_all_attrs()

    # trigger the plugin
    group.add_member(user.dn)

    # assert that the memberof attribute was automatically set by the plugin
    assert group.dn in user.get_attr_vals(memberofattr)

    # clean up for subsequent test cases
    delete_objects([user, group])

def test_memberofgroupattr(plugin):
    """
    memberOfGroupAttr gives the attribute in the group entry to poll to identify
    member DNs. Test that the memberOf is set on a user only for groups whose
    membership attribute is included in memberOfGroupAttr.
    """
    memberofattr = plugin.get_attr()

    # initially "member" should be the default and only value of memberOfGroupAttr
    assert plugin.get_attr_vals('memberofgroupattr') == ['member']

    user = create_test_user(plugin._instance)
    group_normal = create_test_group(plugin._instance)
    group_unique = create_test_group(plugin._instance, unique_group=True)

    # add user to both groups
    group_normal.add_member(user.dn)
    group_unique.add_member(user.dn)

    # assert that the memberof attribute was set for normal group only
    assert group_normal.dn in user.get_attr_vals(memberofattr)
    assert not group_unique.dn in user.get_attr_vals(memberofattr)

    # add another value to memberOfGroupAttr
    plugin.add_groupattr('uniqueMember')

    # remove user from groups and add them again in order to trigger the plugin
    group_normal.remove_member(user.dn)
    group_normal.add_member(user.dn)
    group_unique.remove_member(user.dn)
    group_unique.add_member(user.dn)

    # assert that the memberof attribute was set for both groups this time
    assert group_normal.dn in user.get_attr_vals(memberofattr)
    assert group_unique.dn in user.get_attr_vals(memberofattr)

    # clean up for subsequent test cases
    delete_objects([user, group_normal, group_unique])

def test_memberofallbackends(plugin):
    """
    By default the MemberOf plugin only looks for potential members for users
    who are in the same database as the group. Test that when memberOfAllBackends
    is enabled, memberOf will search across all databases instead.
    """
    ou_value = "People2"
    ou_suffix = DEFAULT_SUFFIX

    # create a new backend
    plugin._instance.backends.create(
        None, properties={
            BACKEND_NAME: "People2Data",
            BACKEND_SUFFIX: "ou=" + ou_value + "," + ou_suffix,
    })

    # create a new sub-suffix stored in the new backend
    ou2 = create_test_ou(plugin._instance, ou=ou_value, suffix=ou_suffix)

    # add a user in the default backend
    user_b1 = create_test_user(plugin._instance)
    # add a user in the new backend
    user_b2 = create_test_user(plugin._instance, suffix=ou2.dn)
    # create a group in the default backend
    group = create_test_group(plugin._instance)

    # configure memberof to search only for users who are in the same backend as the group
    plugin.disable_allbackends()

    group.add_member(user_b1.dn)
    group.add_member(user_b2.dn)

    memberofattr = plugin.get_attr()

    # assert that memberOfAttr was set for the user stored in the same backend as the group
    assert group.dn in user_b1.get_attr_vals(memberofattr)
    # assert that memberOfAttr was NOT set for the user stored in a different backend
    assert not memberofattr in user_b2.get_all_attrs()

    # configure memberof to search across all backends
    plugin.enable_allbackends()

    # remove users from group and add them again in order to re-trigger the plugin
    group.remove_member(user_b1.dn)
    group.remove_member(user_b2.dn)
    group.add_member(user_b1.dn)
    group.add_member(user_b2.dn)

    # assert that memberOfAttr was set for users stored in both backends
    assert group.dn in user_b1.get_attr_vals(memberofattr)
    assert group.dn in user_b2.get_attr_vals(memberofattr)

    # clean up for subsequent test cases
    delete_objects([user_b1, user_b2, group, ou2])

def test_memberofskipnested(plugin):
    """
    Test that when memberOfSkipNested is off (default) they plugin can properly
    handle nested groups (groups that are member of other groups). Respectively
    make sure that when memberOfSkipNested is on, the plugin lists only groups
    to which a user was added directly.
    """
    user = create_test_user(plugin._instance)
    group1 = create_test_group(plugin._instance)
    group2 = create_test_group(plugin._instance)

    # don't skip nested groups (this is the default)
    plugin.disable_skipnested()

    # create a nested group by listing group1 as a member of group2
    group2.add_member(group1.dn)
    # add user to group1 only
    group1.add_member(user.dn)

    memberofattr = plugin.get_attr()

    assert group2.dn in group1.get_attr_vals(memberofattr)
    # assert that memberOfAttr of user includes both groups
    # even though they were not directly added to group2
    assert group1.dn in user.get_attr_vals(memberofattr)
    assert group2.dn in user.get_attr_vals(memberofattr)

    # skip nested groups
    plugin.enable_skipnested()

    # remove from groups and add again in order to re-trigger the plugin
    group2.remove_member(group1.dn)
    group1.remove_member(user.dn)
    group2.add_member(group1.dn)
    group1.add_member(user.dn)

    assert group2.dn in group1.get_attr_vals(memberofattr)
    # assert that user's memberOfAttr includes only the group to which they were added
    assert group1.dn in user.get_attr_vals(memberofattr)
    assert not group2.dn in user.get_attr_vals(memberofattr)

    # clean up for subsequent test cases
    delete_objects([user, group1, group2])

def test_memberofautoaddocc(plugin):
    """
    Test that the MemberOf plugin automatically adds the object class defined
    by memberOfAutoAddOC to a user object, if it does not contain an object
    class that allows the memberOf attribute.
    """
    user = create_test_user(plugin._instance)
    group = create_test_group(plugin._instance)

    # delete any object classes that allow the memberOf attribute
    if "nsMemberOf" in user.get_attr_vals("objectClass"):
        user.remove("objectClass", "nsMemberOf")
    if "inetUser" in user.get_attr_vals("objectClass"):
        user.remove("objectClass", "inetUser")
    if "inetAdmin" in user.get_attr_vals("objectClass"):
        user.remove("objectClass", "inetAdmin")

    # set a valid object class to memberOfAutoAddOC
    plugin.set_autoaddoc("nsMemberOf")
    # assert that user has not got this object class at the moment
    assert not "nsMemberOf" in user.get_attr_vals("objectClass")

    # trigger the plugin
    group.add_member(user.dn)

    # assert that the object class defined by memberOfAutoAddOC now exists
    assert "nsMemberOf" in user.get_attr_vals("objectClass")

    # reset user entry
    group.remove_member(user.dn)
    user.remove("objectClass", "nsMemberOf")

    # repeat for different object class
    plugin.set_autoaddoc("inetUser")
    assert not "inetUser" in user.get_attr_vals("objectClass")

    # re-trigger the plugin
    group.add_member(user.dn)

    assert "inetUser" in user.get_attr_vals("objectClass")

    group.remove_member(user.dn)
    user.remove("objectClass", "inetUser")

    # repeat for different object class
    plugin.set_autoaddoc("inetAdmin")
    assert not "inetAdmin" in user.get_attr_vals("objectClass")

    # re-trigger the plugin
    group.add_member(user.dn)

    assert "inetAdmin" in user.get_attr_vals("objectClass")

    # clean up for subsequent test cases
    delete_objects([user, group])

def test_scoping(plugin):
    """
    Tests that the MemberOf plugin works on suffixes listed in memberOfEntryScope,
    but skips suffixes listed in memberOfEntryScopeExcludeSubtree.
    """
    # create a new ou and 2 users under different ous
    ou2 = create_test_ou(plugin._instance)
    user_p1 = create_test_user(plugin._instance)
    user_p2 = create_test_user(plugin._instance, suffix=ou2.dn)
    group = create_test_group(plugin._instance)

    # define include and exclude suffixes for MemberOf
    plugin.add_entryscope(DEFAULT_SUFFIX)
    plugin.add_excludescope(ou2.dn)

    group.add_member(user_p1.dn)
    group.add_member(user_p2.dn)

    memberofattr = plugin.get_attr()

    # assert that memberOfAttr was set for entry of an included suffix
    assert group.dn in user_p1.get_attr_vals(memberofattr)
    # assert that memberOfAttr was NOT set for entry of an excluded suffix
    assert not memberofattr in user_p2.get_all_attrs()

    # clean up for subsequent test cases
    delete_objects([user_p1, user_p2, group, ou2])

    plugin.remove_all_excludescope()
    plugin.remove_all_entryscope()

def test_fixup_task(plugin):
    """
    Test that after creating the fix-up task entry, initial memberOf attributes
    are created on the member's user entries in the directory automatically.
    Also, test that the filter provided to the task is correctly applied.
    """
    # disable the plugin and restart the server for the action to take effect
    plugin.disable()
    plugin._instance.restart()

    user1 = create_test_user(plugin._instance)
    user2 = create_test_user(plugin._instance, cn="testuser2")
    group = create_test_group(plugin._instance)

    group.add_member(user1.dn)
    group.add_member(user2.dn)

    # enable the plugin and restart the server for the action to take effect
    plugin.enable()
    plugin._instance.restart()

    memberofattr = plugin.get_attr()
    # memberof attribute should not appear on user entries
    assert not memberofattr in user1.get_all_attrs()
    assert not memberofattr in user2.get_all_attrs()

    # run the fix-up task and provide a filter for the entry
    task = plugin.fixup(basedn=DEFAULT_SUFFIX, _filter="(cn=testuser2)")
    # wait for the task to complete
    task.wait()

    assert task.is_complete()
    assert task.get_exit_code() == 0

    # memberof attribute should now appear on the user entry matching the filter
    assert memberofattr in user2.get_all_attrs()
    assert group.dn in user2.get_attr_vals(memberofattr)

    # but should not appear on user entry that doesn't match the filter
    assert not memberofattr in user1.get_all_attrs()

    # clean up for subsequent test cases
    delete_objects([user1, user2, group])
