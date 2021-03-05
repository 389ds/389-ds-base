# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_m1
from lib389.tombstone import Tombstones
from lib389.idm.user import UserAccounts, TEST_USER_PROPERTIES

pytestmark = pytest.mark.tier1


def test_purge_success(topology_m1):
    """Verify that tombstones are created successfully

    :id: adb86f50-ae76-4ed6-82b4-3cdc30ccab78
    :setup: Standalone instance
    :steps:
        1. Enable replication to unexisting instance
        2. Add an entry to the replicated suffix
        3. Delete the entry
        4. Check that tombstone entry exists (objectclass=nsTombstone)
    :expectedresults: Tombstone entry exist
        1. Operation should be successful
        2. The entry should be successfully added
        3. The entry should be successfully deleted
        4. Tombstone entry should exist
    """
    m1 = topology_m1.ms['supplier1']

    users = UserAccounts(m1, DEFAULT_SUFFIX)
    user = users.create(properties=TEST_USER_PROPERTIES)

    tombstones = Tombstones(m1, DEFAULT_SUFFIX)

    assert len(tombstones.list()) == 0

    user.delete()

    assert len(tombstones.list()) == 1
    assert len(users.list()) == 0

    ts = tombstones.get('testuser')
    assert ts.exists()

    if not ds_is_older('1.4.0'):
        ts.revive()

        assert len(users.list()) == 1
        user_revived = users.get('testuser')
        

if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
