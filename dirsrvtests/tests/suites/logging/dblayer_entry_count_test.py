# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import os
import re
import ldap
import pytest
from lib389._constants import *
from lib389.backend import Backends
from lib389.idm.domain import Domain
from lib389.idm.user import UserAccounts
from test389.topologies import topology_st as topo

pytestmark = pytest.mark.tier1
DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def test_dblayer_instance_start_logs_entry_count(topo, request):
    """Verify dblayer_instance_start logs the correct number of entries for
    each backend when the server starts

    :id: c94544af-0acc-4c1a-85dc-e6c4098d2512
    :setup: Standalone instance
    :steps:
        1. Create two additional backends/suffixes, each with its own root entry
        2. Add a distinct number of user entries to each backend
        3. Count the entries actually present in each backend
        4. Restart the server with a fresh error log
        5. Check the error log reports the matching entry count for each backend
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
    """
    inst = topo.standalone
    backends = Backends(inst)
    created_backends = []

    # Each backend gets a different number of entries so a copy/paste bug
    # that reports one backend's count for the other is caught.  Built fresh
    # here (rather than at module scope) since the loop below adds an
    # "expected_count" key to each dict - a module-level list would carry
    # that mutation into any other test that imports it.
    backend_defs = [
        {"name": "dblayercount1", "suffix": "dc=dblayercount1,dc=com", "num_users": 5},
        {"name": "dblayercount2", "suffix": "dc=dblayercount2,dc=com", "num_users": 9},
    ]

    def fin():
        for be in created_backends:
            try:
                be.delete()
            except ldap.LDAPError as e:
                log.warning("Failed to delete backend %s: %s", be.dn, str(e))
    request.addfinalizer(fin)

    # Step 1 & 2 - create the backends/suffixes and populate them
    for be_def in backend_defs:
        log.info("Creating backend %s (%s)", be_def["name"], be_def["suffix"])
        be = backends.create(properties={
            'nsslapd-suffix': be_def["suffix"],
            'name': be_def["name"],
        })
        created_backends.append(be)

        dc_value = be_def["suffix"].split('=')[1].split(',')[0]
        Domain(inst, be_def["suffix"]).create(properties={'dc': dc_value})

        users = UserAccounts(inst, be_def["suffix"], rdn=None)
        for i in range(be_def["num_users"]):
            users.create_test_user(uid=1000 + i)

    # Step 3 - count what is actually stored under each suffix (root entry + users)
    for be_def in backend_defs:
        entries = inst.search_s(be_def["suffix"], ldap.SCOPE_SUBTREE, "(objectclass=*)")
        be_def["expected_count"] = len(entries)
        log.info("Backend %s has %d entries before restart",
                 be_def["name"], be_def["expected_count"])

    # Step 4 - restart with a clean error log so each backend's startup
    # message appears exactly once
    inst.deleteErrorLogs()

    # Step 5 - verify the logged entry count matches what is on disk
    for be_def in backend_defs:
        pattern = r'.*dblayer_instance_start.*%s:\s*(\d+)\s*entries.*' % re.escape(be_def["name"])
        matches = inst.ds_error_log.match(pattern)
        assert matches, "No dblayer_instance_start log entry found for backend %s" % be_def["name"]

        found_count = int(re.match(pattern, matches[-1]).group(1))
        assert found_count == be_def["expected_count"], (
            "dblayer_instance_start reported %d entries for %s, expected %d"
            % (found_count, be_def["name"], be_def["expected_count"]))


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
