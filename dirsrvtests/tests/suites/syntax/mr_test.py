# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import logging
import pytest
import os
import ldap
from lib389.dbgen import dbgen_users
from lib389._constants import *
from lib389.topologies import topology_st as topo
from lib389._controls import SSSRequestControl

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def test_sss_mr(topo):
    """Test matching rule/server side sort does not crash DS

    :id: 48c73d76-1694-420f-ab55-187135f2d260
    :setup: Standalone Instance
    :steps:
        1. Add sample entries to the database
        2. Perform search using server side control (uid:2.5.13.3)
    :expectedresults:
        1. Success
        2. Success
    """

    log.info("Creating LDIF...")
    ldif_dir = topo.standalone.get_ldif_dir()
    ldif_file = os.path.join(ldif_dir, 'mr-crash.ldif')
    dbgen_users(topo.standalone, 5, ldif_file, DEFAULT_SUFFIX)

    log.info("Importing LDIF...")
    topo.standalone.stop()
    assert topo.standalone.ldif2db(DEFAULT_BENAME, None, None, None, ldif_file)
    topo.standalone.start()

    log.info('Search using server side sorting using undefined mr in the attr...')
    sort_ctrl = SSSRequestControl(True, ['uid:2.5.13.3'])
    controls = [sort_ctrl]
    msg_id = topo.standalone.search_ext(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE,
                                        "objectclass=*", serverctrls=controls)
    try:
        rtype, rdata, rmsgid, response_ctrl = topo.standalone.result3(msg_id)
    except ldap.OPERATIONS_ERROR:
        pass

    log.info("Test PASSED")


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])

