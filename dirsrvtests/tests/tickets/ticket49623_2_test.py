# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import ldap
import pytest
import subprocess
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_m1
from lib389.idm.user import UserAccounts
from lib389._constants import DEFAULT_SUFFIX
from contextlib import contextmanager

pytestmark = pytest.mark.tier1

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


@pytest.mark.ds49623
@pytest.mark.bz1790986
def test_modrdn_loop(topology_m1):
    """Test that renaming the same entry multiple times reusing the same
       RDN multiple times does not result in cenotaph error messages

    :id: 631b2be9-5c03-44c7-9853-a87c923d5b30

    :customerscenario: True

    :setup: Single master instance

    :steps: 1. Add an entry with RDN start rdn
            2. Rename the entry to rdn change
            3. Rename the entry to start again
            4. Rename the entry to rdn change
            5. check for cenotaph error messages
    :expectedresults:
            1. No error messages
    """

    topo = topology_m1.ms['master1']
    TEST_ENTRY_RDN_START = 'start'
    TEST_ENTRY_RDN_CHANGE = 'change'
    TEST_ENTRY_NAME = 'tuser'
    users = UserAccounts(topo, DEFAULT_SUFFIX)
    user_properties = {
        'uid': TEST_ENTRY_RDN_START,
        'cn': TEST_ENTRY_NAME,
        'sn': TEST_ENTRY_NAME,
        'uidNumber': '1001',
        'gidNumber': '2001',
        'homeDirectory': '/home/{}'.format(TEST_ENTRY_NAME)
    }

    tuser = users.create(properties=user_properties)
    tuser.rename('uid={}'.format(TEST_ENTRY_RDN_CHANGE), newsuperior=None, deloldrdn=True)
    tuser.rename('uid={}'.format(TEST_ENTRY_RDN_START), newsuperior=None, deloldrdn=True)
    tuser.rename('uid={}'.format(TEST_ENTRY_RDN_CHANGE), newsuperior=None, deloldrdn=True)

    log.info("Check the log messages for cenotaph error")
    error_msg = ".*urp_fixup_add_cenotaph - failed to add cenotaph, err= 68"
    assert not topo.ds_error_log.match(error_msg)
