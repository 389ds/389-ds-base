# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2023 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest, time
import os
import glob
import ldap
from shutil import copyfile, rmtree
from contextlib import contextmanager
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
from lib389.idm.organization import Organization
from ldap.controls.vlv import VLVRequestControl
from ldap.controls.sss import SSSRequestControl
from lib389.replica import BootstrapReplicationManager, Replicas, ReplicationManager
from lib389.agreement import Agreements
from lib389.cli_ctl.dblib import run_dbscan
from lib389.idm.organizationalunit import OrganizationalUnits




pytestmark = pytest.mark.tier1

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)
DEBUGGING = os.getenv("DEBUGGING", default=False)

STARTING_UID_INDEX = 1000
# VLV search always returns first the entry before the target,
# and the entry uid=demo_user,ou=people,dc=example,dc=com
# is before the uid=testuserNNNN,dc=example,dc=com entries
# so VLV_SEARCH_OFFSET (The difference between the vlv index
# and the NNNN value) is:
VLV_SEARCH_OFFSET = STARTING_UID_INDEX - 2

# A VLV Index with invalid vlvSrch:
# ( Using objectClass: extensibleobject instead of objectClass: vlvSrch )
BAD_DSE_DATA = """dn: cn=vlvSrch,cn=userRoot,cn=ldbm database,cn=plugins,cn=config
objectClass: top
objectClass: extensibleobject
cn: vlvSrch
vlvBase: dc=example,dc=com
vlvFilter: (uid=*)
vlvScope: 2
modifiersName: cn=directory manager
createTimestamp: 20240110175704Z
modifyTimestamp: 20240110175704Z
numSubordinates: 1

dn: cn=vlvIdx,cn=vlvSrch,cn=userRoot,cn=ldbm database,cn=plugins,cn=config
objectClass: top
objectClass: vlvIndex
cn: vlvIdx
vlvSort: cn
creatorsName: cn=directory manager
modifiersName: cn=directory manager
createTimestamp: 20240110175704Z
modifyTimestamp: 20240110175704Z
"""


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
    basedn = kwargs.get('basedn', DEFAULT_SUFFIX)

    log.info('Testing that VLV search is returning the expected entries: '
             f'basedn={basedn} '
             f'before_count={before_count} '
             f'after_count={after_count} '
             f'offset={offset}')
    vlv_control = VLVRequestControl(criticality=True,
        before_count=before_count,
        after_count=after_count,
        offset=offset,
        content_count=0,
        greater_than_or_equal=None,
        context_id=None)

    sss_control = SSSRequestControl(criticality=True, ordering_rules=['cn'])
    result = conn.search_ext_s(
        base=basedn,
        scope=ldap.SCOPE_SUBTREE,
        filterstr='(uid=*)',
        serverctrls=[vlv_control, sss_control]
    )
    imin = offset + VLV_SEARCH_OFFSET - before_count
    imax = offset + VLV_SEARCH_OFFSET + after_count

    for i, (dn, entry) in enumerate(result, start=imin):
        assert i <= imax
        expected_rdn = f'uid=testuser{i},'
        log.debug(f'found {dn} should starts with {expected_rdn}')
        assert dn.lower().startswith(expected_rdn)


def add_user(inst, users, uid):
    user_properties = {
        'uid': f'testuser{uid}',
        'cn': f'testuser{uid}',
        'sn': 'user',
        'uidNumber': str(uid),
        'gidNumber': str(uid),
        'homeDirectory': f'/home/testuser{uid}'
    }
    users.create(properties=user_properties)


def add_users(inst, users_num, suffix=DEFAULT_SUFFIX):
    users = UserAccounts(inst, suffix)
    log.info(f'Adding {users_num} users')
    for i in range(0, users_num):
        uid = STARTING_UID_INDEX + i
        add_user(inst, users, uid)


def create_vlv_search_and_index(inst, basedn=DEFAULT_SUFFIX, bename='userRoot', scope=2, prefix="vlv"):
    vlv_searches = VLVSearch(inst)
    vlv_search_properties = {
        "objectclass": ["top", "vlvSearch"],
        "cn": f"{prefix}Srch",
        "vlvbase": basedn,
        "vlvfilter": "(uid=*)",
        "vlvscope": str(scope),
    }
    vlv_searches.create(
        basedn=f"cn={bename},cn=ldbm database,cn=plugins,cn=config",
        properties=vlv_search_properties
    )

    vlv_index = VLVIndex(inst)
    vlv_index_properties = {
        "objectclass": ["top", "vlvIndex"],
        "cn": f"{prefix}Idx",
        "vlvsort": "cn",
    }
    vlv_index.create(
        basedn=f"cn={prefix}Srch,cn={bename},cn=ldbm database,cn=plugins,cn=config",
        properties=vlv_index_properties
    )
    return vlv_searches, vlv_index


def remove_entries(inst, basedn, filter):
    # Remove all entries matching the criteriae.
    conn = open_new_ldapi_conn(inst.serverid)
    entries = conn.search_s(basedn, ldap.SCOPE_SUBTREE, filter)
    for entry in entries:
        conn.delete_s(entry[0])


def cleanup(inst):
    # Remove the left over from previous tests.
    remove_entries(inst, "cn=config", "(objectclass=vlvIndex)")
    remove_entries(inst, "cn=config", "(objectclass=vlvSearch)")
    remove_entries(inst, DEFAULT_SUFFIX, "(cn=testuser*)")


@pytest.fixture
def vlv_setup_with_two_backend(topology_st, request):
    inst = topology_st.standalone
    belist = []

    def fin():
        # Cleanup function
        if not DEBUGGING and inst.exists() and inst.status():
            for be in Backends(inst).list():
                if be.get_attr_val_utf8_l('cn') in [ 'be1', 'be2' ]:
                    be.delete()

    request.addfinalizer(fin)

    def setup_vlv_and_backend(inst, bename):
        # Create a backend, add vlv index and populate the backend.
        bes = Backends(inst)
        suffix = f'o={bename}'
        be = bes.create(properties={
            'cn': bename,
            'nsslapd-suffix': suffix,
        })
        # Add suffix entry
        Organization(inst, dn=suffix).create(properties={ 'o': bename, })
        # Configure vlv
        vlv_search, vlv_index = create_vlv_search_and_index(
            inst, basedn=suffix,
            bename=bename, scope=1,
            prefix=f'vlv_1lvl_{bename}')
        # Add ou=People entry
        OrganizationalUnits(inst, suffix).create(properties={'ou': 'People'})
        # Add another ou that will be deleted before the export
        # so that import will change the vlv search basedn entryid
        ou2 = OrganizationalUnits(inst, suffix).create(properties={'ou': 'dummy ou'})
        # Add a demo user so that vlv_check is happy
        dn = f'uid=demo_user,ou=people,{suffix}'
        UserAccount(inst, dn=dn).create( properties= {
                'uid': 'demo_user',
                'cn': 'Demo user',
                'sn': 'Demo user',
                'uidNumber': '99998',
                'gidNumber': '99998',
                'homeDirectory': '/var/empty',
                'loginShell': '/bin/false', })
        # Add regular user
        add_users(inst, 10, suffix=suffix)
        # Removing ou2
        ou2.delete()
        # And export
        tasks = Tasks(inst)
        ldif = f'{inst.get_ldif_dir()}/db-{bename}.ldif'
        assert tasks.exportLDIF(suffix=suffix,
                                output_file=ldif,
                                args={TASK_WAIT: True}) == 0
        # Add the various parameters in topology_st.belist
        belist.append( { 'be': be,
                         'suffix': suffix,
                         'ldif': ldif,
                         'vlv_search' : vlv_search,
                         'vlv_index' : vlv_index, })

    # Make sure that our backend are not already present.
    # and gran userRoot be while doing that
    be0 = None
    for be in Backends(inst).list():
        bename = be.get_attr_val_utf8_l('cn')
        if bename in [ 'be1', 'be2' ]:
            be.delete()
        if bename == 'userroot':
            be0 = be

    # Add userRoot backend to the backend list
    be0ldif = f'{inst.get_ldif_dir()}/db-userroot.ldif'
    assert Tasks(inst).exportLDIF(suffix=DEFAULT_SUFFIX,
                                  output_file=be0ldif,
                                  args={TASK_WAIT: True}) == 0
    belist.append( { 'be': be0,
                     'suffix': DEFAULT_SUFFIX,
                     'ldif': be0ldif, })
    # Then add two new backends
    setup_vlv_and_backend(inst, "be1")
    setup_vlv_and_backend(inst, "be2")
    topology_st.belist = belist
    return topology_st


@pytest.fixture
def freeipa(topology_st):
    # generate a standalone instance with same vlv config than freeipa
    inst = topology_st.standalone
    datadir = os.path.join(os.path.dirname(__file__), '../../data/freeipa/issue6136')
    # Stop instance
    inst.stop()
    # Copy schema
    for file in glob.glob(f'{datadir}/schema/*.ldif'):
        target = f'{inst.schemadir}/{os.path.basename(file)}'
        log.info(f'Copying Schema {file} to {target}')
        copyfile(file, target)
    # append dse
    file = f'{datadir}/dse.ldif'
    target = f'{inst.confdir}/{os.path.basename(file)}'
    log.info(f'Appending ldif {file} to dse {target}')
    with open(file, 'r') as fin:
        with open(target, 'a') as fout:
            fout.write(fin.read())
    # import ipaca
    file = f'{datadir}/ipaca.ldif'
    log.info(f'Importing ldif {file} to ipaca')
    assert inst.ldif2db('ipaca', None, None, None, file)
    # restart instance
    inst.restart()
    return topology_st


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
    entries = M2.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, "(uid=*)")
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

    add_users(inst, 5000)

    conn = open_new_ldapi_conn(inst.serverid)
    assert len(conn.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, "(uid=*)")) > 0
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
    assert len(conn.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, "(uid=*)")) > 0
    check_vlv_search(conn)


def get_db_filename(vlvname, prefix='', iscache=False):
    # Normalize the vlv db name
    chars = set('abcdefghijklmnopqrstuvwxyz0123456789')
    vlvname = ''.join(c for c in vlvname.lower() if c in chars)
    bename = 'userRoot'
    cache = ''
    if iscache:
        cache = '~recno-cache/'
    if get_default_db_lib() == "mdb":
        bename = bename.lower()
    return f'{prefix}{bename}/{cache}vlv#{vlvname}.db'


def verify_keys_in_subdb(vlvname, inst, count):
    """
    Verify that vlv index as the expected number of keys.
    """
    dblib = get_default_db_lib()
    dbfile = get_db_filename(vlvname, prefix=f'{inst.dbdir}/')
    output = run_dbscan(['-D', dblib, '-f', dbfile])
    # Count all lines except the MDB environment ones.
    found = output.count('\n') - output.count('MDB environment')
    if found != count:
        log.info(f'Running: dbscan -D {dblib} -f "{dbfile}"')
        log.info(f'dbscan output for vlv {vlvname} is: {output}')
    assert found == count


def verify_vlv_subdb_names(vlvname, inst, count=None):
    """
    Verify that vlv index and cache sub database names are conistent on lmdb.
    """
    dblib = get_default_db_lib()
    if dblib == "bdb":
        # Let the time to sync bdb txn log to the db
        prefix = ''
        time.sleep(60)
    else:
        prefix = f'{inst.dbdir}/'
    output = run_dbscan(['-D', dblib, '-L', inst.dbdir])
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

    assert get_db_filename(vlvname, prefix=prefix) in vlvsubdbs
    if dblib == "mdb":
        assert get_db_filename(vlvname, prefix=prefix, iscache=True) in vlvsubdbs
        assert len(vlvsubdbs) == 2 * nbvlvindex
    else:
        assert len(vlvsubdbs) == nbvlvindex
    if count:
        verify_keys_in_subdb(vlvname, inst, count)


def bootstrap_replication(M1, M2):
    """
    Setup a replication manager entry on M2 and update the
    M1 -> M2 replication agreement to use it.
    """
    # Create a repl manager entry on M2
    repl_manager_password = "Secret12"
    brm = BootstrapReplicationManager(M2)
    brm.create(properties={
        'cn': brm.common_name,
        'userPassword': repl_manager_password
    })
    # Let M1 -> M2 agreement use that entry
    replica1 = Replicas(M1).get(DEFAULT_SUFFIX)
    replica2 = Replicas(M2).get(DEFAULT_SUFFIX)
    agmt = replica1.get_agreements().list()[0]
    agmt.replace_many(("nsds5ReplicaBindDN", brm.dn),
                      ("nsds5ReplicaCredentials", repl_manager_password))
    replica2.remove_all('nsDS5ReplicaBindDNGroup')
    # Assign the entry to the replica
    replica2.replace('nsDS5ReplicaBindDN', brm.dn)


def remove_database(inst):
    # remove the database
    inst.stop()
    for file in glob.glob(f'{inst.dbdir}/*'):
        if os.path.islink(file):
            continue
        if os.path.isfile(file):
            os.remove(file)
        if os.path.isdir(file):
            rmtree(file)
    inst.start()


@pytest.mark.skipif(get_default_db_lib() == "bdb", reason="Not supported over bdb")
def test_vlv_cache_subdb_names(topology_m2):
    """
    Testing that vlv index cache sub database name is consistent.

    :id: 7a138006-aa24-11ee-9d5f-482ae39447e5
    :setup: Replication with two suppliers.
    :steps:
        1. Generate vlvSearch and vlvIndex entries
        2. Add users and wait that they get replicated.
        3. Check subdb names and perform vlv searches
        4. Destroy supplierr2 database and restart
        5. Recreate M2 tree with a new user
        6. Reinit M2 from M1
        7. Check subdb names and perform vlv searches
    :expectedresults:
        1. Should Success.
        2. Should Success.
        3. Should Success.
        4. Should Success.
        5. Should Success.
        6. Should Success.
        7. Should Success.
    """

    NUM_USERS = 50
    M1 = topology_m2.ms["supplier1"]
    M2 = topology_m2.ms["supplier2"]
    repl = ReplicationManager(DEFAULT_SUFFIX)

    # Clean test_bulk_import_when_the_backend_with_vlv_was_recreated leftover that cause trouble
    entries = [ UserAccount(M1, dn=f'uid=testuser{uid},DEFAULT_SUFFIX') for uid in
        range(STARTING_UID_INDEX, STARTING_UID_INDEX+NUM_USERS) ]
    entries.append(VLVIndex(M2, dn='cn=vlvIdx,cn=vlvSrch,cn=userRoot,cn=ldbm database,cn=plugins,cn=config'))
    entries.append(VLVSearch(M2, dn='cn=vlvSrch,cn=userRoot,cn=ldbm database,cn=plugins,cn=config'))
    for entry in entries:
        if entry.exists():
            entry.delete()
    repl.wait_for_replication(M1, M2)
    # Restart the instance to workaround https://github.com/389ds/389-ds-base/issues/6029
    if get_default_db_lib() == "bdb":
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
    count = len(conn.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, "(uid=*)"))
    assert count > 0
    check_vlv_search(conn, offset=23)
    verify_vlv_subdb_names(vlvname, M2, count=count)

    # Destroy the database and restart
    # (Ensure that the db is really destroyed.
    #  Avoid getting tricked if database is not properly cleared by the reimport)
    #  by a double bug (index keys not properly reset)
    M2.stop()
    remove_database(M2)
    M2.start()

    # Recreate tree with a new user on M2 (To check that its key get removed)
    # This user is not replicated towards M1 because the db has been
    # recreated with a different generation number
    Domain(M2, DEFAULT_SUFFIX).create(properties={'dc': 'example'})
    OrganizationalUnits(M2, DEFAULT_SUFFIX).create(properties={'ou': 'People'})
    users = UserAccounts(M2, DEFAULT_SUFFIX)
    add_user(M2, users, 2*STARTING_UID_INDEX)
    bootstrap_replication(M1, M2)

    # Reinit M2 from M1
    agmt = Agreements(M1).list()[0]
    agmt.begin_reinit()
    (done, error) = agmt.wait_reinit()
    assert done is True
    assert error is False

    # Check subdb names and perform vlv searches
    conn = open_new_ldapi_conn(M2.serverid)
    count = len(conn.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, "(uid=*)"))
    assert count > 0
    check_vlv_search(conn, offset=23)
    verify_vlv_subdb_names(vlvname, M2, count=count)


@contextmanager
def custom_dse(*args, **kwds):
    """ A Constext manager to handle a special configuration.
    That restores the original config when exiting out of the context.

    Usage:
        with custom_dse(inst) as newdsename, olddsename:
            update(newdsename)
            inst.start()
            action ...
    """
    inst = args[0]
    olddsename = f'{inst.ds_paths.config_dir}/dse.ldif.vlvtc1'
    newdsename = f'{inst.ds_paths.config_dir}/dse.ldif'
    inst.stop()
    assert os.path.isfile(newdsename)
    os.replace(newdsename, olddsename)
    copyfile(olddsename, newdsename)
    try:
        yield (newdsename, olddsename)
    finally:
        inst.stop()
        if os.path.isfile(olddsename):
            os.remove(newdsename)
            os.rename(olddsename, newdsename)
        inst.start()


def test_vlv_start_with_bad_dse(topology_st):
    """
    Testing that server does not crash if dse.ldif is in disorder.

    :id: ceb3e9e0-b607-11ee-845a-482ae39447e5
    :setup: Standalone
    :steps:
        1. Append vlvIndex entries then vlvSearch entry in the dse.ldif
        2. Restart the server
    :expectedresults:
        1. Should Success.
        2. Should Success.
    """
    inst = topology_st.standalone
    # Use a context to ensure that the instance config is restored and instance
    # restarted at the end of the test.
    with custom_dse(inst) as (newdsename, olddsename):
        # Step 1: Append vlvIndex entries then vlvSearch entry in the dse.ldif
        with open(newdsename, 'at') as dse:
            dse.write(BAD_DSE_DATA)
        # Step 2: Restart the server
        inst.start()

@pytest.mark.skipif(get_default_db_lib() == "bdb", reason="bdb may hang because of github #6029")
@pytest.mark.parametrize("prefix", ( 'vlv', 'vl-v' ))
@pytest.mark.parametrize("basedn", ( DEFAULT_SUFFIX,  f'ou=People,{DEFAULT_SUFFIX}' ))
def test_vlv_reindex(topology_st, prefix, basedn):
    """Test VLV reindexing.

    :id: d5dc0d8e-cbe6-11ee-95b1-482ae39447e5
    :setup: Standalone instance.
    :steps:
        1. Cleanup leftover from previous tests
        2. Add users
        3. Create new VLVs and do the reindex.
        4. Test the new VLVs.
        5. Restart the server and test again the VLVs
    :expectedresults:
        1. Should Success.
        2. Should Success.
        3. Should Success.
        4. Should Success.
        5. Should Success.
    """

    NUM_USERS = 50
    inst = topology_st.standalone
    reindex_task = Tasks(inst)

    # Clean previous tests leftover that cause trouble
    cleanup(inst)

    add_users(inst, NUM_USERS)

    # Create and reindex VLVs
    vlv_search, vlv_index = create_vlv_search_and_index(inst, prefix=prefix, basedn=basedn)
    assert reindex_task.reindex(
        suffix=DEFAULT_SUFFIX,
        attrname=vlv_index.rdn,
        args={TASK_WAIT: True},
        vlv=True
    ) == 0

    conn = open_new_ldapi_conn(inst.serverid)
    count = len(conn.search_s(basedn, ldap.SCOPE_SUBTREE, "(uid=*)"))
    assert count > 0
    verify_vlv_subdb_names(vlv_index.rdn, inst, count=count)
    check_vlv_search(conn, offset=23, basedn=basedn)

    # Restart and check again
    inst.restart()
    conn = open_new_ldapi_conn(inst.serverid)
    count = len(conn.search_s(basedn, ldap.SCOPE_SUBTREE, "(uid=*)"))
    assert count > 0
    verify_vlv_subdb_names(vlv_index.rdn, inst, count=count)
    check_vlv_search(conn, offset=23, basedn=basedn)


@pytest.mark.skipif(get_default_db_lib() == "bdb", reason="bdb may hang because of github #6029")
@pytest.mark.parametrize("prefix", ( 'vlv', 'vl-v' ))
@pytest.mark.parametrize("basedn", ( DEFAULT_SUFFIX,  f'ou=People,{DEFAULT_SUFFIX}' ))
def test_vlv_offline_import(topology_st, prefix, basedn):
    """Test VLV after off line import.

    :id: 8732d7a8-e851-11ee-9d63-482ae39447e5
    :setup: Standalone instance.
    :steps:
        1. Cleanup leftover from previous tests
        2. Add users
        3. Create new VLVs and do the reindex.
        4. Test the new VLVs.
        5. Export userRoot backend
        6. Stop the instance
        7. Perform off-line import
        8. Restart instance and check the vlvs


    :expectedresults:
        1. Should Success.
        2. Should Success.
        3. Should Success.
        4. Should Success.
        5. Should Success.
        6. Should Success.
        7. Should Success.
        8. Should Success.
    """

    NUM_USERS = 50
    inst = topology_st.standalone
    tasks = Tasks(inst)

    # Clean previous tests leftover that cause trouble
    cleanup(inst)

    add_users(inst, NUM_USERS)

    # Create and reindex VLVs
    vlv_search, vlv_index = create_vlv_search_and_index(inst, prefix=prefix, basedn=basedn)
    assert tasks.reindex(
        suffix=DEFAULT_SUFFIX,
        attrname=vlv_index.rdn,
        args={TASK_WAIT: True},
        vlv=True
    ) == 0

    conn = open_new_ldapi_conn(inst.serverid)
    count = len(conn.search_s(basedn, ldap.SCOPE_SUBTREE, "(uid=*)"))
    assert count > 0
    verify_vlv_subdb_names(vlv_index.rdn, inst, count=count)
    check_vlv_search(conn, offset=23, basedn=basedn)

    # Export to ldif
    ldif = f'{inst.get_ldif_dir()}/db.ldif'
    assert tasks.exportLDIF(
        suffix=DEFAULT_SUFFIX,
        output_file=ldif,
        args={TASK_WAIT: True}
    ) == 0

    # Perform off-line import
    inst.stop()
    assert inst.ldif2db('userRoot', None, None, None, ldif)

    # Restart and check again the vlv
    inst.start()
    conn = open_new_ldapi_conn(inst.serverid)
    count = len(conn.search_s(basedn, ldap.SCOPE_SUBTREE, "(uid=*)"))
    assert count > 0
    verify_vlv_subdb_names(vlv_index.rdn, inst, count=count)
    check_vlv_search(conn, offset=23, basedn=basedn)


def test_vlv_as_freeipa_backup_restore(freeipa):
    """Test vlv as if freeipa restore was performed.

    :id: fb0621da-e879-11ee-900e-482ae39447e5
    :setup: Standalone instance with freeipa test config.
    :steps:
        1. Check that standard search returns 16 valid certificate
        2. Check that vlv search returns 16 valid certificate

    :expectedresults:
        1. Should Success.
        2. Should Success.
    """

    DATE_LIMIT = '20260323135138Z'
    EXPECTED_COUNT = 12
    inst = freeipa.standalone
    inst.restart()
    conn = open_new_ldapi_conn(inst.serverid)
    basedn = 'ou=certificateRepository,ou=ca,o=ipaca'
    count = len(conn.search_s(basedn, ldap.SCOPE_SUBTREE, f"(&(certStatus=VALID)(notAfter<={DATE_LIMIT}))"))
    assert count == EXPECTED_COUNT

    vlv_control = VLVRequestControl(criticality=True,
        before_count=200,
        after_count=0,
        offset=None,
        content_count=None,
        greater_than_or_equal='20260323135138Z',
        context_id=None)

    sss_control = SSSRequestControl(criticality=True, ordering_rules=['notAfter'])
    result = conn.search_ext_s(
        base=basedn,
        scope=ldap.SCOPE_SUBTREE,
        filterstr='(certStatus=VALID)',
        serverctrls=[vlv_control, sss_control]
    )
    assert len(result) == EXPECTED_COUNT


def test_vlv_scope_one_on_two_backends(vlv_setup_with_two_backend):
    """Test one level scoped vlv on two backends.

    :id: 7652a2ea-eab0-11ee-891f-482ae39447e5
    :setup: Standalone instance with two backens and one level scoped vlv
    :steps:
        1. Check that standard search returns 16 valid certificate
        2. Check that vlv search returns 16 valid certificate

    :expectedresults:
        1. Should Success.
        2. Should Success.
    """
    inst = vlv_setup_with_two_backend.standalone
    tasks = Tasks(inst)
    conn = open_new_ldapi_conn(inst.serverid)
    assert len(conn.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, "(uid=*)")) > 0
    # Check that vlv are working as excpected
    for tmpd in vlv_setup_with_two_backend.belist:
        if 'vlv_index' in tmpd:
            check_vlv_search(conn, offset=6, basedn=tmpd['suffix'])

    # Export userRoot backend
    ldif = f'{inst.get_ldif_dir()}/db.ldif'
    assert tasks.exportLDIF(
        suffix=DEFAULT_SUFFIX,
        output_file=ldif,
        args={TASK_WAIT: True}
    ) == 0
    # Remove the db
    remove_database(inst)
    conn = open_new_ldapi_conn(inst.serverid)
    # Check that vlv are broken
    for tmpd in vlv_setup_with_two_backend.belist:
        if 'vlv_index' in tmpd:
            with pytest.raises(ldap.NO_SUCH_OBJECT):
                check_vlv_search(conn, offset=6, basedn=tmpd['suffix'])
    # Reimport the backends
    for tmpd in vlv_setup_with_two_backend.belist:
        assert tasks.importLDIF(
            suffix=tmpd['suffix'],
            input_file=tmpd['ldif'],
            args={TASK_WAIT: True}, ) == 0
    # Check that vlv are working as excpected
    for tmpd in vlv_setup_with_two_backend.belist:
        if 'vlv_index' in tmpd:
            check_vlv_search(conn, offset=6, basedn=tmpd['suffix'])


if __name__ == "__main__":
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
