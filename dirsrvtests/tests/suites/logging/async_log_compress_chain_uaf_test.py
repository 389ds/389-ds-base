# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import glob
import logging
import os
import time

import pytest

from lib389._constants import DEFAULT_SUFFIX
from lib389.idm.domain import Domain
from test389.topologies import topology_st as topo

log = logging.getLogger(__name__)

pytestmark = pytest.mark.tier2


def _generate_access_log_load(suffix, iterations=100):
    for i in range(iterations):
        suffix.replace('description', f'compress_chain_uaf_{i}')
        suffix.get_attr_val('description')


def _assert_no_empty_compressed_logs(log_dir, log_type='access'):
    for gz_path in glob.glob(f'{log_dir}/{log_type}.*.gz'):
        assert os.path.getsize(gz_path) > 0, f'Empty compressed log: {gz_path}'


def _assert_accesslog_list_matches_disk(inst, log_dir):
    accesslog_list = inst.config.get_attr_vals_utf8('nsslapd-accesslog-list')
    if len(accesslog_list) == 1 and accesslog_list[0] == '':
        accesslog_list = []
    disk_files = glob.glob(f'{log_dir}/access.2*')

    disk_files_for_compare = set()
    for fpath in disk_files:
        if fpath.endswith('.gz'):
            disk_files_for_compare.add(fpath[:-3])
        else:
            disk_files_for_compare.add(fpath)

    missing_from_disk = set(accesslog_list) - disk_files_for_compare
    assert not missing_from_disk, (
        'nsslapd-accesslog-list references files missing from disk: '
        f'{missing_from_disk}'
    )


def test_async_compress_survives_chain_node_reuse(topo):
    """Regression: compress jobs must not dereference freed rotation-chain nodes.

    Background compress jobs used to keep a raw LogFileInfo pointer into the
    rotation chain. Retention trims, logdir changes, and disk-monitor purges
    can free those nodes while jobs are still queued, causing use-after-free
    when the worker updates l_compressed.

    :id: d8b693be-860e-47d8-a9cf-bf0ba6f8dabf
    :setup: Standalone Instance
    :steps:
        1. Enable access log compression with aggressive rotation limits
        2. Rapidly rotate logs to queue compress jobs while retention trims
        3. Change the access log path to discard the in-memory chain
        4. Rotate again on the new path
        5. Verify LDAP still works and accesslog-list matches disk
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Server remains responsive; list matches files on disk
    """
    inst = topo.standalone
    suffix = Domain(inst, DEFAULT_SUFFIX)
    log_dir = inst.get_log_dir()

    inst.config.set('nsslapd-accesslog-logbuffering', 'off')
    inst.config.set('nsslapd-accesslog-compress', 'on')
    inst.config.set('nsslapd-accesslog-maxlogsize', '1')
    inst.config.set('nsslapd-accesslog-maxlogsperdir', '2')
    inst.config.set('nsslapd-accesslog-logexpirationtime', '-1')
    inst.config.set('nsslapd-accesslog-logrotationsync-enabled', 'off')

    for cycle in range(12):
        log.info('Rapid rotation cycle %d/12', cycle + 1)
        _generate_access_log_load(suffix, iterations=120)

    original_accesslog = inst.config.get_attr_val_utf8('nsslapd-accesslog')
    alt_accesslog = os.path.join(log_dir, 'access_chain_uaf')
    inst.config.set('nsslapd-accesslog', alt_accesslog)

    for cycle in range(6):
        log.info('Post-logdir-change rotation cycle %d/6', cycle + 1)
        _generate_access_log_load(suffix, iterations=120)

    time.sleep(2)
    suffix.get_attr_val('description')
    _assert_no_empty_compressed_logs(log_dir)
    _assert_accesslog_list_matches_disk(inst, log_dir)

    inst.config.set('nsslapd-accesslog', original_accesslog)
