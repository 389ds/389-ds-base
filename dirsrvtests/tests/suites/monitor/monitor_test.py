import logging
import pytest
import os
from lib389.monitor import *
from lib389._constants import *
from lib389.topologies import topology_st as topo

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

pytestmark = pytest.mark.tier1
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


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
