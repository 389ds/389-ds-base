# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

"""
Will test Import (Offline/Online)
"""

import os
import pytest
import time
import re
import glob
import os
from lib389.topologies import topology_st as topo
from lib389._constants import DEFAULT_SUFFIX
from lib389.dbgen import dbgen_users
from lib389.tasks import ImportTask
from lib389.index import Indexes
from lib389.monitor import Monitor
from lib389.config import LDBMConfig
from lib389.utils import ds_is_newer
from lib389.idm.user import UserAccount, UserAccounts
from lib389.idm.account import Accounts, Account

pytestmark = pytest.mark.tier1


def _generate_ldif(topo, no_no):
    """
    Will generate the ldifs
    """
    ldif_dir = topo.standalone.get_ldif_dir()
    import_ldif = ldif_dir + '/basic_import.ldif'
    dbgen_users(topo.standalone, no_no, import_ldif, DEFAULT_SUFFIX)


def _check_users_before_test(topo, no_no):
    """
    Will check no user before test.
    """
    accounts = Accounts(topo.standalone, DEFAULT_SUFFIX)
    assert len(accounts.filter('(uid=*)')) < no_no


def _search_for_user(topo, no_n0):
    """
    Will make sure that users are imported
    """
    accounts = Accounts(topo.standalone, DEFAULT_SUFFIX)
    assert len(accounts.filter('(uid=*)')) == no_n0


@pytest.fixture(scope="function")
def _import_clean(request, topo):
    def finofaci():
        accounts = Accounts(topo.standalone, DEFAULT_SUFFIX)
        for i in accounts.filter('(uid=*)'):
            UserAccount(topo.standalone, i.dn).delete()

        ldif_dir = topo.standalone.get_ldif_dir()
        import_ldif = ldif_dir + '/basic_import.ldif'
        os.remove(import_ldif)
    request.addfinalizer(finofaci)


def _import_offline(topo, no_no):
    """
    Will import ldifs offline
    """
    _check_users_before_test(topo, no_no)
    ldif_dir = topo.standalone.get_ldif_dir()
    import_ldif = ldif_dir + '/basic_import.ldif'
    # Generate ldif
    _generate_ldif(topo, no_no)
    # Offline import
    topo.standalone.stop()
    t1 = time.time()
    if not topo.standalone.ldif2db('userRoot', None, None, None, import_ldif):
        assert False
    total_time = time.time() - t1
    topo.standalone.start()
    _search_for_user(topo, no_no)
    return total_time


def _import_online(topo, no_no):
    """
    Will import ldifs online
    """
    _check_users_before_test(topo, no_no)
    ldif_dir = topo.standalone.get_ldif_dir()
    import_ldif = ldif_dir + '/basic_import.ldif'
    _generate_ldif(topo, no_no)
    # Online
    import_task = ImportTask(topo.standalone)
    import_task.import_suffix_from_ldif(ldiffile=import_ldif, suffix=DEFAULT_SUFFIX)

    # Wait a bit till the task is created and available for searching
    time.sleep(0.5)

    # Good as place as any to quick test the task has some expected attributes
    if ds_is_newer('1.4.1.2'):
        assert import_task.present('nstaskcreated')
    assert import_task.present('nstasklog')
    assert import_task.present('nstaskcurrentitem')
    assert import_task.present('nstasktotalitems')
    assert import_task.present('ttl')
    import_task.wait()
    topo.standalone.searchAccessLog('ADD dn="cn=import')
    topo.standalone.searchErrorsLog('import userRoot: Import complete.')
    _search_for_user(topo, no_no)


def test_import_with_index(topo, _import_clean):
    """
    Add an index, then import via cn=tasks

    :id: 5bf75c47-a283-430e-a65c-3c5fd8dbadb8
    :setup: Standalone Instance
    :steps:
        1. Creating the room number index
        2. Importing online
        3. Import is done -- verifying that it worked
    :expected results:
        1. Operation successful
        2. Operation successful
        3. Operation successful
    """
    place = topo.standalone.dbdir
    assert f'{place}/userRoot/roomNumber.db' not in glob.glob(f'{place}/userRoot/*.db', recursive=True)
    # Creating the room number index
    indexes = Indexes(topo.standalone)
    indexes.create(properties={
        'cn': 'roomNumber',
        'nsSystemIndex': 'false',
        'nsIndexType': 'eq'})
    topo.standalone.restart()
    # Importing online
    _import_online(topo, 5)
    # Import is done -- verifying that it worked
    assert f'{place}/userRoot/roomNumber.db' in glob.glob(f'{place}/userRoot/*.db', recursive=True)


def test_crash_on_ldif2db(topo, _import_clean):
    """
    Delete the cn=monitor entry for an LDBM backend instance. Doing this will
    cause the DS to re-create that entry the next time it starts up.

    :id: aecad390-9352-11ea-8a31-8c16451d917b
    :setup: Standalone Instance
    :steps:
        1. Delete the cn=monitor entry for an LDBM backend instance
        2. Restart the server and verify that the LDBM monitor entry was re-created.
    :expected results:
        1. Operation successful
        2. Operation successful
    """
    # Delete the cn=monitor entry for an LDBM backend instance. Doing this will
    # cause the DS to re-create that entry the next time it starts up.
    monitor = Monitor(topo.standalone)
    monitor.delete()
    # Restart the server and verify that the LDBM monitor entry was re-created.
    _import_offline(topo, 5)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

