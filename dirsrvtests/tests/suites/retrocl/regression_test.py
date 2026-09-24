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
import threading
import time

import ldap
import pytest

from lib389._constants import DEFAULT_SUFFIX
from lib389.idm.user import UserAccounts
from lib389.plugins import RetroChangelogPlugin
from lib389.properties import LOG_ACCESS_LEVEL
from lib389.rootdse import RootDSE
from lib389.utils import ensure_bytes
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


class RetroclModifyThread(threading.Thread):
    """Keep modifying the given entries until stopped, counting completed updates"""

    def __init__(self, inst, dns, stop):
        threading.Thread.__init__(self)
        self.daemon = True
        self.inst = inst
        self.dns = dns
        self.stop = stop
        self.done = 0

    def run(self):
        conn = self.inst.clone()
        conn.open()
        conn.set_option(ldap.OPT_TIMEOUT, 30)
        i = 0
        while not self.stop.is_set():
            dn = self.dns[i % len(self.dns)]
            i += 1
            try:
                conn.modify_s(dn, [(ldap.MOD_REPLACE, 'description', ensure_bytes(str(i)))])
                self.done += 1
            except ldap.LDAPError as e:
                log.error(f'Failed to modify {dn}: {e!r}')
        conn.close()


@pytest.fixture(scope="function")
def retrocl_fast_trim_setup(topology_st, request):
    """Enable retrocl with a short maxage and trim interval; restore on teardown."""
    inst = topology_st.standalone
    rcl = RetroChangelogPlugin(inst)
    orig_maxage = rcl.get_attr_val_utf8('nsslapd-changelogmaxage')
    orig_interval = rcl.get_attr_val_utf8('nsslapd-changelog-trim-interval')

    log.info('Enable retro changelog with maxage 5s and trim interval 1s')
    rcl.enable()
    rcl.replace('nsslapd-changelogmaxage', '5s')
    rcl.replace('nsslapd-changelog-trim-interval', '1')
    inst.restart()

    def fin():
        log.info('Restore retro changelog configuration')
        rcl = RetroChangelogPlugin(inst)
        for attr, value in (('nsslapd-changelogmaxage', orig_maxage),
                            ('nsslapd-changelog-trim-interval', orig_interval)):
            if value is None:
                rcl.remove_all(attr)
            else:
                rcl.replace(attr, value)
        if rcl.status():
            rcl.disable()
        inst.restart()

    request.addfinalizer(fin)

    return topology_st


def test_retrocl_trim_no_deadlock_with_updates(retrocl_fast_trim_setup):
    """Verify retro changelog trimming does not deadlock with concurrent updates

    :id: 44e1171f-61cb-432a-bbdf-601708b83e52
    :setup: Standalone instance with retro changelog enabled, maxage 5s and trim interval 1s
    :steps:
        1. Create test users
        2. Modify the users from several threads for 60 seconds while the
           retro changelog trimming thread deletes expired records
        3. Check that updates keep completing during the whole run
        4. Check that the retro changelog was trimmed during the run
    :expectedresults:
        1. Success
        2. Success
        3. No 20 seconds window without a completed update (no deadlock)
        4. firstchangenumber is greater than 1
    """
    inst = retrocl_fast_trim_setup.standalone
    duration = 60
    stall_limit = 20

    log.info('Create test users')
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    test_users = [users.create_test_user(uid=7200 + i) for i in range(16)]
    dns = [user.dn for user in test_users]

    stop = threading.Event()
    workers = [RetroclModifyThread(inst, dns, stop) for _ in range(8)]
    try:
        log.info(f'Modify the users from {len(workers)} threads for {duration}s')
        for worker in workers:
            worker.start()
        start = last_progress = time.time()
        last_done = -1
        while time.time() - start < duration:
            time.sleep(1)
            done = sum(worker.done for worker in workers)
            if done != last_done:
                last_done, last_progress = done, time.time()
            assert time.time() - last_progress < stall_limit, \
                f'No update completed for {stall_limit}s after {done} updates: ' \
                'retro changelog trimming deadlocked with an update'
    finally:
        stop.set()
        for worker in workers:
            worker.join(timeout=35)

    log.info(f'{last_done} updates completed')
    first = RootDSE(inst).get_attr_val_int('firstchangenumber')
    assert first > 1, 'Retro changelog was not trimmed during the test'

    for user in test_users:
        user.delete()


if __name__ == '__main__':
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
