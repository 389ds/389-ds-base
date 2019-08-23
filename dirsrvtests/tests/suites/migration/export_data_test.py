# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import logging
import pytest
import os

from lib389._constants import *
from lib389.topologies import topology_st
from lib389.idm.user import UserAccounts, TEST_USER_PROPERTIES

pytestmark = pytest.mark.tier3

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


@pytest.mark.skipif(os.getenv('MIGRATION') is None, reason="This test is meant to execute in specific test environment")
def test_export_data_from_source_host(topology_st):
    """Prepare export file for migration using a single instance of Directory Server

    :id: 47f97d87-60f7-4f80-a72b-e7daa1de0061
    :setup: Standalone
    :steps:
        1. Add a test user with employeeNumber and telephoneNumber
        2. Add a test user with escaped DN
        3. Create export file
        4. Check if values of searched attributes are present in exported file
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
    """

    standalone = topology_st.standalone
    output_file = os.path.join(topology_st.standalone.ds_paths.ldif_dir, "migration_export.ldif")

    log.info("Add a test user")
    users = UserAccounts(standalone, DEFAULT_SUFFIX)
    test_user = users.create(properties=TEST_USER_PROPERTIES)
    test_user.add('employeeNumber', '1000')
    test_user.add('telephoneNumber', '1234567890')

    assert test_user.present('employeeNumber', value='1000')
    assert test_user.present('telephoneNumber', value='1234567890')

    log.info("Creating user with escaped DN")
    users.create(properties={
        'uid': '\\#\\,\\+"\\\\>:\\=\\<\\<\\>\\;/',
        'cn': 'tuser2',
        'sn': 'user',
        'uidNumber': '1000',
        'gidNumber': '2000',
        'homeDirectory': '/home/tuser2',
    })

    log.info("Exporting LDIF offline...")
    standalone.stop()
    standalone.db2ldif(bename=DEFAULT_BENAME, suffixes=[DEFAULT_SUFFIX],
                       excludeSuffixes=None, encrypt=None, repl_data=None, outputfile=output_file)
    standalone.start()

    log.info("Check that value of attribute is present in the exported file")
    with open(output_file, 'r') as ldif_file:
        ldif = ldif_file.read()
        assert 'employeeNumber: 1000' in ldif
        assert 'telephoneNumber: 1234567890' in ldif
        assert 'uid: \\#\\,\\+"\\\\>:\\=\\<\\<\\>\\;/' in ldif


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
