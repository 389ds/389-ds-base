# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import os
import time

import ldap
import pytest

from lib389._constants import DEFAULT_SUFFIX
from lib389.idm.user import UserAccount, UserAccounts
from test389.topologies import topology_st

pytestmark = pytest.mark.tier1
DEBUGGING = os.getenv("DEBUGGING", default=False)
logging.getLogger(__name__).setLevel(logging.DEBUG if DEBUGGING else logging.INFO)

HISTORICAL = "History-Previous-Password1!"
CURRENT = ["History-Current-Password2!", "History-Current-Password3!"]
FRESH = ["History-Fresh-Password4!", "History-Fresh-Password5!"]


@pytest.fixture(params=["CLEAR", "PBKDF2-SHA512"])
def password_entry(topology_st, request):
    inst = topology_st.standalone
    policy = {
        "passwordHistory": "on",
        "passwordInHistory": "3",
        "passwordChange": "on",
        "passwordMinAge": "0",
        "passwordMustChange": "off",
        "passwordExp": "off",
        "passwordCheckSyntax": "off",
        "passwordStorageScheme": request.param,
    }
    saved = {attr: inst.config.get_attr_vals(attr) for attr in policy}
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    user = users.create_test_user(uid=14003)
    connections = []

    def cleanup():
        for conn in connections:
            conn.close()
        if not DEBUGGING:
            user.delete()
            for attr, values in saved.items():
                inst.config.replace(attr, values)

    request.addfinalizer(cleanup)
    inst.config.replace_many(*policy.items())
    user.replace("userPassword", HISTORICAL)
    user.add("aci", '(targetattr="userPassword")(version 3.0; '
             'acl "Change own password for history test"; '
             'allow (write) userdn="ldap:///self";)')
    conn = user.bind(HISTORICAL)
    connections.append(conn)
    bound_user = UserAccount(conn, user.dn)
    bound_user.replace("userPassword", CURRENT)
    # The backend sends the modify result before update_pw_info writes history.
    # Wait for that write before reading from the separate admin connection.
    deadline = time.monotonic() + 10
    history = user.get_attr_vals("passwordHistory")
    while not history and time.monotonic() < deadline:
        time.sleep(0.1)
        history = user.get_attr_vals("passwordHistory")
    assert len(history) == 1, "Expected one passwordHistory value after setup"
    return inst, user, bound_user


def assert_passwords_work(user, passwords):
    for password in passwords:
        conn = user.bind(password)
        conn.close()


@pytest.mark.parametrize("reused", ["current", "historical"])
@pytest.mark.parametrize("position", [0, 1, 2])
def test_reject_reused_value(password_entry, reused, position):
    """Reject a reused password at any position without changing stored passwords.

    :id: ff92429c-991e-4290-99ef-bafc058d6199
    :parametrized: yes
    :setup: Standalone instance with password history enabled and a user with two current passwords
    :steps:
        1. Submit two fresh passwords and a current or historical password at the selected position
        2. Read the stored passwords and history after the rejected operation
        3. Bind with both current passwords and with the proposed fresh passwords
    :expectedresults:
        1. The entire modification fails with a constraint violation
        2. Stored passwords and history are unchanged
        3. Both current passwords work and neither fresh password works
    """
    inst, user, bound_user = password_entry
    passwords_before = set(user.get_attr_vals("userPassword"))
    history_before = set(user.get_attr_vals("passwordHistory"))
    values = list(FRESH)
    values.insert(position, CURRENT[1] if reused == "current" else HISTORICAL)

    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        bound_user.replace("userPassword", values)

    assert set(user.get_attr_vals("userPassword")) == passwords_before
    assert set(user.get_attr_vals("passwordHistory")) == history_before
    assert_passwords_work(user, CURRENT)
    for password in FRESH:
        with pytest.raises(ldap.INVALID_CREDENTIALS):
            user.bind(password)


def test_accept_fresh_values(password_entry):
    """Keep support for multiple passwords when every value passes history checks.

    :id: b1ca28b4-eee6-49de-bcca-26f427c15fce
    :parametrized: yes
    :setup: Standalone instance with password history enabled and a user with two current passwords
    :steps:
        1. Replace both current passwords with two fresh passwords
        2. Bind with each fresh password and each replaced password
    :expectedresults:
        1. The replacement succeeds and stores two password values
        2. Both fresh passwords work and neither replaced password works
    """
    inst, user, bound_user = password_entry
    bound_user.replace("userPassword", FRESH)
    assert len(user.get_attr_vals("userPassword")) == 2
    assert_passwords_work(user, FRESH)
    for password in CURRENT:
        with pytest.raises(ldap.INVALID_CREDENTIALS):
            user.bind(password)


def test_history_disabled(password_entry):
    """Allow reused values when password history is disabled.

    :id: b52a0253-0f30-4c72-badd-1900070c1c66
    :parametrized: yes
    :setup: Standalone instance with a user and existing password history
    :steps:
        1. Disable password history
        2. Replace userPassword with a fresh, current and historical password
        3. Bind with each submitted password
    :expectedresults:
        1. Configuration succeeds
        2. The replacement succeeds
        3. All three passwords work
    """
    inst, user, bound_user = password_entry
    inst.config.replace("passwordHistory", "off")
    values = [FRESH[0], CURRENT[1], HISTORICAL]
    bound_user.replace("userPassword", values)
    assert_passwords_work(user, values)


def test_zero_history_count(password_entry):
    """A zero history count still rejects any current password.

    :id: fb73e43f-1c9e-4cd1-8682-ff32697042a7
    :parametrized: yes
    :setup: Standalone instance with a user and existing password history
    :steps:
        1. Set passwordInHistory to zero while keeping passwordHistory enabled
        2. Submit a fresh password followed by a current password
        3. Submit a fresh password followed by a historical password
    :expectedresults:
        1. Configuration succeeds
        2. The replacement is rejected and both current passwords still work
        3. The replacement succeeds and both submitted passwords work
    """
    inst, user, bound_user = password_entry
    inst.config.replace("passwordInHistory", "0")
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        bound_user.replace("userPassword", [FRESH[0], CURRENT[1]])
    assert_passwords_work(user, CURRENT)
    values = [FRESH[0], HISTORICAL]
    bound_user.replace("userPassword", values)
    assert_passwords_work(user, values)
