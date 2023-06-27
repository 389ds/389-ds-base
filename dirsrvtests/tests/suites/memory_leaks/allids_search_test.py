# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2023 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
from lib389.dbgen import dbgen_groups
from lib389.tasks import ImportTask
from lib389.utils import *
from lib389.paths import Paths
from lib389.topologies import topology_st as topo
from lib389._constants import *

pytestmark = pytest.mark.tier2

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

ds_paths = Paths()


@pytest.mark.skipif(not ds_paths.asan_enabled, reason="Don't run if ASAN is not enabled")
def test_allids_search(topo):
    """Add 100 groups, and run a search with a special filter that triggers a memleak.

    :id: 8aeca831-e671-4203-9d50-2bfe9567bec7
    :setup: Standalone instance
    :steps:
        1. Add 100 test groups
        2. Issue a search with a special filter
        3. There should be no leak
    :expectedresults:
        1. 100 test groups should be added
        2. Search should be successful
        3. Success
    """

    inst = topo.standalone

    import_ldif = inst.ldifdir + '/import_100_users.ldif'
    props = {
        "name": "grp",
        "suffix": DEFAULT_SUFFIX,
        "parent": DEFAULT_SUFFIX,
        "number": 100,
        "numMembers": 0,
        "createMembers": False,
        "memberParent": DEFAULT_SUFFIX,
        "membershipAttr": "member",
    }
    dbgen_groups(inst, import_ldif, props)

    task = ImportTask(inst)
    task.import_suffix_from_ldif(ldiffile=import_ldif, suffix=DEFAULT_SUFFIX)
    task.wait()

    inst.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE,
                            '(&(objectClass=groupOfNames)(!(objectClass=nsTombstone))(member=doesnt_exist))')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)


