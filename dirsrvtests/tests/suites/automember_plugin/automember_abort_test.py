# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
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
from lib389._constants import DEFAULT_SUFFIX
from lib389.plugins import AutoMembershipPlugin, AutoMembershipDefinitions
from lib389.idm.user import UserAccounts
from lib389.idm.group import Groups
from lib389.topologies import topology_st as topo

log = logging.getLogger(__name__)

# maximum number of retries when rebuild task is too fast.
MAX_TRIES = 10

@pytest.fixture(scope="module")
def automember_fixture(topo, request):
    # Create group
    group_obj = Groups(topo.standalone, DEFAULT_SUFFIX)
    automem_group = group_obj.create(properties={'cn': 'testgroup'})

    # Create users
    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX, rdn=None)
    NUM_USERS = 5000
    for num in range(NUM_USERS):
        num_ran = int(round(num))
        USER_NAME = 'test%05d' % num_ran
        users.create(properties={
            'uid': USER_NAME,
            'sn': USER_NAME,
            'cn': USER_NAME,
            'uidNumber': '%s' % num_ran,
            'gidNumber': '%s' % num_ran,
            'homeDirectory': '/home/%s' % USER_NAME,
            'mail': '%s@redhat.com' % USER_NAME,
            'userpassword': 'pass%s' % num_ran,
        })


    # Create automember definitions and regex rules
    automember_prop = {
        'cn': 'testgroup_definition',
        'autoMemberScope': DEFAULT_SUFFIX,
        'autoMemberFilter': 'objectclass=posixaccount',
        'autoMemberDefaultGroup': automem_group.dn,
        'autoMemberGroupingAttr': 'member:dn',
    }
    automembers = AutoMembershipDefinitions(topo.standalone)
    auto_def = automembers.create(properties=automember_prop)
    auto_def.add_regex_rule("regex1", automem_group.dn, include_regex=['uid=.*'])

    # Enable plugin
    automemberplugin = AutoMembershipPlugin(topo.standalone)
    automemberplugin.enable()
    topo.standalone.restart()


def test_abort(automember_fixture, topo):
    """Test the abort rebuild task

    :id: 24763279-48ec-4c34-91b3-f681679dec3a
    :setup: Standalone Instance
    :steps:
        1. Setup automember and create a bunch of users
        2. Start rebuild task
        3. Abort rebuild task
        4. Verify rebuild task was aborted
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
    """

    automemberplugin = AutoMembershipPlugin(topo.standalone)

    # Run rebuild task
    task_exit_code = '0'
    nbtries = 0
    # Loops if previously run task has completed successfully
    while task_exit_code == '0':
        # ensure there is not too many loops.
        assert nbtries < MAX_TRIES
        nbtries += 1
        # Start rebuild task
        task = automemberplugin.fixup(DEFAULT_SUFFIX, "objectclass=top")
        time.sleep(1)
        # Abort rebuild task
        automemberplugin.abort_fixup()
        task_exit_code = task.get_attr_val_utf8('nsTaskExitCode')

    # Wait for task completion
    task.wait()

    # Check errors log for abort message
    log.info('AUTOMEMBER REBUILD TASK: dn=%s %s' % (task.dn, task.get_all_attrs_utf8()))
    assert topo.standalone.searchErrorsLog("task was intentionally aborted")


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])

