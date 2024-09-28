# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2023 Red Hat, Inc.
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
import glob
import logging
import subprocess
from datetime import datetime
from lib389.topologies import topology_st as topo
from lib389._constants import DEFAULT_SUFFIX, TaskWarning
from lib389.dbgen import dbgen_users
from lib389.tasks import ImportTask
from lib389.index import Indexes
from lib389.monitor import Monitor
from lib389.backend import Backends
from lib389.config import LDBMConfig
from lib389.config import LMDB_LDBMConfig
from lib389.utils import ds_is_newer, get_default_db_lib
from lib389.idm.user import UserAccount
from lib389.idm.account import Accounts
from lib389.cli_ctl.dbtasks import dbtasks_ldif2db
from lib389.cli_base import FakeArgs

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

bdb_values = {
  'wait30': 30
}

# Note: I still sometime get failure with a 60s timeout so lets use 90s
mdb_values = {
  'wait30': 90
}

if get_default_db_lib() == 'bdb':
    values = bdb_values
else:
    values = mdb_values


def _generate_ldif(topo, no_no):
    """
    Will generate the ldifs
    """
    ldif_dir = topo.standalone.get_ldif_dir()
    import_ldif = ldif_dir + '/basic_import.ldif'
    if os.path.isfile(import_ldif):
        pass
    else:
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


def _import_clean_topo(topo):
    """
    Cleanup after import
    """
    accounts = Accounts(topo.standalone, DEFAULT_SUFFIX)
    for i in accounts.filter('(uid=*)'):
        UserAccount(topo.standalone, i.dn).delete()

    ldif_dir = topo.standalone.get_ldif_dir()
    import_ldif = ldif_dir + '/basic_import.ldif'
    if os.path.exists(import_ldif):
        os.remove(import_ldif)
    syntax_err_ldif = ldif_dir + '/syntax_err.dif'
    if os.path.exists(syntax_err_ldif):
        os.remove(syntax_err_ldif)


@pytest.fixture(scope="function")
def _import_clean(request, topo):
    request.addfinalizer(lambda: _import_clean_topo(topo))


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
        topo.standalone.start()
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
    if ds_is_newer('1.4.1.2'):
        for x in range(60):
            if import_task.present('nstaskcreated'):
                break
            time.sleep(0.5)
        assert import_task.present('nstaskcreated')
    else:
        time.sleep(0.5)

    # Good as place as any to quick test the task has some expected attributes
    assert import_task.present('nstasklog')
    assert import_task.present('nstaskcurrentitem')
    assert import_task.present('nstasktotalitems')
    assert import_task.present('ttl')
    import_task.wait()
    topo.standalone.searchAccessLog('ADD dn="cn=import')
    topo.standalone.searchErrorsLog('import userRoot: Import complete.')
    _search_for_user(topo, no_no)


def _create_bogus_ldif(topo):
    """
    Will create bogus ldifs
    """
    ldif_dir = topo.standalone.get_ldif_dir()
    line1 = r'dn: cn=Eladio \"A\"\, Santabarbara\, (A\, B\, C),ou=Accounting, dc=example,dc=com'
    line2 = """objectClass: top
            objectClass: person
            objectClass: organizationalPerson
            objectClass: inetOrgPerson
            cn: Eladio "A", Santabarbara, (A, B, C)
            cn: Eladio Santabarbara
            sn: Santabarbara
            givenName: Eladio
            ou: Accounting"""
    with open(f'{ldif_dir}/bogus.dif', 'w') as out:
        out.write(f'{line1}{line2}')
    out.close()
    import_ldif1 = ldif_dir + '/bogus.ldif'
    return import_ldif1


def _create_syntax_err_ldif(topo):
    """
    Create an ldif file, which contains an entry that violates syntax check
    """
    ldif_dir = topo.standalone.get_ldif_dir()
    line1 = """dn: dc=example,dc=com
objectClass: top
objectClass: domain
dc: example

dn: ou=groups,dc=example,dc=com
objectClass: top
objectClass: organizationalUnit
ou: groups

dn: uid=JHunt,ou=groups,dc=example,dc=com
objectClass: top
objectClass: person
objectClass: organizationalPerson
objectClass: inetOrgPerson
objectclass: inetUser
cn: James Hunt
sn: Hunt
uid: JHunt
givenName:
"""
    with open(f'{ldif_dir}/syntax_err.ldif', 'w') as out:
        out.write(f'{line1}')
        os.chmod(out.name, 0o777)
    out.close()
    import_ldif1 = ldif_dir + '/syntax_err.ldif'
    return import_ldif1


def _now():
    """
    Get current time with the format that _check_for_core requires
    """
    now = datetime.now()
    return now.strftime("%Y-%m-%d %H:%M:%S")


def __check_for_core(now):
    """
    Check if ns-slapd generated a core since the provided date by looking in the system logs.
    """
    cmd = [ 'journalctl' ,'-S', now, '-t', 'audit', '-g', 'ANOM_ABEND.*ns-slapd' ]
    result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if result.returncode != 1:
        # journalctl returns 1 if there is no matching records, and 0 if there are records
        log.error('journalctl output is:\n%s' % result.stdout)
        raise AssertionError(f'journalctl reported that ns-slapd crashes after {now}')


def test_import_with_index(topo, _import_clean):
    """
    Add an index, then import via cn=tasks

    :id: 9ddaf0df-7298-42bb-bdfa-a889ee68bc09
    :setup: Standalone Instance
    :steps:
        1. Creating the room number index
        2. Importing online
        3. Import is done -- verifying that it worked
    :expectedresults:
        1. Operation successful
        2. Operation successful
        3. Operation successful
    """
    place = topo.standalone.dbdir
    if topo.standalone.is_dbi_supported():
        assert not topo.standalone.is_dbi('userRoot/roomNumber.db')
    else:
        assert not glob.glob(f'{place}/userRoot/roomNumber.db*', recursive=True)
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
    if topo.standalone.is_dbi_supported():
        assert topo.standalone.is_dbi('userRoot/roomNumber.db')
    else:
        assert glob.glob(f'{place}/userRoot/roomNumber.db*', recursive=True)


def test_online_import_with_warning(topo, _import_clean):
    """
    Import an ldif file with syntax errors, verify skipped entry warning code

    :id: 9b44cd0e-9d4b-4ae9-b750-cc7ba58d4529
    :setup: Standalone Instance
    :steps:
        1. Create standalone Instance
        2. Create an ldif file with an entry that violates syntax check (empty givenname)
        3. Online import of troublesome ldif file
    :expectedresults:
        1. Successful import with skipped entry warning
        """
    topo.standalone.restart()

    import_task = ImportTask(topo.standalone)
    import_ldif1 = _create_syntax_err_ldif(topo)

    # Importing  the offending ldif file - online
    import_task.import_suffix_from_ldif(ldiffile=import_ldif1, suffix=DEFAULT_SUFFIX)

    # There is just  a single entry in this ldif
    import_task.wait(5)

    # Check for the task nsTaskWarning attr, make sure its set to skipped entry code
    assert import_task.present('nstaskwarning')
    assert TaskWarning.WARN_SKIPPED_IMPORT_ENTRY == import_task.get_task_warn()


def test_crash_on_ldif2db(topo, _import_clean):
    """
    Delete the cn=monitor entry for an LDBM backend instance. Doing this will
    cause the DS to re-create that entry the next time it starts up.

    :id: aecad390-9352-11ea-8a31-8c16451d917b
    :setup: Standalone Instance
    :steps:
        1. Delete the cn=monitor entry for an LDBM backend instance
        2. Restart the server and verify that the LDBM monitor entry was re-created.
    :expectedresults:
        1. Operation successful
        2. Operation successful
    """
    # Delete the cn=monitor entry for an LDBM backend instance. Doing this will
    # cause the DS to re-create that entry the next time it starts up.
    monitor = Monitor(topo.standalone)
    monitor.delete()
    # Restart the server and verify that the LDBM monitor entry was re-created.
    _import_offline(topo, 5)


@pytest.mark.bz185477
def test_ldif2db_allows_entries_without_a_parent_to_be_imported(topo, _import_clean):
    """Should reject import of entries that's missing parent suffix

    :id: 27195cea-9c0e-11ea-800b-8c16451d917b
    :setup: Standalone Instance
    :steps:
        1. Import the offending LDIF data - offline
        2. Violates schema, ending line
    :expectedresults:
        1. Operation successful
        2. Operation Fail
    """
    import_ldif1 = _create_bogus_ldif(topo)
    # Import the offending LDIF data - offline
    topo.standalone.stop()
    topo.standalone.ldif2db('userRoot', None, None, None, import_ldif1)
    # which violates schema, ending line
    topo.standalone.searchErrorsLog('import_producer - import userRoot: Skipping entry '
                                    '"dc=example,dc=com" which violates schema')
    topo.standalone.start()


def test_ldif2db_syntax_check(topo, _import_clean):
    """ldif2db should return a warning when a skipped entry has occured.

    :id: 85e75670-42c5-4062-9edc-7f117c97a06f
    :setup:
        1. Standalone Instance
        2. Ldif entry that violates syntax check rule (empty givenname)
    :steps:
        1. Create an ldif file which violates the syntax checking rule
        2. Stop the server and import ldif file with ldif2db
    :expectedresults:
        1. ldif2db import returns a warning to signify skipped entries
    """
    import_ldif1 = _create_syntax_err_ldif(topo)
    # Import the offending LDIF data - offline
    topo.standalone.stop()
    ret = topo.standalone.ldif2db('userRoot', None, None, None, import_ldif1)
    assert ret == TaskWarning.WARN_SKIPPED_IMPORT_ENTRY
    topo.standalone.start()


@pytest.mark.skipif(get_default_db_lib() == "mdb", reason="Not cache size over mdb")
def test_issue_a_warning_if_the_cache_size_is_smaller(topo, _import_clean):
    """Report during startup if nsslapd-cachememsize is too small

    :id: 1aa8cbda-9c0e-11ea-9297-8c16451d917b
    :setup: Standalone Instance
    :steps:
        1. Set nsslapd-cache-autosize to 0
        2. Change cachememsize
        3. Check that cachememsize is sufficiently small
        4. Import some users to make id2entry.db big
        5. Warning message should be there in error logs
    :expectedresults:
        1. Operation successful
        2. Operation successful
        3. Operation successful
        4. Operation successful
        5. Operation successful
    """
    config = LDBMConfig(topo.standalone)
    backend = Backends(topo.standalone).list()[0]
    # Set nsslapd-cache-autosize to 0
    config.replace('nsslapd-cache-autosize', '0')
    # Change cachememsize
    backend.replace('nsslapd-cachememsize', '1')
    # Check that cachememsize is sufficiently small
    assert int(backend.get_attr_val_utf8('nsslapd-cachememsize')) < 1500000
    # Import some users to make id2entry.db big
    _import_offline(topo, 20)
    # warning message should look like
    assert topo.standalone.searchErrorsLog('INFO - ldbm_instance_config_cachememsize_set - '
                                           'force a minimal value 512000')


@pytest.fixture(scope="function")
def _toggle_private_import_mem(request, topo):
    config = LDBMConfig(topo.standalone)
    config.replace_many(
        ('nsslapd-db-private-import-mem', 'on'),
        ('nsslapd-import-cache-autosize', '0'))

    def finofaci():
        # nsslapd-import-cache-autosize: off and
        # nsslapd-db-private-import-mem: off
        config.replace_many(
            ('nsslapd-db-private-import-mem', 'off'))
    request.addfinalizer(finofaci)


#unstable or unstatus tests, skipped for now
#@pytest.mark.flaky(max_runs=2, min_passes=1)
@pytest.mark.skipif(get_default_db_lib() == "mdb", reason="nsslapd-db-private-import-mem and nsslapd-import-cache-autosize parameters are ignored when usign lmdb")
def test_fast_slow_import(topo, _toggle_private_import_mem, _import_clean):
    """With nsslapd-db-private-import-mem: on is faster import.

    :id: 3044331c-9c0e-11ea-ac9f-8c16451d917b
    :setup: Standalone Instance
    :steps:
        1. Let's set nsslapd-db-private-import-mem:on, nsslapd-import-cache-autosize: 0
        2. Measure offline import time duration total_time1
        3. Now nsslapd-db-private-import-mem:off
        4. Measure offline import time duration total_time2
        5. total_time1 < total_time2
        6. Set nsslapd-db-private-import-mem:on, nsslapd-import-cache-autosize: -1
        7. Measure offline import time duration total_time1
        8. Now nsslapd-db-private-import-mem:off
        9. Measure offline import time duration total_time2
        10. total_time1 < total_time2
    :expectedresults:
        1. Operation successful
        2. Operation successful
        3. Operation successful
        4. Operation successful
        5. Operation successful
        6. Operation successful
        7. Operation successful
        8. Operation successful
        9. Operation successful
        10. Operation successful
    """
    # Let's set nsslapd-db-private-import-mem:on, nsslapd-import-cache-autosize: 0
    config = LDBMConfig(topo.standalone)
    # Measure offline import time duration total_time1
    total_time1 = _import_offline(topo, 1000)
    # Now nsslapd-db-private-import-mem:off
    config.replace('nsslapd-db-private-import-mem', 'off')
    accounts = Accounts(topo.standalone, DEFAULT_SUFFIX)
    for i in accounts.filter('(uid=*)'):
        UserAccount(topo.standalone, i.dn).delete()
    # Measure offline import time duration total_time2
    total_time2 = _import_offline(topo, 1000)
    # total_time1 < total_time2
    log.info("total_time1 = %f" % total_time1)
    log.info("total_time2 = %f" % total_time2)
    assert total_time1 < total_time2

    # Set nsslapd-db-private-import-mem:on, nsslapd-import-cache-autosize: -1
    config.replace_many(
        ('nsslapd-db-private-import-mem', 'on'),
        ('nsslapd-import-cache-autosize', '-1'))
    for i in accounts.filter('(uid=*)'):
        UserAccount(topo.standalone, i.dn).delete()
    # Measure offline import time duration total_time1
    total_time1 = _import_offline(topo, 1000)
    # Now nsslapd-db-private-import-mem:off
    config.replace('nsslapd-db-private-import-mem', 'off')
    for i in accounts.filter('(uid=*)'):
        UserAccount(topo.standalone, i.dn).delete()
    # Measure offline import time duration total_time2
    total_time2 = _import_offline(topo, 1000)
    # total_time1 < total_time2
    log.info("total_time1 = %f" % total_time1)
    log.info("total_time2 = %f" % total_time2)
    assert total_time1 < total_time2


@pytest.mark.bz175063
def test_entry_with_escaped_characters_fails_to_import_and_index(topo, _import_clean):
    """If missing entry_id is found, skip it and continue reading the primary db to be re indexed.

    :id: 358c938c-9c0e-11ea-adbc-8c16451d917b
    :setup: Standalone Instance
    :steps:
        1. Import the example data from ldif.
        2. Remove some of the other entries that were successfully imported.
        3. Now re-index the database.
        4. Should not return error.
    :expectedresults:
        1. Operation successful
        2. Operation successful
        3. Operation successful
        4. Operation successful
    """
    # Import the example data from ldif
    _import_offline(topo, 10)
    count = 0
    # Remove some of the other entries that were successfully imported
    for user1 in [user for user in Accounts(topo.standalone, DEFAULT_SUFFIX).list() if user.dn.startswith('uid')]:
        if count <= 2:
            UserAccount(topo.standalone, user1.dn).delete()
            count += 1
    # Now re-index the database
    topo.standalone.stop()
    topo.standalone.db2index()
    topo.standalone.start()
    # Should not return error.
    assert not topo.standalone.searchErrorsLog('error')
    assert not topo.standalone.searchErrorsLog('foreman fifo error')


def test_import_perf_after_failure(topo):
    """Make an import fail by specifying the wrong LDIF file name, then
    try the import with the correct name.  Make sure the import performance
    is what we expect.

    :id: d21dc67f-475e-402a-be9e-3eeb9181c156
    :setup: Standalone Instance
    :steps:
        1. Build LDIF file
        2. Import invalid LDIF filename
        3. Import valid LDIF filename
        4. Import completes in a timely manner
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
    """

    ldif_dir = topo.standalone.get_ldif_dir()
    import_ldif = ldif_dir + '/perf_import.ldif'
    bad_import_ldif = ldif_dir + '/perf_import_typo.ldif'

    # Build LDIF file
    dbgen_users(topo.standalone, 30000, import_ldif, DEFAULT_SUFFIX)

    # Online import which fails
    import_task = ImportTask(topo.standalone)
    import_task.import_suffix_from_ldif(ldiffile=bad_import_ldif, suffix=DEFAULT_SUFFIX)
    import_task.wait()

    # Valid online import
    time.sleep(1)
    import_task = ImportTask(topo.standalone)
    import_task.import_suffix_from_ldif(ldiffile=import_ldif, suffix=DEFAULT_SUFFIX)
    import_task.wait(values['wait30'])  # If things go wrong import takes a lot longer than this
    assert import_task.is_complete()

    # Restart server
    topo.standalone.restart()


def test_import_wrong_file_path(topo):
    """Make an import fail by specifying the wrong LDIF file name

    :id: 6795a3cd-b95e-4777-bc77-25ab864882a3
    :setup: Standalone Instance
    :steps:
        1. Do an import with an invalid file path
        2. Appropriate error is returned
    :expectedresults:
        1. Success
        2. Success
    """
    import_ldif = '/nope/perf_import.ldif'
    args = FakeArgs()
    args.instance = topo.standalone.serverid
    args.backend = "userroot"
    args.encrypted = False
    args.replication = False
    args.ldif = import_ldif

    with pytest.raises(ValueError) as e:
        dbtasks_ldif2db(topo.standalone, log, args)
    assert "The LDIF file does not exist" in str(e.value)


@pytest.mark.skipif(get_default_db_lib() != "mdb", reason="lmdb specific test")
def test_crash_on_ldif2db_with_lmdb(topo, _import_clean):
    """Make an import fail by specifying a too small db size then check that
    there is no crash.

    :id: d42585b6-31d0-11ee-8724-482ae39447e5
    :setup: Standalone Instance
    :steps:
        1. Configure a small database size
        2. Import an ldif with 1K users
        3. Check that ns-slapd has not aborted
        4. Import an ldif with 500 users
        5. Check that ns-slapd has not aborted
    :expectedresults:
        1. Success
        2. Success
        3. Success (ns-slapd should not have aborted)
        4. Import should fail
        5. Success (ns-slapd should not have aborted)

    """
    TINY_MAP_SIZE =  16 * 1024 * 1024
    inst = topo.standalone
    handler = LMDB_LDBMConfig(inst)
    mapsize = TINY_MAP_SIZE
    log.info(f'Set lmdb map size to {mapsize}.')
    handler.replace('nsslapd-mdb-max-size', str(mapsize))
    inst.stop()
    for dbfile in ['data.mdb', 'INFO.mdb', 'lock.mdb']:
        try:
            os.remove(f'{inst.dbdir}/{dbfile}')
        except FileNotFoundError:
            pass
    inst.start()
    now = _now()
    _import_offline(topo, 1000)
    __check_for_core(now)
    _import_clean_topo(topo)
    with pytest.raises(AssertionError):
        _import_offline(topo, 500_000)
    __check_for_core(now)

def test_online_import_under_load(topo):
    """Perform an online import while the server is under load

    :id: 56f7ac6f-4285-4bc4-8822-5ae9b502eee3
    :setup: Standalone Instance
    :steps:
        1. Create and import LDIF for ldclt load
        2. Start ldclt
        3. Start online import
        4. Wait a bit and kill the ldclt load
        5. Check import task successfully completed

    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
    """
    inst = topo.standalone

    # Create and import ldif to use with ldclt
    ldif_dir = inst.get_ldif_dir()
    import_ldif = ldif_dir + '/stress_import.ldif'
    dbgen_users(inst, 1000, import_ldif, generic=True, suffix=DEFAULT_SUFFIX)
    import_task = ImportTask(topo.standalone)
    import_task.import_suffix_from_ldif(ldiffile=import_ldif,
                                        suffix=DEFAULT_SUFFIX)
    import_task.wait()
    assert import_task.get_exit_code() == 0

    # Start ldclt load
    ldclt_cmd = [
        '%s/ldclt' % inst.get_bin_dir(),
        '-h', inst.host, '-p', str(inst.port),
        '-f', '(uid=userXXXX)', '-e', 'esearch,random',
        '-r1', '-R999', '-Q'
    ]
    p = subprocess.Popen(ldclt_cmd, start_new_session=True,
                         stdout=subprocess.PIPE)
    time.sleep(1)

    # Start online import
    import_task = ImportTask(topo.standalone)
    import_task.import_suffix_from_ldif(ldiffile=import_ldif,
                                        suffix=DEFAULT_SUFFIX)

    # Wait a bit till the task is created and available for searching
    for x in range(10):
        if import_task.present('nstaskcreated'):
            break
        time.sleep(0.5)
    assert import_task.present('nstaskcreated')

    # Stop the load
    time.sleep(3)
    cmd = ['kill', '-9', str(p.pid)]
    subprocess.Popen(cmd, stdout=subprocess.PIPE)

    # import should finish
    import_task.wait()
    assert import_task.get_exit_code() == 0


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
