# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st

from lib389._constants import (PLUGIN_RETRO_CHANGELOG, BACKEND_NAME, RETROCL_SUFFIX,
                              VALGRIND_LEAK_STR)

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


@pytest.fixture(scope="module")
def setup(topology_st, request):
    """Enable retro cl, and valgrind.  Since valgrind tests move the ns-slapd binary
    around it's important to always "valgrind_disable" before "assert False"ing,
    otherwise we leave the wrong ns-slapd in place if there is a failure
    """

    log.info('Initializing test_range_search...')

    topology_st.standalone.plugins.enable(name=PLUGIN_RETRO_CHANGELOG)

    # First stop the instance
    topology_st.standalone.stop(timeout=30)

    # Get the sbin directory so we know where to replace 'ns-slapd'
    sbin_dir = get_sbin_dir(prefix=topology_st.standalone.prefix)

    # Enable valgrind
    if not topology_st.standalone.has_asan():
        valgrind_enable(sbin_dir)

    def fin():
        if not topology_st.standalone.has_asan():
            topology_st.standalone.stop(timeout=30)
            sbin_dir = topology_st.standalone.get_sbin_dir()
            valgrind_disable(sbin_dir)
            topology_st.standalone.start()

    request.addfinalizer(fin)

    # Now start the server with a longer timeout
    topology_st.standalone.start()


def test_range_search(topology_st, setup):
    """Add a 100 entries, and run a range search.
    When we encounter an error we still need to
    disable valgrind before exiting
    """

    log.info('Running test_range_search...')

    success = True

    # Add 100 test entries
    for idx in range(1, 100):
        idx = str(idx)
        USER_DN = 'uid=user' + idx + ',' + DEFAULT_SUFFIX
        try:
            topology_st.standalone.add_s(Entry((USER_DN, {'objectclass': "top extensibleObject".split(),
                                                          'uid': 'user' + idx})))
        except ldap.LDAPError as e:
            log.fatal('test_range_search: Failed to add test user ' + USER_DN + ': error ' + e.message['desc'])
            success = False
    time.sleep(1)

    # Issue range search
    if success:
        try:
            topology_st.standalone.search_s(RETROCL_SUFFIX, ldap.SCOPE_SUBTREE,
                                            '(&(changenumber>=74)(changenumber<=84))')
        except ldap.LDAPError as e:
            log.fatal('test_range_search: Failed to search retro changelog(%s), error: %s' %
                      (RETROCL_SUFFIX, e.message('desc')))
            success = False

    if success and not topology_st.standalone.has_asan():
        # Get the results file, stop the server, and check for the leak
        results_file = valgrind_get_results_file(topology_st.standalone)
        topology_st.standalone.stop(timeout=30)
        if valgrind_check_file(results_file, VALGRIND_LEAK_STR, 'range_candidates'):
            log.fatal('test_range_search: Memory leak is still present!')
            assert False


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
