# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import time
import ldap
import pytest
import subprocess
import logging

from lib389._constants import DEFAULT_SUFFIX, DN_DM, PASSWORD, DN_CONFIG
from lib389.topologies import topology_st
from lib389.idm.user import UserAccounts
from lib389.idm.domain import Domain
from lib389.pwpolicy import PwPolicyManager

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

OU_DN = f"ou=People,{DEFAULT_SUFFIX}"
USER_ACI = '(targetattr="userpassword || passwordHistory")(version 3.0; acl "pwp test"; allow (all) userdn="ldap:///self";)'


@pytest.fixture(autouse=True, scope="function")
def restore_global_policy(topology_st, request):
    """Snapshot and restore global password policy around each test in this file."""
    inst = topology_st.standalone
    inst.simple_bind_s(DN_DM, PASSWORD)

    attrs = [
        'nsslapd-pwpolicy-local',
        'nsslapd-pwpolicy-inherit-global',
        'passwordHistory',
        'passwordInHistory',
        'passwordChange',
    ]

    entry = inst.getEntry(DN_CONFIG, ldap.SCOPE_BASE, '(objectClass=*)', attrs)
    saved = {attr: entry.getValue(attr) for attr in attrs}

    def fin():
        inst.simple_bind_s(DN_DM, PASSWORD)
        for attr, value in saved.items():
            inst.config.replace(attr, value)

    request.addfinalizer(fin)


@pytest.fixture(scope="function")
def setup_entries(topology_st, request):
    """Create test OU and user, and install an ACI for self password changes."""

    inst = topology_st.standalone

    suffix = Domain(inst, DEFAULT_SUFFIX)
    suffix.add('aci', USER_ACI)

    users = UserAccounts(inst, DEFAULT_SUFFIX)
    try:
        user = users.create_test_user(uid=1)
    except ldap.ALREADY_EXISTS:
        user = users.get("test_user_1")

    def fin():
        pwp = PwPolicyManager(inst)
        try:
            pwp.delete_local_policy(OU_DN)
        except Exception as e:
            if "No password policy" in str(e):
                pass
            else:
                raise e
        try:
            pwp.delete_local_policy(user.dn)
        except Exception as e:
            if "No password policy" in str(e):
                pass
            else:
                raise e
        suffix.remove('aci', USER_ACI)
    request.addfinalizer(fin)

    return user


def set_user_password(inst, user, new_password, bind_as_user_password=None, expect_violation=False):
    if bind_as_user_password is not None:
        user.rebind(bind_as_user_password)
    try:
        user.reset_password(new_password)
        if expect_violation:
            pytest.fail("Password change unexpectedly succeeded")
    except ldap.CONSTRAINT_VIOLATION:
        if not expect_violation:
            pytest.fail("Password change unexpectedly rejected with CONSTRAINT_VIOLATION")
    finally:
        inst.simple_bind_s(DN_DM, PASSWORD)
        time.sleep(1)


def set_global_history(inst, enabled: bool, count: int, inherit_global: str = 'on'):
    inst.simple_bind_s(DN_DM, PASSWORD)
    inst.config.replace('nsslapd-pwpolicy-local', 'on')
    inst.config.replace('nsslapd-pwpolicy-inherit-global', inherit_global)
    inst.config.replace('passwordHistory', 'on' if enabled else 'off')
    inst.config.replace('passwordInHistory', str(count))
    inst.config.replace('passwordChange', 'on')
    time.sleep(1)


def ensure_local_subtree_policy(inst, count: int, track_update_time: str = 'on'):
    pwp = PwPolicyManager(inst)
    pwp.create_subtree_policy(OU_DN, {
        'passwordChange': 'on',
        'passwordHistory': 'on',
        'passwordInHistory': str(count),
        'passwordTrackUpdateTime': track_update_time,
    })
    time.sleep(1)


def set_local_history_via_cli(inst, count: int):
    sbin_dir = inst.get_sbin_dir()
    inst_name = inst.serverid
    cmd = [f"{sbin_dir}/dsconf", inst_name, "localpwp", "set", f"--pwdhistorycount={count}", OU_DN]
    rc = subprocess.call(cmd)
    assert rc == 0, f"dsconf command failed rc={rc}: {' '.join(cmd)}"
    time.sleep(1)


def test_global_history_only_enforced(topology_st, setup_entries):
    """Global-only history enforcement with count 2

    :id: 3d8cf35b-4a33-4587-9814-ebe18b7a1f92
    :setup: Standalone instance, test OU and user, ACI for self password changes
    :steps:
        1. Remove local policies
        2. Set global policy: passwordHistory=on, passwordInHistory=2
        3. Set password to Alpha1, then change to Alpha2 and Alpha3 as the user
        4. Attempt to change to Alpha1 and Alpha2
        5. Attempt to change to Alpha4
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Changes to Welcome1 and Welcome2 are rejected with CONSTRAINT_VIOLATION
        5. Change to Welcome4 is accepted
    """
    inst = topology_st.standalone
    inst.simple_bind_s(DN_DM, PASSWORD)

    set_global_history(inst, enabled=True, count=2)

    user = setup_entries
    user.reset_password('Alpha1')
    set_user_password(inst, user, 'Alpha2', bind_as_user_password='Alpha1')
    set_user_password(inst, user, 'Alpha3', bind_as_user_password='Alpha2')

    # Within last 2
    set_user_password(inst, user, 'Alpha2', bind_as_user_password='Alpha3', expect_violation=True)
    set_user_password(inst, user, 'Alpha1', bind_as_user_password='Alpha3', expect_violation=True)

    # New password should be allowed
    set_user_password(inst, user, 'Alpha4', bind_as_user_password='Alpha3', expect_violation=False)


def test_local_overrides_global_history(topology_st, setup_entries):
    """Local subtree policy (history=3) overrides global (history=1)

    :id: 97c22f56-5ea6-40c1-8d8c-1cece3bf46fd
    :setup: Standalone instance, test OU and user
    :steps:
        1. Set global policy passwordInHistory=1
        2. Create local subtree policy on the OU with passwordInHistory=3
        3. Set password to Bravo1, then change to Bravo2 and Bravo3 as the user
        4. Attempt to change to Bravo1
        5. Attempt to change to Bravo5
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Change to Welcome1 is rejected (local policy wins)
        5. Change to Welcome5 is accepted
    """
    inst = topology_st.standalone
    inst.simple_bind_s(DN_DM, PASSWORD)

    set_global_history(inst, enabled=True, count=1, inherit_global='on')

    ensure_local_subtree_policy(inst, count=3)

    user = setup_entries
    user.reset_password('Bravo1')
    set_user_password(inst, user, 'Bravo2', bind_as_user_password='Bravo1')
    set_user_password(inst, user, 'Bravo3', bind_as_user_password='Bravo2')

    # Third prior should be rejected under local policy count=3
    set_user_password(inst, user, 'Bravo1', bind_as_user_password='Bravo3', expect_violation=True)

    # New password allowed
    set_user_password(inst, user, 'Bravo5', bind_as_user_password='Bravo3', expect_violation=False)


def test_change_local_history_via_cli_affects_enforcement(topology_st, setup_entries):
    """Changing local policy via CLI is enforced immediately

    :id: 5a6d0d14-4009-4bad-86e1-cde5000c43dc
    :setup: Standalone instance, test OU and user, dsconf available
    :steps:
        1. Ensure local subtree policy passwordInHistory=3
        2. Set password to Charlie1, then change to Charlie2 and Charlie3 as the user
        3. Attempt to change to Charlie1 (within last 3)
        4. Run: dsconf <inst> localpwp set --pwdhistorycount=1 "ou=product testing,<suffix>"
        5. Attempt to change to Charlie1 again
    :expectedresults:
        1. Success
        2. Success
        3. Change to Welcome1 is rejected
        4. CLI command succeeds
        5. Change to Welcome1 now succeeds (only last 1 is disallowed)
    """
    inst = topology_st.standalone
    inst.simple_bind_s(DN_DM, PASSWORD)

    ensure_local_subtree_policy(inst, count=3)

    user = setup_entries
    user.reset_password('Charlie1')
    set_user_password(inst, user, 'Charlie2', bind_as_user_password='Charlie1', expect_violation=False)
    set_user_password(inst, user, 'Charlie3', bind_as_user_password='Charlie2', expect_violation=False)

    # With count=3, Welcome1 is within history
    set_user_password(inst, user, 'Charlie1', bind_as_user_password='Charlie3', expect_violation=True)

    # Reduce local count to 1 via CLI to exercise CLI mapping and updated code
    set_local_history_via_cli(inst, count=1)

    # Now Welcome1 should be allowed
    set_user_password(inst, user, 'Charlie1', bind_as_user_password='Charlie3', expect_violation=False)


def test_history_local_only_enforced(topology_st, setup_entries):
    """Local-only history enforcement with count 3

    :id: af6ff34d-ac94-4108-a7b6-2b589c960154
    :setup: Standalone instance, test OU and user
    :steps:
        1. Disable global password history (passwordHistory=off, passwordInHistory=0, inherit off)
        2. Ensure local subtree policy with passwordInHistory=3
        3. Set password to Delta1, then change to Delta2 and Delta3 as the user
        4. Attempt to change to Delta1
        5. Attempt to change to Delta5
        6. Change once more to Delta6, then change to Delta1
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Change to Welcome1 is rejected (within last 3)
        5. Change to Welcome5 is accepted
        6. Welcome1 is now older than the last 3 and is accepted
    """
    inst = topology_st.standalone
    inst.simple_bind_s(DN_DM, PASSWORD)

    set_global_history(inst, enabled=False, count=0, inherit_global='off')

    ensure_local_subtree_policy(inst, count=3)

    user = setup_entries
    user.reset_password('Delta1')
    set_user_password(inst, user, 'Delta2', bind_as_user_password='Delta1')
    set_user_password(inst, user, 'Delta3', bind_as_user_password='Delta2')

    # Within last 2
    set_user_password(inst, user, 'Delta1', bind_as_user_password='Delta3', expect_violation=True)

    # New password allowed
    set_user_password(inst, user, 'Delta5', bind_as_user_password='Delta3', expect_violation=False)

    # Now Welcome1 is older than last 2 after one more change
    set_user_password(inst, user, 'Delta6', bind_as_user_password='Delta5', expect_violation=False)
    set_user_password(inst, user, 'Delta1', bind_as_user_password='Delta6', expect_violation=False)


def test_user_policy_detection_and_enforcement(topology_st, setup_entries):
    """User local policy is detected and enforced; removal falls back to global policy

    :id: 2213126a-1f47-468c-8337-0d2ee5d2d585
    :setup: Standalone instance, test OU and user
    :steps:
        1. Set global policy passwordInHistory=1
        2. Create a user local password policy on the user with passwordInHistory=3
        3. Verify is_user_policy(USER_DN) is True
        4. Set password to Echo1, then change to Echo2 and Echo3 as the user
        5. Attempt to change to Echo1 (within last 3)
        6. Delete the user local policy
        7. Verify is_user_policy(USER_DN) is False
        8. Attempt to change to Echo1 again (now only last 1 disallowed by global)
    :expectedresults:
        1. Success
        2. Success
        3. is_user_policy returns True
        4. Success
        5. Change to Welcome1 is rejected
        6. Success
        7. is_user_policy returns False
        8. Change to Welcome1 succeeds (two back is allowed by global=1)
    """
    inst = topology_st.standalone
    inst.simple_bind_s(DN_DM, PASSWORD)

    set_global_history(inst, enabled=True, count=1, inherit_global='on')

    pwp = PwPolicyManager(inst)
    user = setup_entries
    pwp.create_user_policy(user.dn, {
        'passwordChange': 'on',
        'passwordHistory': 'on',
        'passwordInHistory': '3',
    })

    assert pwp.is_user_policy(user.dn) is True

    user.reset_password('Echo1')
    set_user_password(inst, user, 'Echo2', bind_as_user_password='Echo1', expect_violation=False)
    set_user_password(inst, user, 'Echo3', bind_as_user_password='Echo2', expect_violation=False)
    set_user_password(inst, user, 'Echo1', bind_as_user_password='Echo3', expect_violation=True)

    pwp.delete_local_policy(user.dn)
    assert pwp.is_user_policy(user.dn) is False

    # With only global=1, Echo1 (two back) is allowed
    set_user_password(inst, user, 'Echo1', bind_as_user_password='Echo3', expect_violation=False)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
