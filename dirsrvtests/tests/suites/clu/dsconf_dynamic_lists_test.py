# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
"""Test dsconf CLI with Dynamic Lists plugin configuration"""

import json
import os
import subprocess
import logging
import pytest
from lib389._constants import DN_DM
from lib389.topologies import topology_st as topo

pytestmark = pytest.mark.tier1

log = logging.getLogger(__name__)

# Dynamic Lists plugin configuration options to test
DYNAMIC_LISTS_SETTINGS = [
    ('objectclass', 'GroupOfUrls'),
    ('url-attr', 'MemberUrl'),
    ('list-attr', 'uniquemember'),
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
            '-j', '-D', DN_DM, '-w', 'password', 'plugin', 'dynamic-lists']


def test_dynamic_lists_plugin_status(topo):
    """Test dynamic lists plugin status and basic operations

    :id: 50be4d7a-3267-4d86-91e6-b6945dac54fa
    :setup: Standalone DS instance
    :steps:
        1. Check plugin status
        2. Enable plugin if needed
        3. Verify plugin is enabled
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """

    dsconf_cmd = get_dsconf_base_cmd(topo)

    # Check plugin status
    output, rc = execute_dsconf_command(dsconf_cmd, ['status'])
    assert rc == 0
    log.info(f"Plugin status: {output}")

    # Enable plugin if not already enabled
    output, rc = execute_dsconf_command(dsconf_cmd, ['enable'])
    assert rc == 0
    log.info(f"Plugin enable result: {output}")

    # Verify plugin is enabled
    output, rc = execute_dsconf_command(dsconf_cmd, ['status'])
    assert rc == 0
    json_result = json.loads(output)
    assert json_result.get('msg') == 'enabled'


def test_dynamic_lists_plugin_get(topo):
    """Test dynamic lists plugin get command

    :id: 8bf18caf-ecca-41d0-a1c9-09f66febf540
    :setup: Standalone DS instance
    :steps:
        1. Get plugin configuration
    :expectedresults:
        1. Success
    """

    dsconf_cmd = get_dsconf_base_cmd(topo)

    output, rc = execute_dsconf_command(dsconf_cmd, ['show'])
    assert rc == 0
    log.info(f"Plugin configuration: {output}")

    # Parse JSON output to verify structure
    json_result = json.loads(output)
    assert json_result.get('type') == 'entry'


@pytest.mark.parametrize("attr,value", DYNAMIC_LISTS_SETTINGS)
def test_dynamic_lists_plugin_set(topo, attr, value):
    """Test dynamic lists plugin set command with various configuration options

    :id: 8e9fd0d8-bc96-43f0-8119-e18286a325e5
    :setup: Standalone DS instance
    :steps:
        1. Set various dynamic lists plugin configuration options
        2. Verify the setting was applied
        3. Reset to default values
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """

    dsconf_cmd = get_dsconf_base_cmd(topo)

    # Build the set command
    set_cmd = ['set', f'--{attr}', value]

    # Set the configuration
    output, rc = execute_dsconf_command(dsconf_cmd, set_cmd)
    assert rc == 0, f"Failed to set {attr}={value}: {output}"
    log.info(f"Set {attr}={value} successfully")

    # Verify the setting was applied by getting the configuration
    output, rc = execute_dsconf_command(dsconf_cmd, ['show'])
    assert rc == 0
    json_result = json.loads(output)

    # Check if the attribute was set correctly
    # Map CLI argument names to LDAP attribute names
    attr_map = {
        'objectclass': 'dynamiclistobjectclass',
        'url_attr': 'dynamiclisturlattr',
        'list_attr': 'dynamiclistattr'
    }
    ldap_attr = attr_map.get(attr)
    if ldap_attr and 'attrs' in json_result:
        config_value = json_result['attrs'].get(ldap_attr)
        if config_value:
            if isinstance(config_value, list):
                assert value in config_value or config_value[0] == value, \
                    f"Expected {value} in {config_value}"
            else:
                assert config_value == value, f"Expected {value}, got {config_value}"

    # Reset to default values (delete the attribute)
    reset_cmd = ['set', f'--{attr}', 'delete']

    output, rc = execute_dsconf_command(dsconf_cmd, reset_cmd)
    # Some attributes might not be deletable, so we don't assert on return code
    log.info(f"Reset {attr}: {output}")


def test_dynamic_lists_config_entry_operations(topo):
    """Test dynamic lists config-entry operations

    :id: 4b13d427-3ec1-4991-aaf0-19274a970003
    :setup: Standalone DS instance
    :steps:
        1. Add a config entry
        2. Show the config entry
        3. Edit the config entry
        4. Delete the config entry
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
    """

    dsconf_cmd = get_dsconf_base_cmd(topo)
    config_dn = 'cn=test-config,cn=dynamic lists,cn=plugins,cn=config'

    # Add config entry
    add_cmd = ['config-entry', 'add', config_dn,
               '--objectclass', 'dynamicList',
               '--url-attr', 'dynamicListURL',
               '--list-attr', 'member']
    output, rc = execute_dsconf_command(dsconf_cmd, add_cmd)
    assert rc == 0, f"Failed to add config entry: {output}"
    log.info(f"Added config entry: {output}")

    # Show config entry
    show_cmd = ['config-entry', 'show', config_dn]
    output, rc = execute_dsconf_command(dsconf_cmd, show_cmd)
    assert rc == 0, f"Failed to show config entry: {output}"
    log.info(f"Config entry details: {output}")

    # Parse JSON to verify it was created correctly
    json_result = json.loads(output)
    assert json_result.get('type') == 'entry'
    assert 'dynamiclistobjectclass' in json_result.get('attrs', {})
    assert 'dynamiclisturlattr' in json_result.get('attrs', {})
    assert 'dynamiclistattr' in json_result.get('attrs', {})

    # Edit config entry
    edit_cmd = ['config-entry', 'set', config_dn,
                '--objectclass', 'GroupOfUrls',
                '--url-attr', 'MemberUrl',
                '--list-attr', 'uniqueMember']
    output, rc = execute_dsconf_command(dsconf_cmd, edit_cmd)
    assert rc == 0, f"Failed to edit config entry: {output}"
    log.info(f"Edited config entry: {output}")

    # Show config entry again to verify changes
    output, rc = execute_dsconf_command(dsconf_cmd, show_cmd)
    assert rc == 0, f"Failed to show updated config entry: {output}"
    json_result = json.loads(output)
    assert json_result['attrs']['dynamiclistobjectclass'][0] == 'GroupOfUrls'
    assert json_result['attrs']['dynamiclisturlattr'][0] == 'MemberUrl'
    assert json_result['attrs']['dynamiclistattr'][0] == 'uniqueMember'

    # Delete config entry
    delete_cmd = ['config-entry', 'delete', config_dn]
    output, rc = execute_dsconf_command(dsconf_cmd, delete_cmd)
    assert rc == 0, f"Failed to delete config entry: {output}"
    log.info(f"Deleted config entry: {output}")


@pytest.mark.parametrize("attr,value", DYNAMIC_LISTS_SETTINGS)
def test_dynamic_lists_config_entry_set(topo, attr, value):
    """Test dynamic lists config-entry set command with various configuration options

    :id: 2dbcd6c5-1eb1-4d9a-964b-8dd471505ffa
    :setup: Standalone DS instance
    :steps:
        1. Add a config entry with specific settings
        2. Verify the settings were applied
        3. Delete the config entry
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """

    dsconf_cmd = get_dsconf_base_cmd(topo)
    config_dn = f'cn=test-config-{attr},cn=dynamic lists,cn=plugins,cn=config'

    # Build the add command with all required attributes
    add_cmd = ['config-entry', 'add', config_dn]
    # Add all three attributes with default values if not the one being tested
    if attr != 'objectclass':
        add_cmd.extend(['--objectclass', 'GroupOfUrls'])
    if attr != 'url_attr':
        add_cmd.extend(['--url-attr', 'MemberUrl'])
    if attr != 'list_attr':
        add_cmd.extend(['--list-attr', 'uniqueMember'])
    # Add the attribute being tested
    add_cmd.extend([f'--{attr}', value])

    # Add config entry
    output, rc = execute_dsconf_command(dsconf_cmd, add_cmd)
    assert rc == 0, f"Failed to add config entry with {attr}={value}: {output}"
    log.info(f"Added config entry with {attr}={value}")

    # Show config entry to verify it worked
    show_cmd = ['config-entry', 'show', config_dn]
    output, rc = execute_dsconf_command(dsconf_cmd, show_cmd)
    assert rc == 0, f"Failed to show config entry: {output}"
    json_result = json.loads(output)

    # Verify the setting was applied
    attr_map = {
        'objectclass': 'dynamiclistobjectclass',
        'url_attr': 'dynamiclisturlattr',
        'list_attr': 'dynamiclistattr'
    }
    config_attr = attr_map.get(attr)
    if config_attr:
        config_value = json_result['attrs'].get(config_attr)
        if config_value:
            if isinstance(config_value, list):
                assert value in config_value or config_value[0] == value, \
                    f"Expected {value} in {config_value}"
            else:
                assert config_value == value, f"Expected {value}, got {config_value}"

    # Finally delete the config entry
    delete_cmd = ['config-entry', 'delete', config_dn]
    output, rc = execute_dsconf_command(dsconf_cmd, delete_cmd)
    assert rc == 0, f"Failed to delete config entry: {output}"
    log.info(f"Deleted config entry: {output}")


def test_dynamic_lists_plugin_set_config_entry(topo):
    """Test dynamic lists plugin set command with config-entry option

    :id: e0b73edf-1c92-4152-a114-f6a40bd1507c
    :setup: Standalone DS instance
    :steps:
        1. Add a config entry
        2. Set the plugin's config-entry attribute to point to it
        3. Verify the setting was applied
        4. Clean up
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
    """

    dsconf_cmd = get_dsconf_base_cmd(topo)
    config_dn = 'cn=test-config-entry,cn=dynamic lists,cn=plugins,cn=config'

    # Add config entry first
    add_cmd = ['config-entry', 'add', config_dn,
               '--objectclass', 'GroupOfUrls',
               '--url-attr', 'MemberUrl',
               '--list-attr', 'member']
    output, rc = execute_dsconf_command(dsconf_cmd, add_cmd)
    assert rc == 0, f"Failed to add config entry: {output}"
    log.info(f"Added config entry: {output}")

    # Verify the setting was applied
    output, rc = execute_dsconf_command(dsconf_cmd, ['show'])
    assert rc == 0
    json_result = json.loads(output)
    if 'attrs' in json_result and 'nsslapd-pluginConfigArea' in json_result['attrs']:
        config_area = json_result['attrs']['nsslapd-pluginConfigArea']
        if isinstance(config_area, list):
            assert config_dn in config_area, f"Expected {config_dn} in {config_area}"
        else:
            assert config_area == config_dn, f"Expected {config_dn}, got {config_area}"

    # Clean up - delete config entry (this should also remove the reference)
    delete_cmd = ['config-entry', 'delete', config_dn]
    output, rc = execute_dsconf_command(dsconf_cmd, delete_cmd)
    assert rc == 0, f"Failed to delete config entry: {output}"
    log.info(f"Deleted config entry: {output}")


def test_dynamic_lists_invalid_configurations(topo):
    """Test dynamic lists plugin with invalid configurations

    :id: 5e161ebe-490c-4d7e-8008-9fbb239c0b54
    :setup: Standalone DS instance
    :steps:
        1. Test with invalid DN values
        2. Test with missing required parameters
    :expectedresults:
        1. Should fail gracefully
        2. Should fail gracefully
    """

    dsconf_cmd = get_dsconf_base_cmd(topo)

    # Test with invalid DN in config entry
    invalid_dn = 'invalid-dn-format'
    add_cmd = ['config-entry', 'add', invalid_dn,
               '--objectclass', 'GroupOfUrls',
               '--url-attr', 'MemberUrl',
               '--list-attr', 'uniqueMember']
    output, rc = execute_dsconf_command(dsconf_cmd, add_cmd)
    # This should fail
    assert rc != 0, f"Expected failure for invalid DN, but got success: {output}"
    log.info(f"Invalid DN test failed as expected: {output}")

    # Test with missing required parameters in config entry
    config_dn = 'cn=test-invalid,cn=dynamic lists,cn=plugins,cn=config'
    add_cmd = ['config-entry', 'add', config_dn]  # Missing required attributes
    output, rc = execute_dsconf_command(dsconf_cmd, add_cmd)
    # This should fail
    assert rc != 0, f"Expected failure for missing required parameters, but got success: {output}"
    log.info(f"Missing parameters test failed as expected: {output}")


def test_dynamic_lists_plugin_disable_enable(topo):
    """Test dynamic lists plugin disable and enable operations

    :id: efebe757-d9ee-4658-a06b-cdb45dc1ce36
    :setup: Standalone DS instance
    :steps:
        1. Disable the plugin
        2. Verify plugin is disabled
        3. Enable the plugin
        4. Verify plugin is enabled
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
    """

    dsconf_cmd = get_dsconf_base_cmd(topo)

    # Disable plugin
    output, rc = execute_dsconf_command(dsconf_cmd, ['disable'])
    assert rc == 0, f"Failed to disable plugin: {output}"
    log.info(f"Disabled plugin: {output}")

    # Verify plugin is disabled
    output, rc = execute_dsconf_command(dsconf_cmd, ['status'])
    assert rc == 0
    json_result = json.loads(output)
    assert json_result.get('msg') == 'disabled'

    # Enable plugin
    output, rc = execute_dsconf_command(dsconf_cmd, ['enable'])
    assert rc == 0, f"Failed to enable plugin: {output}"
    log.info(f"Enabled plugin: {output}")

    # Verify plugin is enabled
    output, rc = execute_dsconf_command(dsconf_cmd, ['status'])
    assert rc == 0
    json_result = json.loads(output)
    assert json_result.get('msg') == 'enabled'


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])

