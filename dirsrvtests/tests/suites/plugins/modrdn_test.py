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
from lib389.topologies import topology_st
from lib389._constants import DEFAULT_SUFFIX
from lib389._mapped_object import DSLdapObject
from lib389.idm.group import Groups
from lib389.idm.user import nsUserAccounts
from lib389.plugins import (
    AutoMembershipDefinitions,
    AutoMembershipPlugin,
    AutoMembershipRegexRules,
    MemberOfPlugin,
    ReferentialIntegrityPlugin,
)
from lib389.schema import Schema

log = logging.getLogger(__name__)

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


@pytest.fixture
def referint_setup(topology_st):
    """Set up custom schema and referint plugin for MUST-attribute tests."""
    inst = topology_st.standalone

    schema = Schema(inst)
    schema.add('attributetypes',
               "( 9.9.8.1 NAME 'testOwner' "
               "DESC 'Test owner DN attribute' "
               "EQUALITY distinguishedNameMatch "
               "SYNTAX 1.3.6.1.4.1.1466.115.121.1.12 "
               "SINGLE-VALUE X-ORIGIN 'test' )")
    schema.add('attributetypes',
               "( 9.9.8.3 NAME 'testMember' "
               "DESC 'Test member DN attribute' "
               "EQUALITY distinguishedNameMatch "
               "SYNTAX 1.3.6.1.4.1.1466.115.121.1.12 "
               "X-ORIGIN 'test' )")
    schema.add('objectclasses',
               "( 9.9.8.2 NAME 'testOwned' "
               "DESC 'Entry with a required owner' "
               "SUP top AUXILIARY "
               "MUST ( testOwner ) MAY ( testMember ) X-ORIGIN 'test' )")

    referint_plugin = ReferentialIntegrityPlugin(inst)
    referint_plugin.enable()
    referint_plugin.ensure_present('referint-membership-attr', 'testOwner')
    referint_plugin.ensure_present('referint-membership-attr', 'testMember')

    inst.restart()

    yield inst

    log.info('Clean up entries')
    base = DSLdapObject(inst, dn=DEFAULT_SUFFIX)
    for oc in ('testOptRef', 'testOwned'):
        for entry in base.search(scope='subtree', filter=f'(objectclass={oc})'):
            obj = DSLdapObject(inst, dn=entry.dn)
            obj._protected = False
            obj.delete()
    users = nsUserAccounts(inst, DEFAULT_SUFFIX)
    for user in users.list():
        user.delete()


def test_modrdn_with_required_singlevalued_referint_attr(topology_st, referint_setup):
    """Test that MODRDN succeeds when a referint-tracked attribute is
    MUST and SINGLE-VALUE in the schema.

    :id: 7c1e3d4a-5f2b-4a8e-9d6c-1b3e5f7a9c2d
    :setup: Standalone instance with custom schema and referint plugin
    :steps:
        1. Create an owner user entry
        2. Create an owned entry with testOwner pointing to the owner
        3. Rename the owner entry
        4. Verify testOwner has exactly one value pointing to new DN
    :expectedresults:
        1. Success
        2. Success
        3. MODRDN succeeds without OBJECT_CLASS_VIOLATION
        4. Exactly one testOwner value with the renamed DN
    """
    inst = referint_setup

    log.info('Create the owner user entry')
    users = nsUserAccounts(inst, DEFAULT_SUFFIX)
    owner = users.create(properties={
        'uid': 'testowner',
        'cn': 'testowner',
        'uidNumber': '5000',
        'gidNumber': '5000',
        'homeDirectory': '/home/testowner',
        'displayName': 'Test Owner',
    })
    owner_orig_dn = owner.dn

    log.info('Create the owned entry with testOwner pointing to the owner')
    owned = DSLdapObject(inst)
    owned._create_objectclasses = ['top', 'person', 'testOwned']
    owned.create(rdn='cn=testowned', basedn=DEFAULT_SUFFIX, properties={
        'sn': 'testowned',
        'testOwner': owner.dn,
    })

    log.info('Rename the owner entry')
    owner.rename(new_rdn='uid=testowner_renamed')
    owner_new_dn = owner.dn

    log.info('Verify testOwner has exactly one value pointing to the new DN')
    vals = owned.get_attr_vals_utf8('testOwner')
    assert len(vals) == 1, f"Expected exactly 1 testOwner value, got {len(vals)}: {vals}"
    assert vals[0].lower() == owner_new_dn.lower(), (
        f"testOwner should be '{owner_new_dn}', got '{vals[0]}'"
    )
    assert vals[0].lower() != owner_orig_dn.lower(), (
        f"testOwner still contains the original DN '{owner_orig_dn}'"
    )


def test_modrdn_with_required_multivalued_referint_attr(topology_st, referint_setup):
    """Test MODRDN when a referint-tracked MUST attribute is multi-valued
    but only one value remains (the one being updated).

    :id: a3b4c5d6-e7f8-4a9b-8c1d-2e3f4a5b6c7d
    :setup: Standalone instance with custom schema and referint plugin
    :steps:
        1. Create two user entries (owner1 and owner2)
        2. Create an owned entry with testMember pointing to both owners
           and testOwner pointing to owner1
        3. Remove one testMember value so only one remains
        4. Rename the remaining referenced owner
        5. Verify testMember has exactly one value pointing to new DN
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. MODRDN succeeds without OBJECT_CLASS_VIOLATION
        5. Exactly one testMember value with the renamed DN
    """
    inst = referint_setup

    log.info('Create two user entries')
    users = nsUserAccounts(inst, DEFAULT_SUFFIX)
    owner1 = users.create(properties={
        'uid': 'mvowner1',
        'cn': 'mvowner1',
        'uidNumber': '5010',
        'gidNumber': '5010',
        'homeDirectory': '/home/mvowner1',
        'displayName': 'MV Owner 1',
    })
    owner2 = users.create(properties={
        'uid': 'mvowner2',
        'cn': 'mvowner2',
        'uidNumber': '5011',
        'gidNumber': '5011',
        'homeDirectory': '/home/mvowner2',
        'displayName': 'MV Owner 2',
    })

    log.info('Create owned entry with testMember pointing to both owners')
    owned = DSLdapObject(inst)
    owned._create_objectclasses = ['top', 'person', 'testOwned']
    owned.create(rdn='cn=mvowned', basedn=DEFAULT_SUFFIX, properties={
        'sn': 'mvowned',
        'testOwner': owner1.dn,
        'testMember': [owner1.dn, owner2.dn],
    })

    log.info('Remove owner2 from testMember so only owner1 remains')
    owned.remove('testMember', owner2.dn)

    vals = owned.get_attr_vals_utf8('testMember')
    assert len(vals) == 1, f"Expected 1 testMember value before rename, got {len(vals)}"

    log.info('Rename owner1')
    owner1_orig_dn = owner1.dn
    owner1.rename(new_rdn='uid=mvowner1_renamed')
    owner1_new_dn = owner1.dn

    log.info('Verify testMember has exactly one value pointing to the new DN')
    vals = owned.get_attr_vals_utf8('testMember')
    assert len(vals) == 1, f"Expected exactly 1 testMember value, got {len(vals)}: {vals}"
    assert vals[0].lower() == owner1_new_dn.lower(), (
        f"testMember should be '{owner1_new_dn}', got '{vals[0]}'"
    )
    assert vals[0].lower() != owner1_orig_dn.lower(), (
        f"testMember still contains the original DN '{owner1_orig_dn}'"
    )


def test_modrdn_with_non_must_singlevalued_referint_attr(topology_st, referint_setup):
    """Test MODRDN when a referint-tracked attribute is SINGLE-VALUE
    but not MUST, DELETE+ADD should work normally.

    :id: f1e2d3c4-b5a6-4789-8012-3456789abcde
    :setup: Standalone instance with custom schema and referint plugin
    :steps:
        1. Create an owner user entry
        2. Create an entry with optional SINGLE-VALUE testMember
           pointing to the owner (testMember is MAY, not MUST)
        3. Rename the owner entry
        4. Verify testMember has exactly one value pointing to new DN
    :expectedresults:
        1. Success
        2. Success
        3. MODRDN succeeds (DELETE+ADD works since attr is not MUST)
        4. Exactly one testMember value with the renamed DN
    """
    inst = referint_setup

    log.info('Add a SINGLE-VALUE variant of testMember for this test')
    schema = Schema(inst)
    schema.add('attributetypes',
               "( 9.9.8.4 NAME 'testSingleRef' "
               "DESC 'Test single-value non-required DN attr' "
               "EQUALITY distinguishedNameMatch "
               "SYNTAX 1.3.6.1.4.1.1466.115.121.1.12 "
               "SINGLE-VALUE X-ORIGIN 'test' )")
    schema.add('objectclasses',
               "( 9.9.8.5 NAME 'testOptRef' "
               "DESC 'Entry with optional single-value ref' "
               "SUP top AUXILIARY "
               "MAY ( testSingleRef ) X-ORIGIN 'test' )")

    referint_plugin = ReferentialIntegrityPlugin(inst)
    referint_plugin.ensure_present('referint-membership-attr', 'testSingleRef')
    inst.restart()

    log.info('Create the owner user entry')
    users = nsUserAccounts(inst, DEFAULT_SUFFIX)
    owner = users.create(properties={
        'uid': 'svowner',
        'cn': 'svowner',
        'uidNumber': '5020',
        'gidNumber': '5020',
        'homeDirectory': '/home/svowner',
        'displayName': 'SV Owner',
    })
    owner_orig_dn = owner.dn

    log.info('Create an entry with optional testSingleRef pointing to owner')
    entry = DSLdapObject(inst)
    entry._create_objectclasses = ['top', 'person', 'testOptRef']
    entry.create(rdn='cn=sventry', basedn=DEFAULT_SUFFIX, properties={
        'sn': 'sventry',
        'testSingleRef': owner.dn,
    })

    log.info('Rename the owner entry')
    owner.rename(new_rdn='uid=svowner_renamed')
    owner_new_dn = owner.dn

    log.info('Verify testSingleRef has exactly one value pointing to the new DN')
    vals = entry.get_attr_vals_utf8('testSingleRef')
    assert len(vals) == 1, f"Expected exactly 1 testSingleRef value, got {len(vals)}: {vals}"
    assert vals[0].lower() == owner_new_dn.lower(), (
        f"testSingleRef should be '{owner_new_dn}', got '{vals[0]}'"
    )
    assert vals[0].lower() != owner_orig_dn.lower(), (
        f"testSingleRef still contains the original DN '{owner_orig_dn}'"
    )


