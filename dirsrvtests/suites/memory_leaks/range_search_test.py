import os
import sys
import time
import ldap
import logging
import pytest
from lib389 import DirSrv, Entry, tools, tasks
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from lib389.utils import *

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

installation1_prefix = None


class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):
    global installation1_prefix
    if installation1_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation1_prefix

    # Creating standalone instance ...
    standalone = DirSrv(verbose=False)
    args_instance[SER_HOST] = HOST_STANDALONE
    args_instance[SER_PORT] = PORT_STANDALONE
    args_instance[SER_SERVERID_PROP] = SERVERID_STANDALONE
    args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
    args_standalone = args_instance.copy()
    standalone.allocate(args_standalone)
    instance_standalone = standalone.exists()
    if instance_standalone:
        standalone.delete()
    standalone.create()
    standalone.open()

    # Clear out the tmp dir
    standalone.clearTmpDir(__file__)

    def fin():
        standalone.delete()
        sbin_dir = get_sbin_dir(prefix=standalone.prefix)
        valgrind_disable(sbin_dir)
    request.addfinalizer(fin)

    return TopologyStandalone(standalone)


def test_range_search_init(topology):
    '''
    Enable retro cl, and valgrind.  Since valgrind tests move the ns-slapd binary
    around it's important to always "valgrind_disable" before "assert False"ing,
    otherwise we leave the wrong ns-slapd in place if there is a failure
    '''

    log.info('Initializing test_range_search...')

    topology.standalone.plugins.enable(name=PLUGIN_RETRO_CHANGELOG)

    # First stop the instance
    topology.standalone.stop(timeout=10)

    # Get the sbin directory so we know where to replace 'ns-slapd'
    sbin_dir = get_sbin_dir(prefix=topology.standalone.prefix)

    # Enable valgrind
    valgrind_enable(sbin_dir)

    # Now start the server with a longer timeout
    topology.standalone.start(timeout=60)


def test_range_search(topology):
    '''
    Add a 100 entries, and run a range search.  When we encounter an error we
    still need to disable valgrind before exiting
    '''

    log.info('Running test_range_search...')

    success = True

    # Add 100 test entries
    for idx in range(1, 100):
        idx = str(idx)
        USER_DN = 'uid=user' + idx + ',' + DEFAULT_SUFFIX
        try:
            topology.standalone.add_s(Entry((USER_DN, {'objectclass': "top extensibleObject".split(),
                                 'uid': 'user' + idx})))
        except ldap.LDAPError, e:
            log.fatal('test_range_search: Failed to add test user ' + USER_DN + ': error ' + e.message['desc'])
            success = False
    time.sleep(1)

    if success:
        # Issue range search
        try:
            topology.standalone.search_s(RETROCL_SUFFIX, ldap.SCOPE_SUBTREE,
                                         '(&(changenumber>=74)(changenumber<=84))')
        except ldap.LDAPError, e:
            log.fatal('test_range_search: Failed to search retro changelog(%s), error: %s' %
                      (RETROCL_SUFFIX, e.message('desc')))
            success = False

    if success:
        # Get the results file, stop the server, and check for the leak
        results_file = valgrind_get_results_file(topology.standalone)
        topology.standalone.stop(timeout=30)
        if valgrind_check_file(results_file, VALGRIND_LEAK_STR, 'range_candidates'):
            log.fatal('test_range_search: Memory leak is still present!')
            assert False

        log.info('test_range_search: PASSED')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

