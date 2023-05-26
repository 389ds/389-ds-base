# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2023 Red Hat, Inc.
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
from lib389.topologies import topology_m1c1 as topo
from lib389.replica import Replicas
from lib389.idm.user import UserAccount
from lib389.agreement import Agreements

log = logging.getLogger(__name__)

TEST_ENTRY_NAME = 'mmrepl_test'
TEST_ENTRY_DN = 'uid={},{}'.format(TEST_ENTRY_NAME, DEFAULT_SUFFIX)


def get_agreement(agmts, consumer):
    # Get agreement towards consumer among the agremment list
    for agmt in agmts.list():
        if (agmt.get_attr_val_utf8('nsDS5ReplicaPort') == str(consumer.port) and
           agmt.get_attr_val_utf8('nsDS5ReplicaHost') == consumer.host):
            return agmt
    return None

def test_repl_linger_timeout(topo):
    """Test the replication linger timeout works for both the global replica
    and per agreement setting.

    :id: bdb706a0-36fe-4803-8bf0-449a970cc0be
    :setup: Supplier Instance, Consumer Instance
    :steps:
        1. Set linger timeout on Replica (global)
        2. Add user to trigger replication
        3. Check linger timeout expired as expected
        4. Set linger timeout on agreement and replica config
        5. Delete user to trigger replication
        6. Check linger did not time out based on replica (global) setting
        7. Check linger timed out based on agmt's timeout setting
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
    """

    s1 = topo.ms["supplier1"]
    c1 = topo.cs["consumer1"]

    # Enable repl logging so we can track linger timeout message
    s1.config.replace('nsslapd-errorlog-level', '8192')

    # Set linger timeout on Replica (global)
    replica = Replicas(s1).get(DEFAULT_SUFFIX)
    replica.replace('nsDS5ReplicaLingerTimeout', '10')
    s1.restart()

    # Add user to trigger replication
    test_user = UserAccount(s1, TEST_ENTRY_DN)
    test_user.create(properties={
        'uid': TEST_ENTRY_NAME,
        'cn': TEST_ENTRY_NAME,
        'sn': TEST_ENTRY_NAME,
        'userPassword': TEST_ENTRY_NAME,
        'uidNumber': '1000',
        'gidNumber': '2000',
        'homeDirectory': '/home/mmrepl_test',
    })

    # Check linger timeout expired as expected
    time.sleep(11)
    assert s1.searchErrorsLog('Linger timeout has expired')

    # Set linger timeout on agreement and replica config
    agmts_s1 = Agreements(s1, replica.dn)
    agmt = get_agreement(agmts_s1, c1)
    agmt.replace('nsDS5ReplicaLingerTimeout', '10')
    replica.replace('nsDS5ReplicaLingerTimeout', '5')

    # Reset logs
    s1.deleteErrorLogs(restart=True)

    # Do update to trigger replication
    test_user.delete()

    # Check that the replica global is skipped/ignored
    time.sleep(6)
    assert not s1.searchErrorsLog('Linger timeout has expired')

    # Check linger timed out based on agmt's timeout setting
    time.sleep(5)
    assert s1.searchErrorsLog('Linger timeout has expired')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
