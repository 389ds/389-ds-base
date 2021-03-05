import pytest
from lib389.utils import *
from lib389.tasks import *
from lib389.topologies import topology_m1h1c1
from lib389.replica import ReplicationManager

from lib389._constants import (DEFAULT_SUFFIX, REPLICA_RUV_FILTER, defaultProperties,
                               REPLICATION_BIND_DN, REPLICATION_BIND_PW, REPLICATION_BIND_METHOD,
                               REPLICATION_TRANSPORT, RA_NAME, RA_BINDDN, RA_BINDPW,
                               RA_METHOD, RA_TRANSPORT_PROT, SUFFIX)

pytestmark = pytest.mark.tier2

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


def checkFirstElement(ds, rid):
    """
    Return True if the first RUV element is for the specified rid
    """
    try:
        entry = ds.search_s(DEFAULT_SUFFIX,
                            ldap.SCOPE_SUBTREE,
                            REPLICA_RUV_FILTER,
                            ['nsds50ruv'])
        assert entry
        entry = entry[0]
    except ldap.LDAPError as e:
        log.fatal('Failed to retrieve RUV entry: %s' % str(e))
        assert False

    ruv_elements = entry.getValues('nsds50ruv')
    if ('replica %s ' % rid) in ensure_str(ruv_elements[1]):
        return True
    else:
        return False


def test_ticket48325(topology_m1h1c1):
    """
    Test that the RUV element order is correctly maintained when promoting
    a hub or consumer.
    """

    #
    # Promote consumer to supplier
    #
    C1 = topology_m1h1c1.cs["consumer1"]
    M1 = topology_m1h1c1.ms["supplier1"]
    H1 = topology_m1h1c1.hs["hub1"]
    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl._ensure_changelog(C1)
    DN = topology_m1h1c1.cs["consumer1"].replica._get_mt_entry(DEFAULT_SUFFIX)
    topology_m1h1c1.cs["consumer1"].modify_s(DN, [(ldap.MOD_REPLACE,
                                                   'nsDS5ReplicaType',
                                                   b'3'),
                                                  (ldap.MOD_REPLACE,
                                                   'nsDS5ReplicaID',
                                                   b'1234'),
                                                  (ldap.MOD_REPLACE,
                                                   'nsDS5Flags',
                                                   b'1')])
    time.sleep(1)

    #
    # Check ruv has been reordered
    #
    if not checkFirstElement(topology_m1h1c1.cs["consumer1"], '1234'):
        log.fatal('RUV was not reordered')
        assert False

    topology_m1h1c1.ms["supplier1"].add_s(Entry((defaultProperties[REPLICATION_BIND_DN],
                                               {'objectclass': 'top netscapeServer'.split(),
                                                'cn': 'replication manager',
                                                'userPassword': 'password'})))

    DN = topology_m1h1c1.ms["supplier1"].replica._get_mt_entry(DEFAULT_SUFFIX)
    topology_m1h1c1.ms["supplier1"].modify_s(DN, [(ldap.MOD_REPLACE,
                                                 'nsDS5ReplicaBindDN', ensure_bytes(defaultProperties[REPLICATION_BIND_DN]))])
    #
    # Create repl agreement from the newly promoted supplier to supplier1

    properties = {RA_NAME: 'meTo_{}:{}'.format(topology_m1h1c1.ms["supplier1"].host,
                                               str(topology_m1h1c1.ms["supplier1"].port)),
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    new_agmt = topology_m1h1c1.cs["consumer1"].agreement.create(suffix=SUFFIX,
                                                                host=topology_m1h1c1.ms["supplier1"].host,
                                                                port=topology_m1h1c1.ms["supplier1"].port,
                                                                properties=properties)

    if not new_agmt:
        log.fatal("Fail to create new agmt from old consumer to the supplier")
        assert False

    # Test replication is working
    repl.test_replication(C1, M1)

    #
    # Promote hub to supplier
    #
    DN = topology_m1h1c1.hs["hub1"].replica._get_mt_entry(DEFAULT_SUFFIX)
    topology_m1h1c1.hs["hub1"].modify_s(DN, [(ldap.MOD_REPLACE,
                                              'nsDS5ReplicaType',
                                              b'3'),
                                             (ldap.MOD_REPLACE,
                                              'nsDS5ReplicaID',
                                              b'5678')])
    time.sleep(1)

    #
    # Check ruv has been reordered
    #
    if not checkFirstElement(topology_m1h1c1.hs["hub1"], '5678'):
        log.fatal('RUV was not reordered')
        assert False

    # Test replication is working
    repl.test_replication(M1, H1)

    # Done
    log.info('Test complete')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
