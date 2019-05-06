# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
from lib389.backend import Backends
from lib389.properties import TASK_WAIT
from lib389.utils import time, ldap, os, logging
from lib389.topologies import topology_st as topo
from lib389._constants import BACKEND_NAME, BACKEND_SUFFIX

from lib389.dbgen import dbgen

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
    backends.create(properties={BACKEND_SUFFIX: TEST_DEFAULT_SUFFIX,
                                BACKEND_NAME: TEST_DEFAULT_NAME})

    log.info('Create LDIF file and import it...')
    ldif_dir = topo.standalone.get_ldif_dir()
    ldif_file = os.path.join(ldif_dir, 'default.ldif')
    dbgen(topo.standalone, 5, ldif_file, TEST_DEFAULT_SUFFIX)

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
    backend = backends.create(properties={BACKEND_SUFFIX: TEST_SUFFIX1, BACKEND_NAME: TEST_BACKEND1})

    log.info('Create LDIF file and import it')
    ldif_dir = topo.standalone.get_ldif_dir()
    ldif_file = os.path.join(ldif_dir, 'suffix_del1.ldif')

    dbgen(topo.standalone, 10, ldif_file, TEST_SUFFIX1)

    log.info('Stopping the server and running offline import')
    topo.standalone.stop()
    assert topo.standalone.ldif2db(TEST_BACKEND1, TEST_SUFFIX1, None, None, ldif_file)
    topo.standalone.start()

    log.info('Deleting suffix-{}'.format(TEST_SUFFIX2))
    backend.delete()

    log.info('Adding the same database-{} after deleting it'.format(TEST_BACKEND1))
    backends.create(properties={BACKEND_SUFFIX: TEST_SUFFIX1, BACKEND_NAME: TEST_BACKEND1})


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
    backend = backends.create(properties={BACKEND_SUFFIX: TEST_SUFFIX2, BACKEND_NAME: TEST_BACKEND2})

    log.info('Create LDIF file and import it')
    ldif_dir = topo.standalone.get_ldif_dir()
    ldif_file = os.path.join(ldif_dir, 'suffix_del2.ldif')

    dbgen(topo.standalone, 10, ldif_file, TEST_SUFFIX2)

    topo.standalone.tasks.importLDIF(suffix=TEST_SUFFIX2, input_file=ldif_file, args={TASK_WAIT: True})

    log.info('Deleting suffix-{}'.format(TEST_SUFFIX2))
    backend.delete()

    log.info('Adding the same database-{} after deleting it'.format(TEST_BACKEND2))
    backends.create(properties={BACKEND_SUFFIX: TEST_SUFFIX2, BACKEND_NAME: TEST_BACKEND2})
    log.info('Checking if server can be restarted after re-adding the same database')
    topo.standalone.restart()
    assert not topo.standalone.detectDisorderlyShutdown()


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s {}".format(CURRENT_FILE))
