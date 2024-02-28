# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022-2024 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import ldap
import logging
import pytest
import os
import time
from lib389._constants import DEFAULT_SUFFIX
from lib389.topologies import topology_st as topo
from lib389.idm.user import UserAccounts

log = logging.getLogger(__name__)


def test_auditlog_display_attrs(topo):
    """Test "display attributes" feature of the audit log

    :id: 01beaf71-4cb5-4943-9774-3210ae5d68a2
    :setup: Standalone Instance
    :steps:
        1. Test "cn" attribute is displayed
        2. Test multiple attributes are displayed
        3. Test modrdn updates log
        4. Test all attributes are displayed
        5. Test delete updates log
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Sucecss
    """

    inst = topo.standalone
    inst.config.replace('nsslapd-auditlog-logging-enabled', 'on')

    # Test "cn" attribute
    inst.config.replace('nsslapd-auditlog-display-attrs', 'cn')
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    user = users.ensure_state(properties={
        'uid': 'test_audit_log',
        'cn': 'test',
        'sn': 'user',
        'uidNumber': '1000',
        'gidNumber': '1000',
        'homeDirectory': '/home/test',
        'userPassword': 'pppppppp'
    })
    user2 = users.ensure_state(properties={
        'uid': 'test_modrdn_delete',
        'cn': 'modrdn_delete',
        'sn': 'modrdn_delete',
        'uidNumber': '1001',
        'gidNumber': '1001',
        'homeDirectory': '/home/modrdn_delete',
        'userPassword': 'pppppppp'
    })
    time.sleep(1)
    assert inst.ds_audit_log.match("#cn: test")
    assert not inst.ds_audit_log.match("#uid: test_audit_log")

    # Test multiple attributes
    inst.config.replace('nsslapd-auditlog-display-attrs', 'uidNumber gidNumber, homeDirectory')
    user.replace('sn', 'new value')
    time.sleep(1)
    assert inst.ds_audit_log.match("#uidNumber: 1000")
    assert inst.ds_audit_log.match("#gidNumber: 1000")
    assert inst.ds_audit_log.match("#homeDirectory: /home/test")
    assert not inst.ds_audit_log.match("#uid: test_audit_log")
    assert not inst.ds_audit_log.match("#uidNumber: 1001")
    assert not inst.ds_audit_log.match("#sn: modrdn_delete")

    # Test modrdn
    user2.rename("uid=modrdn_delete", DEFAULT_SUFFIX)
    time.sleep(1)
    assert inst.ds_audit_log.match("#uidNumber: 1001")
    assert inst.ds_audit_log.match("#gidNumber: 1001")

    # Test ALL attributes
    inst.config.replace('nsslapd-auditlog-display-attrs', '*')
    user.replace('sn', 'new value again')
    time.sleep(1)
    assert inst.ds_audit_log.match("#uid: test_audit_log")
    assert inst.ds_audit_log.match("#cn: test")
    assert inst.ds_audit_log.match("#uidNumber: 1000")
    assert inst.ds_audit_log.match("#objectClass: top")

    # Test delete
    user2.delete()
    time.sleep(1)
    assert inst.ds_audit_log.match("#sn: modrdn_delete")

def test_auditlog_bof(topo):
    """Test that value containing 256 chars doesn't crash the server

    :id: 767c0604-146d-4d07-8bf4-1093f51ce97b
    :setup: Standalone Instance
    :steps:
        1. Change 'cn' attribute to contain exactly 256 chars
        2. Test that server didn't crash
    :expectedresults:
        1. Success
        2. Success
    """

    inst = topo.standalone
    inst.config.replace('nsslapd-auditlog-logging-enabled', 'on')

    inst.config.replace('nsslapd-auditlog-display-attrs', 'cn')
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    users.ensure_state(properties={
        'uid': 'test_auditlog_bof',
        'cn': 'A'*256,
        'sn': 'user',
        'uidNumber': '1001',
        'gidNumber': '1001',
        'homeDirectory': '/home/auditlog_bof',
    })
    time.sleep(1)
    assert inst.status() == True

def test_auditlog_buffering(topo, request):
    """Test log buffering works as expected when on or off

    :id: 08f1ccf0-c1fb-4427-9300-24585e336ae7
    :setup: Standalone Instance
    :steps:
        1. Set buffering on
        2. Make update and immediately check log (update should not be present)
        3. Make invalid update, failed update should not be in log
        4. Disable buffering
        5. Make update and immediately check log (update should be present)
        6. Make invalid update, both failed updates should be in log
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
    """

    # Configure instance
    inst = topo.standalone
    inst.config.replace('nsslapd-auditlog-logging-enabled', 'on')
    inst.config.replace('nsslapd-auditfaillog-logging-enabled', 'on')
    inst.config.replace('nsslapd-auditlog-logbuffering', 'on')
    inst.deleteAuditLogs()  # Start with fresh set of logs
    original_value = inst.config.get_attr_val_utf8('nsslapd-timelimit')

    # Make a good and bad update and check neither are logged
    inst.config.replace('nsslapd-timelimit', '999')
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        inst.config.replace('no_such_attr', 'blah')
    time.sleep(1)
    assert not inst.ds_audit_log.match("nsslapd-timelimit: 999")
    assert not inst.ds_audit_log.match("result: 53")

    # Make a good and bad update and check both are logged
    inst.config.replace('nsslapd-auditlog-logbuffering', 'off')
    inst.config.replace('nsslapd-timelimit', '888')
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        inst.config.replace('no_such_attr', 'nope')
    time.sleep(1)
    assert inst.ds_audit_log.match("nsslapd-timelimit: 888")
    # Both failed updates should be present (easiest way to check log)
    assert len(inst.ds_audit_log.match("result: 53")) == 2

    # Reset timelimit just to be safe
    def fin():
        inst.config.replace('nsslapd-timelimit', original_value)
    request.addfinalizer(fin)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
