# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import pytest
import os
import time
import json
import subprocess
from lib389._constants import DEFAULT_SUFFIX, DN_DM
from lib389.topologies import topology_m2 as topo
from lib389.idm.group import Groups
from lib389.conflicts import ConflictEntries
log = logging.getLogger(__name__)


def execute_dsconf_command(dsconf_cmd, subcommands):
    """Execute dsconf command and return output and return code"""

    cmdline = dsconf_cmd + subcommands
    proc = subprocess.Popen(cmdline, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    out, err = proc.communicate()

    if proc.returncode != 0 and err:
        log.error(f"Command failed: {' '.join(cmdline)}")
        log.error(f"Stderr: {err.decode('utf-8')}")

    return out.decode('utf-8'), proc.returncode


def get_dsconf_base_cmd(inst):
    """Return base dsconf command list"""
    return ['/usr/sbin/dsconf', inst.serverid,
            '-D', DN_DM, '-w', 'password', 'repl-conflict']


def get_conflict_dn(output):
    """Get the conflict DN from the output"""
    return output.split("\n")[0].replace('dn: ', '')


def test_dsconf_repl_coniflict(topo):
    """Test all the dsconf repl-conflict features

    :id: 2ef33fd8-dd5c-4fe6-858e-e8a00e707410
    :setup: 2 Supplier Instances
    :steps:
        1. Create conflict entries
        2. List conflicts
        3. Compare conflict
        4. Delete conflict
        5. Convert conflict
        6. Swap conflict
        7. Delete all conflicts
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
    """

    supplier1 = topo.ms["supplier1"]
    supplier2 = topo.ms["supplier2"]
    group1 = Groups(supplier1, DEFAULT_SUFFIX)
    group2 = Groups(supplier2, DEFAULT_SUFFIX)
    dsconf_cmd = get_dsconf_base_cmd(supplier1)

    # Create conflict entries
    supplier1.stop()
    group2.create(properties={'cn': 'test', 'description': 'value1'})
    group2.create(properties={'cn': 'test2', 'description': 'value1'})
    group2.create(properties={'cn': 'test3', 'description': 'value1'})
    group2.create(properties={'cn': 'test4', 'description': 'value1'})
    group2.create(properties={'cn': 'test5', 'description': 'value1'})
    supplier2.stop()
    supplier1.start()
    group1.create(properties={'cn': 'test', 'description': 'value2'})
    group1.create(properties={'cn': 'test2', 'description': 'value2'})
    group1.create(properties={'cn': 'test3', 'description': 'value2'})
    group1.create(properties={'cn': 'test4', 'description': 'value2'})
    group1.create(properties={'cn': 'test5', 'description': 'value2'})
    supplier2.start()

    time.sleep(2)  # Let replication catch up

    # list conflicts
    conflicts1 = ConflictEntries(supplier1, DEFAULT_SUFFIX)
    assert len(conflicts1.list()) == 5

    output, rc = execute_dsconf_command(dsconf_cmd, ["list", DEFAULT_SUFFIX])
    assert rc == 0
    assert len(output) > 0
    assert output != "There were no conflict entries found under the suffix"
    conflict_dn = get_conflict_dn(output)

    # Compare a conflict
    output, rc = execute_dsconf_command(dsconf_cmd, ["compare", conflict_dn])
    assert rc == 0
    assert len(output) > 0
    assert "Conflict Entry" in output
    assert "Valid Entry" in output

    # Delete a single conflict
    output, rc = execute_dsconf_command(dsconf_cmd, ["delete", conflict_dn])
    assert rc == 0
    assert len(conflicts1.list()) == 4
    output, rc = execute_dsconf_command(dsconf_cmd, ["list", DEFAULT_SUFFIX])
    assert rc == 0
    conflict_dn = get_conflict_dn(output)

    # Convert conflict
    output, rc = execute_dsconf_command(dsconf_cmd, ["convert", conflict_dn,
                                        "--new-rdn=cn=testtest"])
    assert rc == 0
    assert len(conflicts1.list()) == 3
    output, rc = execute_dsconf_command(dsconf_cmd, ["list", DEFAULT_SUFFIX])
    assert rc == 0
    conflict_dn = get_conflict_dn(output)

    # Swap conflict
    output, rc = execute_dsconf_command(dsconf_cmd, ["swap", conflict_dn])
    assert rc == 0
    assert len(conflicts1.list()) == 2

    # Delete all the remaining conflicts
    output, rc = execute_dsconf_command(dsconf_cmd, ["delete-all", DEFAULT_SUFFIX])
    assert rc == 0
    assert "Deleted 2 conflict entries" in output

    # Check conflicts are deleted
    assert len(conflicts1.list()) == 0
    output, rc = execute_dsconf_command(dsconf_cmd, ["list", DEFAULT_SUFFIX])
    assert output.strip() == "There were no conflict entries found under the suffix"


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
