# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import ldap
import logging
import pytest
import os
from lib389._constants import DEFAULT_SUFFIX
from lib389.topologies import topology_st as topo
from lib389.plugins import MemberOfPlugin
from lib389.idm.user import UserAccounts
from lib389.idm.group import Groups


log = logging.getLogger(__name__)


def test_fixup_task_limit(topo):
    """Test only one fixup task is allowed at one time

    :id: 2bb49a10-fca9-4d89-9a7a-34c2ba4baadc
    :setup: Standalone Instance
    :steps:
        1. Add some users and groups
        2. Enable memberOf Plugin
        3. Add fixup task
        4. Add second task
        5. Add a third task after first task completes
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Second task should fail
        5. Success
    """

    # Create group with members
    groups = Groups(topo.standalone, DEFAULT_SUFFIX)
    group = groups.create(properties={'cn': 'test'})

    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    for idx in range(400):
        user = users.create(properties={
            'uid': 'testuser%s' % idx,
            'cn' : 'testuser%s' % idx,
            'sn' : 'user%s' % idx,
            'uidNumber' : '%s' % (1000 + idx),
            'gidNumber' : '%s' % (1000 + idx),
            'homeDirectory' : '/home/testuser%s' % idx
        })
        group.add('member', user.dn)

    # Configure memberOf plugin
    memberof = MemberOfPlugin(topo.standalone)
    memberof.enable()
    topo.standalone.restart()

    # Add first task
    task = memberof.fixup(DEFAULT_SUFFIX)

    # Add second task which should fail
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        memberof.fixup(DEFAULT_SUFFIX)

    # Add second task but on different suffix which should be allowed
    memberof.fixup("ou=people," + DEFAULT_SUFFIX)

    # Wait for first task to complete
    task.wait()

    # Add new task which should be allowed now
    memberof.fixup(DEFAULT_SUFFIX)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])

