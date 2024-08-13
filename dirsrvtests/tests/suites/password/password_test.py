# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging

import pytest
from lib389.tasks import *
from lib389.topologies import topology_st
from lib389._constants import PASSWORD, DEFAULT_SUFFIX

from lib389.idm.user import UserAccounts, TEST_USER_PROPERTIES

pytestmark = pytest.mark.tier1

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


@pytest.mark.bz918684
@pytest.mark.ds394
def test_password_delete_specific_password(topology_st):
    """Delete a specific userPassword, and make sure
    it is actually deleted from the entry

    :id: 800f432a-52ab-4661-ac66-a2bdd9b984d6
    :setup: Standalone instance
    :steps:
        1. Add a user with userPassword attribute in cleartext
        2. Delete the added value of userPassword attribute
        3. Check if the userPassword attribute is deleted
        4. Delete the user
    :expectedresults:
        1. The user with userPassword in cleartext should be added successfully
        2. Operation should be successful
        3. UserPassword should be deleted
        4. The user should be successfully deleted
     """

    log.info('Running test_password_delete_specific_password...')

    users = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX)

    user = users.create(properties=TEST_USER_PROPERTIES)

    #
    # Add a test user with a password
    #
    user.set('userpassword', PASSWORD)

    #
    # Delete the exact password
    #
    user.remove('userpassword', PASSWORD)

    #
    # Check the password is actually deleted
    #
    assert not user.present('userPassword')

    log.info('test_password_delete_specific_password: PASSED')

@pytest.fixture(scope="function")
def pbkdf2_sha512_scheme(request, topology_st):
    """Set default password storage scheme to PBKDF2-SHA512"""

    inst = topology_st.standalone
    default_scheme = inst.config.get_attr_val_utf8('passwordStorageScheme')
    inst.config.set('passwordStorageScheme', 'PBKDF2-SHA512')

    def fin():
        inst.config.set('passwordStorageScheme', default_scheme)

    request.addfinalizer(fin)


def test_password_modify_non_utf8(topology_st, pbkdf2_sha512_scheme):
    """Attempt a modify of the userPassword attribute with
    an invalid non utf8 value

    :id: a31af9d5-d665-42b9-8d6e-fea3d0837d36
    :setup: Standalone instance
    :steps:
        1. Add a user if it doesnt exist and set its password
        2. Verify password with a bind
        3. Modify userPassword attr with invalid value
        4. Attempt a bind with invalid password value
        5. Verify original password with a bind
    :expectedresults:
        1. The user with userPassword should be added successfully
        2. Operation should be successful
        3. Server returns ldap.UNWILLING_TO_PERFORM
        4. Server returns ldap.INVALID_CREDENTIALS
        5. Operation should be successful
     """

    log.info('Running test_password_modify_non_utf8...')

    # Create user and set password
    standalone = topology_st.standalone
    users = UserAccounts(standalone, DEFAULT_SUFFIX)
    if not users.exists(TEST_USER_PROPERTIES['uid'][0]):
        user = users.create(properties=TEST_USER_PROPERTIES)
    else:
        user = users.get(TEST_USER_PROPERTIES['uid'][0])
    user.set('userpassword', PASSWORD)

    # Verify password
    try:
        user.bind(PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('Failed to bind as {}, error: '.format(user.dn) + e.args[0]['desc'])
        assert False

    # Modify userPassword with an invalid value
    password = b'tes\x82t-password' # A non UTF-8 encoded password
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        user.replace('userpassword', password)

    # Verify a bind fails with invalid pasword
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        user.bind(password)

    # Verify we can still bind with original password
    try:
        user.bind(PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('Failed to bind as {}, error: '.format(user.dn) + e.args[0]['desc'])
        assert False

    log.info('test_password_modify_non_utf8: PASSED')

if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
