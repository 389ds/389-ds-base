# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2018 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest, time
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_m2
from lib389.replica import *
from lib389._constants import *
from lib389.index import *
from lib389.mappingTree import *
from lib389.backend import *

pytestmark = pytest.mark.tier1

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


@pytest.mark.DS47966
def test_bulk_import_when_the_backend_with_vlv_was_recreated(topology_m2):
    """
    Testing bulk import when the backend with VLV was recreated.
    If the test passes without the server crash, 47966 is verified.

    :id: 512963fa-fe02-11e8-b1d3-8c16451d917b
    :setup: Replication with two masters.
    :steps:
        1. Generate vlvSearch entry
        2. Generate vlvIndex entry
        3. Delete the backend instance on Master 2
        4. Delete the agreement, replica, and mapping tree, too.
        5. Recreate the backend and the VLV index on Master 2.
        6. Recreating vlvSrchDn and vlvIndexDn on Master 2.
    :expectedresults:
        1. Should Success.
        2. Should Success.
        3. Should Success.
        4. Should Success.
        5. Should Success.
        6. Should Success.
    """
    M1 = topology_m2.ms["master1"]
    M2 = topology_m2.ms["master2"]
    # generate vlvSearch entry
    properties_for_search = {
        "objectclass": ["top", "vlvSearch"],
        "cn": "vlvSrch",
        "vlvbase": DEFAULT_SUFFIX,
        "vlvfilter": "(|(objectclass=*)(objectclass=ldapsubentry))",
        "vlvscope": "2",
    }
    vlv_searches = VLVSearch(M2)
    userroot_vlvsearch = vlv_searches.create(
        basedn="cn=userRoot,cn=ldbm database,cn=plugins,cn=config",
        properties=properties_for_search,
    )
    assert "cn=vlvSrch,cn=userRoot,cn=ldbm database,cn=plugins,cn=config" in M2.getEntry(
        "cn=vlvSrch,cn=userRoot,cn=ldbm database,cn=plugins,cn=config").dn
    # generate vlvIndex entry
    properties_for_index = {
        "objectclass": ["top", "vlvIndex"],
        "cn": "vlvIdx",
        "vlvsort": "cn ou sn",
    }
    vlv_index = VLVIndex(M2)
    userroot_index = vlv_index.create(
        basedn="cn=vlvSrch,cn=userRoot,cn=ldbm database,cn=plugins,cn=config",
        properties=properties_for_index,
    )
    assert "cn=vlvIdx,cn=vlvSrch,cn=userRoot,cn=ldbm database,cn=plugins,cn=config" in M2.getEntry(
        "cn=vlvIdx,cn=vlvSrch,cn=userRoot,cn=ldbm database,cn=plugins,cn=config").dn
    # Delete the backend instance on Master 2."
    userroot_index.delete()
    userroot_vlvsearch.delete_all()
    # delete the agreement, replica, and mapping tree, too.
    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.remove_master(M2)
    MappingTrees(M2).list()[0].delete()
    Backends(M2).list()[0].delete()
    # Recreate the backend and the VLV index on Master 2.
    M2.backend.create(DEFAULT_SUFFIX, {BACKEND_NAME: "userRoot"})
    M2.mappingtree.create(DEFAULT_SUFFIX, "userRoot")
    # Recreating vlvSrchDn and vlvIndexDn on Master 2.
    vlv_searches.create(
        basedn="cn=userRoot,cn=ldbm database,cn=plugins,cn=config",
        properties=properties_for_search,
    )
    vlv_index.create(
        basedn="cn=vlvSrch,cn=userRoot,cn=ldbm database,cn=plugins,cn=config",
        properties=properties_for_index,
    )
    M2.restart()
    repl.join_master(M1, M2)
    repl.test_replication(M1, M2, 30)
    repl.test_replication(M2, M1, 30)
    entries = M2.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, "(cn=*)")
    assert len(entries) > 0
    entries = M2.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, "(objectclass=*)")
    entries = M2.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, "(objectclass=*)")


if __name__ == "__main__":
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
