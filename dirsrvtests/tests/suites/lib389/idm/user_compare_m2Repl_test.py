import os
import pytest
from lib389._constants import DEFAULT_SUFFIX
from lib389.replica import ReplicationManager
from lib389.idm.user import UserAccounts, UserAccount
from lib389.topologies import topology_m2

pytestmark = pytest.mark.tier1

def test_user_compare_m2Repl(topology_m2):
    """
    User compare test between users of supplier to supplier replicaton topology.

    :id: 7c243bea-4075-4304-864d-5b789d364871

    :setup: 2 supplier MMR

    :steps: 1. Add a user to m1
            2. Wait for replication
            3. Compare if the user is the same

    :expectedresults: 1. User is added
                      2. Replication success
                      3. The user is the same
    """
    rm = ReplicationManager(DEFAULT_SUFFIX)
    m1 = topology_m2.ms.get('supplier1')
    m2 = topology_m2.ms.get('supplier2')

    m1_users = UserAccounts(m1, DEFAULT_SUFFIX)
    m2_users = UserAccounts(m2, DEFAULT_SUFFIX)

    # Create 1st user
    user1_properties = {
        'uid': 'testuser',
        'cn': 'testuser',
        'sn': 'user',
        'uidNumber': '1000',
        'gidNumber': '2000',
        'homeDirectory': '/home/testuser'
    }

    m1_users.create(properties=user1_properties)
    m1_testuser = m1_users.get('testuser')

    rm.wait_for_replication(m1, m2)

    m2_testuser = m2_users.get('testuser')

    assert UserAccount.compare(m1_testuser, m2_testuser)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
