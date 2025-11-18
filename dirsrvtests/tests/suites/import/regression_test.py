# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
from abc import ABC, abstractmethod
from decimal import *
import ldap
import logging
import os
import pytest
import threading
import time
from lib389.backend import Backends
from lib389.properties import TASK_WAIT
from lib389.topologies import topology_st as topo
from lib389.dbgen import dbgen_users
from lib389._constants import DEFAULT_SUFFIX, DEFAULT_BENAME
from lib389.tasks import *
from lib389.idm.user import UserAccounts, UserAccount
from lib389.idm.directorymanager import DirectoryManager
from lib389.dbgen import *
from lib389.utils import *
from lib389.config import LMDB_LDBMConfig

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

TEST_SUFFIX1 = "dc=importest1,dc=com"
TEST_BACKEND1 = "importest1"
TEST_SUFFIX2 = "dc=importest2,dc=com"
TEST_BACKEND2 = "importest2"
TEST_DEFAULT_SUFFIX = "dc=default,dc=com"
TEST_DEFAULT_NAME = "default"

BIG_MAP_SIZE = 35 * 1024 * 1024 * 1024

def _check_disk_space():
    if get_default_db_lib() == "mdb":
        statvfs = os.statvfs(os.environ.get('PREFIX', "/"))
        available = statvfs.f_frsize * statvfs.f_bavail
        return available >= BIG_MAP_SIZE
    return True


@pytest.fixture(scope="function")
def _set_mdb_map_size(request, topo):
    if get_default_db_lib() == "mdb":
        handler = LMDB_LDBMConfig(topo.standalone)
        mapsize = BIG_MAP_SIZE
        log.info(f'Set lmdb map size to {mapsize}.')
        handler.replace('nsslapd-mdb-max-size', str(mapsize))
        topo.standalone.restart()

class AddDelUsers(threading.Thread):
    def __init__(self, inst):
        threading.Thread.__init__(self)
        self.daemon = True
        self.inst = inst
        self._should_stop = False
        self._ran = False

    def run(self):
        # Add 1000 entries
        log.info('Run.')
        conn = DirectoryManager(self.inst.standalone).bind()

        time.sleep(30)
        log.info('Adding users.')
        for i in range(1000):
            user = UserAccounts(conn, DEFAULT_SUFFIX)
            users = user.create_test_user(uid=i)
            users.delete()
            self._ran = True
            if self._should_stop:
                break
        if not self._should_stop:
            raise RuntimeError('We finished too soon.')
        conn.close()

    def stop(self):
        self._should_stop = True

    def has_started(self):
        return self._ran


def get_backend_by_name(inst, bename):
    bes = Backends(inst)
    bename = bename.lower()
    be = [ be for be in bes if be.get_attr_val_utf8_l('cn') == bename ]
    return be[0] if len(be) == 1 else None


class LogHandler():
    def __init__(self, logfd, patterns):
        self.logfd = logfd
        self.patterns = [ p.lower() for p in patterns ]
        self.pos = logfd.tell()
        self.last_result = None

    def zero(self):
        return [0 for _ in range(len(self.patterns))]

    def countCaptures(self):
        res = self.zero()
        self.logfd.seek(self.pos)
        for line in iter(self.logfd.readline, ''):
            # Ignore autotune messages that may confuse the counts
            if 'bdb_start_autotune' in line:
                continue
            # Ignore LMDB size warnings that may confuse the counts
            if 'dbmdb_ctx_t_db_max_size_set' in line:
                continue
            log.info(f'ERROR LOG line is {line.strip()}')
            for idx,pattern in enumerate(self.patterns):
                if pattern in line.lower():
                    res[idx] += 1
        self.pos = self.logfd.tell()
        self.last_result = res
        log.info(f'ERROR LOG counts are: {res}')
        return res

    def seek2end(self):
        self.pos = os.fstat(self.logfd.fileno()).st_size

    def check(self, idx, val):
        count = self.last_result[idx]
        assert count == val , f"Should have {val} '{self.patterns[idx]}' messages but got: {count} - idx = {idx}"


class IEHandler(ABC):
    def __init__(self, inst, errlog, ldifname, bename=DEFAULT_BENAME, suffix=None):
        self.inst = inst
        self.errlog = errlog
        self.ldifname = ldifname
        self.bename = bename
        self.suffix = suffix
        self.ldif = ldifname if ldifname.startswith('/') else f'{inst.get_ldif_dir()}/{ldifname}.ldif'

    @abstractmethod
    def get_name(self):
        pass

    @abstractmethod
    def _run_task_b(self):
        pass

    @abstractmethod
    def _run_task_s(self):
        pass

    @abstractmethod
    def _run_offline(self):
        pass

    @abstractmethod
    def _set_log_pattern(self, success):
        pass

    def run(self, extra_checks, success=True):
        if self.errlog:
            self._set_log_pattern(success)
            self.errlog.seek2end()

        if self.inst.status():
            if self.bename:
                log.info(f"Performing online {self.get_name()} of backend {self.bename} into LDIF file {self.ldif}")
                r = self._run_task_b()
            else:
                log.info(f"Performing online {self.get_name()} of suffix {self.suffix} into LDIF file {self.ldif}")
                r = self._run_task_s()
            r.wait()
            time.sleep(1)
        else:
            if self.bename:
                log.info(f"Performing offline {self.get_name()} of backend {self.bename} into LDIF file {self.ldif}")
            else:
                log.info(f"Performing offline {self.get_name()} of suffix {self.suffix} into LDIF file {self.ldif}")
            self._run_offline()
        if self.errlog:
            expected_counts = ['*' for _ in range(len(self.errlog.patterns))]
            for (idx, val) in extra_checks:
                expected_counts[idx] = val
            res = self.errlog.countCaptures()
            log.info(f'Expected errorlog counts are: {expected_counts}')
            if success is True or success is False:
                log.info(f'Number of {self.errlog.patterns[0]} in errorlog is: {res[0]}')
                assert res[0] >= 1
            for (idx, val) in extra_checks:
                self.errlog.check(idx, val)

    def check_db(self):
       assert self.inst.dbscan(bename=self.bename, index='id2entry')


class Importer(IEHandler):
    def get_name(self):
        return "import"

    def _set_log_pattern(self, success):
        if success is True:
            self.errlog.patterns[0] = 'import complete'
        elif success is False:
            self.errlog.patterns[0] = 'import failed'

    def _run_task_b(self):
        bes = Backends(self.inst)
        r = bes.import_ldif(self.bename, [self.ldif,], include_suffixes=self.suffix)
        return r

    def _run_task_s(self):
        r = ImportTask(self.inst)
        r.import_suffix_from_ldif(self.ldif, self.suffix)
        return r

    def _run_offline(self):
        log.info(f'self.inst.ldif2db({self.bename}, {self.suffix}, ...)')
        if self.suffix is None:
            self.inst.ldif2db(self.bename, self.suffix, None, False, self.ldif)
        else:
            self.inst.ldif2db(self.bename, [self.suffix, ], None, False, self.ldif)


class Exporter(IEHandler):
    def get_name(self):
        return "export"

    def _set_log_pattern(self, success):
        if success is True:
            self.errlog.patterns[0] = 'export finished'
        elif success is False:
            self.errlog.patterns[0] = 'export failed'

    def _run_task_b(self):
        bes = Backends(self.inst)
        r = bes.export_ldif(self.bename, self.ldif, include_suffixes=self.suffix)
        return r

    def _run_task_s(self):
        r = ExportTask(self.inst)
        r.export_suffix_to_ldif(self.ldif, self.suffix)
        return r

    def _run_offline(self):
        self.inst.db2ldif(self.bename, self.suffix, None, False, False, self.ldif)


def preserve_func(topo, request, restart):
    # Ensure that topology get preserved helper
    inst = topo.standalone

    def fin():
        if restart:
            inst.restart()
        Importer(inst, None, "save").run(())

    r = Exporter(inst, None, "save")
    if not os.path.isfile(r.ldif):
        r.run(())
    request.addfinalizer(fin)


@pytest.fixture(scope="function")
def preserve(topo, request):
    # Ensure that topology get preserved (no restart)
    preserve_func(topo, request, False)


@pytest.fixture(scope="function")
def preserve_r(topo, request):
    # Ensure that topology get preserved (with restart)
    preserve_func(topo, request, True)


@pytest.fixture(scope="function")
def verify(topo):
    # Check that backend is not broken
    inst = topo.standalone
    dn=f'uid=demo_user,ou=people,{DEFAULT_SUFFIX}'
    assert UserAccount(inst,dn).exists()


def test_replay_import_operation(topo, preserve_r, verify):
    """ Check after certain failed import operation, is it
     possible to replay an import operation

    :id: 5f5ca532-8e18-4f7b-86bc-ac585215a473
    :feature: Import
    :setup: Standalone instance
    :steps:
        1. Export the backend into an ldif file
        2. Perform high load of operation on the server (Add/Del users)
        3. Perform an import operation
        4. Again perform an import operation (same as 3)
    :expectedresults:
        1. It should be successful
        2. It should be successful
        3. It should be unsuccessful, should give OPERATIONS_ERROR
        4. It should be successful now
    """
    log.info("Exporting LDIF online...")
    ldif_dir = topo.standalone.get_ldif_dir()
    export_ldif = ldif_dir + '/export.ldif'

    r = ExportTask(topo.standalone)
    r.export_suffix_to_ldif(ldiffile=export_ldif, suffix=DEFAULT_SUFFIX)
    r.wait()
    add_del_users1 = AddDelUsers(topo)
    add_del_users1.start()

    log.info("Importing LDIF online, should raise operation error.")

    trials = 0
    while not add_del_users1.has_started() and trials < 10:
        trials += 1
        time.sleep(1)
        r = ImportTask(topo.standalone)
        try:
            r.import_suffix_from_ldif(ldiffile=export_ldif, suffix=DEFAULT_SUFFIX)
        except ldap.OPERATIONS_ERROR:
            break
        log.info(f'Looping. Tried {trials} times so far.')
    add_del_users1.stop()
    add_del_users1.join()

    log.info("Importing LDIF online")

    r = ImportTask(topo.standalone)
    r.import_suffix_from_ldif(ldiffile=export_ldif, suffix=DEFAULT_SUFFIX)


def test_import_be_default(topo):
    """ Create a backend using the name "default". previously this name was
    used int

    :id: 8e507beb-e917-4330-8cac-1ff0eee10508
    :feature: Import
    :setup: Standalone instance
    :steps:
        1. Create a test suffix using the be name of "default"
        2. Create an ldif for the "default" backend
        3. Import ldif
        4. Verify all entries were imported
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
    """
    log.info('Adding suffix:{} and backend: {}...'.format(TEST_DEFAULT_SUFFIX,
                                                          TEST_DEFAULT_NAME))
    backends = Backends(topo.standalone)
    backends.create(properties={'nsslapd-suffix': TEST_DEFAULT_SUFFIX,
                                'name': TEST_DEFAULT_NAME})

    log.info('Create LDIF file and import it...')
    ldif_dir = topo.standalone.get_ldif_dir()
    ldif_file = os.path.join(ldif_dir, 'default.ldif')
    dbgen_users(topo.standalone, 5, ldif_file, TEST_DEFAULT_SUFFIX)

    log.info('Stopping the server and running offline import...')
    topo.standalone.stop()
    assert topo.standalone.ldif2db(TEST_DEFAULT_NAME, None, None,
                                   None, ldif_file)
    topo.standalone.start()

    log.info('Verifying entry count after import...')
    entries = topo.standalone.search_s(TEST_DEFAULT_SUFFIX,
                                       ldap.SCOPE_SUBTREE,
                                       "(objectclass=*)")
    assert len(entries) > 1

    log.info('Test PASSED')


def test_del_suffix_import(topo):
    """Adding a database entry fails if the same database was deleted after an import

    :id: 652421ef-738b-47ed-80ec-2ceece6b5d77
    :feature: Import
    :setup: Standalone instance
    :steps: 1. Create a test suffix and add few entries
            2. Stop the server and do offline import using ldif2db
            3. Delete the suffix backend
            4. Add a new suffix with the same database name
            5. Check if adding the same database name is a success
    :expectedresults: Adding database with the same name should be successful
    """

    log.info('Adding suffix:{} and backend: {}'.format(TEST_SUFFIX1, TEST_BACKEND1))
    backends = Backends(topo.standalone)
    backend = backends.create(properties={'nsslapd-suffix': TEST_SUFFIX1,
                                          'name': TEST_BACKEND1})

    log.info('Create LDIF file and import it')
    ldif_dir = topo.standalone.get_ldif_dir()
    ldif_file = os.path.join(ldif_dir, 'suffix_del1.ldif')

    dbgen_users(topo.standalone, 10, ldif_file, TEST_SUFFIX1)

    log.info('Stopping the server and running offline import')
    topo.standalone.stop()
    assert topo.standalone.ldif2db(TEST_BACKEND1, TEST_SUFFIX1, None, None, ldif_file)
    topo.standalone.start()

    log.info('Deleting suffix-{}'.format(TEST_SUFFIX2))
    backend.delete()

    log.info('Adding the same database-{} after deleting it'.format(TEST_BACKEND1))
    backends.create(properties={'nsslapd-suffix': TEST_SUFFIX1,
                                'name': TEST_BACKEND1})


def test_del_suffix_backend(topo):
    """Adding a database entry fails if the same database was deleted after an import

    :id: ac702c35-74b6-434e-8e30-316433f3e91a
    :feature: Import
    :setup: Standalone instance
    :steps: 1. Create a test suffix and add entries
            2. Stop the server and do online import using ldif2db
            3. Delete the suffix backend
            4. Add a new suffix with the same database name
            5. Restart the server and check the status
    :expectedresults: Adding database with the same name should be successful and the server should not hang
    """

    log.info('Adding suffix:{} and backend: {}'.format(TEST_SUFFIX2, TEST_BACKEND2))
    backends = Backends(topo.standalone)
    backend = backends.create(properties={'nsslapd-suffix': TEST_SUFFIX2,
                                          'name': TEST_BACKEND2})

    log.info('Create LDIF file and import it')
    ldif_dir = topo.standalone.get_ldif_dir()
    ldif_file = os.path.join(ldif_dir, 'suffix_del2.ldif')

    dbgen_users(topo.standalone, 10, ldif_file, TEST_SUFFIX2)

    topo.standalone.tasks.importLDIF(suffix=TEST_SUFFIX2, input_file=ldif_file, args={TASK_WAIT: True})

    log.info('Deleting suffix-{}'.format(TEST_SUFFIX2))
    backend.delete()

    log.info('Adding the same database-{} after deleting it'.format(TEST_BACKEND2))
    backends.create(properties={'nsslapd-suffix': TEST_SUFFIX2,
                                'name': TEST_BACKEND2})
    log.info('Checking if server can be restarted after re-adding the same database')
    topo.standalone.restart()
    assert not topo.standalone.detectDisorderlyShutdown()


@pytest.mark.bz1406101
@pytest.mark.ds49071
def test_import_duplicate_dn(topo):
    """Import ldif with duplicate DNs, should not log error "unable to flush"

    :id: dce2b898-119d-42b8-a236-1130f58bff17
    :setup: Standalone instance, ldif file with duplicate entries
    :steps:
        1. Create a ldif file with duplicate entries
        2. Import ldif file to DS
        3. Check error log file, it should not log "unable to flush"
        4. Check error log file, it should log "Duplicated DN detected"
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
    """

    standalone = topo.standalone

    log.info('Delete the previous error logs')
    standalone.deleteErrorLogs()

    log.info('Create import file')
    l = """dn: dc=example,dc=com
objectclass: top
objectclass: domain
dc: example

dn: ou=myDups00001,dc=example,dc=com
objectclass: top
objectclass: organizationalUnit
ou: myDups00001

dn: ou=myDups00001,dc=example,dc=com
objectclass: top
objectclass: organizationalUnit
ou: myDups00001
"""

    ldif_dir = standalone.get_ldif_dir()
    ldif_file = os.path.join(ldif_dir, 'data.ldif')
    with open(ldif_file, "w") as fd:
        fd.write(l)
        fd.close()
    os.chmod(ldif_file, 0o777)

    log.info('Import ldif with duplicate entry')
    assert standalone.tasks.importLDIF(suffix=DEFAULT_SUFFIX, input_file=ldif_file, args={TASK_WAIT: True})

    log.info('Restart the server to flush the logs')
    standalone.restart()

    log.info('Error log should not have "unable to flush" message')
    assert not standalone.ds_error_log.match('.*unable to flush.*')

    log.info('Error log should have "Duplicated DN detected" message')
    assert standalone.ds_error_log.match('.*Duplicated DN detected.*')

@pytest.mark.bz1749595
@pytest.mark.tier2
@pytest.mark.skipif(not _check_disk_space(), reason="not enough disk space for lmdb map")
@pytest.mark.xfail(ds_is_older("1.3.10.1"), reason="bz1749595 not fixed on versions older than 1.3.10.1")
def test_large_ldif2db_ancestorid_index_creation(topo, _set_mdb_map_size):
    """Import with ldif2db a large file - check that the ancestorid index creation phase has a correct performance

    :id: fe7f78f6-6e60-425d-ad47-b39b67e29113
    :setup: Standalone instance
    :steps:
        1. Delete the previous errors log to start from a fresh one
        2. Create test suffix and backend
        3. Create a large nested ldif file
        4. Stop the server
        5. Run an offline import
        6. Restart the server
        7. Check in the errors log that an independant ancestorid IDs sorting is done
        8. Get the log of the starting of the ancestorid indexing process
        9. Get the log of the end of the ancestorid indexing process
        10. Get the start and end time for ancestorid index creation from these logs
        11. Calculate the duration of the ancestorid indexing process
    :expectedresults:
        1. Success
        2. Test suffix and backend successfully created
        3. ldif file successfully created
        4. Success
        5. Import is successfully performed
        6. Success
        7. Log of ancestorid sorting start and end are present
        8. Log of the beginning of gathering ancestorid is found
        9. Log of the final ancestorid index creation is found
        10. Start and end times are successfully extracted
        11. The duration of the ancestorid index creation process should be less than 10s
    """

    ldif_dir = topo.standalone.get_ldif_dir()
    ldif_file = os.path.join(topo.standalone.ds_paths.ldif_dir, 'large_nested.ldif')

    # Have a reasonable balance between the need for a large ldif file to import and the time of test execution
    # total number of users
    num_users = 100000

    # Choose a limited number of users per node to get as much as possible non-leaf entries
    node_limit = 5

    # top suffix
    suffix = 'o=test'

    # backend
    backend = 'test'

    log.info('Delete the previous errors logs')
    topo.standalone.deleteErrorLogs()

    log.info('Add suffix:{} and backend: {}...'.format(suffix, backend))

    backends = Backends(topo.standalone)
    backends.create(properties={'nsslapd-suffix': suffix,
                                'name': backend})

    props = {
        'numUsers' : num_users,
        'nodeLimit' : node_limit,
        'suffix' : suffix
    }
    instance = topo.standalone

    log.info('Create a large nested ldif file using dbgen : %s' % ldif_file)
    dbgen_nested_ldif(instance, ldif_file, props)

    log.info('Stop the server and run offline import...')
    topo.standalone.stop()
    assert topo.standalone.ldif2db(backend, None, None,
                                   None, ldif_file)

    log.info('Starting the server')
    topo.standalone.start()

    # With lmdb there is no more any special phase for ancestorid
    # because ancestorsid get updated on the fly while processing the
    # entryrdn (by up the parents chain to compute the parentid
    #
    # But there is still a numSubordinates generation phase
    if get_default_db_lib() == "mdb":
        log.info('parse the errors logs to check lines with "Generating numSubordinates complete." are present')
        end_numsubordinates = str(topo.standalone.ds_error_log.match(r'.*Generating numSubordinates complete.*'))[1:-1]
        assert len(end_numsubordinates) > 0

    else:
        log.info('parse the errors logs to check lines with "Starting sort of ancestorid" are present')
        start_sort_str = str(topo.standalone.ds_error_log.match(r'.*Starting sort of ancestorid non-leaf IDs*'))[1:-1]
        assert len(start_sort_str) > 0

        log.info('parse the errors logs to check lines with "Finished sort of ancestorid" are present')
        end_sort_str = str(topo.standalone.ds_error_log.match(r'.*Finished sort of ancestorid non-leaf IDs*'))[1:-1]
        assert len(end_sort_str) > 0

        log.info('parse the error logs for the line with "Gathering ancestorid non-leaf IDs"')
        start_ancestorid_indexing_op_str = str(topo.standalone.ds_error_log.match(r'.*Gathering ancestorid non-leaf IDs*'))[1:-1]
        assert len(start_ancestorid_indexing_op_str) > 0

        log.info('parse the error logs for the line with "Created ancestorid index"')
        end_ancestorid_indexing_op_str = str(topo.standalone.ds_error_log.match(r'.*Created ancestorid index*'))[1:-1]
        assert len(end_ancestorid_indexing_op_str) > 0

        log.info('get the ancestorid non-leaf IDs indexing start and end time from the collected strings')
        # Collected lines look like : '[15/May/2020:05:30:27.245967313 -0400] - INFO - bdb_get_nonleaf_ids - import userRoot: Gathering ancestorid non-leaf IDs...'
        # We are getting the sec.nanosec part of the date, '27.245967313' in the above example
        start_time = (start_ancestorid_indexing_op_str.split()[0]).split(':')[3]
        end_time = (end_ancestorid_indexing_op_str.split()[0]).split(':')[3]

        log.info('Calculate the elapsed time for the ancestorid non-leaf IDs index creation')
        etime = (Decimal(end_time) - Decimal(start_time))
        # The time for the ancestorid index creation should be less than 10s for an offline import of an ldif file with 100000 entries / 5 entries per node
        # Should be adjusted if these numbers are modified in the test
        assert etime <= 10


def create_backend_and_import(instance, ldif_file, suffix, backend):
    log.info(f'Add suffix:{suffix} and backend: {backend}...')
    backends = Backends(instance)
    backends.create(properties={'nsslapd-suffix': suffix, 'name': backend})
    props = {'numUsers': 10000, 'nodeLimit': 5, 'suffix': suffix}

    log.info(f'Create a large nested ldif file using dbgen : {ldif_file}')
    dbgen_nested_ldif(instance, ldif_file, props)

    log.info('Stop the server and run offline import...')
    instance.stop()
    log.info('Measure the import time for the ldif file...')
    start = time.time()
    assert instance.ldif2db(backend, None, None, None, ldif_file)
    end = time.time()
    instance.start()
    return end - start


@pytest.mark.skipif(get_default_db_lib() == "mdb", reason="Not cache size over mdb")
def test_ldif2db_after_backend_create(topo):
    """Test that ldif2db after backend creation is not slow first time

    :id: c1ab1df7-c70a-46be-bbca-8d65c6ebaa14
    :setup: Standalone Instance
    :steps:
        1. Create backend and suffix
        2. Generate large LDIF file
        3. Stop server and run offline import
        4. Measure import time
        5. Restart server and repeat steps 1-4 with new backend and suffix
    :expectedresults:
        1. Operation successful
        2. Operation successful
        3. Operation successful
        4. Import times should be approximately the same
        5. Operation successful
    """

    instance = topo.standalone
    ldif_dir = instance.get_ldif_dir()
    ldif_file_1 = os.path.join(ldif_dir, 'large_nested_1.ldif')
    ldif_file_2 = os.path.join(ldif_dir, 'large_nested_2.ldif')

    import_time_1 = create_backend_and_import(instance, ldif_file_1, 'o=test_1', 'test_1')
    import_time_2 = create_backend_and_import(instance, ldif_file_2, 'o=test_2', 'test_2')

    log.info('Import times should be approximately the same')
    assert abs(import_time_1 - import_time_2) < 15


def test_ldif_missing_suffix_entry(topo, request, verify):
    """Test that ldif2db/import aborts if suffix entry is not in the ldif

    :id: 731bd0d6-8cc8-11f0-8ef2-c85309d5c3e3
    :setup: Standalone Instance
    :steps:
        1. Prepare final cleanup
        2. Add a few users
        3. Export ou=people subtree
        4. Online import using backend name ou=people subtree
        5. Online import using suffix name ou=people subtree
        6. Stop the instance
        7. Offline import using backend name ou=people subtree
        8. Offline import using suffix name ou=people subtree
        9. Generate ldif with a far away suffix
        10. Offline import using backend name  and "far" ldif
        11. Offline import using suffix name and "far" ldif
        12. Start the instance
        13. Online import using backend name  and "far" ldif
        14. Online import using suffix name and "far" ldif
    :expectedresults:
        1. Operation successful
        2. Operation successful
        3. Operation successful
        4. Import should success, skip all entries, db should exists
        5. Import should success, skip all entries, db should exists
        6. Operation successful
        7. Import should success, skip all entries, db should exists
        8. Import should success, skip all entries, db should exists
        9. Operation successful
        10. Import should success, skip all entries, db should exists
        11. Import should success, 10 entries skipped, db should exists
        12. Operation successful
        13. Import should success, skip all entries, db should exists
        14. Import should success, 10 entries skipped, db should exists
    """

    inst = topo.standalone
    inst.config.set('nsslapd-errorlog-level', '266354688')
    no_suffix_on = (
        (1, 0), # no errors are expected.
        (2, 1), # 1 warning is expected.
        (3, 0), # no 'no parent' warning is expected.
        (4, 1), # 1 'all entries were skipped' warning
        (5, 0), # no 'returning task warning' info message
    )
    no_suffix_off = (
        (1, 0), # no errors are expected.
        (2, 1), # 1 warning is expected.
        (3, 0), # no 'no parent' warning is expected.
        (4, 1), # 1 'all entries were skipped' warning
        (5, 1), # 1 'returning task warning' info message
    )

    far_suffix_on = (
        (1, 0),  # no errors are expected.
        (2, 1),  # 1 warning (consolidated, pre-check aborts after 4 entries)
        (3, 0),  # 0 'no parent' warnings (pre-check aborts before processing)
        (4, 1),  # 1 'all entries were skipped' warning (from pre-check)
        (5, 0),  # 0 'returning task warning' info message (online import)
    )
    # Backend-specific behavior for orphan detection when suffix parameter is provided
    nbw = 0 if get_default_db_lib() == "bdb" else 10
    far_suffix_with_suffix_on = (
        (1, 0),  # no errors are expected.
        (2, nbw),  # 0 (BDB early filtering) or 10 (LMDB orphan detection) warnings
        (3, nbw),  # 0 (BDB early filtering) or 10 (LMDB orphan detection) 'no parent' warnings
        (4, 0),  # 0 'all entries were skipped' warning (no pre-check abort)
        (5, 0),  # 0 'returning task warning' info message (online import)
    )
    far_suffix_off = (
        (1, 0),  # no errors are expected.
        (2, 1),  # 1 warning (consolidated, pre-check detects missing suffix)
        (3, 0),  # 0 'no parent' warnings (pre-check aborts before processing)
        (4, 1),  # 1 'all entries were skipped' warning (from pre-check)
        (5, 1),  # 1 'returning task warning' info message (offline import)
    )
    far_suffix_with_suffix_off = (
        (1, 0),  # no errors are expected.
        (2, nbw),  # 0 (BDB early filtering) or 10 (LMDB orphan detection) warnings
        (3, nbw),  # 0 (BDB early filtering) or 10 (LMDB orphan detection) 'no parent' warnings
        (4, 0),  # 0 'all entries were skipped' warning (no pre-check abort)
        (5, 0),  # 0 'returning task warning' (rc=0, successful import of suffix)
    )

    with open(inst.ds_paths.error_log, 'at+') as fd:
        patterns = (
            "Reserved for IEHandler",
            " ERR ",
            " WARN ",
            "has no parent",
            "all entries were skipped",
            "returning task warning",
        )
        errlog = LogHandler(fd, patterns)
        no_errors = ((1, 0), (2, 0)) # no errors nor warnings are expected.


        # 1. Prepare final cleanup
        Exporter(inst, errlog, "full").run(no_errors)

        def fin():
            inst.start()
            with open(inst.ds_paths.error_log, 'at+') as cleanup_fd:
                cleanup_errlog = LogHandler(cleanup_fd, patterns)
                Importer(inst, cleanup_errlog, "full").run(no_errors)

        if not DEBUGGING:
            request.addfinalizer(fin)

        # 2. Add a few users
        user = UserAccounts(inst, DEFAULT_SUFFIX)
        users = [ user.create_test_user(uid=i) for i in range(10) ]

        # 3. Export ou=people subtree
        e = Exporter(inst, errlog, "people", suffix=f'ou=people,{DEFAULT_SUFFIX}')
        e.run(no_errors) # no errors nor warnings are expected.

        # 4. Online import using backend name ou=people subtree
        e = Importer(inst, errlog, "people")
        e.run(no_suffix_on)
        e.check_db()

        # 5. Online import using suffix name ou=people subtree
        e = Importer(inst, errlog, "people", suffix=DEFAULT_SUFFIX)
        e.run(no_suffix_on)
        e.check_db()

        # 6. Stop the instance
        inst.stop()

        # 7. Offline import using backend name ou=people subtree
        e = Importer(inst, errlog, "people")
        e.run(no_suffix_off)
        e.check_db()

        # 8. Offline import using suffix name ou=people subtree
        e = Importer(inst, errlog, "people", suffix=DEFAULT_SUFFIX)
        e.run(no_suffix_off)
        e.check_db()

        # 9. Generate ldif with a far away suffix
        e = Importer(inst, errlog, "full")
        people_ldif = e.ldif
        e = Importer(inst, errlog, "far")
        with open(e.ldif, "wt") as fout:
            with open(people_ldif, "rt") as fin:
                # Copy version
                line = fin.readline()
                fout.write(line)
                line = fin.readline()
                fout.write(line)
                # Generate fake entries
                for idx in range(10):
                    fout.write(f"dn: uid=id{idx},dc=foo\nobjectclasses: extensibleObject\n\n")
                for line in iter(fin.readline, ''):
                    fout.write(line)

        os.chmod(e.ldif, 0o644)

        # 10. Offline import using backend name ou=people subtree
        e.run(far_suffix_off)
        e.check_db()

        # 11. Offline import using suffix name ou=people subtree
        e = Importer(inst, errlog, "far", suffix=DEFAULT_SUFFIX)
        e.run(far_suffix_with_suffix_off)
        e.check_db()

        # 12. Start the instance
        inst.start()

        # 13. Online import using backend name ou=people subtree
        e = Importer(inst, errlog, "far")
        e.run(far_suffix_on)
        e.check_db()

        # 14. Online import using suffix name ou=people subtree
        e = Importer(inst, errlog, "far", suffix=DEFAULT_SUFFIX)
        e.run(far_suffix_with_suffix_on)
        e.check_db()


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s {}".format(CURRENT_FILE))
