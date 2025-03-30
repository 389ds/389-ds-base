# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import json
import os
import subprocess
import pytest
from lib389.topologies import topology_st as topo

log = logging.getLogger(__name__)


def execute_dsconf_command(dsconf_cmd, subcommands):
    """Execute dsconf command and return output and return code"""

    cmdline = dsconf_cmd + subcommands
    proc = subprocess.Popen(cmdline, stdout=subprocess.PIPE)
    out, _ = proc.communicate()
    return out.decode('utf-8'), proc.returncode


def get_dsconf_base_cmd(topo):
    """Return base dsconf command list"""
    return ['/usr/sbin/dsconf', topo.standalone.serverid,
            '-j', 'schema']


def test_schema_oc_superior(topo):
    """Specify a test case purpose or name here

    :id: d12aab4a-1436-43eb-802a-0661281a13d0
    :setup: Standalone Instance
    :steps:
        1. List all the schema
        2. List all the schema and include superior OC's attrs
        3. Get objectclass list
        4. Get objectclass list and include superior OC's attrs
        5. Get objectclass
        6. Get objectclass and include superior OC's attrs
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
    """

    dsconf_cmd = get_dsconf_base_cmd(topo)

    # Test default schema list
    output, rc = execute_dsconf_command(dsconf_cmd, ['list'])
    assert rc == 0
    json_result = json.loads(output)
    for schema_item in json_result:
        if 'name' in schema_item and schema_item['name'] == 'inetOrgPerson':
            assert len(schema_item['must']) == 0
            break

    # Test including the OC superior attributes
    output, rc = execute_dsconf_command(dsconf_cmd, ['list',
                                                     '--include-oc-sup'])
    assert rc == 0
    json_result = json.loads(output)
    for schema_item in json_result:
        if 'name' in schema_item and schema_item['name'] == 'inetOrgPerson':
            assert len(schema_item['must']) > 0 and \
                'cn' in schema_item['must'] and 'sn' in schema_item['must']
            break

    # Test default objectclass list
    output, rc = execute_dsconf_command(dsconf_cmd, ['objectclasses', 'list'])
    assert rc == 0
    json_result = json.loads(output)
    for schema_item in json_result:
        if 'name' in schema_item and schema_item['name'] == 'inetOrgPerson':
            assert len(schema_item['must']) == 0
            break

    # Test objectclass list and inslude superior attributes
    output, rc = execute_dsconf_command(dsconf_cmd, ['objectclasses', 'list',
                                                     '--include-sup'])
    assert rc == 0
    json_result = json.loads(output)
    for schema_item in json_result:
        if 'name' in schema_item and schema_item['name'] == 'inetOrgPerson':
            assert len(schema_item['must']) > 0 and \
                'cn' in schema_item['must'] and 'sn' in schema_item['must']
            break

    # Test default objectclass query
    output, rc = execute_dsconf_command(dsconf_cmd, ['objectclasses', 'query',
                                                     'inetOrgPerson'])
    assert rc == 0
    result = json.loads(output)
    schema_item = result['oc']
    assert 'names' in schema_item
    assert schema_item['names'][0] == 'inetOrgPerson'
    assert len(schema_item['must']) == 0

    # Test objectclass query and include superior attributes
    output, rc = execute_dsconf_command(dsconf_cmd, ['objectclasses', 'query',
                                                     'inetOrgPerson',
                                                     '--include-sup'])
    assert rc == 0
    result = json.loads(output)
    schema_item = result['oc']
    assert 'names' in schema_item
    assert schema_item['names'][0] == 'inetOrgPerson'
    assert len(schema_item['must']) > 0 and 'cn' in schema_item['must'] \
        and 'sn' in schema_item['must']


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
