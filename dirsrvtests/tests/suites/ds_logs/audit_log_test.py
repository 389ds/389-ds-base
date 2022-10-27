# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

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


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])

