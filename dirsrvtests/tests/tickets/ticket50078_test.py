import pytest
from lib389.utils import *
from lib389.topologies import topology_m1h1c1
from lib389.idm.user import UserAccounts

from lib389._constants import (DEFAULT_SUFFIX, REPLICA_RUV_FILTER, defaultProperties,
                              REPLICATION_BIND_DN, REPLICATION_BIND_PW, REPLICATION_BIND_METHOD,
                              REPLICATION_TRANSPORT, SUFFIX, RA_NAME, RA_BINDDN, RA_BINDPW,
                              RA_METHOD, RA_TRANSPORT_PROT, SUFFIX)

pytestmark = pytest.mark.tier2

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

TEST_USER = "test_user"

def test_ticket50078(topology_m1h1c1):
    """
    Test that for a MODRDN operation the cenotaph entry is created on
    a hub or consumer.
    """

    M1 = topology_m1h1c1.ms["supplier1"]
    H1 = topology_m1h1c1.hs["hub1"]
    C1 = topology_m1h1c1.cs["consumer1"]
    #
    # Test replication is working
    #
    if M1.testReplication(DEFAULT_SUFFIX, topology_m1h1c1.cs["consumer1"]):
        log.info('Replication is working.')
    else:
        log.fatal('Replication is not working.')
        assert False

    ua = UserAccounts(M1, DEFAULT_SUFFIX)
    ua.create(properties={
            'uid': "%s%d" % (TEST_USER, 1),
            'cn' : "%s%d" % (TEST_USER, 1),
            'sn' : 'user',
            'uidNumber' : '1000',
            'gidNumber' : '2000',
            'homeDirectory' : '/home/testuser'
            })

    user = ua.get('%s1' % TEST_USER)
    log.info("  Rename the test entry %s..." % user)
    user.rename('uid=test_user_new')

    # wait until replication is in sync
    if M1.testReplication(DEFAULT_SUFFIX, topology_m1h1c1.cs["consumer1"]):
        log.info('Replication is working.')
    else:
        log.fatal('Replication is not working.')
        assert False

    # check if cenotaph was created on hub and consumer
    ents = H1.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, filterstr="(&(objectclass=nstombstone)(cenotaphid=*))")
    assert len(ents) == 1

    ents = C1.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, filterstr="(&(objectclass=nstombstone)(cenotaphid=*))")
    assert len(ents) == 1



if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
