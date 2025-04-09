# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2021 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import time
import subprocess
import pytest
import logging
import os
import ldap

from lib389 import DEFAULT_SUFFIX
from lib389.cli_idm.user import list, get, get_dn, create, delete, modify, rename
from lib389.topologies import topology_st
from lib389.cli_base import FakeArgs
from lib389.utils import ds_is_older, ensure_str
from lib389.idm.user import nsUserAccounts
from . import check_value_in_log_and_reset

pytestmark = pytest.mark.tier0

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


@pytest.fixture(scope="function")
def create_test_user(topology_st, request):
    user_name = 'test_user_1000'
    users = nsUserAccounts(topology_st.standalone, DEFAULT_SUFFIX)

    log.info('Create test user')
    if users.exists(user_name):
        test_user = users.get(user_name)
        test_user.delete()
    else:
        test_user = users.create_test_user()

    def fin():
        log.info('Delete test user')
        if test_user.exists():
            test_user.delete()

    request.addfinalizer(fin)


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_user_list(topology_st, create_test_user):
    """ Test dsidm user list option

    :id: a7400ac2-b629-4507-bc05-c6402a5b437b
    :setup: Standalone instance
    :steps:
         1. Run dsidm user list option without json
         2. Check the output content is correct
         3. Run dsidm user list option with json
         4. Check the json content is correct
         5. Delete the user
         6. Check the user is not in the list with json
         7. Check the user is not in the list without json
    :expectedresults:
         1. Success
         2. Success
         3. Success
         4. Success
         5. Success
         6. Success
         7. Success
    """

    standalone = topology_st.standalone
    args = FakeArgs()
    args.json = False
    user_value = 'test_user_1000'
    json_list = ['type',
                 'list',
                 'items']


    log.info('Empty the log file to prevent false data to check about user')
    topology_st.logcap.flush()

    log.info('Test dsidm user list without json')
    list(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value=user_value)

    log.info('Test dsidm user list with json')
    args.json = True
    list(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=json_list, check_value=user_value)

    log.info('Delete the user')
    users = nsUserAccounts(topology_st.standalone, DEFAULT_SUFFIX)
    testuser = users.get(user_value)
    testuser.delete()

    log.info('Test empty dsidm user list with json')
    list(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=json_list, check_value_not=user_value)

    log.info('Test empty dsidm user list without json')
    args.json = False
    list(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value_not=user_value)


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_user_get_rdn(topology_st, create_test_user):
    """ Test dsidm user get option

    :id: 8c7247cd-7588-45d3-817c-ac5a9f135b32
    :setup: Standalone instance
    :steps:
         1. Run dsidm get option for created user with json
         2. Check the output content is correct
         3. Run dsidm get option for created user without json
         4. Check the json content is correct
    :expectedresults:
         1. Success
         2. Success
         3. Success
         4. Success
    """

    standalone = topology_st.standalone
    users = nsUserAccounts(topology_st.standalone, DEFAULT_SUFFIX)
    testuser = users.get('test_user_1000')

    user_content = ['dn: {}'.format(testuser.dn),
                    'cn: {}'.format(testuser.rdn),
                    'displayName: {}'.format(testuser.rdn),
                    'gidNumber: 2000',
                    'homeDirectory: /home/{}'.format(testuser.rdn),
                    'objectClass: top',
                    'objectClass: nsPerson',
                    'objectClass: nsAccount',
                    'objectClass: nsOrgPerson',
                    'objectClass: posixAccount',
                    'uid: {}'.format(testuser.rdn),
                    'uidNumber: 1000']

    json_content = ['attrs',
                    'objectclass',
                    'top',
                    'nsPerson',
                    'nsAccount',
                    'nsOrgPerson',
                    'posixAccount',
                    'uid',
                    testuser.rdn,
                    'cn',
                    'displayname',
                    'uidnumber',
                    'gidnumber',
                    '2000',
                    'homedirectory',
                    '/home/{}'.format(testuser.rdn),
                    'creatorsname',
                    'cn=directory manager',
                    'modifiersname',
                    'createtimestamp',
                    'modifytimestamp',
                    'nsuniqueid',
                    'parentid',
                    'entryid',
                    'entrydn',
                    testuser.dn]

    args = FakeArgs()
    args.json = False
    args.selector = 'test_user_1000'

    log.info('Empty the log file to prevent false data to check about user')
    topology_st.logcap.flush()

    log.info('Test dsidm user get without json')
    get(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=user_content)

    log.info('Test dsidm user get with json')
    args.json = True
    get(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=json_content)


@pytest.mark.xfail(reason="Will fail because of bz1893667")
@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_user_get_dn(topology_st, create_test_user):
    """ Test dsidm user get_dn option

    :id: 787bf278-87c3-402e-936e-6161799d098d
    :setup: Standalone instance
    :steps:
         1. Run dsidm user get_dn for created user
         2. Check the output content is correct
    :expectedresults:
         1. Success
         2. Success
    """

    standalone = topology_st.standalone
    users = nsUserAccounts(standalone, DEFAULT_SUFFIX)
    test_user = users.get('test_user_1000')
    args = FakeArgs()
    args.dn = test_user.dn

    log.info('Empty the log file to prevent false data to check about user')
    topology_st.logcap.flush()

    log.info('Test dsidm user get_dn without json')
    get_dn(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=user_name)

    log.info('Test dsidm user get_dn with json')
    args.json = True
    get_dn(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=user_name)


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_user_create(topology_st):
    """ Test dsidm user create option

    :id: 862f5875-11fd-4e8e-92c1-397010386eb8
    :setup: Standalone instance
    :steps:
         1. Run dsidm user create
         2. Check that a message is provided on creation
         3. Check that created user exists
    :expectedresults:
         1. Success
         2. Success
         3. Success
    """

    standalone = topology_st.standalone
    user_name = 'new_user'
    output = 'Successfully created {}'.format(user_name)

    args = FakeArgs()
    args.uid = user_name
    args.cn = user_name
    args.displayName = user_name
    args.uidNumber = '1030'
    args.gidNumber = '2030'
    args.homeDirectory = '/home/{}'.format(user_name)

    log.info('Test dsidm user create')
    create(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value=output)

    log.info('Check that user is present')
    users = nsUserAccounts(standalone, DEFAULT_SUFFIX)
    new_user = users.get(user_name)
    assert new_user.exists()

    log.info('Clean up for next test')
    new_user.delete()


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_user_delete(topology_st, create_test_user):
    """ Test dsidm user delete option

    :id: 3704dc3a-9787-4f74-aaa8-45f38e4a6a52
    :setup: Standalone instance
    :steps:
         1. Run dsidm user delete on created user
         2. Check that a message is provided on deletion
         3. Check that user does not exist
    :expectedresults:
         1. Success
         2. Success
         3. Success
    """

    standalone = topology_st.standalone
    users = nsUserAccounts(standalone, DEFAULT_SUFFIX)
    test_user = users.get('test_user_1000')
    output = 'Successfully deleted {}'.format(test_user.dn)

    args = FakeArgs()
    args.dn = test_user.dn

    log.info('Test dsidm user delete')
    delete(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args, warn=False)
    check_value_in_log_and_reset(topology_st, check_value=output)

    log.info('Check that user does not exist')
    assert not test_user.exists()


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_user_modify(topology_st, create_test_user):
    """ Test dsidm user modify add, replace, delete option

    :id: 7a27be19-1a63-44d0-b11b-f877e06e1a9b
    :setup: Standalone instance
    :steps:
         1. Run dsidm user modify replace cn value
         2. Run dsidm user modify add telephoneNumber attribute to user
         3. Run dsidm user modify delete for telephoneNumber attribute
    :expectedresults:
         1. cn value is replaced with new name
         2. telephoneNumber attribute is present
         3. telephoneNumber attribute is deleted
    """

    standalone = topology_st.standalone
    users = nsUserAccounts(standalone, DEFAULT_SUFFIX)
    test_user = users.get('test_user_1000')
    output = 'Successfully modified {}'.format(test_user.dn)

    args = FakeArgs()
    args.selector = 'test_user_1000'
    args.changes = ['replace:cn:test']

    log.info('Test dsidm user modify replace')
    modify(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args, warn=False)
    check_value_in_log_and_reset(topology_st, check_value=output)

    log.info('Test dsidm user modify add')
    args.changes = ['add:telephoneNumber:1234567890']
    modify(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args, warn=False)
    check_value_in_log_and_reset(topology_st, check_value=output)
    assert test_user.present('telephoneNumber', '1234567890')

    log.info('Test dsidm user modify delete')
    args.changes = ['delete:telephoneNumber:1234567890']
    modify(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args, warn=False)
    check_value_in_log_and_reset(topology_st, check_value=output)
    assert not test_user.present('telephoneNumber', '1234567890')


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_user_rename_keep_old_rdn(topology_st, create_test_user):
    """ Test dsidm user rename option with keep-old-rdn

    :id: 3fd0827c-ab5e-4586-9493-55bc5076a887
    :setup: Standalone instance
    :steps:
         1. Run dsidm user rename option with keep-old-rdn
         2. Check the user does have another uid attribute with the old rdn
         3. Check the old user is deleted
    :expectedresults:
         1. Success
         2. Success
         3. Success
    """

    standalone = topology_st.standalone
    users = nsUserAccounts(standalone, DEFAULT_SUFFIX)
    test_user = users.get('test_user_1000')

    args = FakeArgs()
    args.selector = test_user.rdn
    args.new_name = 'my_user'
    args.keep_old_rdn = True

    log.info('Test dsidm user rename')
    rename(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    my_user = users.get(args.new_name)
    output = 'Successfully renamed to {}'.format(my_user.dn)
    check_value_in_log_and_reset(topology_st, check_value=output)

    log.info('my_user should have uid attribute with the old rdn')
    assert my_user.present('uid', 'test_user_1000')
    assert my_user.get_attr_val_utf8('cn') == 'test_user_1000'
    assert my_user.get_attr_val_utf8('displayName') == 'test_user_1000'

    log.info('Old user dn should not exist')
    assert not test_user.exists()

    log.info('Clean up')
    my_user.delete()


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_user_rename(topology_st, create_test_user):
    """ Test dsidm user rename option

    :id: fa569966-3954-465f-92b0-331a3a088b1b
    :setup: Standalone instance
    :steps:
         1. Run dsidm user rename option on created user
         2. Check the user does not have another uid attribute with the old rdn
         3. Check the old user is deleted
    :expectedresults:
         1. Success
         2. Success
         3. Success
    """

    standalone = topology_st.standalone
    users = nsUserAccounts(standalone, DEFAULT_SUFFIX)
    test_user = users.get('test_user_1000')

    args = FakeArgs()
    args.selector = test_user.rdn
    args.new_name = 'my_user'
    args.keep_old_rdn = False

    log.info('Test dsidm user rename')
    args.new_name = 'my_user'
    rename(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    my_user = users.get(args.new_name)
    output = 'Successfully renamed to {}'.format(my_user.dn)
    check_value_in_log_and_reset(topology_st, check_value=output)

    log.info('New user should not have uid attribute with the old rdn')
    assert not my_user.present('uid', 'test_user_1000')
    assert my_user.get_attr_val_utf8('cn') == 'test_user_1000'
    assert my_user.get_attr_val_utf8('displayName') == 'test_user_1000'

    log.info('Old user dn should not exist.')
    assert not test_user.exists()

    log.info('Clean up')
    my_user.delete()


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_user_rename_nonexistent(topology_st):
    """Test dsidm user rename with nonexistent user

    :id: 284f9cf7-53d4-4f5e-9d2e-ae38081cb4e0
    :setup: Standalone instance
    :steps:
         1. Create a user
         2. Rename the user from original name to new name
         3. Try to rename the original name to new name again (should fail)
         4. Verify the renamed user exists
    :expectedresults:
         1. Success
         2. Success
         3. Failure - should report that the original user doesn't exist
         4. Success
    """

    standalone = topology_st.standalone
    users = nsUserAccounts(standalone, DEFAULT_SUFFIX)
    original_name = 'original_user'
    new_name = 'renamed_user'
    renamed_user = None

    try:
        log.info('Create a user')
        args = FakeArgs()
        args.uid = original_name
        args.cn = original_name
        args.displayName = original_name
        args.uidNumber = '1050'
        args.gidNumber = '2050'
        args.homeDirectory = f'/home/{original_name}'
        create(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
        check_value_in_log_and_reset(topology_st, check_value=f'Successfully created {original_name}')

        log.info('Rename the user')
        args = FakeArgs()
        args.selector = original_name
        args.new_name = new_name
        args.keep_old_rdn = False
        rename(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
        check_value_in_log_and_reset(topology_st, check_value=f'Successfully renamed to')

        log.info('Try to rename the nonexistent user')
        try:
            rename(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
            assert False, "The rename operation should have failed for a nonexistent user"
        except ValueError as e:
            assert "The entry does not exist" in str(e)

        log.info('Verify the renamed user exists')
        renamed_user = users.get(new_name)
        assert renamed_user.exists()
    finally:
        log.info('Clean up')
        for username in [original_name, new_name]:
            try:
                user = users.get(username)
                if user.exists():
                    user.delete()
            except:
                pass


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_user_rename_same_cn_displayname(topology_st):
    """Test dsidm user rename when another user has same cn/displayName

    :id: 4b65b68f-aeda-43a0-99b6-f4c65034ba4b
    :setup: Standalone instance
    :steps:
         1. Create first user with uid=user1, cn=testuser, displayName=testuser
         2. Create second user with uid=user2, cn=testuser, displayName=testuser
         3. Rename user1 to new_user1
         4. Try to rename user1 again - should fail as user1 no longer exists
         5. Verify user2 still exists with original attributes
    :expectedresults:
         1. Success
         2. Success
         3. Success - user1 is renamed despite shared cn/displayName
         4. Failure - should report user1 doesn't exist
         5. Success - user2 is unchanged
    """

    standalone = topology_st.standalone
    users = nsUserAccounts(standalone, DEFAULT_SUFFIX)
    user1_uid = 'user1'
    user2_uid = 'user2'
    common_name = 'testuser'
    new_name = 'new_user1'
    renamed_user = None
    user2 = None

    try:
        log.info('Create first user')
        args = FakeArgs()
        args.uid = user1_uid
        args.cn = common_name
        args.displayName = common_name
        args.uidNumber = '1101'
        args.gidNumber = '2101'
        args.homeDirectory = f'/home/{user1_uid}'
        create(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
        check_value_in_log_and_reset(topology_st, check_value=f'Successfully created {user1_uid}')

        log.info('Create second user with same cn/displayName')
        args = FakeArgs()
        args.uid = user2_uid
        args.cn = common_name
        args.displayName = common_name
        args.uidNumber = '1102'
        args.gidNumber = '2102'
        args.homeDirectory = f'/home/{user2_uid}'
        create(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
        check_value_in_log_and_reset(topology_st, check_value=f'Successfully created {user2_uid}')

        log.info('Rename first user')
        args = FakeArgs()
        args.selector = user1_uid
        args.new_name = new_name
        args.keep_old_rdn = False
        rename(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
        check_value_in_log_and_reset(topology_st, check_value=f'Successfully renamed to')

        log.info('Try to rename the first user again')
        try:
            rename(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
            assert False, "The rename operation should have failed since user1 no longer exists"
        except ValueError as e:
            assert "The entry does not exist" in str(e)

        log.info('Verify second user still exists with original attributes')
        user2 = users.get(user2_uid)
        assert user2.exists()
        assert user2.get_attr_val_utf8('cn') == common_name
        assert user2.get_attr_val_utf8('displayName') == common_name
    finally:
        log.info('Clean up')
        for username in [user1_uid, user2_uid, new_name]:
            try:
                user = users.get(username)
                if user.exists():
                    user.delete()
            except:
                pass


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_user_rename_to_existing_name(topology_st):
    """Test dsidm user rename to an existing user name

    :id: 410bec4f-ecfb-4662-917c-1e7bf97876f2
    :setup: Standalone instance
    :steps:
         1. Create first user with uid=user1
         2. Create second user with uid=user2
         3. Try to rename user1 to user2
    :expectedresults:
         1. Success
         2. Success
         3. Failure - should report name already in use
    """

    standalone = topology_st.standalone
    users = nsUserAccounts(standalone, DEFAULT_SUFFIX)
    user1_uid = 'user1_duplicate'
    user2_uid = 'user2_duplicate'
    user1 = None
    user2 = None

    try:
        log.info('Create first user')
        args = FakeArgs()
        args.uid = user1_uid
        args.cn = user1_uid
        args.displayName = user1_uid
        args.uidNumber = '1201'
        args.gidNumber = '2201'
        args.homeDirectory = f'/home/{user1_uid}'
        create(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
        check_value_in_log_and_reset(topology_st, check_value=f'Successfully created {user1_uid}')

        log.info('Create second user')
        args = FakeArgs()
        args.uid = user2_uid
        args.cn = user2_uid
        args.displayName = user2_uid
        args.uidNumber = '1202'
        args.gidNumber = '2202'
        args.homeDirectory = f'/home/{user2_uid}'
        create(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
        check_value_in_log_and_reset(topology_st, check_value=f'Successfully created {user2_uid}')

        log.info('Try to rename first user to second user name')
        args = FakeArgs()
        args.selector = user1_uid
        args.new_name = user2_uid
        args.keep_old_rdn = False

        try:
            rename(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
            assert False, "The rename operation should have failed"
        except ldap.ALREADY_EXISTS as e:
            assert "Already exists" in str(e)

        log.info('Verify both original users still exist')
        user1 = users.get(user1_uid)
        user2 = users.get(user2_uid)
        assert user1.exists()
        assert user2.exists()
    finally:
        log.info('Clean up')
        for username in [user1_uid, user2_uid]:
            try:
                user = users.get(username)
                if user.exists():
                    user.delete()
            except:
                pass


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_user_rename_keep_old_rdn_and_search(topology_st):
    """Test dsidm user rename with keep-old-rdn and search behavior

    :id: 7e9dd9dd-48a5-44c1-a5ec-9882d2bbae97
    :setup: Standalone instance
    :steps:
         1. Create a user
         2. Rename the user with keep-old-rdn=True
         3. Verify the user has both the new and old uid values in its entry
         4. Try to rename by using the old uid as selector
    :expectedresults:
         1. Success
         2. Success - user is renamed with old RDN kept
         3. Success - both uid values are present
         4. Failure - should not be able to find the user by its old uid as the DN
    """

    standalone = topology_st.standalone
    users = nsUserAccounts(standalone, DEFAULT_SUFFIX)
    original_name = 'keep_old_rdn_user'
    new_name = 'new_keep_old_user'
    renamed_user = None

    try:
        log.info('Create a user')
        args = FakeArgs()
        args.uid = original_name
        args.cn = original_name
        args.displayName = original_name
        args.uidNumber = '1501'
        args.gidNumber = '2501'
        args.homeDirectory = f'/home/{original_name}'
        create(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
        check_value_in_log_and_reset(topology_st, check_value=f'Successfully created {original_name}')

        log.info('Rename the user with keep-old-rdn=True')
        args = FakeArgs()
        args.selector = original_name
        args.new_name = new_name
        args.keep_old_rdn = True
        rename(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
        check_value_in_log_and_reset(topology_st, check_value=f'Successfully renamed to')

        log.info('Verify that both old and new uid values exist in the entry')
        renamed_user = users.get(new_name)
        assert renamed_user.exists()

        # The entry should have both the new and old uid values
        uid_values = renamed_user.get_attr_vals_utf8('uid')
        assert new_name in uid_values
        assert original_name in uid_values

        # Other attributes should remain as they were
        assert renamed_user.get_attr_val_utf8('cn') == original_name
        assert renamed_user.get_attr_val_utf8('displayName') == original_name

        log.info('Try to rename using the original name as selector - should fail because entry does not exist at that DN')
        args.selector = original_name
        args.new_name = 'another_new_name'
        args.keep_old_rdn = False

        try:
            rename(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
            assert False, "The rename operation should have failed because the entry with original RDN doesn't exist"
        except ValueError as e:
            assert "The entry does not exist" in str(e)
    finally:
        log.info('Clean up')
        for username in [original_name, new_name, 'another_new_name']:
            try:
                user = users.get(username)
                if user.exists():
                    user.delete()
            except:
                pass


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_user_list_rdn_after_rename(topology_st):
    """Test dsidm user list displays correct RDN after renaming with keep-old-rdn

    :id: 5d8b758d-ddca-427d-9aac-7e4d84d7537e
    :setup: Standalone instance
    :steps:
         1. Create a user
         2. Rename the user with keep-old-rdn=True
         3. Run dsidm user list and check the output contains the new name
         4. Run dsidm user list with json and check it also shows the new name
         5. Directly verify that the RDN extraction works correctly
    :expectedresults:
         1. Success
         2. Success
         3. Success - list should show the new name from the DN's RDN
         4. Success - json output should also show the new name
         5. Success - RDN value should match the new name, not the old one
    """

    standalone = topology_st.standalone
    users = nsUserAccounts(standalone, DEFAULT_SUFFIX)
    original_name = 'original_rdn_user'
    new_name = 'new_rdn_user'
    renamed_user = None

    try:
        log.info('Create a user')
        args = FakeArgs()
        args.uid = original_name
        args.cn = original_name
        args.displayName = original_name
        args.uidNumber = '1601'
        args.gidNumber = '2601'
        args.homeDirectory = f'/home/{original_name}'
        create(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
        check_value_in_log_and_reset(topology_st, check_value=f'Successfully created {original_name}')

        log.info('Rename the user with keep-old-rdn=True')
        args = FakeArgs()
        args.selector = original_name
        args.new_name = new_name
        args.keep_old_rdn = True
        rename(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
        check_value_in_log_and_reset(topology_st, check_value=f'Successfully renamed to')

        log.info('Test dsidm user list without json')
        args = FakeArgs()
        args.json = False
        list(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
        # Should show the new name, not the original name
        check_value_in_log_and_reset(topology_st, check_value=new_name, check_value_not=original_name)

        log.info('Test dsidm user list with json')
        args.json = True
        list(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
        # Should show the new name in JSON output as well
        check_value_in_log_and_reset(topology_st, check_value=new_name, check_value_not=original_name)

        log.info('Directly verify RDN extraction works correctly')
        renamed_user = users.get(new_name)
        rdn_value = renamed_user.get_rdn_from_dn(renamed_user.dn)
        assert rdn_value == new_name, f"Expected RDN value '{new_name}' but got '{rdn_value}'"

        # Check that both uid values exist in the entry
        uid_values = renamed_user.get_attr_vals_utf8('uid')
        assert new_name in uid_values, f"New uid value '{new_name}' not found in {uid_values}"
        assert original_name in uid_values, f"Original uid value '{original_name}' not found in {uid_values}"
    finally:
        log.info('Clean up')
        for username in [original_name, new_name]:
            try:
                user = users.get(username)
                if user.exists():
                    user.delete()
            except:
                pass


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_user_rename_no_changes(topology_st):
    """Test dsidm user rename reports no changes when renaming to an RDN value already present

    :id: bda8eddf-1cad-45d6-a0d1-7dca6d1c315c
    :setup: Standalone instance
    :steps:
         1. Create a user
         2. Rename the user to a new name with keep-old-rdn=True
         3. Try to rename the user again to the same new name with keep-old-rdn=True
         4. Verify the message indicates no changes made
         5. Try to rename the user again to the same new name with keep-old-rdn=False
         6. Verify the message indicates no changes made again
         7. Try to use the original name as selector (should fail)
    :expectedresults:
         1. Success
         2. Success - user is renamed and keeps old RDN
         3. Success - rename operation doesn't error
         4. Success - message indicates no changes rather than success
         5. Success - rename operation doesn't error
         6. Success - message indicates no changes rather than success
         7. Failure - entry with original name as RDN no longer exists
    """

    standalone = topology_st.standalone
    users = nsUserAccounts(standalone, DEFAULT_SUFFIX)
    original_name = 'no_change_user'
    new_name = 'renamed_user'
    test_user = None

    try:
        log.info('Create a user')
        args = FakeArgs()
        args.uid = original_name
        args.cn = original_name
        args.displayName = original_name
        args.uidNumber = '1800'
        args.gidNumber = '2800'
        args.homeDirectory = f'/home/{original_name}'
        create(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
        check_value_in_log_and_reset(topology_st, check_value=f'Successfully created {original_name}')

        log.info('Rename the user with keep-old-rdn=True')
        args = FakeArgs()
        args.selector = original_name
        args.new_name = new_name
        args.keep_old_rdn = True
        rename(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
        check_value_in_log_and_reset(topology_st, check_value='Successfully renamed')

        log.info('Try to rename the user again to the same name with keep-old-rdn=True')
        # We must use the new name as the selector now, since that's the current RDN
        args.selector = new_name
        rename(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
        check_value_in_log_and_reset(topology_st, check_value='No changes made', check_value_not='Successfully renamed')

        log.info('Try to rename the user again with keep-old-rdn=False')
        args.keep_old_rdn = False
        rename(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
        check_value_in_log_and_reset(topology_st, check_value='No changes made', check_value_not='Successfully renamed')

        log.info('Verify the user exists with both original and new RDNs')
        test_user = users.get(new_name)
        assert test_user.exists()
        assert original_name in test_user.get_attr_vals_utf8('uid')
        assert new_name in test_user.get_attr_vals_utf8('uid')

        log.info('Try to use the original name as selector (should fail)')
        args.selector = original_name
        args.new_name = 'another_name'
        try:
            rename(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
            assert False, "The rename operation should have failed because the entry with original RDN doesn't exist"
        except ValueError as e:
            assert "The entry does not exist" in str(e)

    finally:
        log.info('Clean up')
        try:
            if test_user and test_user.exists():
                test_user.delete()
        except:
            pass

if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
