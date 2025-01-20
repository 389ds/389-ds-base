# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import logging
import os
import json
import pytest
import ldap
from lib389 import DEFAULT_SUFFIX
from lib389.cli_idm.account import list, get_dn, lock, unlock, delete, modify, rename, entry_status, \
    subtree_status, reset_password, change_password
from lib389.cli_idm.user import create
from lib389.topologies import topology_st
from lib389.cli_base import FakeArgs
from lib389.utils import ds_is_older, is_a_dn
from lib389.idm.user import nsUserAccounts
from . import check_value_in_log_and_reset

pytestmark = pytest.mark.tier0

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

test_user_name = 'test_user_1000'

@pytest.fixture(scope="function")
def create_test_user(topology_st, request):
    log.info('Create test user')
    users = nsUserAccounts(topology_st.standalone, DEFAULT_SUFFIX)

    if users.exists(test_user_name):
        test_user = users.get(test_user_name)
        test_user.delete()

    properties = FakeArgs()
    properties.uid = test_user_name
    properties.cn = test_user_name
    properties.sn = test_user_name
    properties.uidNumber = '1000'
    properties.gidNumber = '2000'
    properties.homeDirectory = '/home/test_user_1000'
    properties.displayName = test_user_name

    create(topology_st.standalone, DEFAULT_SUFFIX, topology_st.logcap.log, properties)
    test_user = users.get(test_user_name)

    def fin():
        log.info('Delete test user')
        if test_user.exists():
            test_user.delete()

    request.addfinalizer(fin)


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_account_entry_status_with_lock(topology_st, create_test_user):
    """ Test dsidm account entry-status option with account lock/unlock

    :id: d911bbf2-3a65-42a4-ad76-df1114caa396
    :setup: Standalone instance
    :steps:
         1. Create user account
         2. Run dsidm account entry status
         3. Run dsidm account lock
         4. Run dsidm account subtree status
         5. Run dsidm account entry status
         6. Run dsidm account unlock
         7. Run dsidm account subtree status
         8. Run dsidm account entry status
    :expectedresults:
         1. Success
         2. The state message should be Entry State: activated
         3. Success
         4. The state message should be Entry State: directly locked through nsAccountLock
         5. Success
         6. The state message should be Entry State: activated
         7. Success
         8. The state message should be Entry State: activated
    """

    standalone = topology_st.standalone
    users = nsUserAccounts(standalone, DEFAULT_SUFFIX)
    test_user = users.get(test_user_name)

    entry_list = ['Entry DN: {}'.format(test_user.dn),
                  'Entry Creation Date',
                  'Entry Modification Date']

    state_lock = 'Entry State: directly locked through nsAccountLock'
    state_unlock = 'Entry State: activated'

    lock_msg = 'Entry {} is locked'.format(test_user.dn)
    unlock_msg = 'Entry {} is unlocked'.format(test_user.dn)

    args = FakeArgs()
    args.dn = test_user.dn
    args.json = False
    args.basedn = DEFAULT_SUFFIX
    args.scope = ldap.SCOPE_SUBTREE
    args.filter = "(uid=*)"
    args.become_inactive_on = False
    args.inactive_only = False
    args.json = False

    log.info('Test dsidm account entry-status')
    entry_status(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=entry_list,
                                 check_value=state_unlock)

    log.info('Test dsidm account lock')
    lock(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value=lock_msg)

    log.info('Test dsidm account subtree-status with locked account')
    subtree_status(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=entry_list,
                                 check_value=state_lock)

    log.info('Test dsidm account entry-status with locked account')
    entry_status(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=entry_list,
                                 check_value=state_lock)

    log.info('Test dsidm account unlock')
    unlock(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value=unlock_msg)

    log.info('Test dsidm account subtree-status with unlocked account')
    subtree_status(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=entry_list,
                                 check_value=state_unlock)

    log.info('Test dsidm account entry-status with unlocked account')
    entry_status(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=entry_list,
                                 check_value=state_unlock)


def test_dsidm_account_entry_get_by_dn(topology_st, create_test_user):
    """ Test dsidm account get_dn works with non-json and json

    :id: dd848f67c-9944-48a4-ae5e-98dce4fbc364
    :setup: Standalone instance
    :steps:
        1. Get user by DN (non-json)
        2. Get user by DN (json)
    :expectedresults:
        1. Success
        2. Success
    """

    inst = topology_st.standalone
    user_dn = "uid=test_user_1000,ou=people,dc=example,dc=com"

    args = FakeArgs()
    args.dn = user_dn
    args.json = False
    args.basedn = DEFAULT_SUFFIX
    args.scope = ldap.SCOPE_SUBTREE
    args.filter = "(uid=*)"
    args.become_inactive_on = False
    args.inactive_only = False

    # Test non-json result
    check_val = "homeDirectory: /home/test_user_1000"
    get_dn(inst, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value=check_val)

    # Test json
    args.json = True
    get_dn(inst, DEFAULT_SUFFIX, topology_st.logcap.log, args)

    result = topology_st.logcap.get_raw_outputs()
    json_result = json.loads(result[0])
    assert json_result['dn'] == user_dn


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_account_delete(topology_st, create_test_user):
    """ Test dsidm account delete option

    :id: a7960bc2-0282-4a82-8dfb-3af2088ec661
    :setup: Standalone
    :steps:
        1. Run dsidm account delete on a created account
        2. Check that a message is provided on deletion
        3. Check that the account no longer exists
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """

    standalone = topology_st.standalone
    accounts = nsUserAccounts(standalone, DEFAULT_SUFFIX)
    test_account = accounts.get(test_user_name)
    output = 'Successfully deleted {}'.format(test_account.dn)

    args = FakeArgs()
    args.dn = test_account.dn

    log.info('Test dsidm account delete')
    delete(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args, warn=False)
    check_value_in_log_and_reset(topology_st, check_value=output)

    log.info('Check that the account no longer exists')
    assert not test_account.exists()


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_account_list(topology_st, create_test_user):
    """ Test dsidm account list option

    :id: 4d173a3e-ee36-4a8b-8d0d-4955c792faca
    :setup: Standalone instance
    :steps:
        1. Run dsidm account list without json
        2. Check the output content is correct
        3. Run dsidm account list with json
        4. Check the output content is correct
        5. Test full_dn option with list
        6. Delete the account
        7. Check the account is not in the list with json
        8. Check the account is not in the list without json
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

    log.info('Empty the log file to prevent false data to check about group')
    topology_st.logcap.flush()

    log.info('Test dsidm account list without json')
    list(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value=test_user_name)

    log.info('Test dsidm account list with json')
    args.json = True
    list(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=json_list, check_value=test_user_name)

    log.info('Test full_dn option with list')
    args.full_dn = True
    list(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    result = topology_st.logcap.get_raw_outputs()
    json_result = json.loads(result[0])
    assert is_a_dn(json_result['items'][0])
    args.full_dn = False
    topology_st.logcap.flush()

    log.info('Delete the account')
    accounts = nsUserAccounts(standalone, DEFAULT_SUFFIX)
    test_account = accounts.get(test_user_name)
    test_account.delete()

    log.info('Test empty dsidm account list with json')
    list(standalone,DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=json_list, check_value_not=test_user_name)

    log.info('Test empty dsidm account list without json')
    args.json = False
    list(standalone,DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value_not=test_user_name)


@pytest.mark.xfail(reason='DS6515')
@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_account_get_by_dn(topology_st, create_test_user):
    """ Test dsidm account get-by-dn option

    :id: 07945577-2da0-4fd9-9237-43dd2823f7b8
    :setup: Standalone instance
    :steps:
        1. Run dsidm account get-by-dn for an account without json
        2. Check the output content is correct
        3. Run dsidm account get-by-dn for an account with json
        4. Check the output content is correct
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
    """

    standalone = topology_st.standalone
    accounts = nsUserAccounts(standalone, DEFAULT_SUFFIX)
    test_account = accounts.get(test_user_name)

    args = FakeArgs()
    args.dn = test_account.dn
    args.json = False

    account_content = ['dn: {}'.format(test_account.dn),
                       'cn: {}'.format(test_account.rdn),
                       'displayName: {}'.format(test_user_name),
                       'gidNumber: 2000',
                       'homeDirectory: /home/{}'.format(test_user_name),
                       'objectClass: top',
                       'objectClass: nsPerson',
                       'objectClass: nsAccount',
                       'objectClass: nsOrgPerson',
                       'objectClass: posixAccount',
                       'uid: {}'.format(test_user_name),
                       'uidNumber: 1000']

    json_content = ['attrs',
                    'objectclass',
                    'top',
                    'nsPerson',
                    'nsAccount',
                    'nsOrgPerson',
                    'posixAccount',
                    'cn',
                    test_account.rdn,
                    'gidnumber',
                    '2000',
                    'homedirectory',
                    '/home/{}'.format(test_user_name),
                    'displayname',
                    test_user_name,
                    'uidnumber',
                    '1000',
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
                    test_account.dn]

    log.info('Empty the log file to prevent false data to check about the account')
    topology_st.logcap.flush()

    log.info('Test dsidm account get-by-dn without json')
    get_dn(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=account_content)

    log.info('Test dsidm account get-by-dn with json')
    args.json = True
    get_dn(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=json_content)


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_account_modify_by_dn(topology_st, create_test_user):
    """ Test dsidm account modify-by-dn

    :id: e7288f8c-f0a8-4d8d-a00f-1b243eb117bc
    :setup: Standalone instance
    :steps:
        1. Run dsidm account modify-by-dn add description value
        2. Run dsidm account modify-by-dn replace description value
        3. Run dsidm account modify-by-dn delete description value
    :expectedresults:
        1. A description value is added
        2. The original description value is replaced and the previous is not present
        3. The replaced description value is deleted
    """

    standalone = topology_st.standalone
    accounts = nsUserAccounts(standalone, DEFAULT_SUFFIX)
    test_account = accounts.get(test_user_name)
    output = 'Successfully modified {}'.format(test_account.dn)

    args = FakeArgs()
    args.dn = test_account.dn
    args.changes = ['add:description:new_description']

    log.info('Test dsidm account modify add')
    modify(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args, warn=False)
    check_value_in_log_and_reset(topology_st, check_value=output)
    assert test_account.present('description', 'new_description')

    log.info('Test dsidm account modify replace')
    args.changes = ['replace:description:replaced_description']
    modify(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args, warn=False)
    check_value_in_log_and_reset(topology_st, check_value=output)
    assert test_account.present('description', 'replaced_description')
    assert not test_account.present('description', 'new_description')

    log.info('Test dsidm account modify delete')
    args.changes = ['delete:description:replaced_description']
    modify(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args, warn=False)
    check_value_in_log_and_reset(topology_st, check_value=output)
    assert not test_account.present('description', 'replaced_description')


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_account_rename_by_dn(topology_st, create_test_user):
    """ Test dsidm account rename-by-dn option

    :id: f4b8e491-35b1-4113-b9c4-e0a80f8985f3
    :setup: Standalone instance
    :steps:
        1. Run dsidm account rename option on existing account
        2. Check the account does not have another uid attribute with the old rdn
        3. Check the old account is deleted
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """

    standalone = topology_st.standalone
    accounts = nsUserAccounts(standalone, DEFAULT_SUFFIX)
    test_account = accounts.get(test_user_name)

    args = FakeArgs()
    args.dn = test_account.dn
    args.new_name = 'renamed_account'
    args.new_dn = 'uid=renamed_account,ou=people,{}'.format(DEFAULT_SUFFIX)
    args.keep_old_rdn = False

    log.info('Test dsidm account rename-by-dn')
    rename(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    new_account = accounts.get(args.new_name)

    try:
        output = 'Successfully renamed to {}'.format(new_account.dn)
        check_value_in_log_and_reset(topology_st, check_value=output)

        log.info('Verify the new account does not have a uid attribute with the old rdn')
        assert not new_account.present('uid', test_user_name)
        assert new_account.present('displayName', test_user_name)

        log.info('Verify the old account does not exist')
        assert not test_account.exists()
    finally:
        log.info('Clean up')
        new_account.delete()


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_account_rename_by_dn_keep_old_rdn(topology_st, create_test_user):
    """ Test dsidm account rename-by-dn option with keep-old-rdn

    :id: a128bdbb-c0a4-4d9d-9a95-9be2d3780094
    :setup: Standalone instance
    :steps:
        1. Run dsidm account rename option on existing account
        2. Check the account has another uid attribute with the old rdn
        3. Check the old account is deleted
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """

    standalone = topology_st.standalone
    accounts = nsUserAccounts(standalone, DEFAULT_SUFFIX)
    test_account = accounts.get(test_user_name)

    args = FakeArgs()
    args.dn = test_account.dn
    args.new_name = 'renamed_account'
    args.new_dn = 'uid=renamed_account,ou=people,{}'.format(DEFAULT_SUFFIX)
    args.keep_old_rdn = True

    log.info('Test dsidm account rename-by-dn')
    rename(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    new_account = accounts.get(args.new_name)

    try:
        output = 'Successfully renamed to {}'.format(new_account.dn)
        check_value_in_log_and_reset(topology_st, check_value=output)

        log.info('Verify the new account does not have a uid attribute with the old rdn')
        assert new_account.present('uid', test_user_name)
        assert new_account.present('displayName', test_user_name)

        log.info('Verify the old account does not exist')
        assert not test_account.exists()
    finally:
        log.info('Clean up')
        new_account.delete()


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_account_reset_password(topology_st, create_test_user):
    """ Test dsidm account reset_password option

    :id: 02ffa044-08ae-40c5-9108-b02d0c3b0521
    :setup: Standalone instance
    :steps:
        1. Run dsidm account reset_password on an existing user
        2. Verify that the user has now userPassword attribute set
    :expectedresults:
        1. Success
        2. Success
    """

    standalone = topology_st.standalone
    accounts = nsUserAccounts(standalone, DEFAULT_SUFFIX)
    test_account = accounts.get(test_user_name)

    args = FakeArgs()
    args.dn = test_account.dn
    args.new_password = 'newpasswd'
    output = 'reset password for {}'.format(test_account.dn)

    log.info('Test dsidm account reset_password')
    reset_password(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value=output)

    log.info('Verify the userPassword attribute is set')
    assert test_account.present('userPassword')


@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_account_change_password(topology_st, create_test_user):
    """ Test dsidm account change_password option

    :id: 24c25b8f-df2b-4d43-a88e-47e24bc4ff36
    :setup: Standalone instance
    :steps:
        1. Run dsidm account change_password on an existing user
        2. Verify that the user has userPassword attribute set
    :expectedresults:
        1. Success
        2. Success
    """

    standalone = topology_st.standalone
    accounts = nsUserAccounts(standalone, DEFAULT_SUFFIX)
    test_account = accounts.get(test_user_name)

    args = FakeArgs()
    args.dn = test_account.dn
    args.new_password = 'newpasswd'
    output = 'changed password for {}'.format(test_account.dn)

    log.info('Test dsidm account change_password')
    change_password(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value=output)

    log.info('Verify the userPassword attribute is set')
    assert test_account.present('userPassword')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s {}".format(CURRENT_FILE))