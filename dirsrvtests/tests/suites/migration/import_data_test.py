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
from lib389.idm.user import UserAccounts

pytestmark = pytest.mark.tier3

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


@pytest.mark.skipif(os.getenv('MIGRATION') is None, reason="This test is meant to execute in specific test environment")
def test_import_data_to_target_host(topology_st):
    """Import file created in export_data_test.py using a single instance of Directory Server

    :id: 7e896b0c-6838-49c7-8e1d-5e8114f5fb02
    :setup: Standalone
    :steps:
        1. Check that attribute values are present in input file
        2. Import input file
        3. Check imported user data
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """

    standalone = topology_st.standalone
    input_file = os.path.join(topology_st.standalone.ds_paths.ldif_dir, "migration_export.ldif")

    log.info("Check that value of attribute is present in the exported file")
    with open(input_file, 'r') as ldif_file:
        ldif = ldif_file.read()
        assert 'employeeNumber: 1000' in ldif
        assert 'telephoneNumber: 1234567890' in ldif
        assert 'uid: \\#\\,\\+"\\\\>:\\=\\<\\<\\>\\;/' in ldif

    log.info('Stopping the server and running offline import...')
    standalone.stop()
    assert standalone.ldif2db(bename=DEFAULT_BENAME, suffixes=[DEFAULT_SUFFIX], encrypt=None, excludeSuffixes=None,
                                     import_file=input_file)
    standalone.start()

    log.info("Check imported user data")
    users = UserAccounts(standalone, DEFAULT_SUFFIX)
    test_user = users.get('testuser')
    assert test_user.present('employeeNumber', value='1000')
    assert test_user.present('telephoneNumber', value='1234567890')
    test_user = users.get('\\#\\,\\+"\\\\>:\\=\\<\\<\\>\\;/')
    assert test_user.present('cn', value='tuser2')
    assert test_user.present('uid', value='\\#\\,\\+"\\\\>:\\=\\<\\<\\>\\;/')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
