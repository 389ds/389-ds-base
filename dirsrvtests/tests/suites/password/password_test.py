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

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


def test_password_delete_specific_password(topology_st):
    """ Delete a specific userpassword, and make sure
    it is actually deleted from the entry
    """

    log.info('Running test_password_delete_specific_password...')

    USER_DN = 'uid=test_entry,' + DEFAULT_SUFFIX

    #
    # Add a test user with a password
    #
    try:
        topology_st.standalone.add_s(Entry((USER_DN, {'objectclass': "top extensibleObject".split(),
                                                      'sn': '1',
                                                      'cn': 'user 1',
                                                      'uid': 'user1',
                                                      'userpassword': PASSWORD})))
    except ldap.LDAPError as e:
        log.fatal('test_password_delete_specific_password: Failed to add test user ' +
                  USER_DN + ': error ' + e.message['desc'])
        assert False

    #
    # Delete the exact password
    #
    try:
        topology_st.standalone.modify_s(USER_DN, [(ldap.MOD_DELETE, 'userpassword', PASSWORD)])
    except ldap.LDAPError as e:
        log.fatal('test_password_delete_specific_password: Failed to delete userpassword: error ' +
                  e.message['desc'])
        assert False

    #
    # Check the password is actually deleted
    #
    try:
        entry = topology_st.standalone.search_s(USER_DN, ldap.SCOPE_BASE, 'objectclass=top')
        if entry[0].hasAttr('userpassword'):
            log.fatal('test_password_delete_specific_password: Entry incorrectly still have the userpassword attribute')
            assert False
    except ldap.LDAPError as e:
        log.fatal('test_password_delete_specific_password: Failed to search for user(%s), error: %s' %
                  (USER_DN, e.message('desc')))
        assert False

    #
    # Cleanup
    #
    try:
        topology_st.standalone.delete_s(USER_DN)
    except ldap.LDAPError as e:
        log.fatal('test_password_delete_specific_password: Failed to delete user(%s), error: %s' %
                  (USER_DN, e.message('desc')))
        assert False

    log.info('test_password_delete_specific_password: PASSED')


def test_password_modify_non_utf8(topology_st):
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
