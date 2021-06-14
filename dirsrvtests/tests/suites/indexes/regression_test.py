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
from lib389._constants import DEFAULT_BENAME, DEFAULT_SUFFIX
from lib389.index import Indexes
from lib389.backend import Backends, DatabaseConfig
from lib389.idm.user import UserAccounts
from lib389.idm.group import Groups, Group
from lib389.topologies import topology_st as topo
from lib389.utils import ds_is_older
from lib389.plugins import MemberOfPlugin

pytestmark = pytest.mark.tier1


@pytest.fixture(scope="function")
def add_a_group_with_users(request, topo):
    """
    Add a group and users, which are members of this group.
    """
    groups = Groups(topo.standalone, DEFAULT_SUFFIX, rdn=None)
    group = groups.create(properties={'cn': 'test_group'})
    users_list = []
    users_num = 100
    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX, rdn=None)
    for num in range(users_num):
        USER_NAME = f'test_{num}'
        user = users.create(properties={
            'uid': USER_NAME,
            'sn': USER_NAME,
            'cn': USER_NAME,
            'uidNumber': f'{num}',
            'gidNumber': f'{num}',
            'homeDirectory': f'/home/{USER_NAME}'
        })
        users_list.append(user)
        group.add_member(user.dn)

    def fin():
        """
        Removes group and users.
        """
        # If the server crashed, start it again to do the cleanup
        if not topo.standalone.status():
            topo.standalone.start()
        for user in users_list:
            user.delete()
        group.delete()

    request.addfinalizer(fin)


@pytest.fixture(scope="function")
def set_small_idlistscanlimit(request, topo):
    """
    Set nsslapd-idlistscanlimit to a smaller value to accelerate the reproducer
    """
    db_cfg = DatabaseConfig(topo.standalone)
    old_idlistscanlimit = db_cfg.get_attr_vals_utf8('nsslapd-idlistscanlimit')
    db_cfg.set([('nsslapd-idlistscanlimit', '100')])
    topo.standalone.restart()

    def fin():
        """
        Set nsslapd-idlistscanlimit back to the default value
        """
        # If the server crashed, start it again to do the cleanup
        if not topo.standalone.status():
            topo.standalone.start()
        db_cfg.set([('nsslapd-idlistscanlimit', old_idlistscanlimit)])
        topo.standalone.restart()

    request.addfinalizer(fin)

#unstable or unstatus tests, skipped for now
@pytest.mark.flaky(max_runs=2, min_passes=1)
@pytest.mark.skipif(ds_is_older("1.4.4.4"), reason="Not implemented")
def test_reindex_task_creates_abandoned_index_file(topo):
    """
    Recreating an index for the same attribute but changing
    the case of for example 1 letter, results in abandoned indexfile

    :id: 07ae5274-481a-4fa8-8074-e0de50d89ac6
    :customerscenario: True
    :setup: Standalone instance
    :steps:
        1. Create a user object with additional attributes:
           objectClass: mozillaabpersonalpha
           mozillaCustom1: xyz
        2. Add an index entry mozillacustom1
        3. Reindex the backend
        4. Check the content of the index (after it has been flushed to disk) mozillacustom1.db
        5. Remove the index
        6. Notice the mozillacustom1.db is removed
        7. Recreate the index but now use the exact case as mentioned in the schema
        8. Reindex the backend
        9. Check the content of the index (after it has been flushed to disk) mozillaCustom1.db
        10. Check that an ldapsearch does not return a result (mozillacustom1=xyz)
        11. Check that an ldapsearch returns the results (mozillaCustom1=xyz)
        12. Restart the instance
        13. Notice that an ldapsearch does not return a result(mozillacustom1=xyz)
        14. Check that an ldapsearch does not return a result (mozillacustom1=xyz)
        15. Check that an ldapsearch returns the results (mozillaCustom1=xyz)
        16. Reindex the backend
        17. Notice the second indexfile for this attribute
        18. Check the content of the index (after it has been flushed to disk) no mozillacustom1.db
        19. Check the content of the index (after it has been flushed to disk) mozillaCustom1.db
    :expectedresults:
        1. Should Success.
        2. Should Success.
        3. Should Success.
        4. Should Success.
        5. Should Success.
        6. Should Success.
        7. Should Success.
        8. Should Success.
        9. Should Success.
        10. Should Success.
        11. Should Success.
        12. Should Success.
        13. Should Success.
        14. Should Success.
        15. Should Success.
        16. Should Success.
        17. Should Success.
        18. Should Success.
        19. Should Success.
    """

    inst = topo.standalone
    attr_name = "mozillaCustom1"
    attr_value = "xyz"

    users = UserAccounts(inst, DEFAULT_SUFFIX)
    user = users.create_test_user()
    user.add("objectClass", "mozillaabpersonalpha")
    user.add(attr_name, attr_value)

    backends = Backends(inst)
    backend = backends.get(DEFAULT_BENAME)
    indexes = backend.get_indexes()
    index = indexes.create(properties={
        'cn': attr_name.lower(),
        'nsSystemIndex': 'false',
        'nsIndexType': ['eq', 'pres']
        })

    backend.reindex()
    time.sleep(3)
    assert os.path.exists(f"{inst.ds_paths.db_home_dir}/{DEFAULT_BENAME}/{attr_name.lower()}.db")
    index.delete()
    assert not os.path.exists(f"{inst.ds_paths.db_home_dir}/{DEFAULT_BENAME}/{attr_name.lower()}.db")

    index = indexes.create(properties={
        'cn': attr_name,
        'nsSystemIndex': 'false',
        'nsIndexType': ['eq', 'pres']
        })

    backend.reindex()
    time.sleep(3)
    assert not os.path.exists(f"{inst.ds_paths.db_home_dir}/{DEFAULT_BENAME}/{attr_name.lower()}.db")
    assert os.path.exists(f"{inst.ds_paths.db_home_dir}/{DEFAULT_BENAME}/{attr_name}.db")

    entries = inst.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, f"{attr_name}={attr_value}")
    assert len(entries) > 0
    inst.restart()
    entries = inst.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, f"{attr_name}={attr_value}")
    assert len(entries) > 0

    backend.reindex()
    time.sleep(3)
    assert not os.path.exists(f"{inst.ds_paths.db_home_dir}/{DEFAULT_BENAME}/{attr_name.lower()}.db")
    assert os.path.exists(f"{inst.ds_paths.db_home_dir}/{DEFAULT_BENAME}/{attr_name}.db")


@pytest.mark.bz1905450
def test_unindexed_internal_search_crashes_server(topo, add_a_group_with_users, set_small_idlistscanlimit):
    """
    An internal unindexed search was able to crash the server due to missing logging function.

    :id: 2d0e4070-96d6-46e5-b2c8-9495925e3e87
    :customerscenario: True
    :setup: Standalone instance
    :steps:
        1. Add a group with users
        2. Change nsslapd-idlistscanlimit to a smaller value to accelerate the reproducer
        3. Enable memberOf plugin
        4. Restart the instance
        5. Run memberOf fixup task
        6. Wait for the task to complete
    :expectedresults:
        1. Should succeed
        2. Should succeed
        3. Should succeed
        4. Should succeed
        5. Should succeed
        6. Server should not crash
    """
    inst = topo.standalone
    memberof = MemberOfPlugin(inst)
    memberof.enable()
    inst.restart()
    task = memberof.fixup(DEFAULT_SUFFIX)
    task.wait()
    assert inst.status()


if __name__ == "__main__":
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
