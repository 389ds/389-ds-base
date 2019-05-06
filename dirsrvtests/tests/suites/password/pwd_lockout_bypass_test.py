# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st
from lib389.idm.user import UserAccounts, TEST_USER_PROPERTIES
import ldap

pytestmark = pytest.mark.tier1

# The irony of these names is not lost on me.
GOOD_PASSWORD = 'password'
BAD_PASSWORD = 'aontseunao'

logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def test_lockout_bypass(topology_st):
    """Check basic password lockout functionality

    :id: 2482a992-1719-495c-b75b-78fe5c48c873
    :setup: Standalone instance
    :steps:
        1. Set passwordMaxFailure to 1
        2. Set passwordLockDuration to 7
        3. Set passwordLockout to 'on'
        4. Create a user
        5. Set a userPassword attribute
        6. Bind as the user with a bad credentials
        7. Bind as the user with a bad credentials
        8. Bind as the user with a good credentials
    :expectedresults:
        1. passwordMaxFailure should be successfully set
        2. passwordLockDuration should be successfully set
        3. passwordLockout should be successfully set
        4. User should be created
        5. userPassword should be successfully set
        6. Should throw an invalid credentials error
        7. Should throw a constraint violation error
        8. Should throw a constraint violation error
    """

    inst = topology_st.standalone

    # Configure the lock policy
    inst.config.set('passwordMaxFailure', '1')
    inst.config.set('passwordLockoutDuration', '99999')
    inst.config.set('passwordLockout', 'on')

    # Create the account
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    testuser = users.create(properties=TEST_USER_PROPERTIES)
    testuser.set('userPassword', GOOD_PASSWORD)

    conn = testuser.bind(GOOD_PASSWORD)
    assert conn != None
    conn.unbind_s()

    # Bind with bad creds twice
    # This is the failure.
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        conn = testuser.bind(BAD_PASSWORD)
    # Now we should not be able to ATTEMPT the bind. It doesn't matter that
    # we disclose that we have hit the rate limit here, what matters is that
    # it exists.
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        conn = testuser.bind(BAD_PASSWORD)

    # now bind with good creds
    # Should be error 19 still.
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        conn = testuser.bind(GOOD_PASSWORD)


