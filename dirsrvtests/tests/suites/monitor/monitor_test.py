import logging
import pytest
import os
from lib389.monitor import *
from lib389.backend import Backends, DatabaseConfig
from lib389._constants import *
from lib389.topologies import topology_st as topo
from lib389._mapped_object import DSLdapObjects

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def test_monitor(topo):
    """This test is to display monitor attributes to check the performace

    :id: f7c8a815-07cf-4e67-9574-d26a0937d3db
    :setup: Single instance
    :steps:
        1. Get the cn=monitor connections attributes
        2. Print connections attributes
        3. Get the cn=monitor version
        4. Print cn=monitor version
        5. Get the cn=monitor threads attributes
        6. Print cn=monitor threads attributes
        7. Get cn=monitor backends attributes
        8. Print cn=monitor backends attributes
        9. Get cn=monitor operations attributes
        10. Print cn=monitor operations attributes
        11. Get cn=monitor statistics attributes
        12. Print cn=monitor statistics attributes
    :expectedresults:
        1. cn=monitor attributes should be fetched and printed successfully.
    """

    #define the monitor object from Monitor class in lib389
    monitor = Monitor(topo.standalone)

    #get monitor connections
    connections = monitor.get_connections()
    log.info('connection: {0[0]}, currentconnections: {0[1]}, totalconnections: {0[2]}'.format(connections))

    #get monitor version
    version = monitor.get_version()
    log.info('version :: %s' %version)

    #get monitor threads
    threads = monitor.get_threads()
    log.info('threads: {0[0]},currentconnectionsatmaxthreads: {0[1]},maxthreadsperconnhits: {0[2]}'.format(threads))

    #get monitor backends
    backend = monitor.get_backends()
    log.info('nbackends: {0[0]}, backendmonitordn: {0[1]}'.format(backend))

    #get monitor operations
    operations = monitor.get_operations()
    log.info('opsinitiated: {0[0]}, opscompleted: {0[1]}'.format(operations))

    #get monitor stats
    stats = monitor.get_statistics()
    log.info('dtablesize: {0[0]},readwaiters: {0[1]},entriessent: {0[2]},bytessent: {0[3]},currenttime: {0[4]},starttime: {0[5]}'.format(stats))


def test_monitor_ldbm(topo):
    """This test is to check if we are getting the correct monitor entry

    :id: e62ba369-32f5-4b03-8865-f597a5bb6a70
    :setup: Single instance
    :steps:
        1. Get the backend library (bdb, ldbm, etc)
        2. Get the database monitor
        3. Check for expected attributes in output
        4. Check for expected DB library specific attributes
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
    """

    # Are we using BDB?
    db_config = DatabaseConfig(topo.standalone)
    db_lib = db_config.get_db_lib()

    # Get the database monitor entry
    monitor = MonitorLDBM(topo.standalone).get_status()

    # Check that known attributes exist (only NDN cache stats)
    assert 'normalizeddncachehits' in monitor

    # Check for library specific attributes
    if db_lib == 'bdb':
        assert 'dbcachehits' in monitor
        assert 'nsslapd-db-configured-locks' in monitor
    elif db_lib == 'lmdb':
        pass
    else:
        # Unknown - the server would probably fail to start but check it anyway
        log.fatal(f'Unknown backend library: {db_lib}')
        assert False


def test_monitor_backend(topo):
    """This test is to check if we are getting the correct backend monitor entry

    :id: 27b0534f-a18c-4c95-aa2b-936bc1886a7b
    :setup: Single instance
    :steps:
        1. Get the backend library (bdb, ldbm, etc)
        2. Get the backend monitor
        3. Check for expected attributes in output
        4. Check for expected DB library specific attributes
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
    """

    # Are we using BDB?
    db_config = DatabaseConfig(topo.standalone)
    db_lib = db_config.get_db_lib()

    # Get the backend monitor
    be = Backends(topo.standalone).list()[0]
    monitor = be.get_monitor().get_status()

    # Check for expected attributes
    assert 'entrycachehits' in monitor
    assert 'dncachehits' in monitor

    # Check for library specific attributes
    if db_lib == 'bdb':
        assert 'dbfilename-0' in monitor
    elif db_lib == 'lmdb':
        pass
    else:
        # Unknown - the server would probably fail to start but check it anyway
        log.fatal(f'Unknown backend library: {db_lib}')
        assert False


@pytest.mark.bz1843550
@pytest.mark.ds4153
@pytest.mark.bz1903539
@pytest.mark.ds4528
def test_num_subordinates_with_monitor_suffix(topo):
    """This test is to compare the numSubordinates value on the root entry
    with the actual number of direct subordinate(s).

    :id: fdcfe0ac-33c3-4252-bf38-79819ec58a51
    :setup: Single instance
    :steps:
        1. Create sample entries and perform a search with basedn as cn=monitor,
        filter as "(objectclass=*)" and scope as base.
        2. Extract the numSubordinates value.
        3. Perform another search with basedn as cn=monitor, filter as
        "(|(objectclass=*)(objectclass=ldapsubentry))" and scope as one.
        4. Compare numSubordinates value with the number of sub-entries.
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Should be same
    """

    raw_objects = DSLdapObjects(topo.standalone, basedn='cn=monitor')
    filter1 = raw_objects.filter("(objectclass=*)", scope=0)
    num_subordinates_val = filter1[0].get_attr_val_int('numSubordinates')
    filter2 = raw_objects.filter("(|(objectclass=*)(objectclass=ldapsubentry))",scope=1)
    assert len(filter2) == num_subordinates_val


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
