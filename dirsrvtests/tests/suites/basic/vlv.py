# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2021 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest, time
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st
from lib389.replica import *
from lib389._constants import *
from lib389.index import *
from lib389.mappingTree import *
from lib389.backend import *
from lib389.idm.user import UserAccount, UserAccounts
import ldap
from ldap.controls.vlv import VLVRequestControl
from ldap.controls.sss import SSSRequestControl


pytestmark = pytest.mark.tier1

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


def open_new_ldapi_conn(dsinstance):
    ldapurl, certdir = get_ldapurl_from_serverid(dsinstance)
    # Only ldapi is handled in this functon
    assert 'ldapi://' in ldapurl
    conn = ldap.initialize(ldapurl)
    # Send SASL bind request for mechanism EXTERNAL
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
    r = conn.search_ext_s(base='dc=example,dc=com', scope=ldap.SCOPE_SUBTREE, filterstr='(uid=*)', serverctrls=[vlv_control, sss_control])
    imin=offset+999-before_count
    if imin < 1000:
        imin = 1000
    imax=offset+999+after_count
    i=imin
    for dn,entry in r:
        assert i <= imax
        expected_dn = f'uid=testuser{i},ou=People,dc=example,dc=com'
        print(f'found {repr(dn)} expected {expected_dn}')
        assert dn.lower() == expected_dn.lower()
        i=i+1



def add_users(topology_st, users_num):
    users = UserAccounts(topology_st, DEFAULT_SUFFIX)
    log.info('Adding %d users' % users_num)
    for i in range(0, users_num):
        uid = 1000 + i
        users.create(properties={
            'uid': 'testuser%d' % uid,
            'cn': 'testuser%d' % uid,
            'sn': 'user',
            'uidNumber': '%d' % uid,
            'gidNumber': '%d' % uid,
            'homeDirectory': '/home/testuser%d' % uid
        })


@pytest.mark.DS47966
def test_vlv(topology_st):
    """
    Testing bulk import when the backend with VLV was recreated.
    If the test passes without the server crash, 47966 is verified.

    :id: 512963fa-fe02-11e8-b1d3-8c16451d917b
    :setup: Replication with two suppliers.
    :steps:
        1. Generate vlvSearch entry
        2. Generate vlvIndex entry
        3. Add 5K users
        4. Search users
        5. test a vlv search result
    :expectedresults:
        1. Should Success.
        2. Should Success.
        3. Should Success.
        4. Should Success.
        5. Should Success.
    """
    inst = topology_st.standalone

    # generate vlvSearch entry
    properties_for_search = {
        "objectclass": ["top", "vlvSearch"],
        "cn": "vlvSrch",
        "vlvbase": DEFAULT_SUFFIX,
        "vlvfilter": "(uid=*)",
        "vlvscope": "2",
    }
    vlv_searches = VLVSearch(inst)
    userroot_vlvsearch = vlv_searches.create(
        basedn="cn=userRoot,cn=ldbm database,cn=plugins,cn=config",
        properties=properties_for_search,
    )
    assert "cn=vlvSrch,cn=userRoot,cn=ldbm database,cn=plugins,cn=config" in inst.getEntry(
        "cn=vlvSrch,cn=userRoot,cn=ldbm database,cn=plugins,cn=config").dn
    # generate vlvIndex entry
    properties_for_index = {
        "objectclass": ["top", "vlvIndex"],
        "cn": "vlvIdx",
        "vlvsort": "cn",
    }
    vlv_index = VLVIndex(inst)
    userroot_index = vlv_index.create(
        basedn="cn=vlvSrch,cn=userRoot,cn=ldbm database,cn=plugins,cn=config",
        properties=properties_for_index,
    )
    assert "cn=vlvIdx,cn=vlvSrch,cn=userRoot,cn=ldbm database,cn=plugins,cn=config" in inst.getEntry(
        "cn=vlvIdx,cn=vlvSrch,cn=userRoot,cn=ldbm database,cn=plugins,cn=config").dn

    # opening a new LDAPSimpleObject connection avoid the warning we got when using directly inst
    conn = open_new_ldapi_conn(inst.serverid)
    add_users(inst, 5000);
    entries = conn.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, "(cn=*)")
    assert len(entries) > 0
    check_vlv_search(conn)


if __name__ == "__main__":
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
