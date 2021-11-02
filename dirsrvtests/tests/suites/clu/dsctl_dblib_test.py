# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2021 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import logging
import pytest
import ldap
import os
from lib389._constants import DEFAULT_SUFFIX
from lib389.backend import DatabaseConfig
from lib389.cli_ctl.dblib import (FakeArgs, dblib_bdb2mdb, dblib_mdb2bdb, dblib_cleanup)
from lib389.idm.user import UserAccounts
from lib389.replica import ReplicationManager
from lib389.topologies import topology_m2 as topo_m2


log = logging.getLogger(__name__)


@pytest.fixture
def init_user(topo_m2, request):
    """Initialize a user - Delete and re-add test user
    """
    s1 = topo_m2.ms["supplier1"]
    users = UserAccounts(s1, DEFAULT_SUFFIX)
    try:
        user_data = {'uid': 'test entry',
                     'cn': 'test entry',
                     'sn': 'test entry',
                     'uidNumber': '3000',
                     'gidNumber': '4000',
                     'homeDirectory': '/home/test_entry',
                     'userPassword': 'foo'}
        test_user = users.create(properties=user_data)
    except ldap.ALREADY_EXISTS:
        pass
    except ldap.SERVER_DOWN:
        pass

    def fin():
        try:
            test_user.delete()
        except ldap.NO_SUCH_OBJECT:
            pass

    request.addfinalizer(fin)


def _check_db(inst, log, impl):
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    # Cannot use inst..get_db_lib() because it caches the value
    assert DatabaseConfig(inst).get_db_lib() == impl
    assert users.get('test entry')

    db_files = os.listdir(inst.dbdir)
    if inst.ds_paths.db_home_dir is not None and inst.ds_paths.db_home_dir != inst.dbdir:
        db_files.append(os.listdir(inst.ds_paths.db_home_dir))
    mdb_list = ['data.mdb', 'INFO.mdb', 'lock.mdb']
    bdb_list = ['__db.001', 'DBVERSION', '__db.003', 'userRoot', 'log.0000000001', '__db.002']
    mdb_list.sort()
    bdb_list.sort()
    db_files.sort()
    log.debug(f"INFO: _check_db dbdir={inst.dbdir}")
    log.debug(f"INFO: _check_db db_files={db_files}")
    log.debug(f"INFO: _check_db mdb_list={mdb_list}")
    log.debug(f"INFO: _check_db bdb_list={bdb_list}")
    if impl == 'bdb':
        assert db_files == bdb_list
        assert db_files != mdb_list
    else:
        assert db_files != bdb_list
        assert db_files == mdb_list


def test_dblib_migration(topo_m2, init_user):
    """
    Verify dsctl dblib xxxxxxx  sub commands ( migration between bdb and lmdb )

    :id: 5d327c34-e77a-46e5-a8aa-0a552f9bbdef
    :setup: Two suppliers Instance
    :steps:
        1. Determine current database
        2. Switch to the other database
        3 Check that
    :expectedresults:
        1. Success
        2. Success
    """
    s1 = topo_m2.ms["supplier1"]
    s2 = topo_m2.ms["supplier2"]
    db_lib = s1.get_db_lib()
    repl = ReplicationManager(DEFAULT_SUFFIX)
    users = UserAccounts(s1, DEFAULT_SUFFIX)
    assert users.get('test entry')
    args = FakeArgs({'tmpdir': None})
    if db_lib == 'bdb':
        dblib_bdb2mdb(s1, log, args)
        dblib_cleanup(s1, log, args)
        _check_db(s1, log, 'mdb')
        repl.test_replication_topology([s1, s2])
        dblib_mdb2bdb(s1, log, args)
        dblib_cleanup(s1, log, args)
        _check_db(s1, log, 'bdb')
        repl.test_replication_topology([s1, s2])
    else:
        dblib_mdb2bdb(s1, log, args)
        dblib_cleanup(s1, log, args)
        _check_db(s1, log, 'bdb')
        repl.test_replication_topology([s1, s2])
        dblib_bdb2mdb(s1, log, args)
        dblib_cleanup(s1, log, args)
        _check_db(s1, log, 'mdb')
        repl.test_replication_topology([s1, s2])
