# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
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
from lib389._constants import DEFAULT_SUFFIX
from lib389.tasks import *
from lib389.idm.user import UserAccounts
from lib389.idm.directorymanager import DirectoryManager
from lib389.dbgen import *
from lib389.utils import *

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


def test_replay_import_operation(topo):
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
@pytest.mark.xfail(ds_is_older("1.3.10.1"), reason="bz1749595 not fixed on versions older than 1.3.10.1")
def test_large_ldif2db_ancestorid_index_creation(topo):
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


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s {}".format(CURRENT_FILE))
