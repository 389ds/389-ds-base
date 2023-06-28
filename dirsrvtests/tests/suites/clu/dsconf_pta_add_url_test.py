# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2023 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
import os
import logging

from lib389.topologies import topology_st
from lib389.cli_conf.plugins.ldappassthrough import pta_add
from lib389._constants import DEFAULT_SUFFIX
from lib389.cli_base import FakeArgs
from . import check_value_in_log_and_reset

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)
new_url = "ldap://localhost:7389/o=redhat"


def test_dsconf_add_pta_url(topology_st):
    """ Test dsconf add a PTA URL

        :id: 38c7331c-b828-4671-a39f-4f57d1742178
        :setup: Standalone instance
        :steps:
             1. Try to add new PTA URL
             2. Check if new PTA URL is added.
        :expectedresults:
             1. Success
             2. Success
        """

    args = FakeArgs()
    args.URL = new_url

    log.info("Add new URL.")
    pta_add(topology_st.standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value="Successfully added URL")


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
