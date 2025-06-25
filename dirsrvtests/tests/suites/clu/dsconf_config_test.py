# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2024 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import subprocess
import logging
import pytest
from lib389._constants import DN_DM
from lib389.topologies import topology_st

pytestmark = pytest.mark.tier1

log = logging.getLogger(__name__)

HAPROXY_IPS = {
    'single': '192.168.1.1',
    'multiple': ['10.0.0.1', '172.16.0.1', '192.168.2.1']
}

REFERRALS = {
    'single': 'ldap://primary.example.com',
    'multiple': [
        'ldap://server1.example.com',
        'ldap://server2.example.com',
        'ldap://server3.example.com'
    ]
}

TEST_ATTRS = [
    ('nsslapd-haproxy-trusted-ip', HAPROXY_IPS),
    ('nsslapd-referral', REFERRALS)
]


def execute_dsconf_command(dsconf_cmd, subcommands):
    """Execute dsconf command and return output and return code"""

    cmdline = dsconf_cmd + subcommands
    proc = subprocess.Popen(cmdline, stdout=subprocess.PIPE)
    out, _ = proc.communicate()
    return out.decode('utf-8'), proc.returncode


def get_dsconf_base_cmd(topology):
    """Return base dsconf command list"""

    return ['/usr/sbin/dsconf', topology.standalone.serverid,
            '-D', DN_DM, '-w', 'password']


@pytest.mark.parametrize("attr_name,values_dict", TEST_ATTRS)
def test_single_value_add(topology_st, attr_name, values_dict):
    """Test adding a single value to an attribute

    :id: ffc912a6-c188-413d-9c35-7f4b3774d946
    :parametrized: yes
    :setup: Standalone DS instance
    :steps:
        1. Add a single value to the specified attribute
        2. Verify the value was added correctly
    :expectedresults:
        1. Command should execute successfully
        2. Added value should be present in the configuration
    """
    dsconf_cmd = get_dsconf_base_cmd(topology_st)

    try:
        value = values_dict['single']
        test_attr = f"{attr_name}={value}"

        # Add single value
        output, rc = execute_dsconf_command(dsconf_cmd, ['config', 'add', test_attr])
        assert rc == 0

        # Verify
        output, _ = execute_dsconf_command(dsconf_cmd, ['config', 'get', attr_name])
        assert value in output

    finally:
        execute_dsconf_command(dsconf_cmd, ['config', 'delete', attr_name])


@pytest.mark.parametrize("attr_name,values_dict", TEST_ATTRS)
def test_single_value_replace(topology_st, attr_name, values_dict):
    """Test replacing a single value in configuration attributes

    :id: 112e3e5e-8db8-4974-9ea4-ed789c2d02f2
    :parametrized: yes
    :setup: Standalone DS instance
    :steps:
        1. Add initial value to the specified attribute
        2. Replace the value with a new one
        3. Verify the replacement was successful
    :expectedresults:
        1. Initial value should be added successfully
        2. Replace command should execute successfully
        3. New value should be present and old value should be absent
    """
    dsconf_cmd = get_dsconf_base_cmd(topology_st)

    try:
        # Add initial value
        value = values_dict['single']
        test_attr = f"{attr_name}={value}"
        execute_dsconf_command(dsconf_cmd, ['config', 'add', test_attr])

        # Replace with new value
        new_value = values_dict['multiple'][0]
        replace_attr = f"{attr_name}={new_value}"
        output, rc = execute_dsconf_command(dsconf_cmd, ['config', 'replace', replace_attr])
        assert rc == 0

        # Verify
        output, _ = execute_dsconf_command(dsconf_cmd, ['config', 'get', attr_name])
        assert new_value in output
        assert value not in output

    finally:
        execute_dsconf_command(dsconf_cmd, ['config', 'delete', attr_name])


@pytest.mark.parametrize("attr_name,values_dict", TEST_ATTRS)
def test_multi_value_batch_add(topology_st, attr_name, values_dict):
    """Test adding multiple values in a single batch command

    :id: 4c34c7f8-16cc-4ab6-938a-967537be5470
    :parametrized: yes
    :setup: Standalone DS instance
    :steps:
        1. Add multiple values to the attribute in a single command
        2. Verify all values were added correctly
    :expectedresults:
        1. Batch add command should execute successfully
        2. All added values should be present in the configuration
    """
    dsconf_cmd = get_dsconf_base_cmd(topology_st)

    try:
        # Add multiple values in one command
        attr_values = [f"{attr_name}={val}" for val in values_dict['multiple']]
        output, rc = execute_dsconf_command(dsconf_cmd, ['config', 'add'] + attr_values)
        assert rc == 0

        # Verify all values
        output, _ = execute_dsconf_command(dsconf_cmd, ['config', 'get', attr_name])
        for value in values_dict['multiple']:
            assert value in output

    finally:
        execute_dsconf_command(dsconf_cmd, ['config', 'delete', attr_name])


@pytest.mark.parametrize("attr_name,values_dict", TEST_ATTRS)
def test_multi_value_batch_replace(topology_st, attr_name, values_dict):
    """Test replacing with multiple values in a single batch command

    :id: 05cf28b8-000e-4856-a10b-7e1df012737d
    :parametrized: yes
    :setup: Standalone DS instance
    :steps:
        1. Add initial single value
        2. Replace with multiple values in a single command
        3. Verify the replacement was successful
    :expectedresults:
        1. Initial value should be added successfully
        2. Batch replace command should execute successfully
        3. All new values should be present and initial value should be absent
    """
    dsconf_cmd = get_dsconf_base_cmd(topology_st)

    try:
        # Add initial value
        initial_value = values_dict['single']
        execute_dsconf_command(dsconf_cmd, ['config', 'add', f"{attr_name}={initial_value}"])

        # Replace with multiple values
        attr_values = [f"{attr_name}={val}" for val in values_dict['multiple']]
        output, rc = execute_dsconf_command(dsconf_cmd, ['config', 'replace'] + attr_values)
        assert rc == 0

        # Verify
        output, _ = execute_dsconf_command(dsconf_cmd, ['config', 'get', attr_name])
        assert initial_value not in output
        for value in values_dict['multiple']:
            assert value in output

    finally:
        execute_dsconf_command(dsconf_cmd, ['config', 'delete', attr_name])


@pytest.mark.parametrize("attr_name,values_dict", TEST_ATTRS)
def test_multi_value_specific_delete(topology_st, attr_name, values_dict):
    """Test deleting specific values from multi-valued attribute

    :id: bb325c9a-eae8-438a-b577-bd63540b91cb
    :parametrized: yes
    :setup: Standalone DS instance
    :steps:
        1. Add multiple values to the attribute
        2. Delete a specific value
        3. Verify the deletion was successful
    :expectedresults:
        1. Multiple values should be added successfully
        2. Specific delete command should execute successfully
        3. Deleted value should be absent while others remain present
    """
    dsconf_cmd = get_dsconf_base_cmd(topology_st)

    try:
        # Add multiple values
        attr_values = [f"{attr_name}={val}" for val in values_dict['multiple']]
        execute_dsconf_command(dsconf_cmd, ['config', 'add'] + attr_values)

        # Delete middle value
        delete_value = values_dict['multiple'][1]
        output, rc = execute_dsconf_command(dsconf_cmd,
                                          ['config', 'delete', f"{attr_name}={delete_value}"])
        assert rc == 0

        # Verify remaining values
        output, _ = execute_dsconf_command(dsconf_cmd, ['config', 'get', attr_name])
        assert delete_value not in output
        assert values_dict['multiple'][0] in output
        assert values_dict['multiple'][2] in output

    finally:
        execute_dsconf_command(dsconf_cmd, ['config', 'delete', attr_name])


@pytest.mark.parametrize("attr_name,values_dict", TEST_ATTRS)
def test_multi_value_batch_delete(topology_st, attr_name, values_dict):
    """Test deleting multiple values in a single batch command

    :id: 4b105824-b060-4f83-97d7-001a01dba1a5
    :parametrized: yes
    :setup: Standalone DS instance
    :steps:
        1. Add multiple values to the attribute
        2. Delete multiple values in a single command
        3. Verify the batch deletion was successful
    :expectedresults:
        1. Multiple values should be added successfully
        2. Batch delete command should execute successfully
        3. Deleted values should be absent while others remain present
    """
    dsconf_cmd = get_dsconf_base_cmd(topology_st)

    try:
        # Add all values
        attr_values = [f"{attr_name}={val}" for val in values_dict['multiple']]
        execute_dsconf_command(dsconf_cmd, ['config', 'add'] + attr_values)

        # Delete multiple values in one command
        delete_values = [f"{attr_name}={val}" for val in values_dict['multiple'][:2]]
        output, rc = execute_dsconf_command(dsconf_cmd, ['config', 'delete'] + delete_values)
        assert rc == 0

        # Verify
        output, _ = execute_dsconf_command(dsconf_cmd, ['config', 'get', attr_name])
        assert values_dict['multiple'][2] in output
        assert values_dict['multiple'][0] not in output
        assert values_dict['multiple'][1] not in output

    finally:
        execute_dsconf_command(dsconf_cmd, ['config', 'delete', attr_name])


@pytest.mark.parametrize("attr_name,values_dict", TEST_ATTRS)
def test_single_value_persists_after_restart(topology_st, attr_name, values_dict):
    """Test single value persists after server restart

    :id: be1a7e3d-a9ca-48a1-a3bc-062990d4f3e9
    :parametrized: yes
    :setup: Standalone DS instance
    :steps:
        1. Add single value to the attribute
        2. Verify the value is present
        3. Restart the server
        4. Verify the value persists after restart
    :expectedresults:
        1. Value should be added successfully
        2. Value should be present before restart
        3. Server should restart successfully
        4. Value should still be present after restart
    """
    dsconf_cmd = get_dsconf_base_cmd(topology_st)

    try:
        # Add single value
        value = values_dict['single']
        output, rc = execute_dsconf_command(dsconf_cmd,
                                          ['config', 'add', f"{attr_name}={value}"])
        assert rc == 0

        # Verify before restart
        output, _ = execute_dsconf_command(dsconf_cmd, ['config', 'get', attr_name])
        assert value in output

        # Restart the server
        topology_st.standalone.restart(timeout=10)

        # Verify after restart
        output, _ = execute_dsconf_command(dsconf_cmd, ['config', 'get', attr_name])
        assert value in output

    finally:
        execute_dsconf_command(dsconf_cmd, ['config', 'delete', attr_name])


@pytest.mark.parametrize("attr_name,values_dict", TEST_ATTRS)
def test_multi_value_batch_persists_after_restart(topology_st, attr_name, values_dict):
    """Test multiple values added in batch persist after server restart

    :id: fd0435e2-90b1-465a-8968-d3a375c8fb22
    :parametrized: yes
    :setup: Standalone DS instance
    :steps:
        1. Add multiple values in a single batch command
        2. Verify all values are present
        3. Restart the server
        4. Verify all values persist after restart
    :expectedresults:
        1. Batch add command should execute successfully
        2. All values should be present before restart
        3. Server should restart successfully
        4. All values should still be present after restart
    """
    dsconf_cmd = get_dsconf_base_cmd(topology_st)

    try:
        # Add multiple values
        attr_values = [f"{attr_name}={val}" for val in values_dict['multiple']]
        output, rc = execute_dsconf_command(dsconf_cmd, ['config', 'add'] + attr_values)
        assert rc == 0

        # Verify before restart
        output, _ = execute_dsconf_command(dsconf_cmd, ['config', 'get', attr_name])
        for value in values_dict['multiple']:
            assert value in output

        # Restart the server
        topology_st.standalone.restart(timeout=10)

        # Verify after restart
        output, _ = execute_dsconf_command(dsconf_cmd, ['config', 'get', attr_name])
        for value in values_dict['multiple']:
            assert value in output

    finally:
        execute_dsconf_command(dsconf_cmd, ['config', 'delete', attr_name])
