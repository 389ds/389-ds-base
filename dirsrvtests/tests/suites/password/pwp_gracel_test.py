"""
# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
"""

import os
import pytest
from lib389.topologies import topology_st as topo
from lib389.idm.user import UserAccounts, UserAccount
from lib389._constants import DEFAULT_SUFFIX
from lib389.config import Config
import ldap
import time

pytestmark = pytest.mark.tier1


def test_password_gracelimit_section(topo):
    """Password grace limit section.

    :id: d6f4a7fa-473b-11ea-8766-8c16451d917c
    :setup: Standalone
    :steps:
        1. Resets the default password policy
        2. Turning on password expiration, passwordMaxAge: 30 and passwordGraceLimit: 7
        3. Check users have 7 grace login attempts after their password expires
        4. Reset the user passwords to start the clock
        5. The the 8th should fail
        6. Now try resetting the password before the grace login attempts run out
        7. Bind 6 times, and on the 7th change the password
        8. Setting passwordMaxAge: 1 and passwordGraceLimit: 7
        9. Modify the users passwords to start the clock of zero
        10. First 7 good attempts, 8th should fail
        11. Setting the passwordMaxAge to 3 seconds once more and the passwordGraceLimit to 0
        12. Modify the users passwords to start the clock
        13. Users should be blocked automatically after 3 second
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
        13. Success
    """
    config = Config(topo.standalone)
    # Resets the default password policy
    config.replace_many(
        ('passwordmincategories', '1'),
        ('passwordStorageScheme', 'CLEAR'))
    user = UserAccounts(topo.standalone, DEFAULT_SUFFIX, rdn=None).create_test_user()
    # Turning on password expiration, passwordMaxAge: 30 and passwordGraceLimit: 7
    config.replace_many(
        ('passwordMaxAge', '3'),
        ('passwordGraceLimit', '7'),
        ('passwordexp', 'on'),
        ('passwordwarning', '30'))
    # Reset the user passwords to start the clock
    # Check users have 7 grace login attempts after their password expires
    user.replace('userpassword', '00fr3d1')
    for _ in range(3):
        time.sleep(1)
    user_account = UserAccount(topo.standalone, user.dn)
    for _ in range(7):
        conn = user_account.bind('00fr3d1')
    # The the 8th should fail
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        conn = user_account.bind('00fr3d1')
    # Now try resetting the password before the grace login attempts run out
    user.replace('userpassword', '00fr3d2')
    for _ in range(3):
        time.sleep(1)
    user_account = UserAccount(topo.standalone, user.dn)
    # Bind 6 times, and on the 7th change the password
    for _ in range(6):
        conn = user_account.bind('00fr3d2')
    user.replace('userpassword', '00fr3d1')
    for _ in range(3):
        time.sleep(1)
    for _ in range(7):
        conn = user_account.bind('00fr3d1')
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        conn = user_account.bind('00fr3d1')
    # Setting passwordMaxAge: 1 and passwordGraceLimit: 7
    config.replace_many(
        ('passwordMaxAge', '1'),
        ('passwordwarning', '1'))
    # Modify the users passwords to start the clock of zero
    user.replace('userpassword', '00fr3d2')
    time.sleep(1)
    # First 7 good attempts, 8th should fail
    user_account = UserAccount(topo.standalone, user.dn)
    for _ in range(7):
        conn = user_account.bind('00fr3d2')
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        conn = user_account.bind('00fr3d2')
    # Setting the passwordMaxAge to 3 seconds once more and the passwordGraceLimit to 0
    config.replace_many(
        ('passwordMaxAge', '3'),
        ('passwordGraceLimit', '0'))
    # Modify the users passwords to start the clock
    # Users should be blocked automatically after 3 second
    user.replace('userpassword', '00fr3d1')
    for _ in range(3):
        time.sleep(1)
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        conn = user_account.bind('00fr3d1')


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)