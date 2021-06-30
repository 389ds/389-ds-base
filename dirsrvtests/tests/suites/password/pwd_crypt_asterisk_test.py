# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2021 William Brown <william@blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import ldap
import pytest
from lib389.topologies import topology_st
from lib389.idm.user import UserAccounts
from lib389._constants import (DEFAULT_SUFFIX, PASSWORD)

pytestmark = pytest.mark.tier1

def test_password_crypt_asterisk_is_rejected(topology_st):
    """It was reported that {CRYPT}* was allowing all passwords to be
    valid in the bind process. This checks that we should be rejecting
    these as they should represent locked accounts. Similar, {CRYPT}!

    :id: 0b8f1a6a-f3eb-4443-985e-da14d0939dc3
    :setup: Single instance
    :steps: 1. Set a password hash in with CRYPT and the content *
            2. Test a bind
            3. Set a password hash in with CRYPT and the content !
            4. Test a bind
    :expectedresults:
            1. Successfully set the values
            2. The bind fails
            3. Successfully set the values
            4. The bind fails
    """
    topology_st.standalone.config.set('nsslapd-allow-hashed-passwords', 'on')
    topology_st.standalone.config.set('nsslapd-enable-upgrade-hash', 'off')

    users = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX)
    user = users.create_test_user()

    user.set('userPassword', "{CRYPT}*")

    # Attempt to bind with incorrect password.
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        badconn = user.bind('badpassword')

    user.set('userPassword', "{CRYPT}!")
    # Attempt to bind with incorrect password.
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        badconn = user.bind('badpassword')

