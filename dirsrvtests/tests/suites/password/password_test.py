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


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
