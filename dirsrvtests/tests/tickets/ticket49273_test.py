# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import pytest
import ldap

from lib389.topologies import topology_st
# This pulls in logging I think
from lib389.utils import *
from lib389.sasl import PlainSASL
from lib389.idm.services import ServiceAccounts

pytestmark = pytest.mark.tier2

log = logging.getLogger(__name__)

def test_49273_corrupt_dbversion(topology_st):
    """
    ticket 49273 was caused by a disk space full, which corrupted
    the users DBVERSION files. We can't prevent this, but we can handle
    the error better than "crash".
    """

    standalone = topology_st.standalone

    # Stop the instance
    standalone.stop()
    # Corrupt userRoot dbversion
    dbvf = os.path.join(standalone.ds_paths.db_dir, 'userRoot/DBVERSION')
    with open(dbvf, 'w') as f:
        # This will trunc the file
        f.write('')
    # Start up
    try:
        # post_open false, means ds state is OFFLINE, which allows
        # dspaths below to use defaults rather than ldap check.
        standalone.start(timeout=20, post_open=False)
    except:
        pass
    # Trigger an update of the running server state, to move it OFFLINE.
    standalone.status()

    # CHeck error log?
    error_lines = standalone.ds_error_log.match('.*Could not parse file.*')
    assert(len(error_lines) > 0)

