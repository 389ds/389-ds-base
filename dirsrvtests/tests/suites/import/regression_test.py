# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
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

    log.info('Import ldif with duplicate entry')
    assert standalone.tasks.importLDIF(suffix=DEFAULT_SUFFIX, input_file=ldif_file, args={TASK_WAIT: True})

    log.info('Restart the server to flush the logs')
    standalone.restart()

    log.info('Error log should not have "unable to flush" message')
    assert not standalone.ds_error_log.match('.*unable to flush.*')

    log.info('Error log should have "Duplicated DN detected" message')
    assert standalone.ds_error_log.match('.*Duplicated DN detected.*')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s {}".format(CURRENT_FILE))
