# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import pytest
import json
import logging
import os

from lib389 import DEFAULT_SUFFIX
from lib389.cli_idm.role import (
    list, get, get_dn,
    create_managed, create_filtered, create_nested,
    delete, modify, rename, entry_status, subtree_status, lock, unlock
    )
from lib389.topologies import topology_st
from lib389.cli_base import FakeArgs
from lib389.utils import ds_is_older, is_a_dn
from lib389.idm.role import Roles, ManagedRoles, FilteredRoles, NestedRoles
from . import check_value_in_log_and_reset, check_value_in_log


pytestmark = pytest.mark.tier0

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

managed_role_name = 'test_managed_role'
filtered_role_name = 'test_filtered_role'
nested_role_name = 'test_nested_role'


@pytest.fixture(scope="function")
def create_test_managed_role(topology_st, request):
    managed_roles = ManagedRoles(topology_st.standalone, DEFAULT_SUFFIX)

    log.info('Create test managed role')
    if managed_roles.exists(managed_role_name):
        test_managed_role = managed_roles.get(managed_role_name)
        test_managed_role.delete()

    properties = FakeArgs()
    properties.cn = managed_role_name
    create_managed(topology_st.standalone, DEFAULT_SUFFIX, topology_st.logcap.log, properties)
    test_managed_role = managed_roles.get(managed_role_name)

    def fin():
        log.info('Delete test managed role')
        if test_managed_role.exists():
            test_managed_role.delete()

    request.addfinalizer(fin)


@pytest.fixture(scope="function")
def create_test_filtered_role(topology_st, request):
    filtered_roles = FilteredRoles(topology_st.standalone, DEFAULT_SUFFIX)

    log.info('Create test filtered role')
    if filtered_roles.exists(filtered_role_name):
        test_filtered_role = filtered_roles.get(filtered_role_name)
        test_filtered_role.delete()

    properties = FakeArgs()
    properties.cn = filtered_role_name
    create_filtered(topology_st.standalone, DEFAULT_SUFFIX, topology_st.logcap.log, properties)
    test_filtered_role = filtered_roles.get(filtered_role_name)

    def fin():
        log.info('Delete test filtered role')
        if test_filtered_role.exists():
            test_filtered_role.delete()

    request.addfinalizer(fin)


@pytest.fixture(scope="function")
def create_test_nested_role(topology_st, create_test_managed_role, request):
    managed_roles = ManagedRoles(topology_st.standalone, DEFAULT_SUFFIX)
    managed_role = managed_roles.get(managed_role_name)

    nested_roles = NestedRoles(topology_st.standalone, DEFAULT_SUFFIX)

    log.info('Create test nested role')
    if nested_roles.exists(nested_role_name):
        test_nested_role = nested_roles.get(nested_role_name)
        test_nested_role.delete()

    properties = FakeArgs()
    properties.cn = nested_role_name
    properties.nsRoleDN = managed_role.dn
    create_nested(topology_st.standalone, DEFAULT_SUFFIX, topology_st.logcap.log, properties)
    test_nested_role = nested_roles.get(nested_role_name)

    def fin():
        log.info('Delete test nested role')
        if test_nested_role.exists():
            test_nested_role.delete()

    request.addfinalizer(fin)


@pytest.fixture(scope="function")
def add_description(topology_st, request):
    managed_roles = ManagedRoles(topology_st.standalone, DEFAULT_SUFFIX)
    test_managed_role = managed_roles.get(managed_role_name)
    args = FakeArgs()
    args.dn = test_managed_role.dn
    args.selector = managed_role_name
    args.changes = ['add:description:{}'.format(managed_role_name)]
    modify(topology_st.standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args, warn=False)


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_role_create_managed(topology_st):
    """ Test dsidm role create-managed option

    :id: 8a7f221d-ceae-4d64-acd5-eb02b714a883
    :setup: Standalone instance
    :steps:
        1. Run dsidm role create-managed
        2. Check that correct message is provided on creation
        3. Check that created role exists
    :expectedresults:
        1. Success
        1. Success
        1. Success
    """

    standalone = topology_st.standalone
    output = 'Successfully created {}'.format(managed_role_name)

    args = FakeArgs()
    args.cn = managed_role_name

    log.info('Test dsidm role create-managed')
    create_managed(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value=output)

    log.info('Check that the managed role exists and has proper objectclasses')
    managed_roles = ManagedRoles(standalone, DEFAULT_SUFFIX)
    new_managed_role = managed_roles.get(managed_role_name)
    assert new_managed_role.exists()
    assert new_managed_role.present('objectClass', 'nsSimpleRoleDefinition')
    assert new_managed_role.present('objectClass', 'nsManagedRoleDefinition')

    log.info('Clean up for next test')
    new_managed_role.delete()

@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_role_create_filtered(topology_st):
    """ Test dsidm role create-filtered option

    :id: 8913e2f0-992f-49d9-8a82-92d584ded84f
    :setup: Standalone instance
    :steps:
        1. Run dsidm role create-filtered
        2. Check that correct message is provided on creation
        3. Check that created role exists
    :expectedresults:
        1. Success
        1. Success
        1. Success
    """

    standalone = topology_st.standalone
    output = 'Successfully created {}'.format(filtered_role_name)

    args = FakeArgs()
    args.cn = filtered_role_name
    args.nsrolefilter = "(cn=*)"

    log.info('Test dsidm role create-filtered')
    create_filtered(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value=output)

    log.info('Check that the filtered role exists and has proper objectclasses')
    filtered_roles = FilteredRoles(standalone, DEFAULT_SUFFIX)
    new_filtered_role = filtered_roles.get(filtered_role_name)
    assert new_filtered_role.exists()
    assert new_filtered_role.present('objectClass', 'nsComplexRoleDefinition')
    assert new_filtered_role.present('objectClass', 'nsFilteredRoleDefinition')

    log.info('Clean up for next test')
    new_filtered_role.delete()


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_role_create_nested(topology_st, create_test_managed_role):
    """ Test dsidm role create-nested option

    :id: db3b6f70-121c-466a-b28f-b4331e54b5d7
    :setup: Standalone instance
    :steps:
        1. Run dsidm role create-nested
        2. Check that correct message is provided on creation
        3. Check that created role exists
    :expectedresults:
        1. Success
        1. Success
        1. Success
    """

    standalone = topology_st.standalone
    output = 'Successfully created {}'.format(nested_role_name)

    managed_roles = ManagedRoles(standalone, DEFAULT_SUFFIX)
    managed_role = managed_roles.get(managed_role_name)

    args = FakeArgs()
    args.cn = nested_role_name
    args.nsroledn = managed_role.dn

    log.info('Test dsidm role create-nested')
    create_nested(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value=output)

    log.info('Check that the nested role exists and has proper objectclasses')
    nested_roles = NestedRoles(standalone, DEFAULT_SUFFIX)
    new_nested_role = nested_roles.get(nested_role_name)
    assert new_nested_role.exists()
    assert new_nested_role.present('objectClass', 'nsComplexRoleDefinition')
    assert new_nested_role.present('objectClass', 'nsNestedRoleDefinition')

    log.info('Clean up for next test')
    new_nested_role.delete()


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_role_delete(topology_st, create_test_managed_role):
    """ Test dsidm role delete

    :id: ade850ae-9304-4eae-ba47-38bfaf3d8747
    :setup: Standalone instance
    :steps:
        1. Run dsidm role delete on a created role
        2. Check that a message is provided on deletion
        3. Check that role does not exist
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """

    standalone = topology_st.standalone
    managed_roles = ManagedRoles(standalone, DEFAULT_SUFFIX)
    test_managed_role = managed_roles.get(managed_role_name)
    output = 'Successfully deleted {}'.format(test_managed_role.dn)

    args = FakeArgs()
    args.dn = test_managed_role.dn

    log.info('Test dsidm role delete')
    delete(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args, warn=False)
    check_value_in_log_and_reset(topology_st, check_value=output)

    log.info('Check that role does not exist')
    args.json = False
    list(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    assert not test_managed_role.exists()


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_role_list(topology_st, create_test_managed_role):
    """ Test dsidm role list option

    :id: a1dd201a-3a04-42aa-913c-6c22f0244ff0
    :setup: Standalone instance
    :steps:
        1. Run dsidm role list option without json
        2. Check the output content is correct
        3. Run dsidm role list option with json
        4. Test "full_dn" option with list
        5. Check the output content is correct
        6. Delete the role
        7. Check the role is not in the list with json
        8. Check the role is not in the list without json

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

    standalone = topology_st.standalone
    args = FakeArgs()
    args.json = False
    args.full_dn = False
    json_list = ['type',
                 'list',
                 'items']

    log.info('Empty the log file to prevent false data to check about role')
    topology_st.logcap.flush()

    log.info('Test dsidm role list without json')
    list(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value=managed_role_name)

    log.info('Test dsidm role list with json')
    args.json = True
    list(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=json_list, check_value=managed_role_name)

    log.info('Test full_dn option with list')
    args.full_dn = True
    list(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    result = topology_st.logcap.get_raw_outputs()
    json_result = json.loads(result[0])
    assert is_a_dn(json_result['items'][0])
    args.full_dn = False

    log.info('Delete the role')
    roles = ManagedRoles(standalone, DEFAULT_SUFFIX)
    test_role = roles.get(managed_role_name)
    test_role.delete()

    log.info('Test empty dsidm role list with json')
    topology_st.logcap.flush()
    list(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    result = topology_st.logcap.get_raw_outputs()
    json_result = json.loads(result[0])
    assert len(json_result['items']) == 0
    check_value_in_log_and_reset(topology_st, content_list=json_list, check_value_not=managed_role_name)

    log.info('Test empty dsidm role list without json')
    list(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value_not=managed_role_name)


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
@pytest.mark.parametrize(
    "role_name, fixture, objectclasses",
    [(managed_role_name, 'create_test_managed_role', ['nsSimpleRoleDefinition', 'nsManagedRoleDefinition']),
     (pytest.param(filtered_role_name,
                   create_test_filtered_role,
                   ['nsComplexRoleDefinition', 'nsFilteredRoleDefinition'],
                   marks=pytest.mark.xfail(reason="DS6492"))),
     (pytest.param(nested_role_name,
                   create_test_nested_role,
                   ['nsComplexRoleDefinition', 'nsNestedRoleDefinition'],
                   marks=pytest.mark.xfail(reason="DS6493")))])
def test_dsidm_role_get(topology_st, role_name, fixture, objectclasses, request):
    """ Test dsidm role get option for managed, filtered and nested role

    :id: 814f269d-c230-4105-a215-3f55b7d8ae47
    :setup: Standalone instance
    :steps:
        1. Run dsidm get option for a created role without json
        2. Check the output content is correct
        3. Run dsidm get option for a created role with json
        4. Check the output content is correct
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
    """

    request.getfixturevalue(fixture)
    standalone = topology_st.standalone
    roles = Roles(standalone, DEFAULT_SUFFIX)
    test_role = roles.get(role_name)

    role_content = ['dn: {}'.format(test_role.dn),
                    'cn: {}'.format(test_role.rdn),
                    'objectClass: top',
                    'objectClass: ldapSubEntry',
                    'objectClass: nsRoleDefinition',
                    'objectClass: {}'.format(objectclasses[0]),
                    'objectClass: {}'.format(objectclasses[1])]

    json_content = ['attrs',
                    'objectclass',
                    'top',
                    'ldapSubEntry',
                    'nsRoleDefinition',
                    objectclasses[0],
                    objectclasses[1],
                    'cn',
                    test_role.rdn,
                    'creatorsname',
                    'cn=directory manager',
                    'modifiersname',
                    'createtimestamp',
                    'modifytimestamp',
                    'nsuniqueid',
                    'parentid',
                    'entryid',
                    'entryuuid',
                    'dsentrydn',
                    'entrydn',
                    test_role.dn]

    args = FakeArgs()
    args.json = False
    args.selector = role_name

    log.info('Empty the log file to prevent false data to check about role')
    topology_st.logcap.flush()

    log.info('Test dsidm role get without json')
    get(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=role_content)

    log.info('Test dsidm role get with json')
    args.json = True
    get(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=json_content)


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
@pytest.mark.parametrize(
    "role_name, fixture, objectclasses",
    [(managed_role_name, 'create_test_managed_role', ['nsSimpleRoleDefinition', 'nsManagedRoleDefinition']),
     (pytest.param(filtered_role_name,
                   create_test_filtered_role,
                   ['nsComplexRoleDefinition', 'nsFilteredRoleDefinition'],
                   marks=pytest.mark.xfail(reason="DS6492"))),
     (pytest.param(nested_role_name,
                   create_test_nested_role,
                   ['nsComplexRoleDefinition', 'nsNestedRoleDefinition'],
                   marks=pytest.mark.xfail(reason="DS6493")))])
def test_dsidm_role_get_by_dn(topology_st, role_name, fixture, objectclasses, request):
    """ Test dsidm role get-by-dn option for managed, filtered and nested role

    :id: 5fe4cb2b-dc26-43fd-b6a2-c507be689d31
    :setup: Standalone instance
    :steps:
        1. Run dsidm get-by-dn option for a created role without json
        2. Check the output content is correct
        3. Run dsidm get-by-dn option for a created role with json
        4. Check the output content is correct
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
    """

    request.getfixturevalue(fixture)
    standalone = topology_st.standalone

    roles = Roles(standalone, DEFAULT_SUFFIX)
    test_role = roles.get(role_name)

    role_content = ['dn: {}'.format(test_role.dn),
                    'cn: {}'.format(test_role.rdn),
                    'objectClass: top',
                    'objectClass: ldapSubEntry',
                    'objectClass: nsRoleDefinition',
                    'objectClass: {}'.format(objectclasses[0]),
                    'objectClass: {}'.format(objectclasses[1])]

    json_content = ['attrs',
                    'objectclass',
                    'top',
                    'ldapSubEntry',
                    'nsRoleDefinition',
                    objectclasses[0],
                    objectclasses[1],
                    'cn',
                    test_role.rdn,
                    'creatorsname',
                    'cn=directory manager',
                    'modifiersname',
                    'createtimestamp',
                    'modifytimestamp',
                    'nsuniqueid',
                    'parentid',
                    'entryid',
                    'entryuuid',
                    'dsentrydn',
                    'entrydn',
                    test_role.dn]

    args = FakeArgs()
    args.dn = test_role.dn
    args.json = False
    args.selector = test_role.dn

    log.info('Empty the log file to prevent false data to check about role')
    topology_st.logcap.flush()

    log.info('Test dsidm role get-by-dn without json')
    get_dn(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=role_content)

    log.info('Test dsidm role get-by-dn with json')
    args.json = True
    get_dn(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=json_content)
    # verify influence of changing cli_idm/__init__.py _generic_get_dn on line 144


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_role_modify_by_dn(topology_st, create_test_managed_role):
    """ Test dsidm role modify-by-dn

    :id: 7670f70b-eb8e-40bd-aeaf-4ae5ea7ddee0
    :setup: Standalone instance
    :steps:
        1. Run dsidm role modify-by-dn add:description:value
        2. Run dsidm role modify-by-dn replace:description:newvalue
        3. Run dsidm role modify-by-dn delete:description:newvalue
    :expectedresults:
        1. Description value is present
        2. Description value is replaced with the new one
        3. Description value is deleted
    """

    standalone = topology_st.standalone
    roles = Roles(standalone, DEFAULT_SUFFIX)
    test_role = roles.get(managed_role_name)
    output = 'Successfully modified {}'.format(test_role.dn)

    args = FakeArgs()
    args.dn = test_role.dn
    args.selector = managed_role_name

    log.info('Test dsidm role modify-by-dn add')
    args.changes = ['add:description:test managed role']
    modify(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args, warn=False)
    check_value_in_log_and_reset(topology_st, check_value=output)
    assert test_role.present('description', 'test managed role')

    log.info('Test dsidm role modify-by-dn replace')
    args.changes = ['replace:description:modified managed role']
    modify(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args, warn=False)
    check_value_in_log_and_reset(topology_st, check_value=output)
    assert test_role.present('description', 'modified managed role')
    assert not test_role.present('description', 'test managed role')

    log.info('Test dsidm role modify-by-dn delete')
    args.changes = ['delete:description:modified managed role']
    modify(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args, warn=False)
    check_value_in_log_and_reset(topology_st, check_value=output)
    assert not test_role.present('description', 'modified managed role')


@pytest.mark.xfail(reason="DS6501")
@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_role_rename_by_dn(topology_st, create_test_managed_role, add_description):
    """ Test dsidm role rename-by-dn

    :id: 3a5295ad-dd9d-4819-aa1c-44f7a785a7d5
    :setup: Standalone instance
    :steps:
        1. Run dsidm role rename-by-dn option on existing role
        2. Check the role does not have another cn attribute with the old rdn
        3. Verify the role with original name does not exist
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """

    standalone = topology_st.standalone
    roles = Roles(standalone, DEFAULT_SUFFIX)
    test_role = roles.get(managed_role_name)

    args = FakeArgs()
    args.dn = test_role.dn
    args.new_name = 'renamed_role'
    args.new_dn = 'cn=renamed_role,{}'.format(DEFAULT_SUFFIX)
    args.keep_old_rdn = False

    log.info('Test dsidm role rename-by-dn')
    rename(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    new_role = roles.get(args.new_name)
    output = 'Successfully renamed to {}'.format(new_role.dn)
    check_value_in_log_and_reset(topology_st, check_value=output)


    log.info('Verify the new role does not have a cn attribute with the old rdn')
    assert not new_role.present('cn', managed_role_name)
    assert new_role.present('description', managed_role_name)

    log.info('Verify the old role does not exist')
    assert not test_role.exists()


@pytest.mark.xfail(reason="DS6501")
@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_role_rename_by_dn_keep_old_rdn(topology_st, create_test_managed_role, add_description):
    """ Test dsidm role rename-by-dn with keep-old-rdn option

    :id: 7c2c55cd-7d3c-4562-8b6e-4f64590fb5f0
    :setup: Standalone instance
    :steps:
        1. Run dsidm role rename-by-dn option on existing role
        2. Check the role has another cn attribute with the old rdn
        3. Verify the role with original name does not exist
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """

    standalone = topology_st.standalone
    roles = Roles(standalone, DEFAULT_SUFFIX)
    test_role = roles.get(managed_role_name)

    args = FakeArgs()
    args.dn = test_role.dn
    args.new_name = 'renamed_role'
    args.new_dn = 'cn=renamed_role,{}'.format(DEFAULT_SUFFIX)
    args.keep_old_rdn = True

    log.info('Test dsidm role rename-by-dn')
    rename(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    new_role = roles.get(args.new_name)
    output = 'Successfully renamed to {}'.format(new_role.dn)
    check_value_in_log_and_reset(topology_st, check_value=output)

    log.info('Verify the new role does not have a cn attribute with the old rdn')
    assert new_role.present('cn', managed_role_name)
    assert new_role.present('description', managed_role_name)

    log.info('Verify the old role does not exist')
    assert not test_role.exists()


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_role_lock_unlock_entrystatus(topology_st, create_test_managed_role):
    """ Test dsidm role lock option

    :id: 26b03489-81eb-4a00-bf2c-115f58ced07d
    :setup: Standalone instance
    :steps:
        1. Run dsidm role entry-status on an existing role with no locking done previously without json
        2. Run dsidm role entry-status on an existing role with no locking done previously with json
        3. Run dsidm role lock on an existing role
        4. Verify new roles for the purposes of maintaining locked roles were created
        5. Verify locked role is member of the nested role for locked roles
        6. Verify the role is locked by running dsidm role entry-status on the role without json
        7. Verify the role is locked by running dsidm role entry-status on the role with json
        8. Run dsidm role unlock on the locked role
        9. Verify the auxiliary roles still exist
        10. Verify the unlocked role is not associated with auxiliary roles
        11. Verify the role is unlocked by running dsidm role entry-status on the role without json
        12. Verify the role is unlocked by running dsidm role entry-status on the role with json
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
        12. Success
    """

    standalone = topology_st.standalone
    roles = Roles(standalone, DEFAULT_SUFFIX)
    test_role = roles.get(managed_role_name)

    args = FakeArgs()
    args.dn = test_role.dn
    args.json = False

    lock_output = 'Entry {} is locked'.format(test_role.dn)
    unlock_output = 'Entry {} is unlocked'.format(test_role.dn)
    entry_dn_output = 'Entry DN: {}'.format(test_role.dn)
    entry_locked_output = 'Entry State: directly locked through nsDisabledRole'
    entry_unlocked_output = 'Entry State: activated'
    probably_activated = "probably activated or nsDisabledRole setup and its CoS entries are not\n" \
                         "in a valid state or there is no access to the settings."
    entry_never_locked_output = 'Entry State: {}'.format(probably_activated)

    entry_locked_content = ['dn',
                            test_role.dn,
                            'state',
                            'directly locked through nsDisabledRole']

    entry_unlocked_content = ['dn',
                              test_role.dn,
                              'state',
                              'activated']

    entry_never_locked_content = ['dn',
                                  test_role.dn,
                                  'state',
                                  repr(probably_activated).strip("'")]

    log.info('Test dsidm role entry-status when no role has been disabled before')
    entry_status(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log(topology_st, check_value=entry_dn_output)
    check_value_in_log_and_reset(topology_st, check_value=entry_never_locked_output)

    log.info('Test dsidm role entry-status when no role has been disabled before')
    args.json = True
    entry_status(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=entry_never_locked_content)

    log.info('Test dsidm role lock on a managed role')
    args.json = False
    lock(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value=lock_output)

    log.info('Test new auxiliary roles for locked roles are created')
    disabled_role = roles.get('nsDisabledRole')
    managed_disabled_role = roles.get('nsManagedDisabledRole')
    assert managed_disabled_role.exists()
    assert disabled_role.exists()

    log.info('Verify the locked role is a nsRoleDN attribute of auxiliary nsDisabledRole')
    assert disabled_role.present('nsRoleDN', test_role.dn)

    log.info('Test dsidm role entry-status to verify activation status of the locked role')
    entry_status(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log(topology_st, check_value=entry_dn_output)
    check_value_in_log_and_reset(topology_st, check_value=entry_locked_output)

    log.info('Test dsidm role entry-status to verify activation status of the locked role')
    args.json = True
    entry_status(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=entry_locked_content)

    log.info('Test dsidm role unlock on a locked managed role')
    args.json = False
    unlock(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value=unlock_output)

    log.info('Verify auxiliary roles still exist')
    assert managed_disabled_role.exists()
    assert disabled_role.exists()

    log.info('Verify the unlocked role is not associated with auxiliary roles')
    assert not disabled_role.present('nsRoleDN', test_role.dn)

    log.info('Test dsidm role entry-status to verify activation status of the unlocked role')
    entry_status(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log(topology_st, check_value=entry_dn_output)
    check_value_in_log_and_reset(topology_st, check_value=entry_unlocked_output)

    log.info('Test dsidm role entry-status to verify activation status of the unlocked role - json')
    args.json = True
    entry_status(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=entry_unlocked_content)

    log.info('Clean up')
    test_role.delete()
    disabled_role.delete()
    managed_disabled_role.delete()


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_role_subtree_status(topology_st, create_test_managed_role):
    """ Test dsidm role subtree-status option

    :id: 0057a93f-edfc-43db-a151-1c3a553c08f2
    :setup: Standalone instance
    :steps:
        1. Run subtree-status on existing role
        2. Check the output is correct
    :expectedresults:
        1. Success
        2. Success
    """

    standalone = topology_st.standalone
    roles = Roles(standalone, DEFAULT_SUFFIX)
    test_role = roles.get(managed_role_name)

    args = FakeArgs()
    args.dn = test_role.dn
    args.json = False
    args.basedn = DEFAULT_SUFFIX
    output = 'Entry DN: {}'.format(test_role.dn)

    log.info('Test dsidm role subtree-status')
    topology_st.logcap.flush()
    subtree_status(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    result = topology_st.logcap.get_raw_outputs()
    check_value_in_log_and_reset(topology_st, check_value=output)

    log.info('Test dsidm role subtree-status - json')
    args.json = True
    subtree_status(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    result = topology_st.logcap.get_raw_outputs()
    json_result = json.loads(result[0])
    found = False
    for entry in json_result['entries']:
        if entry['dn'] == test_role.dn:
            found = True
            break
    assert found

    log.info('Clean up')
    test_role.delete()


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s {}".format(CURRENT_FILE))
