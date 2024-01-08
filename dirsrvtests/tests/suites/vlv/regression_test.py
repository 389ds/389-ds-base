# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2023 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest, time
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_m2, topology_st
from lib389.replica import *
from lib389._constants import *
from lib389.properties import TASK_WAIT
from lib389.index import *
from lib389.mappingTree import *
from lib389.backend import *
from lib389.idm.user import UserAccounts, UserAccount
from ldap.controls.vlv import VLVRequestControl
from ldap.controls.sss import SSSRequestControl
from lib389.replica import ReplicationManager
from lib389.agreement import Agreements
from lib389.cli_ctl.dblib import run_dbscan


pytestmark = pytest.mark.tier1

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

STARTING_UID_INDEX = 1000
# VLV search always returns first the entry before the target,
# and the entry uid=demo_user,ou=people,dc=example,dc=com
# is before the uid=testuserNNNN,dc=example,dc=com entries
# so VLV_SEARCH_OFFSET (The difference between the vlv index
# and the NNNN value) is:
VLV_SEARCH_OFFSET = STARTING_UID_INDEX - 2
NUMUSERS_TEST_VLV_REINDEX = 5000


def open_new_ldapi_conn(dsinstance):
    ldapurl, certdir = get_ldapurl_from_serverid(dsinstance)
    assert 'ldapi://' in ldapurl
    conn = ldap.initialize(ldapurl)
    conn.sasl_interactive_bind_s("", ldap.sasl.external())
    return conn


def check_vlv_search(conn, **kwargs):
    before_count = kwargs.get('before_count', 1)
    after_count = kwargs.get('after_count', 3)
    offset = kwargs.get('offset', 3501)

    vlv_control = VLVRequestControl(criticality=True,
        before_count=before_count,
        after_count=after_count,
        offset=offset,
        content_count=0,
        greater_than_or_equal=None,
        context_id=None)

    sss_control = SSSRequestControl(criticality=True, ordering_rules=['cn'])
    result = conn.search_ext_s(
        base='dc=example,dc=com',
        scope=ldap.SCOPE_SUBTREE,
        filterstr='(uid=*)',
        serverctrls=[vlv_control, sss_control]
    )
    imin = offset + VLV_SEARCH_OFFSET - before_count
    imax = offset + VLV_SEARCH_OFFSET + after_count

    for i, (dn, entry) in enumerate(result, start=imin):
        assert i <= imax
        expected_dn = f'uid=testuser{i},ou=People,dc=example,dc=com'
        log.debug(f'found {dn} expected {expected_dn}')
        assert dn.lower() == expected_dn.lower()


def add_users(inst, users_num):
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    log.info(f'Adding {users_num} users')
    for i in range(0, users_num):
        uid = STARTING_UID_INDEX + i
        user_properties = {
            'uid': f'testuser{uid}',
            'cn': f'testuser{uid}',
            'sn': 'user',
            'uidNumber': str(uid),
            'gidNumber': str(uid),
            'homeDirectory': f'/home/testuser{uid}'
        }
        users.create(properties=user_properties)


def create_vlv_search_and_index(inst):
    vlv_searches = VLVSearch(inst)
    vlv_search_properties = {
        "objectclass": ["top", "vlvSearch"],
        "cn": "vlvSrch",
        "vlvbase": DEFAULT_SUFFIX,
        "vlvfilter": "(uid=*)",
        "vlvscope": "2",
    }
    vlv_searches.create(
        basedn="cn=userRoot,cn=ldbm database,cn=plugins,cn=config",
        properties=vlv_search_properties
    )

    vlv_index = VLVIndex(inst)
    vlv_index_properties = {
        "objectclass": ["top", "vlvIndex"],
        "cn": "vlvIdx",
        "vlvsort": "cn",
    }
    vlv_index.create(
        basedn="cn=vlvSrch,cn=userRoot,cn=ldbm database,cn=plugins,cn=config",
        properties=vlv_index_properties
    )
    return vlv_searches, vlv_index


@pytest.mark.DS47966
def test_bulk_import_when_the_backend_with_vlv_was_recreated(topology_m2):
    """
    Testing bulk import when the backend with VLV was recreated.
    If the test passes without the server crash, 47966 is verified.

    :id: 37d42d12-2544-49a0-81ef-7dbfb69edc0f
    :setup: Replication with two suppliers.
    :steps:
        1. Generate vlvSearch entry
        2. Generate vlvIndex entry
        3. Delete the backend instance on Supplier 2
        4. Delete the agreement, replica, and mapping tree, too.
        5. Recreate the backend and the VLV index on Supplier 2.
        6. Recreating vlvSrchDn and vlvIndexDn on Supplier 2.
    :expectedresults:
        1. Should Success.
        2. Should Success.
        3. Should Success.
        4. Should Success.
        5. Should Success.
        6. Should Success.
    """
    M1 = topology_m2.ms["supplier1"]
    M2 = topology_m2.ms["supplier2"]
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
    # Delete the backend instance on Supplier 2."
    userroot_index.delete()
    userroot_vlvsearch.delete_all()
    # delete the agreement, replica, and mapping tree, too.
    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.remove_supplier(M2)
    MappingTrees(M2).list()[0].delete()
    Backends(M2).list()[0].delete()
    # Recreate the backend and the VLV index on Supplier 2.
    M2.backend.create(DEFAULT_SUFFIX, {BACKEND_NAME: "userRoot"})
    M2.mappingtree.create(DEFAULT_SUFFIX, "userRoot")
    # Recreating vlvSrchDn and vlvIndexDn on Supplier 2.
    vlv_searches.create(
        basedn="cn=userRoot,cn=ldbm database,cn=plugins,cn=config",
        properties=properties_for_search,
    )
    vlv_index.create(
        basedn="cn=vlvSrch,cn=userRoot,cn=ldbm database,cn=plugins,cn=config",
        properties=properties_for_index,
    )
    M2.restart()
    repl.join_supplier(M1, M2)
    repl.test_replication(M1, M2, 30)
    repl.test_replication(M2, M1, 30)
    entries = M2.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, "(cn=*)")
    assert len(entries) > 0
    entries = M2.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, "(objectclass=*)")
    entries = M2.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, "(objectclass=*)")


def test_vlv_recreation_reindex(topology_st):
    """Test VLV recreation and reindexing.

    :id: 29f4567f-4ac0-410f-bc99-a32e217a939f
    :setup: Standalone instance.
    :steps:
        1. Create new VLVs and do the reindex.
        2. Test the new VLVs.
        3. Remove the existing VLVs.
        4. Create new VLVs (with the same name).
        5. Perform online re-indexing of the new VLVs.
        6. Test the new VLVs.
    :expectedresults:
        1. Should Success.
        2. Should Success.
        3. Should Success.
        4. Should Success.
        5. Should Success.
        6. Should Success.
    """

    inst = topology_st.standalone
    reindex_task = Tasks(inst)

    # Create and test VLVs
    vlv_search, vlv_index = create_vlv_search_and_index(inst)
    assert reindex_task.reindex(
        suffix=DEFAULT_SUFFIX,
        attrname=vlv_index.rdn,
        args={TASK_WAIT: True},
        vlv=True
    ) == 0

    add_users(inst, NUMUSERS_TEST_VLV_REINDEX)

    conn = open_new_ldapi_conn(inst.serverid)
    assert len(conn.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, "(cn=*)")) > 0
    check_vlv_search(conn)

    # Remove and recreate VLVs
    vlv_index.delete()
    vlv_search.delete()

    vlv_search, vlv_index = create_vlv_search_and_index(inst)
    assert reindex_task.reindex(
        suffix=DEFAULT_SUFFIX,
        attrname=vlv_index.rdn,
        args={TASK_WAIT: True},
        vlv=True
    ) == 0

    conn = open_new_ldapi_conn(inst.serverid)
    assert len(conn.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, "(cn=*)")) > 0
    check_vlv_search(conn)


def verify_vlv_subdb_names(vlvname, inst):
    """
    Verify that vlv index and cache sub database names are conistent on lmdb.
    """
    if get_default_db_lib() != "mdb":
        return
    output = run_dbscan(['-D', 'mdb', '-L', inst.dbdir])
    vlvsubdbs = []
    nbvlvindex = 0
    for line in output.split("\n"):
        if not line:
            continue
        fname = line.split()[0]
        if 'vlv#' in fname:
            vlvsubdbs.append(fname)
            if '/~recno-cache/' not in fname:
                nbvlvindex += 1
    log.info(f'vlv sub databases are: {vlvsubdbs}')
    assert f'{inst.dbdir}/userroot/vlv#{vlvname}.db' in vlvsubdbs
    assert f'{inst.dbdir}/userroot/~recno-cache/vlv#{vlvname}.db' in vlvsubdbs
    assert len(vlvsubdbs) == 2 * nbvlvindex


def test_vlv_cache_subdb_names(topology_m2):
    """
    Testing that vlv index cache sub database name is consistent.

    :id: 7a138006-aa24-11ee-9d5f-482ae39447e5
    :setup: Replication with two suppliers.
    :steps:
        1. Generate vlvSearch and vlvIndex entries
        2. Add users and wait that they get replicated.
        3. Check subdb names and perform vlv searches
        4. Reinit M2 from M1
        5. Check subdb names and perform vlv searches
    :expectedresults:
        1. Should Success.
        2. Should Success.
        3. Should Success.
        4. Should Success.
        5. Should Success.
    """

    NUM_USERS = 50
    M1 = topology_m2.ms["supplier1"]
    M2 = topology_m2.ms["supplier2"]
    repl = ReplicationManager(DEFAULT_SUFFIX)

    # Clean test_vlv_recreation_reindex leftover that cause trouble
    entries = [ UserAccount(M1, dn=f'uid=testuser{uid},DEFAULT_SUFFIX') for uid in
        range(STARTING_UID_INDEX, STARTING_UID_INDEX+NUMUSERS_TEST_VLV_REINDEX) ]
    entries.append(VLVIndex(M2, dn='cn=vlvIdx,cn=vlvSrch,cn=userRoot,cn=ldbm database,cn=plugins,cn=config'))
    entries.append(VLVSearch(M2, dn='cn=vlvSrch,cn=userRoot,cn=ldbm database,cn=plugins,cn=config'))
    for entry in entries:
        if entry.exists():
            entry.delete()
    repl.wait_for_replication(M1, M2)
    # Restart the instance to workaround github issue #6028
    M2.restart()

    # generate vlvSearch and vlvIndex entries
    vlv_search, vlv_index = create_vlv_search_and_index(M2)
    vlvname = vlv_index.get_attr_val_utf8_l('cn')
    assert "cn=vlvIdx,cn=vlvSrch,cn=userRoot,cn=ldbm database,cn=plugins,cn=config" in M2.getEntry(
        "cn=vlvIdx,cn=vlvSrch,cn=userRoot,cn=ldbm database,cn=plugins,cn=config").dn
    reindex_task = Tasks(M2)
    assert reindex_task.reindex(
        suffix=DEFAULT_SUFFIX,
        attrname=vlv_index.rdn,
        args={TASK_WAIT: True},
        vlv=True
    ) == 0

    # Add users and wait that they get replicated.
    add_users(M1, NUM_USERS)
    repl.wait_for_replication(M1, M2)

    # Check subdb names and perform vlv searches
    conn = open_new_ldapi_conn(M2.serverid)
    assert len(conn.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, "(cn=*)")) > 0
    check_vlv_search(conn, offset=23)
    verify_vlv_subdb_names(vlvname, M2)

    # Reinit M2 from M1
    agmt = Agreements(M1).list()[0]
    agmt.begin_reinit()
    (done, error) = agmt.wait_reinit()
    assert done is True
    assert error is False

    # Check subdb names and perform vlv searches
    conn = open_new_ldapi_conn(M2.serverid)
    assert len(conn.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, "(cn=*)")) > 0
    check_vlv_search(conn, offset=23)
    verify_vlv_subdb_names(vlvname, M2)


if __name__ == "__main__":
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
