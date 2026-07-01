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
import pytest
from lib389._constants import DEFAULT_SUFFIX
from lib389.idm.user import UserAccounts, TEST_USER_PROPERTIES
from lib389.paths import Paths
from lib389.utils import check_asan_report
from test389.topologies import topology_st as topo

pytestmark = pytest.mark.tier2

log = logging.getLogger(__name__)

ds_paths = Paths()


@pytest.mark.skipif(not ds_paths.asan_enabled, reason="Don't run if ASAN is not enabled")
def test_auditlog_pwd_mask_overflow(topo):
    """Audit log password masking must not overflow the entry buffer

    :id: 6969c95b-faeb-4d36-8824-c1564954e691
    :setup: Standalone Instance
    :steps:
        1. Enable the audit log
        2. Set the password storage scheme to CLEAR
        3. Add a user with a one-character password
        4. Stop the server and check the ASAN report for heap-buffer-overflow
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. No heap-buffer-overflow is reported
    """
    inst = topo.standalone

    inst.config.set('nsslapd-auditlog-logging-enabled', 'on')
    inst.config.set('passwordStorageScheme', 'CLEAR')

    users = UserAccounts(inst, DEFAULT_SUFFIX)
    user_props = TEST_USER_PROPERTIES.copy()
    user_props.update({
        'uid': 'asan_auditlog_user',
        'cn': 'asan_auditlog_user',
        'userpassword': 'a',
    })
    users.create(properties=user_props)

    overflow_detected = False
    try:
        overflow_detected = check_asan_report(inst, 'heap-buffer-overflow')
    except ValueError as e:
        log.info('No ASAN report found (expected when no overflow): %s', e)

    assert not overflow_detected, 'heap-buffer-overflow detected in ASAN report'


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
