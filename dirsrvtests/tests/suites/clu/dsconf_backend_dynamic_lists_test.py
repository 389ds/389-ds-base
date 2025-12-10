
# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
"""Test dsconf CLI with Dynamic Lists configuration"""

import json
import os
import subprocess
import logging
import pytest
from lib389._constants import DN_DM
from lib389.topologies import topology_st as topo

pytestmark = pytest.mark.tier1

log = logging.getLogger(__name__)

# Dynamic Lists configuration options to test
DYNAMIC_LISTS_SETTINGS = [
    ('dynamic-oc', 'GroupOfUrls'),
    ('dynamic-url-attr', 'MemberUrl'),
    ('dynamic-list-attr', 'uniquemember'),
    ('enable-dynamic-lists', None),
    ('disable-dynamic-lists', None),
]


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
            '-j', '-D', DN_DM, '-w', 'password', 'backend', 'config']


@pytest.mark.parametrize("attr,value", DYNAMIC_LISTS_SETTINGS)
def test_dynamic_lists_plugin_set(topo, attr, value):
    """Test dynamic lists plugin set command with various configuration options

    :id: 8e9fd0d8-bc96-43f0-8119-e18286a325e5
    :setup: Standalone DS instance
    :steps:
        1. Set various dynamic lists plugin configuration options
        2. Verify the setting was applied
    :expectedresults:
        1. Success
        2. Success
    """

    dsconf_cmd = get_dsconf_base_cmd(topo)

    # Build the set command
    if value is None:
        set_cmd = ['set', f'--{attr}']
    else:
        set_cmd = ['set', f'--{attr}', value]

    # Set the configuration
    output, rc = execute_dsconf_command(dsconf_cmd, set_cmd)
    assert rc == 0, f"Failed to set {attr}={value}: {output}"
    log.info(f"Set {attr}={value} successfully")

    # Verify the setting was applied by getting the configuration
    output, rc = execute_dsconf_command(dsconf_cmd, ['get'])
    assert rc == 0
    json_result = json.loads(output)

    # Check if the attribute was set correctly
    # Map CLI argument names to LDAP attribute names
    attr_map = {
        'dynamic-oc': 'nsslapd-dynamic-lists-oc',
        'dynamic-url-attr': 'nsslapd-dynamic-lists-url-attr',
        'dynamic-list-attr': 'nsslapd-dynamic-lists-attr',
        'enable-dynamic-lists': 'nsslapd-dynamic-lists-enabled',
        'disable-dynamic-lists': 'nsslapd-dynamic-lists-enabled',
    }
    ldap_attr = attr_map.get(attr)
    assert ldap_attr and 'attrs' in json_result
    config_value = json_result['attrs'].get(ldap_attr)
    assert config_value is not None
    if isinstance(config_value, list):
        config_value = config_value[0]

    if value is None:
        if attr == 'enable-dynamic-lists':
            assert config_value == 'on', f"Expected 'on', got {config_value}"
        elif attr == 'disable-dynamic-lists':
            assert config_value == 'off', f"Expected 'off', got {config_value}"
    else:
        assert config_value == value, f"Expected {value} in {config_value}"


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])

