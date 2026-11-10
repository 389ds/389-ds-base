# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import time

import ldap
import pytest

from lib389._constants import DEFAULT_SUFFIX
from lib389.idm.user import UserAccount, UserAccounts
from test389.topologies import topology_st

pytestmark = pytest.mark.tier1


@pytest.mark.parametrize("reused", ["Previous-Password1!", "Current-Password2!"])
def test_reject_reused_value(topology_st, reused):
    """Check password history for the second value of a password change.

    :id: ff92429c-991e-4290-99ef-bafc058d6199
    :parametrized: yes
    :setup: Standalone instance with password history enabled
    :steps:
        1. Change a user's password to put the previous password in history
        2. Submit a fresh password followed by a current or historical password
        3. Submit two fresh passwords
    :expectedresults:
        1. The previous password is recorded in history
        2. The change is rejected and the current password still works
        3. Both fresh passwords work
    """
    inst = topology_st.standalone
    policy = {"passwordHistory": "on", "passwordInHistory": "3",
              "passwordChange": "on", "passwordMinAge": "0"}
    saved = {attr: inst.config.get_attr_vals(attr) for attr in policy}
    user = UserAccounts(inst, DEFAULT_SUFFIX).create_test_user(uid=14003)
    conn = None
    try:
        inst.config.replace_many(*policy.items())
        user.replace("userPassword", "Previous-Password1!")
        user.add("aci", '(targetattr="userPassword")(version 3.0; '
                 'acl "Change own password"; allow (write) userdn="ldap:///self";)')
        conn = user.bind("Previous-Password1!")
        bound_user = UserAccount(conn, user.dn)
        bound_user.replace("userPassword", "Current-Password2!")

        # History is written after the modify response; wait before testing it.
        deadline = time.monotonic() + 10
        while not user.get_attr_vals("passwordHistory") and time.monotonic() < deadline:
            time.sleep(0.1)
        assert user.get_attr_vals("passwordHistory")

        with pytest.raises(ldap.CONSTRAINT_VIOLATION):
            bound_user.replace("userPassword", ["Fresh-Password3!", reused])
        user.bind("Current-Password2!").close()

        bound_user.replace("userPassword", ["Fresh-Password3!", "Fresh-Password4!"])
        user.bind("Fresh-Password3!").close()
        user.bind("Fresh-Password4!").close()
    finally:
        if conn is not None:
            conn.close()
        user.delete()
        inst.config.replace_many(*saved.items())
