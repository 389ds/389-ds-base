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


@pytest.mark.bz1893667
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
    # check_value_in_log_and_reset(topology_st, content_list=user_content)
    # The check_value_in_log_and_reset will have to be updated accordinly after bz1893667 is fixed
    # because now I can't determine the output


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


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
