# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2024 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import pytest
import os
import ldap
import re
import time
from lib389._constants import DEFAULT_SUFFIX
from lib389.topologies import topology_st as topo
from lib389.idm.user import UserAccount, UserAccounts
from lib389.dirsrv_log import DirsrvAuditJSONLog

log = logging.getLogger(__name__)

DN = "uid=audit_json,ou=people," + DEFAULT_SUFFIX
MAIN_KEYS = [
    "gm_time",
    "local_time",
    "target_dn",
    "bind_dn",
    "client_ip",
    "server_ip",
    "conn_id",
    "op_id",
    "result",
]

@pytest.fixture
def setup_test(topo, request):
    """Configure log settings"""
    inst = topo.standalone
    inst.config.replace('nsslapd-auditlog-logbuffering', 'off')
    inst.config.replace('nsslapd-auditlog-logging-enabled', 'on')
    inst.config.replace('nsslapd-auditfaillog-logging-enabled', 'on')
    inst.config.replace('nsslapd-auditlog-log-format', 'json')
    inst.config.replace('nsslapd-auditlog-display-attrs', 'cn')
    inst.deleteAuditLogs()


def get_log_event(inst, dn, op):
    """Get a specific audit log event by target_dn and op type
    """
    time.sleep(1)  # give a little time to flush to disk

    audit_log = DirsrvAuditJSONLog(inst)
    log_lines = audit_log.readlines()
    for line in log_lines:
        if re.match(r'[ \t\n]', line):
            # Skip log title lines and newlines
            continue

        event = audit_log.parse_line(line)
        if event['target_dn'].lower() == dn.lower() and op in event:
            return event

    # Not found, must assert
    assert False


def test_audit_json_logging(topo, setup_test):
    """Test audit json logging is working

    :id: 837a89a3-9c5a-4f8f-90fd-27c6b32f13f6
    :setup: Standalone Instance
    :steps:
        1. Add entry
        2. Check it was logged in json and default keys are present
        3. Modify entry
        4. Check audit log
        5. Modrdn of entry
        6. Check log
        7. Delete entry
        8. Check log
        9. Modify time format
        10. Delete of non-existent entry fails
        11. Check log for expected "result == 32"
        12. Verify new time format is applied
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
    """
    inst = topo.standalone

    # Add entry
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    user_dn = "uid=test_audit_log,ou=people," + DEFAULT_SUFFIX
    user = users.ensure_state(properties={
        'uid': 'test_audit_log',
        'cn': 'test',
        'sn': 'user',
        'uidNumber': '1000',
        'gidNumber': '1000',
        'homeDirectory': '/home/test',
        'userPassword': 'pppppppp'
    })

    event = get_log_event(inst, user_dn, "add")
    # Check all the expected keys are present
    for key in MAIN_KEYS:
        assert key in event and event[key] != ""

    # Check specific values
    assert event['result'] == 0
    assert event['add']
    assert 'id_list' in event
    assert event['id_list'][0]['cn'] == "test"

    # Modify entry
    user.replace('sn', 'new sn')
    event = get_log_event(inst, user_dn, "modify")
    assert event['result'] == 0
    assert event['modify'][0]['op'] == "replace"
    assert event['modify'][0]['attr'] == "sn"
    assert event['modify'][0]['values'][0] == "new sn"

    # ModRDN entry
    NEW_SUP = "ou=groups," + DEFAULT_SUFFIX
    user.rename('uid=test_modrdn', newsuperior=NEW_SUP, deloldrdn=True)
    event = get_log_event(inst, user_dn, "modrdn")
    assert event['result'] == 0
    assert event['modrdn']['newrdn'] == "uid=test_modrdn"
    assert event['modrdn']['deleteoldrdn'] is True
    assert event['modrdn']['newsuperior'] == NEW_SUP

    # Delete entry
    DN = "uid=test_modrdn," + NEW_SUP
    user.delete()
    event = get_log_event(inst, DN, "delete")
    assert event['result'] == 0
    assert event['delete']['dn'] == DN

    # CHange time format and check it after the next test
    inst.config.replace('nsslapd-auditlog-time-format', '%F %T')

    # Perform invalid update (result != 0)
    DN = "uid=does_not_exist," + DEFAULT_SUFFIX
    user = UserAccount(inst, DN)
    with pytest.raises(ldap.NO_SUCH_OBJECT):
        user.delete()
    event = get_log_event(inst, DN, "delete")
    assert event['result'] == 32

    # Check time format
    assert re.match(
        r'[0-9]{4}\-[0-9]{2}\-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}',
        event['local_time'])


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
