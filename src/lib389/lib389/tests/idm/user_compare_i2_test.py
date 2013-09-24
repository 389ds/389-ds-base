import os
import sys
import time
import ldap
import logging
import pytest
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from lib389.utils import *

from lib389.idm.group import Groups
from lib389.idm.user import UserAccounts, UserAccount

from lib389.topologies import topology_i2

DEBUGGING = os.getenv('DEBUGGING', False)

if DEBUGGING is not False:
    DEBUGGING = True

if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)

log = logging.getLogger(__name__)


def test_user_compare_i2(topology_i2):
    """
    Compare test between users of two different Directory Server intances.
    
    """
    if DEBUGGING:
        # Add debugging steps(if any)...
        pass
        
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

    assert(UserAccount.compare(st1_testuser, st2_testuser) == True)

    log.info("Test PASSED")


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
