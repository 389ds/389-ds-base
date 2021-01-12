import os
import pytest
from lib389._constants import DEFAULT_SUFFIX
from lib389.idm.user import UserAccounts, UserAccount
from lib389.topologies import topology_i2

pytestmark = pytest.mark.tier1

def test_user_compare_i2(topology_i2):
    """
    Compare test between users of two different Directory Server intances.

    :id: f0ffaf59-e2c2-41ec-9f26-e9b1ef287463

    :setup: two isolated directory servers

    :steps: 1. Add an identical user to each server
            2. Compare if the users are "the same"

    :expectedresults: 1. Users are added
                      2. The users are reported as the same
    """
    st1_users = UserAccounts(topology_i2.ins.get('standalone1'), DEFAULT_SUFFIX)
    st2_users = UserAccounts(topology_i2.ins.get('standalone2'), DEFAULT_SUFFIX)

    # Create user
    user_properties = {
        'uid': 'testuser',
        'cn': 'testuser',
        'sn': 'user',
        'uidNumber': '1000',
        'gidNumber': '2000',
        'homeDirectory': '/home/testuser'
    }

    st1_users.create(properties=user_properties)
    st1_testuser = st1_users.get('testuser')

    st2_users.create(properties=user_properties)
    st2_testuser = st2_users.get('testuser')

    st1_testuser._compare_exclude.append('entryuuid')
    st2_testuser._compare_exclude.append('entryuuid')

    assert UserAccount.compare(st1_testuser, st2_testuser)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
