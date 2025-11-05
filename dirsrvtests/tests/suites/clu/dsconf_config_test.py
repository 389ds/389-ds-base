# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
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
    'multiple': ['10.0.0.1', '172.16.0.1', '192.168.2.1'],
    'subnet_ipv4': '192.168.1.0/24',
    'subnet_ipv6': '2001:db8::/32',
    'mixed': ['192.168.1.0/24', '10.0.0.1', '2001:db8::/64']
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


def test_haproxy_subnet_ipv4(topology_st):
    """Test adding IPv4 subnet in CIDR notation to nsslapd-haproxy-trusted-ip

    :id: 19456de2-06f6-4c6c-be00-b07f84ba1c18
    :setup: Standalone DS instance
    :steps:
        1. Add an IPv4 subnet in CIDR notation
        2. Verify the subnet was added correctly
    :expectedresults:
        1. Command should execute successfully
        2. Subnet should be present in the configuration
    """
    dsconf_cmd = get_dsconf_base_cmd(topology_st)
    attr_name = 'nsslapd-haproxy-trusted-ip'

    try:
        subnet = HAPROXY_IPS['subnet_ipv4']
        test_attr = f"{attr_name}={subnet}"

        # Add subnet
        output, rc = execute_dsconf_command(dsconf_cmd, ['config', 'add', test_attr])
        assert rc == 0

        # Verify
        output, _ = execute_dsconf_command(dsconf_cmd, ['config', 'get', attr_name])
        assert subnet in output

    finally:
        execute_dsconf_command(dsconf_cmd, ['config', 'delete', attr_name])


def test_haproxy_subnet_ipv6(topology_st):
    """Test adding IPv6 subnet in CIDR notation to nsslapd-haproxy-trusted-ip

    :id: b0b8b546-979c-47a2-bc9a-a4c664be8eba
    :setup: Standalone DS instance
    :steps:
        1. Add an IPv6 subnet in CIDR notation
        2. Verify the subnet was added correctly
    :expectedresults:
        1. Command should execute successfully
        2. Subnet should be present in the configuration
    """
    dsconf_cmd = get_dsconf_base_cmd(topology_st)
    attr_name = 'nsslapd-haproxy-trusted-ip'

    try:
        subnet = HAPROXY_IPS['subnet_ipv6']
        test_attr = f"{attr_name}={subnet}"

        # Add subnet
        output, rc = execute_dsconf_command(dsconf_cmd, ['config', 'add', test_attr])
        assert rc == 0

        # Verify
        output, _ = execute_dsconf_command(dsconf_cmd, ['config', 'get', attr_name])
        assert subnet in output

    finally:
        execute_dsconf_command(dsconf_cmd, ['config', 'delete', attr_name])


def test_haproxy_mixed_ips_and_subnets(topology_st):
    """Test adding mixed individual IPs and subnets to nsslapd-haproxy-trusted-ip

    :id: d4f70db4-8f32-4771-95c2-32e6c83838df
    :setup: Standalone DS instance
    :steps:
        1. Add multiple values including both individual IPs and subnets
        2. Verify all values were added correctly
    :expectedresults:
        1. Batch add command should execute successfully
        2. All values should be present in the configuration
    """
    dsconf_cmd = get_dsconf_base_cmd(topology_st)
    attr_name = 'nsslapd-haproxy-trusted-ip'

    try:
        # Add mixed values
        attr_values = [f"{attr_name}={val}" for val in HAPROXY_IPS['mixed']]
        output, rc = execute_dsconf_command(dsconf_cmd, ['config', 'add'] + attr_values)
        assert rc == 0

        # Verify all values
        output, _ = execute_dsconf_command(dsconf_cmd, ['config', 'get', attr_name])
        for value in HAPROXY_IPS['mixed']:
            assert value in output

    finally:
        execute_dsconf_command(dsconf_cmd, ['config', 'delete', attr_name])


def test_haproxy_invalid_cidr_prefix(topology_st):
    """Test that invalid CIDR prefix lengths are rejected

    :id: cfcc9fcc-70e8-49a7-82ac-dcf5e1a0cca3
    :setup: Standalone DS instance
    :steps:
        1. Try to add an IPv4 address with invalid prefix length (>32)
        2. Try to add an IPv6 address with invalid prefix length (>128)
    :expectedresults:
        1. IPv4 command should fail
        2. IPv6 command should fail
    """
    dsconf_cmd = get_dsconf_base_cmd(topology_st)
    attr_name = 'nsslapd-haproxy-trusted-ip'

    try:
        # Test invalid IPv4 prefix
        invalid_ipv4 = f"{attr_name}=192.168.1.0/33"
        output, rc = execute_dsconf_command(dsconf_cmd, ['config', 'add', invalid_ipv4])
        assert rc != 0  # Should fail

        # Test invalid IPv6 prefix
        invalid_ipv6 = f"{attr_name}=2001:db8::/129"
        output, rc = execute_dsconf_command(dsconf_cmd, ['config', 'add', invalid_ipv6])
        assert rc != 0  # Should fail

    finally:
        execute_dsconf_command(dsconf_cmd, ['config', 'delete', attr_name])


def test_haproxy_subnet_persists_after_restart(topology_st):
    """Test subnet configuration persists after server restart

    :id: 7ea0d55c-5345-465e-a33b-46f2e41a90cf
    :setup: Standalone DS instance
    :steps:
        1. Add subnet in CIDR notation
        2. Verify the subnet is present
        3. Restart the server
        4. Verify the subnet persists after restart
    :expectedresults:
        1. Subnet should be added successfully
        2. Subnet should be present before restart
        3. Server should restart successfully
        4. Subnet should still be present after restart
    """
    dsconf_cmd = get_dsconf_base_cmd(topology_st)
    attr_name = 'nsslapd-haproxy-trusted-ip'

    try:
        # Add subnet
        subnet = HAPROXY_IPS['subnet_ipv4']
        output, rc = execute_dsconf_command(dsconf_cmd,
                                          ['config', 'add', f"{attr_name}={subnet}"])
        assert rc == 0

        # Verify before restart
        output, _ = execute_dsconf_command(dsconf_cmd, ['config', 'get', attr_name])
        assert subnet in output

        # Restart the server
        topology_st.standalone.restart(timeout=10)

        # Verify after restart
        output, _ = execute_dsconf_command(dsconf_cmd, ['config', 'get', attr_name])
        assert subnet in output

    finally:
        execute_dsconf_command(dsconf_cmd, ['config', 'delete', attr_name])
