# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import pytest
import os
import time
from lib389._constants import (
    DN_CONFIG_LDBM, DEFAULT_SUFFIX, DEFAULT_BENAME,
    REPLICA_PRECISE_PURGING, REPLICA_PURGE_DELAY, REPLICA_PURGE_INTERVAL)
from lib389.topologies import topology_m1 as topo
from lib389.dseldif import DSEldif
from lib389.tombstone import Tombstones
from lib389.idm.user import UserAccounts, TEST_USER_PROPERTIES

pytestmark = pytest.mark.tier1

def test_import_with_moddn_off(topo):
    """Test that imports and replication work after switching to entrydn index

    :id: c92d45cd-1a76-483b-a386-7ad0445f3c37
    :setup: 2 Supplier Instances
    :steps:
        1. Create tombstone entry
        2. Export db to ldif
        3. Edit dse.ldif to disable subtree rename
        4. Import ldif
        5. Create new tombstone
        6. Test tombstone purging
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
    """

    m1 = topo.ms['supplier1']

    users = UserAccounts(m1, DEFAULT_SUFFIX)
    user = users.create(properties=TEST_USER_PROPERTIES)
    tombstones = Tombstones(m1, DEFAULT_SUFFIX)
    assert len(tombstones.list()) == 0

    # Create tombstone
    user.delete()
    assert len(tombstones.list()) == 1
    assert len(users.list()) == 0

    ts = tombstones.get('testuser')
    assert ts.exists()

    # Stop server and export db
    m1.stop()
    ldif_dir = m1.get_ldif_dir()
    ldif_file = ldif_dir + '/test.ldif'
    assert m1.db2ldif(DEFAULT_BENAME, (DEFAULT_SUFFIX,),
                      None, None, True, ldif_file)

    # Edit dse.ldif
    m1_dse_ldif = DSEldif(m1)
    m1_dse_ldif.replace(DN_CONFIG_LDBM, 'nsslapd-subtree-rename-switch', 'off')

    # Import LDIF
    assert m1.ldif2db(DEFAULT_BENAME, None, None, None, ldif_file)
    m1.start()

    # Check for tombstone
    assert len(tombstones.list()) == 1

    # Create a new tombstone
    users = UserAccounts(m1, DEFAULT_SUFFIX)
    user_properties = {
        'uid': 'tuser1',
        'givenname': 'tuser1',
        'cn': 'tuser1',
        'sn': 'tuser1',
        'uidNumber': '1000',
        'gidNumber': '2000',
        'homeDirectory': '/home/tuser1'
    }
    user = users.create(properties=user_properties)
    user.delete()
    assert len(tombstones.list()) == 2

    # Test tombstone purging
    args = {REPLICA_PRECISE_PURGING: b'on',
            REPLICA_PURGE_DELAY: b'5',
            REPLICA_PURGE_INTERVAL: b'5'}
    m1.replica.setProperties(DEFAULT_SUFFIX, None, None, args)
    time.sleep(6)
    users.create_test_user(uid=1002)  # trigger replication to wake up
    time.sleep(6)

    assert len(tombstones.list()) == 0


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
