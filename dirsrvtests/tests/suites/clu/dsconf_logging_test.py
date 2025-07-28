# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import json
import subprocess
import logging
import pytest
from lib389._constants import DN_DM
from lib389.topologies import topology_st as topo

pytestmark = pytest.mark.tier1

log = logging.getLogger(__name__)

SETTINGS = [
    ('logging-enabled', None),
    ('logging-disabled', None),
    ('mode', '700'),
    ('compress-enabled', None),
    ('compress-disabled', None),
    ('buffering-enabled', None),
    ('buffering-disabled', None),
    ('max-logs', '4'),
    ('max-logsize', '7'),
    ('rotation-interval', '2'),
    ('rotation-interval-unit', 'week'),
    ('rotation-tod-enabled', None),
    ('rotation-tod-disabled', None),
    ('rotation-tod-hour', '12'),
    ('rotation-tod-minute', '20'),
    ('deletion-interval', '3'),
    ('deletion-interval-unit', 'day'),
    ('max-disk-space', '20'),
    ('free-disk-space', '2'),
]

DEFAULT_TIME_FORMAT = "%FT%TZ"


def execute_dsconf_command(dsconf_cmd, subcommands):
    """Execute dsconf command and return output and return code"""

    cmdline = dsconf_cmd + subcommands
    proc = subprocess.Popen(cmdline, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    out, err = proc.communicate()

    if proc.returncode != 0 and err:
        log.error(f"Command failed: {' '.join(cmdline)}")
        log.error(f"Stderr: {err.decode('utf-8')}")

    return out.decode('utf-8'), proc.returncode


def get_dsconf_base_cmd(topo):
    """Return base dsconf command list"""
    return ['/usr/sbin/dsconf', topo.standalone.serverid,
            '-j', '-D', DN_DM, '-w', 'password', 'logging']


@pytest.mark.parametrize("log_type", ['access', 'audit', 'auditfail', 'error', 'security'])
def test_log_settings(topo, log_type):
    """Test each log setting can be set successfully

    :id: b800fd03-37f5-4e74-9af8-eeb07030eb52
    :setup: Standalone DS instance
    :steps:
        1. Test each log's settings
    :expectedresults:
        1. Success
    """

    dsconf_cmd = get_dsconf_base_cmd(topo)

    output, rc = execute_dsconf_command(dsconf_cmd, [log_type, 'get'])
    assert rc == 0
    json_result = json.loads(output)
    default_location = json_result['Log name and location']

    output, rc = execute_dsconf_command(dsconf_cmd, [log_type, 'set',
                                                     'location',
                                                     f'/tmp/{log_type}'])
    assert rc == 0
    output, rc = execute_dsconf_command(dsconf_cmd, [log_type, 'set',
                                                     'location',
                                                     default_location])
    assert rc == 0

    if log_type == "access":
        output, rc = execute_dsconf_command(dsconf_cmd,
                                            [log_type, 'list-levels'])
        assert rc == 0

        output, rc = execute_dsconf_command(dsconf_cmd,
                                            [log_type, 'set', 'level',
                                             'internal'])
        assert rc == 0
        output, rc = execute_dsconf_command(dsconf_cmd,
                                            [log_type, 'set', 'level',
                                             'internal', 'entry'])
        assert rc == 0
        output, rc = execute_dsconf_command(dsconf_cmd,
                                            [log_type, 'set', 'level',
                                             'internal', 'default'])
        assert rc == 0

    if log_type == "error":
        output, rc = execute_dsconf_command(dsconf_cmd,
                                            [log_type, 'list-levels'])
        assert rc == 0

        output, rc = execute_dsconf_command(dsconf_cmd,
                                            [log_type, 'set', 'level',
                                             'plugin', 'replication'])
        assert rc == 0
        output, rc = execute_dsconf_command(dsconf_cmd,
                                            [log_type, 'set', 'level',
                                             'default'])
        assert rc == 0

    if log_type in ["access", "audit", "error"]:
        output, rc = execute_dsconf_command(dsconf_cmd,
                                            [log_type, 'set',
                                             'time-format', '%D'])
        assert rc == 0
        output, rc = execute_dsconf_command(dsconf_cmd,
                                            [log_type, 'set',
                                             'time-format',
                                             DEFAULT_TIME_FORMAT])
        assert rc == 0

        output, rc = execute_dsconf_command(dsconf_cmd,
                                            [log_type, 'set',
                                             'log-format',
                                             'json'])
        assert rc == 0, f"Failed to set {log_type} log-format to json: {output}"

        output, rc = execute_dsconf_command(dsconf_cmd,
                                            [log_type, 'set',
                                             'log-format',
                                             'default'])
        assert rc == 0, f"Failed to set {log_type} log-format to default: {output}"

    if log_type == "audit":
        output, rc = execute_dsconf_command(dsconf_cmd,
                                            [log_type, 'set',
                                             'display-attrs', 'cn'])
        assert rc == 0

    for attr, value in SETTINGS:
        if log_type == "auditfail" and attr.startswith("buffer"):
            continue

        if value is None:
            output, rc = execute_dsconf_command(dsconf_cmd, [log_type,
                                                'set', attr])
        else:
            output, rc = execute_dsconf_command(dsconf_cmd, [log_type,
                                                'set', attr, value])
        assert rc == 0
