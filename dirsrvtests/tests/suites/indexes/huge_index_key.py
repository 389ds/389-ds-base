# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import time
import os
import pytest
import ldap
import logging
from lib389._constants import DEFAULT_BENAME, DEFAULT_SUFFIX
from lib389.index import Indexes
from lib389.backend import Backends, DatabaseConfig
from lib389.idm.user import UserAccounts
from lib389.idm.group import Groups, Group
from lib389.topologies import topology_st as topo
from lib389.utils import ds_is_older, get_default_db_lib
from lib389.plugins import MemberOfPlugin
from lib389 import DirSrv
pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

users_list = []

@pytest.fixture(scope="function")
def add_users(request, topo):
    """
    Add users
    """
    users_num = 200
    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX, rdn=None)
    for num in range(users_num):
        USER_NAME = "test_{:0>3d}".format( num )
        user = users.create(properties={
            'uid': USER_NAME,
            'sn': USER_NAME,
            'cn': USER_NAME,
            'uidNumber': f'{num}',
            'gidNumber': f'{num}',
            'homeDirectory': f'/home/{USER_NAME}'
        })
        users_list.append(user)

    def fin():
        """
        Removes users.
        """
        if not topo.standalone.status():
            topo.standalone.start()
        for user in users_list:
            user.delete()

    request.addfinalizer(fin)


def test_huge_index_key(topo, add_users):
    """
    Test very long indexed attribute values (that should be hashed on mdb)

    :id: 4bbd0ee2-0108-11ec-a5ce-482ae39447e5
    :customerscenario: False
    :setup: Standalone instance
    :steps:
        1. Add users
        2. Change nsslapd-idlistscanlimit to a smaller value to accelerate the reproducer
        3. Replace sn with a 600 bytes value
        4. equality search for the sn
        5. Range search including the sn
        6. Replace sn back with small value
        7. equality search for the sn
        8. Range search including the sn
    :expectedresults:
        1. Should succeed
        2. Should succeed
        3. Should succeed
        4. Should succeed and have exactly 1 result
           search should be indexed.
        5. Should succeed and have exactly 3 results
           on bdb: search should be indexed.
           on mdb: search should be unindexed.
        6. Should succeed
        7. Should succeed and have exactly 1 result
           search should be indexed.
        8. Should succeed and have exactly 3 results
           search should be indexed.
    """
    inst = topo.standalone
    ldc = super(DirSrv, inst);  # ldap connection to be able to use
                                #  the SimpleLDAPObject methods 
    shortsn='test_020'
    test_user=users_list[20]
    log.debug(f'Check user {test_user} sn: {test_user.get_attr_val_utf8("sn")}')
    assert(test_user)

    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX, rdn=None)
    log.debug(f'Check users {users.list()}')

    sn600b = shortsn + \
             "0001abcdefghijklmnopqrstuvwxyz0001abcdefghijklmnopqrstuvwxyz" + \
             "0002abcdefghijklmnopqrstuvwxyz0001abcdefghijklmnopqrstuvwxyz" + \
             "0003abcdefghijklmnopqrstuvwxyz0001abcdefghijklmnopqrstuvwxyz" + \
             "0004abcdefghijklmnopqrstuvwxyz0001abcdefghijklmnopqrstuvwxyz" + \
             "0005abcdefghijklmnopqrstuvwxyz0001abcdefghijklmnopqrstuvwxyz" + \
             "0006abcdefghijklmnopqrstuvwxyz0001abcdefghijklmnopqrstuvwxyz" + \
             "0007abcdefghijklmnopqrstuvwxyz0001abcdefghijklmnopqrstuvwxyz" + \
             "0008abcdefghijklmnopqrstuvwxyz0001abcdefghijklmnopqrstuvwxyz" + \
             "0009abcdefghijklmnopqrstuvwxyz0001abcdefghijklmnopqrstuvwxyz" + \
             "0010abcdefghijklmnopqrstuvwxyz0001abcdefghijklmnopqrstuvwxyz"

    test_user.replace('sn', sn600b);
    result = ldc.search_s(base=DEFAULT_SUFFIX, scope=ldap.SCOPE_SUBTREE, filterstr=f'(sn={sn600b})')
    assert (len(result) == 1)
    assert (not inst.searchAccessLog("notes=U"))
    result = ldc.search_s(base=DEFAULT_SUFFIX, scope=ldap.SCOPE_SUBTREE, filterstr=f'(&(sn>=test_019)(sn<=test_021))')
    #with pytest.raises(ldap.INVALID_SYNTAX):
    assert (len(result) == 3)
    if (get_default_db_lib() == "bdb"):
        assert (not inst.searchAccessLog("notes=U"))
    else:
        assert (inst.searchAccessLog("notes=U"))
    inst.deleteLog(inst.accesslog);
    test_user.replace('sn', shortsn);
    result = ldc.search_s(base=DEFAULT_SUFFIX, scope=ldap.SCOPE_SUBTREE, filterstr=f'(sn={shortsn})')
    assert (len(result) == 1)
    assert (not inst.searchAccessLog("notes=U"))
    result = ldc.search_s(base=DEFAULT_SUFFIX, scope=ldap.SCOPE_SUBTREE, filterstr=f'(&(sn>=test_019)(sn<=test_021))')
    assert (len(result) == 3)
    assert (not inst.searchAccessLog("notes=U"))


if __name__ == "__main__":
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
