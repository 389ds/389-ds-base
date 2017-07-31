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

# The irony of these names is not lost on me.
GOOD_PASSWORD = 'password'
BAD_PASSWORD = 'aontseunao'

logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

def test_lockout_bypass(topology_st):
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


