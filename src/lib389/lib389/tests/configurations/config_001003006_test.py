# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import ldap

import logging
import sys
import time

import pytest

from lib389 import DirSrv
from lib389._constants import *
from lib389.properties import *

from lib389.topologies import topology_st

from lib389.utils import ds_is_older
pytestmark = pytest.mark.skipif(ds_is_older('1.4.0'), reason="Not implemented")

REQUIRED_DNS = [
    'dc=example,dc=com',
    'ou=Groups,dc=example,dc=com',
    'ou=People,dc=example,dc=com',
    'ou=Special Users,dc=example,dc=com',
    'cn=Accounting Managers,ou=Groups,dc=example,dc=com',
    'cn=HR Managers,ou=Groups,dc=example,dc=com',
    'cn=QA Managers,ou=Groups,dc=example,dc=com',
    'cn=PD Managers,ou=Groups,dc=example,dc=com',
    'cn=Directory Administrators,dc=example,dc=com',
]

def test_install_sample_entries(topology_st):
    # Assert that our entries match.

    entries = topology_st.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE)

    for entry in entries:
        assert(entry.dn in REQUIRED_DNS)
        # We can make this assert the full object content, plugins and more later.

    assert(True)
