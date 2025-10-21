# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
"""Test dsconf CLI with MemberOf plugin configuration"""

import json
import subprocess
import logging
import pytest
from lib389._constants import DN_DM
from lib389.topologies import topology_st as topo

pytestmark = pytest.mark.tier1

log = logging.getLogger(__name__)

# MemberOf plugin configuration options to test
MEMBEROF_SETTINGS = [
    # ('attr', 'memberOf'), Already set by default
    ('groupattr', ['member', 'uniquemember']),
    ('allbackends', 'on'),
    ('allbackends', 'off'),
    ('skipnested', 'on'),
    ('skipnested', 'off'),
    ('scope', ['dc=example,dc=com', 'ou=groups,dc=example,dc=com']),
    ('exclude', ['ou=excluded,dc=example,dc=com']),
    ('autoaddoc', 'inetuser'),
    ('deferredupdate', 'on'),
    ('deferredupdate', 'off'),
    ('launchfixup', 'on'),
    ('launchfixup', 'off'),
    ('specific-group-oc', 'groupofnames'),
]

# Config entry specific settings
CONFIG_ENTRY_SETTINGS = [
    ('attr', 'memberOf'),
    ('groupattr', ['member', 'uniquemember']),
    ('allbackends', 'on'),
    ('skipnested', 'on'),
    ('scope', ['dc=example,dc=com']),
    ('exclude', ['ou=excluded,dc=example,dc=com']),
    ('autoaddoc', 'inetuser'),
    ('specific-group-oc', 'groupofnames'),
]

MEMBEROF_ATTR_SETTINGS = [
    ('specific-group-filter', '(entrydn=cn=group1,ou=groups,dc=example,dc=com)'),
    ('exclude-specific-group-filter', '(entrydn=cn=excluded,ou=groups,dc=example,dc=com)'),
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
            '-j', '-D', DN_DM, '-w', 'password', 'plugin', 'memberof']


def test_memberof_plugin_status(topo):
    """Test memberof plugin status and basic operations

    :id: memberof-status-test
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


def test_memberof_plugin_get(topo):
    """Test memberof plugin get command

    :id: memberof-get-test
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


@pytest.mark.parametrize("attr,value", MEMBEROF_SETTINGS)
def test_memberof_plugin_set(topo, attr, value):
    """Test memberof plugin set command with various configuration options

    :id: memberof-set-test
    :setup: Standalone DS instance
    :steps:
        1. Set various memberof plugin configuration options
        2. Verify the setting was applied
        3. Reset to default values
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """

    dsconf_cmd = get_dsconf_base_cmd(topo)

    # Build the set command
    set_cmd = ['set', f'--{attr}']
    if isinstance(value, list):
        set_cmd.extend(value)
    else:
        set_cmd.append(value)

    # Set the configuration
    output, rc = execute_dsconf_command(dsconf_cmd, set_cmd)
    assert rc == 0, f"Failed to set {attr}={value}: {output}"
    log.info(f"Set {attr}={value} successfully")

    # Verify the setting was applied by getting the configuration
    output, rc = execute_dsconf_command(dsconf_cmd, ['show'])
    assert rc == 0
    json_result = json.loads(output)

    # Check if the attribute was set correctly
    if attr == 'groupattr' and isinstance(value, list):
        # For multi-valued attributes, check if all values are present
        config_value = json_result['attrs']['memberofgroupattr']
        if isinstance(config_value, str):
            config_value = [config_value]
        for val in value:
            assert val in config_value, f"Expected {val} in {config_value}"
    elif attr in ['scope', 'exclude'] and isinstance(value, list):
        # For multi-valued attributes
        config_attr = f'memberof{attr.title().replace("-", "")}'
        if attr == 'scope':
            config_attr = 'memberofentryscope'
        elif attr == 'exclude':
            config_attr = 'memberofentryscopeexcludesubtree'

        config_value = json_result['attrs'][config_attr]
        if isinstance(config_value, str):
            config_value = [config_value]
        for val in value:
            assert val in config_value, f"Expected {val} in {config_value}"
    else:
        # For single-valued attributes
        config_attr = f'memberof{attr.title().replace("-", "")}'
        if attr == 'attr':
            config_attr = 'memberofattr'
        elif attr == 'groupattr':
            config_attr = 'memberofgroupattr'
        elif attr == 'allbackends':
            config_attr = 'memberofallbackends'
        elif attr == 'skipnested':
            config_attr = 'memberofskipnested'
        elif attr == 'autoaddoc':
            config_attr = 'memberofautoaddoc'
        elif attr == 'deferredupdate':
            config_attr = 'memberofdeferredupdate'
        elif attr == 'launchfixup':
            config_attr = 'memberoflaunchfixup'
        elif attr == 'specific-group-oc':
            config_attr = 'memberofspecificgroupoc'

        if config_attr not in json_result['attrs']:
            config_value = ""
        else:
            config_value = json_result['attrs'][config_attr][0].lower()
        if attr in ['allbackends', 'skipnested', 'deferredupdate', 'launchfixup']:
            expected_value = 'on' if value == 'on' else 'off'
            assert config_value == expected_value, f"Expected {expected_value}, got {config_value}"
        else:
            assert config_value == value.lower(), f"Expected {value}, got {config_value}"

    # Reset to default values (delete the attribute)
    reset_cmd = ['set', f'--{attr}', 'delete']

    output, rc = execute_dsconf_command(dsconf_cmd, reset_cmd)
    # Some attributes might not be deletable, so we don't assert on return code
    log.info(f"Reset {attr}: {output}")


@pytest.mark.parametrize("attr,value", MEMBEROF_ATTR_SETTINGS)
def test_memberof_plugin_add_del_attr(topo, attr, value):
    """Test memberof plugin add-attr/del-attr command with various
    configuration options

    :id: 8b3a2d38-e8ec-4b1e-8d45-ba86fbf0360c
    :setup: Standalone DS instance
    :steps:
        1. Add various memberof plugin attributes
    :expectedresults:
        1. Success
    """
    dsconf_cmd = get_dsconf_base_cmd(topo)

    # Build the set command
    add_cmd = ['add-attr', f'--{attr}', value]

    # Set the configuration
    output, rc = execute_dsconf_command(dsconf_cmd, add_cmd)
    assert rc == 0, f"Failed to add {attr}={value}: {output}"
    log.info(f"Set {attr}={value} successfully")

    # Verify the setting was applied by getting the configuration
    output, rc = execute_dsconf_command(dsconf_cmd, ['show'])
    assert rc == 0
    json_result = json.loads(output)
    config_attr = f'memberof{attr.replace("-", "")}'
    config_value = json_result['attrs'][config_attr][0]
    assert config_value == value, f"Expected {value} in {config_value}"

    # Now delete the attributes
    delete_cmd = ['del-attr', f'--{attr}', value]
    output, rc = execute_dsconf_command(dsconf_cmd, delete_cmd)
    assert rc == 0, f"Failed to delete {attr}={value}: {output}"
    log.info(f"Deleted {attr}={value} successfully")

    # Verify the setting was applied by getting the configuration
    output, rc = execute_dsconf_command(dsconf_cmd, ['show'])
    assert rc == 0
    json_result = json.loads(output)
    config_dict= json_result['attrs']
    assert config_attr not in config_dict, f"Expected {config_attr} not to be present"


def test_memberof_config_entry_operations(topo):
    """Test memberof config-entry operations

    :id: memberof-config-entry-test
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
    config_dn = 'cn=test-config,cn=memberof plugin,cn=plugins,cn=config'

    # Add config entry
    add_cmd = ['config-entry', 'add', config_dn, '--attr', 'memberOf', '--groupattr', 'member']
    output, rc = execute_dsconf_command(dsconf_cmd, add_cmd)
    assert rc == 0, f"Failed to add config entry: {output}"
    log.info(f"Added config entry: {output}")

    # Show config entry
    show_cmd = ['config-entry', 'show', config_dn]
    output, rc = execute_dsconf_command(dsconf_cmd, show_cmd)
    assert rc == 0, f"Failed to show config entry: {output}"
    log.info(f"Config entry details: {output}")

    # Edit config entry
    edit_cmd = ['config-entry', 'set', config_dn, '--attr', 'memberOf', '--groupattr', 'uniqueMember']
    output, rc = execute_dsconf_command(dsconf_cmd, edit_cmd)
    assert rc == 0, f"Failed to edit config entry: {output}"
    log.info(f"Edited config entry: {output}")

    # Show config entry again to verify changes
    output, rc = execute_dsconf_command(dsconf_cmd, show_cmd)
    assert rc == 0, f"Failed to show updated config entry: {output}"
    json_result = json.loads(output)
    assert json_result['attrs']['memberofgroupattr'][0] == 'uniqueMember'

    # Delete config entry
    delete_cmd = ['config-entry', 'delete', config_dn]
    output, rc = execute_dsconf_command(dsconf_cmd, delete_cmd)
    assert rc == 0, f"Failed to delete config entry: {output}"
    log.info(f"Deleted config entry: {output}")


@pytest.mark.parametrize("attr,value", CONFIG_ENTRY_SETTINGS)
def test_memberof_config_entry_set(topo, attr, value):
    """Test memberof config-entry set command with various configuration options

    :id: memberof-config-entry-set-test
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
    config_dn = f'cn=test-config-{attr},cn=memberof plugin,cn=plugins,cn=config'

    # Build the add command
    add_cmd = ['config-entry', 'add', config_dn, '--attr', 'memberOf']
    if attr != 'groupattr':
        add_cmd.extend(['--groupattr', 'memberof'] )

    if isinstance(value, list):
        add_cmd.extend([f'--{attr}'] + value)
    else:
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
    if attr == 'groupattr':
        config_attr = 'memberofgroupattr'
    else:
        config_attr = f'memberof{attr.replace("-", "")}'
        if attr == 'scope':
            config_attr = 'memberofentryscope'
        elif attr == 'exclude':
            config_attr = 'memberofentryscopeexcludesubtree'

    config_value = json_result['attrs'][config_attr]
    if isinstance(value, list):
        if isinstance(config_value, str):
            config_value = [config_value]
        for val in value:
            assert val in config_value, f"Expected {val} in {config_value}"
    else:
        assert config_value[0] == value, f"Expected {value}, got {config_value}"

    #
    # Finally delete the config entry
    #
    delete_cmd = ['config-entry', 'delete', config_dn]
    output, rc = execute_dsconf_command(dsconf_cmd, delete_cmd)
    assert rc == 0, f"Failed to delete config entry: {output}"
    log.info(f"Deleted config entry: {output}")



@pytest.mark.parametrize("attr,value", MEMBEROF_ATTR_SETTINGS)
def test_memberof_plugin_config_add_del_attr(topo, attr, value):
    """Test memberof plugin config entry add-attr/del-attr command with various
    configuration options

    :id: fa27012e-624e-43cd-8b18-5accd0f090d4
    :setup: Standalone DS instance
    :steps:
        1. Add various memberof plugin attributes
    :expectedresults:
        1. Success
    """

    #
    # Add config entry
    #
    dsconf_cmd = get_dsconf_base_cmd(topo)
    config_dn = f'cn=test-config-add-del-{attr},cn=memberof plugin,cn=plugins,cn=config'
    add_cmd = ['config-entry', 'add', config_dn, '--attr', 'memberOf']
    if attr != 'groupattr':
        add_cmd.extend(['--groupattr', 'memberof'] )
    output, rc = execute_dsconf_command(dsconf_cmd, add_cmd)
    assert rc == 0, f"Failed to add config entry with {attr}={value}: {output}"
    log.info(f"Added config entry with {attr}={value}")

    # Show config entry to verify it was added
    show_cmd = ['config-entry', 'show', config_dn]
    output, rc = execute_dsconf_command(dsconf_cmd, show_cmd)
    assert rc == 0, f"Failed to show config entry: {output}"
    json_result = json.loads(output)

    #
    # Add the attribute to the config entry
    #
    config_attr = f'memberof{attr.replace("-", "")}'
    set_cmd = ['config-entry', 'add-attr', config_dn, f'--{attr}', value]
    output, rc = execute_dsconf_command(dsconf_cmd, set_cmd)
    assert rc == 0, f"Failed to set {attr}={value}: {output}"
    log.info(f"Set {attr}={value} successfully")

    # Verify the setting was applied by getting the configuration
    output, rc = execute_dsconf_command(dsconf_cmd, ['show'])
    assert rc == 0
    json_result = json.loads(output)
    assert attr not in json_result['attrs'], f"Unexpectedly found {attr} in config entry"

    #
    # Now delete the attribute from the config entry
    #
    delete_cmd = ['config-entry', 'del-attr', config_dn, f'--{attr}', value]
    output, rc = execute_dsconf_command(dsconf_cmd, delete_cmd)
    assert rc == 0, f"Failed to delete {attr}={value}: {output}"
    log.info(f"Deleted {attr}={value} successfully")

    # Verify the setting was applied by getting the configuration
    output, rc = execute_dsconf_command(dsconf_cmd, ['show'])
    assert rc == 0
    json_result = json.loads(output)
    config_dict= json_result['attrs']
    assert config_attr not in config_dict, f"Expected {config_attr} not to be present"

    #
    # Finally delete the config entry
    #
    delete_cmd = ['config-entry', 'delete', config_dn]
    output, rc = execute_dsconf_command(dsconf_cmd, delete_cmd)
    assert rc == 0, f"Failed to delete config entry: {output}"
    log.info(f"Deleted config entry: {output}")


def test_memberof_fixup_operations(topo):
    """Test memberof fixup operations

    :id: memberof-fixup-test
    :setup: Standalone DS instance
    :steps:
        1. Test fixup command with a base DN
        2. Test fixup-status command
    :expectedresults:
        1. Success
        2. Success
    """

    dsconf_cmd = get_dsconf_base_cmd(topo)
    base_dn = 'dc=example,dc=com'

    # Test fixup command (this will create a task entry)
    fixup_cmd = ['fixup', base_dn]
    output, rc = execute_dsconf_command(dsconf_cmd, fixup_cmd)
    # Fixup might fail if the base DN doesn't exist, which is expected
    log.info(f"Fixup command result: {output}")

    # Test fixup-status command
    status_cmd = ['fixup-status']
    output, rc = execute_dsconf_command(dsconf_cmd, status_cmd)
    # Status command should work even if no tasks exist
    log.info(f"Fixup status: {output}")


def test_memberof_invalid_configurations(topo):
    """Test memberof plugin with invalid configurations

    :id: memberof-invalid-config-test
    :setup: Standalone DS instance
    :steps:
        1. Test with invalid attribute values
        2. Test with invalid DN values
        3. Test with missing required parameters
    :expectedresults:
        1. Should fail gracefully
        2. Should fail gracefully
        3. Should fail gracefully
    """

    dsconf_cmd = get_dsconf_base_cmd(topo)

    # Test with invalid attribute value
    output, rc = execute_dsconf_command(dsconf_cmd, ['set', '--attr', ''])
    # This might succeed or fail depending on validation
    log.info(f"Empty attr test: {output}")

    # Test with invalid DN in config entry
    invalid_dn = 'invalid-dn-format'
    add_cmd = ['config-entry', 'add', invalid_dn, '--attr', 'memberOf']
    output, rc = execute_dsconf_command(dsconf_cmd, add_cmd)
    # This should fail
    assert rc != 0, f"Expected failure for invalid DN, but got success: {output}"
    log.info(f"Invalid DN test failed as expected: {output}")

    # Test with missing required parameters in config entry
    config_dn = 'cn=test-invalid,cn=memberof config,cn=plugins,cn=config'
    add_cmd = ['config-entry', 'add', config_dn]  # Missing --attr
    output, rc = execute_dsconf_command(dsconf_cmd, add_cmd)
    # This should fail
    assert rc != 0, f"Expected failure for missing required parameters, but got success: {output}"
    log.info(f"Missing parameters test failed as expected: {output}")


def test_memberof_plugin_disable_enable(topo):
    """Test memberof plugin disable and enable operations

    :id: memberof-disable-enable-test
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
