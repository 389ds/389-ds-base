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
from lib389.paths import Paths
from lib389.topologies import topology_st
from lib389._constants import *

pytestmark = pytest.mark.tier2

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

ds_paths = Paths()


@pytest.mark.skipif(not ds_paths.asan_enabled, reason="Don't run if ASAN is not enabled")
def test_range_search(topology_st):
    """Add 100 entries, and run a range search. When we encounter an error
    we still need to disable valgrind before exiting

    :id: aadccf78-a2a8-48cc-8769-4764c7966189
    :setup: Standalone instance, Retro changelog file,
            Enabled Valgrind if the system doesn't have asan
    :steps:
        1. Add 100 test entries
        2. Issue a range search with a changenumber filter
        3. There should be no leak
    :expectedresults:
        1. 100 test entries should be added
        2. Search should be successful
        3. Success
    """

    log.info('Running test_range_search...')
    topology_st.standalone.plugins.enable(name=PLUGIN_RETRO_CHANGELOG)
    topology_st.standalone.restart()

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
    assert success
    entries = topology_st.standalone.search_s(RETROCL_SUFFIX, ldap.SCOPE_SUBTREE,
                                              '(&(changenumber>=74)(changenumber<=84))')
    assert entries


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
