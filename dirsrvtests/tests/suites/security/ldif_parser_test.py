# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
import os
import logging
import ldap
from lib389.tasks import *
from lib389.utils import *
from test389.topologies import topology_st as topo
from lib389._constants import DEFAULT_SUFFIX

pytestmark = pytest.mark.tier1

log = logging.getLogger(__name__)


def test_ldif_parser_long_attr_name(topo):
    """
    Test that importing an LDIF with a very long attribute name followed by a semicolon
    does not cause a stack-buffer-overflow or crash in ASAN builds.

    :id: 763c44bd-7591-4f93-bc6c-1dbb7b21a996
    :feature: LDIF Parser
    :setup: Standalone Instance
    :steps:
        1. Skip if not an ASAN build
        2. Create an LDIF file where the base DN entry has a > 2500 char attribute name
           followed by a semicolon and 10 more characters.
        3. Import the LDIF file.
        4. Check ASAN report (stops server).
        5. Start server.
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
    """
    if not topo.standalone.has_asan():
        pytest.skip("This test requires an ASAN build")

    standalone = topo.standalone
    ldif_dir = standalone.get_ldif_dir()

    # Case 1: > 2500 chars, followed by semicolon and 10 chars
    log.info("Testing Case 1: base DN entry with long attribute name + ; + 10 chars")
    long_attr_name_1 = 'a' * 2505 + ';1234567890'
    ldif_file_1 = os.path.join(ldif_dir, 'long_attr_1.ldif')
    ldif_content_1 = f"""dn: {DEFAULT_SUFFIX}
objectclass: top
objectclass: domain
objectclass: extensibleObject
dc: example
{long_attr_name_1}: some value
"""
    with open(ldif_file_1, "w") as fd:
        fd.write(ldif_content_1)
    os.chmod(ldif_file_1, 0o644)
    standalone.tasks.importLDIF(suffix=DEFAULT_SUFFIX, input_file=ldif_file_1, args={TASK_WAIT: True})
    assert standalone.status(), "Server crashed during Case 1 import"

    # Check ASAN report (this stops the server)
    log.info("Checking ASAN report for Case 1")
    assert not check_asan_report(standalone, 'stack-buffer-overflow'), "ASAN error detected in Case 1 report"


    log.info("Test PASSED")


if __name__ == '__main__':
    # Run isolated
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s {}".format(CURRENT_FILE))
