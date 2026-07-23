# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import logging
import os
import re

import pytest

from lib389._constants import DEFAULT_SUFFIX
from lib389.idm.user import UserAccounts
from lib389.plugins import RetroChangelogPlugin
from lib389.properties import LOG_ACCESS_LEVEL
from test389.topologies import topology_st

pytestmark = pytest.mark.tier1

log = logging.getLogger(__name__)


@pytest.fixture(scope="function")
def retrocl_access_log_setup(topology_st, request):
    """Configure retrocl and internal-op access logging; restore on teardown."""
    inst = topology_st.standalone
    orig_log_level = inst.config.get_attr_val_utf8(LOG_ACCESS_LEVEL)
    orig_buffering = inst.config.get_attr_val_utf8('nsslapd-accesslog-logbuffering')

    log.info('Set access log level to 260 to log internal operations')
    inst.config.set(LOG_ACCESS_LEVEL, '260')
    inst.config.set('nsslapd-accesslog-logbuffering', 'off')

    log.info('Enable retro changelog plugin')
    rcl = RetroChangelogPlugin(inst)
    rcl.enable()

    log.info('Restart instance')
    inst.restart()

    log.info('Clear access logs')
    inst.stop()
    inst.deleteAccessLogs(restart=False)
    inst.start()

    def fin():
        log.info('Restore access log configuration')
        inst.config.set(LOG_ACCESS_LEVEL, orig_log_level)
        inst.config.set('nsslapd-accesslog-logbuffering', orig_buffering)
        rcl = RetroChangelogPlugin(inst)
        if rcl.status():
            rcl.disable()
        inst.restart()

    request.addfinalizer(fin)

    return topology_st


def test_retrocl_access_log_wtime_not_negative(retrocl_access_log_setup):
    """Verify retro changelog internal ops do not log negative wtime

    :id: 82501ba5-9cb4-4d8c-aa9a-9d235806b74a
    :setup: Standalone instance with retro changelog enabled and internal-op access logging
    :steps:
        1. Set nsslapd-accesslog-level to 260
        2. Enable retro changelog plugin and restart the instance
        3. Clear the access log
        4. Perform LDAP add, modify, and delete operations
        5. Search the access log for lines containing wtime=-
    :expectedresults:
        1. Access log level is set to 260
        2. Retro changelog plugin is enabled
        3. Access log is cleared
        4. LDAP operations succeed and generate retrocl internal operations
        5. No access log lines contain negative wtime values
    """
    inst = retrocl_access_log_setup.standalone

    log.info('Perform LDAP operations to generate retrocl internal operations')
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    user = users.create(properties={
        'uid': 'retrocl_wtime_user',
        'cn': 'retrocl wtime user',
        'sn': 'user',
        'uidNumber': '7100',
        'gidNumber': '7100',
        'homeDirectory': '/home/retrocl_wtime_user',
        'userPassword': 'password',
    })
    user.replace('description', 'updated')
    user.delete()

    log.info('Verify internal operations were logged to the access log')
    internal_result_lines = inst.ds_access_log.match(r'.*conn=Internal.*RESULT.*')
    assert len(internal_result_lines) > 0, \
        'Expected internal operation RESULT lines in access log'

    log.info('Check access log for negative wtime values')
    negative_wtime_lines = inst.ds_access_log.match(r'.*wtime=-.*')
    assert len(negative_wtime_lines) == 0, \
        f'Found access log lines with negative wtime: {negative_wtime_lines}'

    wtime_pattern = re.compile(r'wtime=(-?[0-9.]+)')
    for line in internal_result_lines:
        match = wtime_pattern.search(line)
        if match is not None:
            assert float(match.group(1)) >= 0, \
                f'Negative wtime on internal operation: {line}'


if __name__ == '__main__':
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
