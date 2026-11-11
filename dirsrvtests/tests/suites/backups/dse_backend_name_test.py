# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
"""Check that a user backend named "dse" is not used as the internal DSE"""

import logging
import os
import pytest
from lib389.backend import Backends
from lib389.config import LDBMConfig
from test389.topologies import topology_st as topo

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

IMPLEMENT_ATTR = "nsslapd-backend-implement"
CONFIG_ATTR = "nsslapd-accesslog-maxlogsperdir"


def test_backend_named_dse(topo):
    """A user backend named "dse" is not used as the internal DSE

    :id: 2af825bd-68ea-44aa-9cdb-b542e6164792
    :setup: Standalone instance
    :steps:
        1. Read nsslapd-backend-implement
        2. Create a backend named "dse" with its own suffix
        3. Change a cn=config attribute so that dse.ldif is written
        4. Read nsslapd-backend-implement again
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. The value is the same as in step 1
    """
    inst = topo.standalone
    ldbm_config = LDBMConfig(inst)
    implement = ldbm_config.get_attr_val_utf8(IMPLEMENT_ATTR)
    log.info("%s is %s", IMPLEMENT_ATTR, implement)

    props = {"nsslapd-suffix": "dc=dsetest", "name": "dse"}
    Backends(inst).create(properties=props)
    new_value = str(int(inst.config.get_attr_val_utf8(CONFIG_ATTR)) + 1)
    inst.config.replace(CONFIG_ATTR, new_value)

    assert ldbm_config.get_attr_val_utf8(IMPLEMENT_ATTR) == implement


if __name__ == "__main__":
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
