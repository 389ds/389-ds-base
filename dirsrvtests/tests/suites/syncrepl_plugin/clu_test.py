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
import subprocess
import json
from test389.topologies import topology_st as topo
from lib389.plugins import RetroChangelogPlugin, ContentSyncPlugin
from lib389._constants import DN_DM

pytestmark = pytest.mark.tier2

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

def _dsconf_contentsync(topo, *args):
    cmd = [
        '/usr/sbin/dsconf', topo.standalone.serverid,
        '-j', '-D', DN_DM, '-w', 'password',
        'plugin', 'contentsync',
        *args,
    ]
    return subprocess.run(cmd, text=True, capture_output=True)


def test_contentsync_queue_max_size_cli(topo):
    """Verify dsconf accepts and stores Content Sync queue max size

    :id: fceca807-bf01-466f-8a36-c65c4d2d18cd
    :setup: Standalone instance
    :steps:
        1. Enable Content Sync
        2. Set queue max size with dsconf
        3. Show plugin config
        4. Try an out-of-range queue size
    :expectedresults:
        1. Success
        2. Success
        3. syncrepl-queue-max-size is set as a string value
        4. dsconf rejects the invalid value cleanly
    """
    # Enable RetroChangelog.
    rcl = RetroChangelogPlugin(topo.standalone)
    rcl.enable()
    # Enable ContentSync
    plugin = ContentSyncPlugin(topo.standalone)
    plugin.enable()

    result = _dsconf_contentsync(topo, 'set', '--queue-max-size', '5000')
    assert result.returncode == 0, result.stderr

    result = _dsconf_contentsync(topo, 'show')
    assert result.returncode == 0, result.stderr
    attrs = json.loads(result.stdout)['attrs']
    assert attrs['syncrepl-queue-max-size'] == ['5000']

    result = _dsconf_contentsync(topo, 'set', '--queue-max-size', '10')
    assert result.returncode != 0
    assert 'Traceback' not in result.stderr
    assert 'range' in result.stderr.lower()

def test_contentsync_max_concurrent_cli(topo):
    """Verify dsconf exposes the renamed max-concurrent setting

    :id: 7fbef7bf-3ac8-4925-a5aa-cd873c6f8e81
    :setup: Standalone instance
    :steps:
        1. Enable Content Sync
        2. Set max concurrent persistent searches with dsconf
        3. Show plugin config
    :expectedresults:
        1. Success
        2. Success
        3. syncrepl-max-concurrent is set
    """
    # Enable RetroChangelog.
    rcl = RetroChangelogPlugin(topo.standalone)
    rcl.enable()
    # Enable ContentSync
    plugin = ContentSyncPlugin(topo.standalone)
    plugin.enable()

    result = _dsconf_contentsync(topo, 'set', '--max-concurrent', '12')
    assert result.returncode == 0, result.stderr

    result = _dsconf_contentsync(topo, 'show')
    assert result.returncode == 0, result.stderr
    attrs = json.loads(result.stdout)['attrs']
    assert attrs['syncrepl-max-concurrent'] == ['12']
