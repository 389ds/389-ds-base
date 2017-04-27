import os
import sys
import time
import ldap
import logging
import pytest
import time
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from lib389.utils import *

from lib389.idm.user import UserAccounts, UserAccount

from lib389.topologies import topology_m2

DEBUGGING = os.getenv('DEBUGGING', False)

if DEBUGGING is not False:
    DEBUGGING = True

if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)

log = logging.getLogger(__name__)


def test_user_compare_m2Repl(topology_m2):
    """
    User compare test between users of master to master replicaton topology.
    """
    if DEBUGGING:
        # Add debugging steps(if any)...
        pass

    m1 = topology_m2.ms.get('master1')
    m2 = topology_m2.ms.get('master2')

    m1_m2_agmtdn = topology_m2.ms.get("master1_agmts").get("m1_m2")

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
    
    log.info("Waiting for completion of replicaation.....")
    # wait for replication to complete
    m1.startReplication(m1_m2_agmtdn)

    m1_ruv = m1.replica.ruv(DEFAULT_SUFFIX)
    m2_ruv = m2.replica.ruv(DEFAULT_SUFFIX)
    
    log.debug("m1 ruv : " +  str(m1_ruv))
    log.debug("m2 ruv : " + str(m2_ruv))
    log.debug("RUV diffs: " + str(m1_ruv.getdiffs(m2_ruv)))
    
    # ruv comparison, if replication is complete then ruv's should be same
    assert(m1_ruv == m2_ruv)
    log.info("Replication completed")
    m2_testuser = m2_users.get('testuser')
    
    assert(UserAccount.compare(m1_testuser, m2_testuser) == True)
    log.info("Test PASSED")


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
