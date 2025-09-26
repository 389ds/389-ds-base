# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import pytest
import ldap
import logging
from lib389.utils import ensure_str, ds_is_older
from lib389.topologies import topology_st
from lib389._constants import DEFAULT_SUFFIX
from lib389.plugins import MemberOfPlugin, ReferentialIntegrityPlugin
from lib389.idm.user import UserAccount, UserAccounts
from lib389.idm.group import Group, Groups
from lib389.idm.nscontainer import nsContainers

log = logging.getLogger(__name__)

pytestmark = [pytest.mark.tier1,
              pytest.mark.skipif(ds_is_older('1.3.4'), reason="Not implemented")]

SCOPE_IN_CN = 'in'
SCOPE_OUT_CN = 'out'
SCOPE_IN_DN = f'cn={SCOPE_IN_CN},{DEFAULT_SUFFIX}'
SCOPE_OUT_DN = f'cn={SCOPE_OUT_CN},{DEFAULT_SUFFIX}'

PROVISIONING_CN = "provisioning"
PROVISIONING_DN = f'cn={PROVISIONING_CN},{SCOPE_IN_DN}'

ACTIVE_CN = "active"
STAGE_CN = "staged users"
ACTIVE_DN = f"cn={ACTIVE_CN},{SCOPE_IN_DN}"
STAGE_DN = f"cn={STAGE_CN},{PROVISIONING_DN}"

STAGE_USER_CN = "stage guy"
ACTIVE_USER_CN = "active guy"
OUT_USER_CN = "out guy"

STAGE_GROUP_CN = "stage group"
ACTIVE_GROUP_CN = "active group"
OUT_GROUP_CN = "out group"
INDIRECT_ACTIVE_GROUP_CN = "indirect active group"


"""
Tree structure:
             ┌─────────────────┐
             │dc=example,dc=com│
             └────────┬────────┘
            ┌─────────┴──────────────────────────┐
         ┌──┴───────────┐                     ┌──┴───────────┐
         │cn=in         │                     │cn=out        │
         |(active scope)|                     |(not in scope)|
         └──┬───────────┘                     └──────────────┘
    ┌───────┴───────────┐
┌───┴───────┐    ┌──────┴─────────┐
│ cn=active │    │cn=provisioning │
|           |    |(excluded scope)|
└───────────┘    └───────┬────────┘
                         │
                    ┌────┴───┐
                    │cn=stage│
                    └────────┘
"""


@pytest.fixture(scope='function')
def containers(topology_st, request):
    """Create containers for the tests"""
    log.info('Create containers for the tests')
    containers = nsContainers(topology_st.standalone, DEFAULT_SUFFIX)
    cont_scope_in = containers.create(properties={'cn': SCOPE_IN_CN})
    cont_scope_out = containers.create(properties={'cn': SCOPE_OUT_CN})

    containers_scope_in = nsContainers(topology_st.standalone, cont_scope_in.dn)
    cont_active = containers_scope_in.create(properties={'cn': ACTIVE_CN})
    cont_provisioning = containers_scope_in.create(properties={'cn': PROVISIONING_CN})

    containers_provisioning = nsContainers(topology_st.standalone, cont_provisioning.dn)
    cont_stage = containers_provisioning.create(properties={'cn': STAGE_CN})

    def fin():
        log.info('Delete containers')
        cont_stage.delete()
        cont_provisioning.delete()
        cont_active.delete()
        cont_scope_out.delete()
        cont_scope_in.delete()

    request.addfinalizer(fin)


@pytest.fixture(scope='function')
def active_group(topology_st, request):
    log.info('Create active group')
    active_groups = Groups(topology_st.standalone, DEFAULT_SUFFIX,
                           rdn=f'cn={ACTIVE_CN},cn={SCOPE_IN_CN}')
    if active_groups.exists(ACTIVE_GROUP_CN):
        active_groups.get(ACTIVE_GROUP_CN).delete()

    active_group = active_groups.create(properties={'cn': ACTIVE_GROUP_CN})

    def fin():
        if active_group.exists():
            log.info('Delete active group')
            active_group.delete()

    request.addfinalizer(fin)

    return active_group


@pytest.fixture(scope='function')
def stage_group(topology_st, request):
    log.info('Create stage group')
    stage_groups = Groups(topology_st.standalone, DEFAULT_SUFFIX,
                          rdn=f'cn={STAGE_CN},cn={PROVISIONING_CN},cn={SCOPE_IN_CN}')
    if stage_groups.exists(STAGE_GROUP_CN):
        stage_groups.get(STAGE_GROUP_CN).delete()

    stage_group = stage_groups.create(properties={'cn': STAGE_GROUP_CN})

    def fin():
        if stage_group.exists():
            log.info('Delete stage group')
            stage_group.delete()

    request.addfinalizer(fin)

    return stage_group


@pytest.fixture(scope='function')
def out_group(topology_st, request):
    log.info('Create out group')
    out_groups = Groups(topology_st.standalone, DEFAULT_SUFFIX,
                        rdn=f'cn={SCOPE_OUT_CN}')
    if out_groups.exists(OUT_GROUP_CN):
        out_groups.get(OUT_GROUP_CN).delete()

    out_group = out_groups.create(properties={'cn': OUT_GROUP_CN})

    def fin():
        if out_group.exists():
            log.info('Delete out group')
            out_group.delete()

    request.addfinalizer(fin)

    return out_group


@pytest.fixture(scope='function')
def indirect_active_group(topology_st, request):
    log.info('Create indirect active group')
    indirect_active_groups = Groups(topology_st.standalone, DEFAULT_SUFFIX,
                                    rdn=f'cn={ACTIVE_CN},cn={SCOPE_IN_CN}')
    if indirect_active_groups.exists(INDIRECT_ACTIVE_GROUP_CN):
        indirect_active_groups.get(INDIRECT_ACTIVE_GROUP_CN).delete()

    indirect_active_group = indirect_active_groups.create(properties={'cn': INDIRECT_ACTIVE_GROUP_CN})

    def fin():
        if indirect_active_group.exists():
            log.info('Delete indirect active group')
            indirect_active_group.delete()

    request.addfinalizer(fin)

    return indirect_active_group


@pytest.fixture(scope='function')
def active_user(topology_st, request):
    log.info('Create active user')
    active_users = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX,
                                rdn=f'cn={ACTIVE_CN},cn={SCOPE_IN_CN}')
    if active_users.exists(ACTIVE_USER_CN):
        active_users.get(ACTIVE_USER_CN).delete()

    active_user = active_users.create(properties={'cn': ACTIVE_USER_CN,
                                               'uid': ACTIVE_USER_CN,
                                               'sn': ACTIVE_USER_CN,
                                               'uidNumber': '1',
                                               'gidNumber': '11',
                                               'homeDirectory': f'/home/{ACTIVE_USER_CN}'})

    def fin():
        if active_user.exists():
            log.info('Delete active user')
            active_user.delete()

    request.addfinalizer(fin)

    return active_user


@pytest.fixture(scope='function')
def stage_user(topology_st, request):
    log.info('Create stage user')
    stage_users = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX,
                               rdn=f'cn={STAGE_CN},cn={PROVISIONING_CN},cn={SCOPE_IN_CN}')
    if stage_users.exists(STAGE_USER_CN):
        stage_users.get(STAGE_USER_CN).delete()

    stage_user = stage_users.create(properties={'cn': STAGE_USER_CN,
                                               'uid': STAGE_USER_CN,
                                               'sn': STAGE_USER_CN,
                                               'uidNumber': '2',
                                               'gidNumber': '12',
                                               'homeDirectory': f'/home/{STAGE_USER_CN}'})

    def fin():
        if stage_user.exists():
            log.info('Delete stage user')
            stage_user.delete()

    request.addfinalizer(fin)

    return stage_user


@pytest.fixture(scope='function')
def out_user(topology_st, request):
    log.info('Create out user')
    out_users = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX,
                             rdn=f'cn={SCOPE_OUT_CN}')
    if out_users.exists(OUT_USER_CN):
        out_users.get(OUT_USER_CN).delete()

    out_user = out_users.create(properties={'cn': OUT_USER_CN,
                                            'uid': OUT_USER_CN,
                                            'sn': OUT_USER_CN,
                                            'uidNumber': '3',
                                            'gidNumber': '13',
                                            'homeDirectory': f'/home/{OUT_USER_CN}'})

    def fin():
        if out_user.exists():
            log.info('Delete out user')
            out_user.delete()

    request.addfinalizer(fin)

    return out_user


def check_memberof(user, group_dn, should_exist=True):
    """Helper function to check memberOf attribute"""
    memberof_values = user.get_attr_vals_utf8_l('memberof')
    found = group_dn.lower() in memberof_values
    
    if should_exist:
        assert found, (
            f"Expected user '{user.dn}' to be a member of group '{group_dn}', "
            f"but it was not found in memberOf: {memberof_values}"
        )
    else:
        assert not found, (
            f"Did not expect user '{user.dn}' to be a member of group '{group_dn}', "
            f"but it was found in memberOf: {memberof_values}"
        )


@pytest.fixture(scope='function')
def config_memberof(request, topology_st):
    memberof = MemberOfPlugin(topology_st.standalone)
    memberof.enable()
    memberof.replace('memberOfEntryScope', SCOPE_IN_DN)
    memberof.replace('memberOfEntryScopeExcludeSubtree', PROVISIONING_DN)

    # Configure Referential Integrity plugin  
    referint = ReferentialIntegrityPlugin(topology_st.standalone)
    referint.enable()
    referint.replace('nsslapd-pluginentryscope', SCOPE_IN_DN)
    referint.replace('nsslapd-plugincontainerscope', SCOPE_IN_DN)
    referint.replace('nsslapd-pluginExcludeEntryScope', PROVISIONING_DN)

    topology_st.standalone.restart()

    def fin():
        memberof.disable()
        referint.disable()
        topology_st.standalone.restart()

    request.addfinalizer(fin)


@pytest.mark.parametrize(
    "user, group, is_memberof",
    [("active_user", "active_group", True),
     ("active_user", "stage_group", False),
     ("active_user", "out_group", False),
     ("stage_user", "active_group", False),
     ("stage_user", "stage_group", False),
     ("stage_user", "out_group", False),
     ("out_user", "active_group", False),
     ("out_user", "stage_group", False),
     ("out_user", "out_group", False),
     ])
def test_memberof_add_user_to_group(topology_st, containers, request, group, user, is_memberof, config_memberof):
    """Verify memberOf behavior when adding/removing users to groups across scopes

    :id: 3f0b0f8a-1d9b-4e4b-8b8e-3a3f3a5b5b11
    :setup: Standalone instance; MemberOf and Referential Integrity configured; test containers, users, and groups
    :steps:
        1. Resolve parametrized fixtures for user and group
        2. Add the user to the group
        3. Verify the group membership is present
        4. Verify memberOf presence or absence according to scope configuration
        5. Remove the user from the group
        6. Verify the group membership is removed
        7. Verify memberOf was removed
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
    """
    
    user = request.getfixturevalue(user)
    group = request.getfixturevalue(group)

    # Add user to group, verify that user is member of group and memberof attribute is updated according to the scope
    group.add_member(user.dn)
    assert group.is_member(user.dn)
    check_memberof(user, group.dn, is_memberof)
    
    # Remove user from group, verify that user is not member of group and memberof attribute is updated according to the scope
    group.remove_member(user.dn)
    assert not group.is_member(user.dn)
    check_memberof(user, group.dn, should_exist=False)


def test_memberof_modrdn_active_user_in_active_group(topology_st, containers, active_group, active_user, config_memberof):
    """Renaming active user within active scope preserves membership and memberOf

    :id: 7f3c2f3f-6a02-4d7c-a2c3-6d4f7f6a2b10
    :setup: Standalone instance; MemberOf and Referential Integrity configured; active user and active group exist
    :steps:
        1. Add the active user to the active group
        2. Rename the user RDN within the active scope
        3. Verify the group membership is preserved
        4. Verify memberOf remains present
        5. Rename the user back to the original RDN
        6. Verify the group membership is preserved
        7. Verify memberOf remains present
        8. Remove the user from the group and verify memberOf is removed
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
        8. Success
    """
    
    olduid = active_user.get_attr_val_utf8_l('uid')

    # Add active user to active group, verify that user is member of group and memberof attribute is filled
    active_group.add_member(active_user.dn)
    assert active_group.is_member(active_user.dn)
    check_memberof(active_user, active_group.dn, should_exist=True)

    # Rename active user, verify that user is still member of group and memberof attribute is filled
    olddn = active_user.dn
    active_user.rename(f"uid=x{olduid}")
    assert active_group.is_member(active_user.dn)
    assert not active_group.is_member(olddn)
    check_memberof(active_user, active_group.dn, should_exist=True)

    # Rename active user back to original name, verify that user is still member of group and memberof attribute is filled
    olddn = active_user.dn
    active_user.rename(f"uid={olduid}")
    assert active_group.is_member(active_user.dn)
    assert not active_group.is_member(olddn)
    check_memberof(active_user, active_group.dn, should_exist=True)

    # Remove active user from active group, verify that user is not member of group and memberof attribute is removed
    active_group.remove_member(active_user.dn)
    assert not active_group.is_member(active_user.dn)
    check_memberof(active_user, active_group.dn, should_exist=False)


def test_memberof_modrdn_active_user_to_stage(topology_st, containers, active_group, active_user, config_memberof):
    """Moving active user to excluded stage subtree cleans membership and memberOf

    :id: 8a1a2c3b-5d6e-4f71-9f41-00b8c63d9a07
    :setup: Standalone instance; MemberOf and Referential Integrity configured; active user and active group exist
    :steps:
        1. Add the active user to the active group and verify memberOf is present
        2. Move the user under the excluded stage subtree
        3. Verify the group membership is removed
        4. Verify memberOf is removed
        5. Move the user back to the active subtree
        6. Verify memberOf is not implicitly restored, and membership is not restored
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
    """
    olduid = active_user.get_attr_val_utf8_l('uid')

    # Add active user to active group, verify that user is member of group and memberof attribute is filled
    active_group.add_member(active_user.dn)
    assert active_group.is_member(active_user.dn)
    check_memberof(active_user, active_group.dn, should_exist=True)

    # Rename active user to stage subtree, v    erify that user is not member of group and memberof attribute is removed
    olddn = active_user.dn
    active_user.rename(f"uid={olduid}", newsuperior=STAGE_DN)
    assert not active_group.is_member(active_user.dn)
    assert not active_group.is_member(olddn)
    check_memberof(active_user, active_group.dn, should_exist=False)

    # Rename active user back to active subtree, verify that user is not member of group and memberof attribute is not set
    olddn = active_user.dn
    active_user.rename(f"uid={olduid}", newsuperior=ACTIVE_DN)
    assert not active_group.is_member(active_user.dn)
    assert not active_group.is_member(olddn)
    check_memberof(active_user, active_group.dn, should_exist=False)


def test_memberof_modrdn_active_user_to_out(topology_st, containers, active_group, active_user, config_memberof):
    """Moving active user to out of scope subtreecleans membership and memberOf

    :id: 2c9b5e4a-4a58-4c19-9b5c-41b9b5a0d0c1
    :setup: Standalone instance; MemberOf and Referential Integrity configured; active user and active group exist
    :steps:
        1. Add the active user to the active group and verify memberOf is present
        2. Move the user to the out-of-scope subtree
        3. Verify the group membership is removed
        4. Verify memberOf is removed
        5. Move the user back to the active subtree
        6. Verify memberOf is not implicitly restored, and membership is not restored
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
    """
    olduid = active_user.get_attr_val_utf8_l('uid')

    # Add active user to active group, verify that user is member of group and memberof attribute is filled
    active_group.add_member(active_user.dn)
    assert active_group.is_member(active_user.dn)
    check_memberof(active_user, active_group.dn, should_exist=True)

    # Rename active user to out-of-scopesubtree, verify that user is not member of group and memberof attribute is removed
    active_user.rename(f"uid=x{olduid}", newsuperior=SCOPE_OUT_DN)
    assert not active_group.is_member(active_user.dn)
    check_memberof(active_user, active_group.dn, should_exist=False)

    # Rename active user back to active subtree, verify that user is not member of group and memberof attribute is not set
    active_user.rename(f"uid={olduid}", newsuperior=ACTIVE_DN)
    assert not active_group.is_member(active_user.dn)
    check_memberof(active_user, active_group.dn, should_exist=False)


def test_memberof_modrdn_stage_user_to_active(topology_st, containers, stage_user, active_group, config_memberof):
    """Move stage user into active scope grants memberOf and preserves membership

    :id: d3f5a6c7-9b12-4d34-b5a1-2f3c4d5e6f70
    :setup: Standalone instance; MemberOf and Referential Integrity configured; stage user and active group exist
    :steps:
        1. Add the stage user to the active group and verify memberOf is not set due to exclusion
        2. Move the user to the active subtree
        3. Verify the group membership is present
        4. Verify memberOf is now present
        5. Move the user back to the stage subtree
        6. Verify the group membership is removed and memberOf is removed
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
    """
    olduid = stage_user.get_attr_val_utf8_l('uid')

    # Add stage user to active group, verify that user is a member of group but memberof attribute is not set
    active_group.add_member(stage_user.dn)
    assert active_group.is_member(stage_user.dn)
    check_memberof(stage_user, active_group.dn, should_exist=False)

    # Rename stage user to active subtree, verify that user is a member of group and memberof attribute is set
    stage_user.rename(f"uid={olduid}", newsuperior=ACTIVE_DN)
    assert active_group.is_member(stage_user.dn)
    check_memberof(stage_user, active_group.dn, should_exist=True)

    # Rename stage user back to stage subtree, verify that user is not member of group and memberof attribute is not set
    stage_user.rename(f"uid={olduid}", newsuperior=STAGE_DN)
    assert not active_group.is_member(stage_user.dn)
    check_memberof(stage_user, active_group.dn, should_exist=False)


def test_memberof_modrdn_stage_user_to_stage(topology_st, containers, stage_user, active_group, config_memberof):
    """Renaming stage user within excluded subtree does not set memberOf and cleans membership

    :id: 0c1b2a3d-4e5f-6a7b-8c9d-0e1f2a3b4c5d
    :setup: Standalone instance; MemberOf and Referential Integrity configured; stage user and active group exist
    :steps:
        1. Add the stage user to the active group and verify memberOf is not set
        2. Rename the user within the stage subtree
        3. Verify the user is not a member of the active group and memberOf remains unset
        4. Rename the user back to the original RDN
        5. Verify the user remains a member according to scope rules and memberOf remains unset
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
    """
    olduid = stage_user.get_attr_val_utf8_l('uid')
    olddn = stage_user.dn

    # Add stage user to active group, verify that user is a member of group but memberof attribute is not set
    active_group.add_member(stage_user.dn)
    assert active_group.is_member(stage_user.dn)
    check_memberof(stage_user, active_group.dn, should_exist=False)

    # Rename stage user, verify that user is not member of group and memberof attribute is not set
    stage_user.rename(f"uid=x{olduid}")
    assert not active_group.is_member(stage_user.dn)
    # Original user DN is still a member of the group due to the referential integrity plugin configuration for this test
    assert active_group.is_member(olddn)
    check_memberof(stage_user, active_group.dn, should_exist=False)

    # Rename stage user back to original name, verify that user is member of group and memberof attribute is not set
    stage_user.rename(f"uid={olduid}")
    assert active_group.is_member(stage_user.dn)
    check_memberof(stage_user, active_group.dn, should_exist=False)


def test_memberof_indirect_active_group(topology_st, containers, active_group, indirect_active_group, active_user, config_memberof):
    """Verify indirect group membership updates memberOf across nested groups

    :id: a1b2c3d4-e5f6-47a8-9b0c-d1e2f3a4b5c6
    :setup: Standalone instance; MemberOf and Referential Integrity configured; active and indirect groups exist
    :steps:
        1. Add the active group as a member of the indirect active group
        2. Verify the active group has memberOf pointing to the indirect group
        3. Add an active user to the active group
        4. Verify the user has memberOf for both the active and indirect groups
        5. Remove the active group from the indirect group
        6. Verify the user's memberOf for the indirect group is removed, and active group memberOf remains
        7. Remove the user from the active group and verify memberOf cleanup
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
    """
    # Add active group to indirect active group
    # Verify that active group is member of indirect active group and memberof attribute is set
    indirect_active_group.add_member(active_group.dn)
    assert indirect_active_group.is_member(active_group.dn)
    check_memberof(active_group, indirect_active_group.dn, should_exist=True)

    # Add active user to active group, verify that user is member of group and memberof attribute is filled for both groups
    active_group.add_member(active_user.dn)
    assert active_group.is_member(active_user.dn)
    check_memberof(active_user, active_group.dn, should_exist=True)
    check_memberof(active_user, indirect_active_group.dn, should_exist=True)

    # Remove active group from indirect active group
    # Verify that active group is not member of indirect active group and memberof attribute is removed between groups
    # User still has active group memberof value
    indirect_active_group.remove_member(active_group.dn)
    assert not indirect_active_group.is_member(active_group.dn)
    check_memberof(active_group, indirect_active_group.dn, should_exist=False)
    assert not indirect_active_group.is_member(active_user.dn)
    check_memberof(active_user, indirect_active_group.dn, should_exist=False)
    assert active_group.is_member(active_user.dn)
    check_memberof(active_user, active_group.dn, should_exist=True)

    # Remove active user from active group
    # Verify that user is not member of group and memberof attribute is removed
    active_group.remove_member(active_user.dn)
    assert not active_group.is_member(active_user.dn)
    check_memberof(active_user, active_group.dn, should_exist=False)


def test_memberof_indirect_active_group_active_user_to_stage(topology_st, containers, active_group, indirect_active_group, active_user, config_memberof):
    """Move active user to stage subtree cleans direct and indirect memberOf

    :id: b2c3d4e5-f6a7-48b9-801c-d2e3f4a5b6c7
    :setup: Standalone instance; MemberOf and Referential Integrity configured; active user, active and indirect groups exist
    :steps:
        1. Add the active group to the indirect active group
        2. Add the active user to the active group and verify memberOf for both groups
        3. Move the active user to the stage subtree
        4. Verify direct and indirect memberOf are removed and group membership is cleaned
        5. Move the user back to the active subtree and verify memberOf is not implicitly restored
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
    """
    olduid = active_user.get_attr_val_utf8_l('uid')

    # Add active group to indirect active group
    # Verify that active group is member of indirect active group and memberof attribute is set
    indirect_active_group.add_member(active_group.dn)
    assert indirect_active_group.is_member(active_group.dn)
    check_memberof(active_group, indirect_active_group.dn, should_exist=True)

    # Add active user to active group
    # Verify that user is member of group and memberof attribute is filled for both groups
    active_group.add_member(active_user.dn)
    assert active_group.is_member(active_user.dn)
    check_memberof(active_user, active_group.dn, should_exist=True)
    check_memberof(active_user, indirect_active_group.dn, should_exist=True)

    # Rename active user to stage subtree
    # Verify that user is not member of group and memberof attribute is removed
    active_user.rename(f"uid={olduid}", newsuperior=STAGE_DN)
    assert not active_group.is_member(active_user.dn)
    check_memberof(active_user, active_group.dn, should_exist=False)
    check_memberof(active_user, indirect_active_group.dn, should_exist=False)

    # Rename active user back to active subtree
    # Verify that user is not member of group and memberof attribute is not set
    active_user.rename(f"uid={olduid}", newsuperior=ACTIVE_DN)
    assert not active_group.is_member(active_user.dn)
    check_memberof(active_user, active_group.dn, should_exist=False)
    check_memberof(active_user, indirect_active_group.dn, should_exist=False)


def test_memberof_indirect_active_group_active_user_to_out(topology_st, containers, active_group, indirect_active_group, active_user, config_memberof):
    """Moving active user out of scope cleans direct and indirect memberOf

    :id: c3d4e5f6-a7b8-49c0-912d-e3f4a5b6c7d8
    :setup: Standalone instance; MemberOf and Referential Integrity configured; active user, active and indirect groups exist
    :steps:
        1. Add the active group to the indirect active group
        2. Add the active user to the active group and verify memberOf for both groups
        3. Move the active user to the out-of-scope subtree
        4. Verify direct and indirect memberOf are removed and group membership is cleaned
        5. Move the user back to the active subtree and verify memberOf is not implicitly restored
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
    """
    olduid = active_user.get_attr_val_utf8_l('uid')

    # Add active group to indirect active group
    # Verify that active group is member of indirect active group and memberof attribute is set
    indirect_active_group.add_member(active_group.dn)
    assert indirect_active_group.is_member(active_group.dn)
    check_memberof(active_group, indirect_active_group.dn, should_exist=True)

    # Add active user to active group
    # Verify that user is member of group and memberof attribute is filled for both groups
    active_group.add_member(active_user.dn)
    assert active_group.is_member(active_user.dn)
    check_memberof(active_user, active_group.dn, should_exist=True)
    check_memberof(active_user, indirect_active_group.dn, should_exist=True)

    # Rename active user to out-of-scope subtree
    # Verify that user is not member of group and memberof attribute is removed
    active_user.rename(f"uid={olduid}", newsuperior=SCOPE_OUT_DN)
    assert not active_group.is_member(active_user.dn)
    check_memberof(active_user, active_group.dn, should_exist=False)
    check_memberof(active_user, indirect_active_group.dn, should_exist=False)

    # Rename active user back to active subtree
    # Verify that user is not member of group and memberof attribute is not set
    active_user.rename(f"uid={olduid}", newsuperior=ACTIVE_DN)
    assert not active_group.is_member(active_user.dn)
    check_memberof(active_user, active_group.dn, should_exist=False)
    check_memberof(active_user, indirect_active_group.dn, should_exist=False)


def test_memberof_indirect_active_group_stage_user_to_active(topology_st, containers, active_group, indirect_active_group, stage_user, config_memberof):
    """Moving stage user to active subtree grants direct and indirect memberOf

    :id: d4e5f6a7-b8c9-4ad1-923e-f4a5b6c7d8e9
    :setup: Standalone instance; MemberOf and Referential Integrity configured; stage user, active and indirect groups exist
    :steps:
        1. Add the active group to the indirect active group
        2. Add the stage user to the active group and verify memberOf is not set
        3. Move the stage user to the active subtree
        4. Verify the user has memberOf for both the active and indirect groups
        5. Move the user back to the stage subtree, verify memberOf is removed from both groups, user is not a member of active group
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
    """
    olduid = stage_user.get_attr_val_utf8_l('uid')

    # Add active group to indirect active group
    # Verify that active group is member of indirect active group and memberof attribute is set
    indirect_active_group.add_member(active_group.dn)
    assert indirect_active_group.is_member(active_group.dn)
    check_memberof(active_group, indirect_active_group.dn, should_exist=True)

    # Add stage user to active group
    # Verify that user is a member of group but memberof attribute is not set
    active_group.add_member(stage_user.dn)  
    assert active_group.is_member(stage_user.dn)
    assert not indirect_active_group.is_member(stage_user.dn)
    check_memberof(stage_user, active_group.dn, should_exist=False)
    check_memberof(stage_user, indirect_active_group.dn, should_exist=False)

    # Rename stage user to active subtree
    # Verify that user is a member of group and memberof attribute is set
    stage_user.rename(f"uid={olduid}", newsuperior=ACTIVE_DN)
    assert active_group.is_member(stage_user.dn)
    check_memberof(stage_user, active_group.dn, should_exist=True) 
    check_memberof(stage_user, indirect_active_group.dn, should_exist=True)

    # Rename stage user back to stage subtree
    # Verify that user is not member of group and memberof attribute is not set
    stage_user.rename(f"uid={olduid}", newsuperior=STAGE_DN)
    assert not active_group.is_member(stage_user.dn)
    assert not indirect_active_group.is_member(stage_user.dn)
    check_memberof(stage_user, active_group.dn, should_exist=False)
    check_memberof(stage_user, indirect_active_group.dn, should_exist=False)