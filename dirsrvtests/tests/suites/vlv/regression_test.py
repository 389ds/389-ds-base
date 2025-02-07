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
from lib389.idm.organization import Organization
from lib389.idm.organizationalunit import OrganizationalUnits
from ldap.controls.vlv import VLVRequestControl
from ldap.controls.sss import SSSRequestControl

pytestmark = pytest.mark.tier1

DEMO_PW = 'secret12'

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


def open_new_ldapi_conn(dsinstance):
    ldapurl, certdir = get_ldapurl_from_serverid(dsinstance)
    assert 'ldapi://' in ldapurl
    conn = ldap.initialize(ldapurl)
    conn.sasl_interactive_bind_s("", ldap.sasl.external())
    return conn


def check_vlv_search(conn):
    before_count=1
    after_count=3
    offset=3501

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
    imin = offset + 998 - before_count
    imax = offset + 998 + after_count

    for i, (dn, entry) in enumerate(result, start=imin):
        assert i <= imax
        expected_dn = f'uid=testuser{i},ou=People,dc=example,dc=com'
        log.debug(f'found {dn} expected {expected_dn}')
        assert dn.lower() == expected_dn.lower()


def add_users(inst, users_num):
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    log.info(f'Adding {users_num} users')
    for i in range(0, users_num):
        uid = 1000 + i
        user_properties = {
            'uid': f'testuser{uid}',
            'cn': f'testuser{uid}',
            'sn': 'user',
            'uidNumber': str(uid),
            'gidNumber': str(uid),
            'homeDirectory': f'/home/testuser{uid}'
        }
        users.create(properties=user_properties)



def create_vlv_search_and_index(inst, basedn=DEFAULT_SUFFIX, bename='userRoot',
                                scope=ldap.SCOPE_SUBTREE, prefix="vlv", vlvsort="cn"):
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
        "vlvsort": vlvsort,
    }
    vlv_index.create(
        basedn=f"cn={prefix}Srch,cn={bename},cn=ldbm database,cn=plugins,cn=config",
        properties=vlv_index_properties
    )
    return vlv_searches, vlv_index

class BackendHandler:
    def __init__(self, inst, bedict, scope=ldap.SCOPE_ONELEVEL):
        self.inst = inst
        self.bedict = bedict
        self.bes = Backends(inst)
        self.scope = scope
        self.data = {}

    def find_backend(self, bename):
        for be in self.bes.list():
            if be.get_attr_val_utf8_l('cn') == bename:
                return be
        return None

    def cleanup(self):
        benames =  list(self.bedict.keys())
        benames.reverse()
        for bename in benames:
            be = self.find_backend(bename)
            if be:
                be.delete()

    def setup(self):
        # Create backends, add vlv index and populate the backends.
        for bename,suffix in self.bedict.items():
            be = self.bes.create(properties={
                'cn': bename,
                'nsslapd-suffix': suffix,
            })
            # Add suffix entry
            Organization(self.inst, dn=suffix).create(properties={ 'o': bename, })
            # Configure vlv
            vlv_search, vlv_index = create_vlv_search_and_index(
                self.inst, basedn=suffix,
                bename=bename, scope=self.scope,
                prefix=f'vlv_1lvl_{bename}')
            # Reindex
            reindex_task = Tasks(self.inst)
            assert reindex_task.reindex(
                suffix=suffix,
                attrname=vlv_index.rdn,
                args={TASK_WAIT: True},
                vlv=True
            ) == 0
            # Add ou=People entry
            OrganizationalUnits(self.inst, suffix).create(properties={'ou': 'People'})
            # Add another ou that will be deleted before the export
            # so that import will change the vlv search basedn entryid
            ou2 = OrganizationalUnits(self.inst, suffix).create(properties={'ou': 'dummy ou'})
            # Add a demo user so that vlv_check is happy
            dn = f'uid=demo_user,ou=people,{suffix}'
            UserAccount(self.inst, dn=dn).create( properties= {
                    'uid': 'demo_user',
                    'cn': 'Demo user',
                    'sn': 'Demo user',
                    'uidNumber': '99998',
                    'gidNumber': '99998',
                    'homeDirectory': '/var/empty',
                    'loginShell': '/bin/false',
                    'userpassword': DEMO_PW })
            # Add regular user
            add_users(self.inst, 10)
            # Removing ou2
            ou2.delete()
            # And export
            tasks = Tasks(self.inst)
            ldif = f'{self.inst.get_ldif_dir()}/db-{bename}.ldif'
            assert tasks.exportLDIF(suffix=suffix,
                                    output_file=ldif,
                                    args={TASK_WAIT: True}) == 0
            # Add the various parameters in topology_st.belist
            self.data[bename] = { 'be': be,
                                 'suffix': suffix,
                                 'ldif': ldif,
                                 'vlv_search' : vlv_search,
                                 'vlv_index' : vlv_index,
                                 'dn' : dn}


@pytest.fixture
def vlv_setup_with_uid_mr(topology_st, request):
    inst = topology_st.standalone
    bename = 'be1'
    besuffix = f'o={bename}'
    beh = BackendHandler(inst, { bename: besuffix })

    def fin():
        # Cleanup function
        if not DEBUGGING and inst.exists() and inst.status():
            beh.cleanup()

    request.addfinalizer(fin)

    # Make sure that our backend are not already present.
    beh.cleanup()

    # Then add the new backend
    beh.setup()

    index = Index(inst, f'cn=uid,cn=index,cn={bename},cn=ldbm database,cn=plugins,cn=config')
    index.add('nsMatchingRule', '2.5.13.2')
    reindex_task = Tasks(inst)
    assert reindex_task.reindex(
        suffix=besuffix,
        attrname='uid',
        args={TASK_WAIT: True}
    ) == 0

    topology_st.beh = beh
    return topology_st


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

    add_users(inst, 5000)

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


def test_vlv_with_mr(vlv_setup_with_uid_mr):
    """
    Testing vlv having specific matching rule

    :id: 5e04afe2-beec-11ef-aa84-482ae39447e5
    :setup: Standalone with uid have a matching rule index
    :steps:
        1. Append vlvIndex entries then vlvSearch entry in the dse.ldif
        2. Restart the server
    :expectedresults:
        1. Should Success.
        2. Should Success.
    """
    inst = vlv_setup_with_uid_mr.standalone
    beh = vlv_setup_with_uid_mr.beh
    bename, besuffix = next(iter(beh.bedict.items()))
    vlv_searches, vlv_index = create_vlv_search_and_index(
                                inst, basedn=besuffix, bename=bename,
                                vlvsort="uid:2.5.13.2")
    # Reindex the vlv
    reindex_task = Tasks(inst)
    assert reindex_task.reindex(
        suffix=besuffix,
        attrname=vlv_index.rdn,
        args={TASK_WAIT: True},
        vlv=True
    ) == 0

    inst.restart()
    users = UserAccounts(inst, besuffix)
    user_properties = {
        'uid': f'a new testuser',
        'cn': f'a new testuser',
        'sn': 'user',
        'uidNumber': '0',
        'gidNumber': '0',
        'homeDirectory': 'foo'
    }
    user = users.create(properties=user_properties)
    user.delete()
    assert inst.status()



if __name__ == "__main__":
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
