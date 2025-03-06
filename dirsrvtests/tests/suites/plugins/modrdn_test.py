# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
from lib389.topologies import topology_st
from lib389._constants import DEFAULT_SUFFIX
from lib389.idm.group import Groups
from lib389.idm.user import nsUserAccounts
from lib389.plugins import (
    AutoMembershipDefinitions,
    AutoMembershipPlugin,
    AutoMembershipRegexRules,
    MemberOfPlugin,
    ReferentialIntegrityPlugin,
)

pytestmark = pytest.mark.tier1

USER_PROPERTIES = {
    "uid": "userwith",
    "cn": "userwith",
    "uidNumber": "1000",
    "gidNumber": "2000",
    "homeDirectory": "/home/testuser",
    "displayName": "test user",
}


def test_modrdn_of_a_member_of_2_automember_groups(topology_st):
    """Test that a member of 2 automember groups can be renamed

    :id: 0e40bdc4-a2d2-4bb8-8368-e02c8920bad2

    :setup: Standalone instance

    :steps:
        1. Enable automember plugin
        2. Create definiton for users with A in the name
        3. Create regex rule for users with A in the name
        4. Create definiton for users with Z in the name
        5. Create regex rule for users with Z in the name
        6. Enable memberof plugin
        7. Enable referential integrity plugin
        8. Restart the instance
        9. Create groups
        10. Create users userwitha, userwithz, userwithaz
        11. Rename userwithaz

    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
        9. Success
        10. Success
        11. Success
    """
    inst = topology_st.standalone

    # Enable automember plugin
    automember_plugin = AutoMembershipPlugin(inst)
    automember_plugin.enable()

    # Create definiton for users with A in the name
    automembers = AutoMembershipDefinitions(inst)
    automember = automembers.create(
        properties={
            "cn": "userswithA",
            "autoMemberScope": DEFAULT_SUFFIX,
            "autoMemberFilter": "objectclass=posixAccount",
            "autoMemberGroupingAttr": "member:dn",
        }
    )

    # Create regex rule for users with A in the name
    automembers_regex_rule = AutoMembershipRegexRules(inst, f"{automember.dn}")
    automembers_regex_rule.create(
        properties={
            "cn": "userswithA",
            "autoMemberInclusiveRegex": ["cn=.*a.*"],
            "autoMemberTargetGroup": [f"cn=userswithA,ou=Groups,{DEFAULT_SUFFIX}"],
        }
    )

    # Create definiton for users with Z in the name
    automember = automembers.create(
        properties={
            "cn": "userswithZ",
            "autoMemberScope": DEFAULT_SUFFIX,
            "autoMemberFilter": "objectclass=posixAccount",
            "autoMemberGroupingAttr": "member:dn",
        }
    )

    # Create regex rule for users with Z in the name
    automembers_regex_rule = AutoMembershipRegexRules(inst, f"{automember.dn}")
    automembers_regex_rule.create(
        properties={
            "cn": "userswithZ",
            "autoMemberInclusiveRegex": ["cn=.*z.*"],
            "autoMemberTargetGroup": [f"cn=userswithZ,ou=Groups,{DEFAULT_SUFFIX}"],
        }
    )

    # Enable memberof plugin
    memberof_plugin = MemberOfPlugin(inst)
    memberof_plugin.enable()

    # Enable referential integrity plugin
    referint_plugin = ReferentialIntegrityPlugin(inst)
    referint_plugin.enable()

    # Restart the instance
    inst.restart()

    # Create groups
    groups = Groups(inst, DEFAULT_SUFFIX)
    groupA = groups.create(properties={"cn": "userswithA"})
    groupZ = groups.create(properties={"cn": "userswithZ"})

    # Create users
    users = nsUserAccounts(inst, DEFAULT_SUFFIX)

    # userwitha
    user_props = USER_PROPERTIES.copy()
    user_props.update(
        {
            "uid": USER_PROPERTIES["uid"] + "a",
            "cn": USER_PROPERTIES["cn"] + "a",
        }
    )
    user = users.create(properties=user_props)

    # userwithz
    user_props.update(
        {
            "uid": USER_PROPERTIES["uid"] + "z",
            "cn": USER_PROPERTIES["cn"] + "z",
        }
    )
    user = users.create(properties=user_props)

    # userwithaz
    user_props.update(
        {
            "uid": USER_PROPERTIES["uid"] + "az",
            "cn": USER_PROPERTIES["cn"] + "az",
        }
    )
    user = users.create(properties=user_props)
    user_orig_dn = user.dn

    # Rename userwithaz
    user.rename(new_rdn="uid=userwith")
    user_new_dn = user.dn

    assert user.get_attr_val_utf8("uid") != "userwithaz"

    # Check groups contain renamed username
    assert groupA.is_member(user_new_dn)
    assert groupZ.is_member(user_new_dn)

    # Check groups dont contain original username
    assert not groupA.is_member(user_orig_dn)
    assert not groupZ.is_member(user_orig_dn)
