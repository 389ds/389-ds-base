# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import ldap
import json
import logging
import pytest
import os
import re
import signal
import subprocess
import time
from lib389._constants import DEFAULT_SUFFIX, PASSWORD, DN_DM
from lib389.topologies import topology_st as topo
from lib389.idm.user import UserAccount, UserAccounts
from lib389.dirsrv_log import DirsrvSecurityLog
from lib389.utils import ensure_str
from lib389.idm.domain import Domain


log = logging.getLogger(__name__)

DN = "uid=security,ou=people," + DEFAULT_SUFFIX
DN_NO_ENTRY = "uid=fredSomething,ou=people," + DEFAULT_SUFFIX
DN_NO_BACKEND = "uid=not_there,o=nope," + DEFAULT_SUFFIX
DN_QUOATED = "uid=\"cn=mark\",ou=people," + DEFAULT_SUFFIX
DN_QUOATED_ESCAPED = "uid=cn\\3dmark,ou=people," + DEFAULT_SUFFIX
DN_LONG = "uid=" + ("z" * 520) + ",ou=people," + DEFAULT_SUFFIX
DN_LONG_TRUNCATED = "uid=" + ("z" * 508) + "..."


@pytest.fixture
def setup_test(topo, request):
    """Disable log buffering"""
    topo.standalone.config.set('nsslapd-securitylog-logbuffering', 'off')

    """Add a test user"""
    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    try:
        users.create(properties={
            'uid': 'security',
            'cn': 'security',
            'sn': 'security',
            'uidNumber': '3000',
            'gidNumber': '4000',
            'homeDirectory': '/home/security',
            'description': 'test security logging with this user',
            'userPassword': PASSWORD
        })
    except ldap.ALREADY_EXISTS:
        pass


def check_log(inst, event_id, msg, dn=None):
    """Check the security log
    """
    time.sleep(1)  # give a little time to flush to disk

    security_log = DirsrvSecurityLog(inst)
    log_lines = security_log.readlines()
    for line in log_lines:
        if re.match(r'[ \t]', line):
            # Skip log title lines
            continue

    event = json.loads(line)
    if dn is not None:
        if event['dn'] == dn.lower() and event['event'] == event_id and event['msg'] == msg:
            # Found it
            return
    elif event['event'] == event_id and event['msg'] == msg:
            # Found it
            return

    assert False


def test_invalid_binds(topo, setup_test):
    """Test the various bind scenarios that should be logged in the security log

    :id: b82e3fb9-f1af-4a75-8d96-5e5d284f31c5
    :setup: Standalone Instance
    :steps:
        1. Test successful bind is logged
        2. Test bad password is logged
        3. Test no such entry is logged
        4. Test no such entry is logged (quoated dn)
        5. Test no such entry is logged (truncated dn)
        6. Test no such backend is logged
        7. Test account lockout is logged
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
    """

    inst = topo.standalone
    user_entry = UserAccount(inst, DN)

    # Good bind
    user_entry.bind(PASSWORD)
    check_log(inst, "BIND_SUCCESS", "", DN)

    # Bad password
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        user_entry.bind("wrongpassword")
    check_log(inst, "BIND_FAILED", "INVALID_PASSWORD", DN)

    # No_such_entry
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        UserAccount(inst, DN_NO_ENTRY).bind(PASSWORD)
    check_log(inst, "BIND_FAILED", "NO_SUCH_ENTRY", DN_NO_ENTRY)

    # No_such_entry (quoted)
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        UserAccount(inst, DN_QUOATED).bind(PASSWORD)
    check_log(inst, "BIND_FAILED", "NO_SUCH_ENTRY", DN_QUOATED_ESCAPED)

    # No such entry (truncated dn)
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        UserAccount(inst, DN_LONG).bind(PASSWORD)
    check_log(inst, "BIND_FAILED", "NO_SUCH_ENTRY", DN_LONG_TRUNCATED)

    # No_such_backend
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        UserAccount(inst, DN_NO_BACKEND).bind(PASSWORD)
    check_log(inst, "BIND_FAILED", "NO_SUCH_ENTRY", DN_NO_BACKEND)


def test_authorization(topo, setup_test):
    """Test the authorization event by performing a modification that the user
    is not allowed to do.

    :id: 17a62670-f86d-4b39-9ee7-e7d36b973ec8
    :setup: Standalone Instance
    :steps:
        1. Bind as a unprivileged user
        2. Attempt to modify restricted resource
        3. Security authorization event is logged
    :expectedresults:
        1. Success
        2. Should fail
        3. Success
    """
    inst = topo.standalone

    # Bind as a user
    user_entry = UserAccount(inst, DN)
    user_conn = user_entry.bind(PASSWORD)

    # Try modifying a restricted resource
    suffix = Domain(user_conn, DEFAULT_SUFFIX)
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        suffix.replace('description', 'not allowed')

    # Check that an authorization event was logged
    check_log(inst, "AUTHZ_ERROR", f"target_dn=({DEFAULT_SUFFIX})")


def test_account_lockout(topo, setup_test):
    """Specify a test case purpose or name here

    :id: b70494f0-7d8e-4d90-8265-9d009bbb08b4
    :setup: Standalone Instance
    :steps:
        1. Configure account lockout
        2. Bind using the wrong password until the account is locked
        3. Check for account lockout event
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """
    inst = topo.standalone

    # Configure account lockout
    inst.config.set('passwordlockout', 'on')
    inst.config.set('passwordMaxFailure', '2')

    # Force entry to get locked out
    user_entry = UserAccount(inst, DN)
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        user_entry.bind("wrong")
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        user_entry.bind("wrong")
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        # Should fail with good or bad password
        user_entry.bind(PASSWORD)

    # Check that an account locked event was logged for this DN
    check_log(inst, "BIND_FAILED", "ACCOUNT_LOCKED", DN)


def test_tcp_events(topo, setup_test):
    """Trigger a TCP_ERROR event event that should be logged in the security log

    :id: 2f653508-89ae-4325-9fed-a2c4ab304149
    :setup: Standalone Instance
    :steps:
        1. Start ldapmodify in its interactive mode
        2. Get the pid of ldapmodify
        3. Kill ldapmodify
        4. Check that a TCP_ERROR is in the security log
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
    """

    inst = topo.standalone

    # Start interactive ldapamodfy command
    ldap_cmd = ['ldapmodify', '-x', '-D', DN_DM, '-w', PASSWORD,
        '-H', f'ldap://{inst.host}:{inst.port}']
    subprocess.Popen(ldap_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, stdin=subprocess.PIPE)
    time.sleep(3)  # need some time for ldapmodify to actually launch

    # Get ldapmodify pid
    result = subprocess.check_output(['pidof','ldapmodify'])
    ldapmodify_pid = ensure_str(result)

    # Kill ldapmodify and check the log
    os.kill(int(ldapmodify_pid), signal.SIGKILL)
    check_log(inst, "TCP_ERROR", "Bad Ber Tag or uncleanly closed connection - B1")


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
