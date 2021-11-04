# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2021 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ----

# Author: Anton Bobrov <abobrov@redhat.com>

import logging
import pytest
import os
import ldap
from ldap.controls import LDAPControl
from lib389 import DirSrv
from lib389.rootdse import RootDSE
from lib389.utils import *
from lib389._constants import *
from lib389.topologies import create_topology
from lib389.topologies import topology_st as topo
from lib389.idm.user import UserAccount, UserAccounts

log = logging.getLogger(__name__)

pytestmark = pytest.mark.tier0

"""
This BooleanControl class is taken from python-ldap,
see https://www.python-ldap.org/ for details.
The reason is python-ldap standard class has not been
updated for Python 3 properly and behaves incorrectly.
When python-ldap is fixed this class can be removed in
exchange for from ldap.controls import BooleanControl
"""
class BooleanControl(LDAPControl):
  """
  Base class for simple request controls with boolean control value.
  Constructor argument and class attribute:
  booleanValue
    Boolean (True/False or 1/0) which is the boolean controlValue.
  """
  boolean2ber = { 1:b'\x01\x01\xFF', 0:b'\x01\x01\x00' }
  ber2boolean = { b'\x01\x01\xFF':1, b'\x01\x01\x00':0 }

  def __init__(self,controlType=None,criticality=False,booleanValue=False):
    self.controlType = controlType
    self.criticality = criticality
    self.booleanValue = booleanValue

  def encodeControlValue(self):
    return self.boolean2ber[int(self.booleanValue)]

  def decodeControlValue(self,encodedControlValue):
    self.booleanValue = self.ber2boolean[encodedControlValue]

def has_subentries_control(topo):
    """
    Checks if server supports LDAP Subentries control
    """
    rdse = RootDSE(topo.standalone)
    return "1.3.6.1.4.1.4203.1.10.1" in rdse.get_supported_ctrls()


@pytest.fixture(scope="module")
def setup_test_entries(topo, request):
    """
    Add nentries entries and nentries subentries.
    """
    users = []
    # Add normal entries.
    user = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    for i in range(0, nentries):
        user1 = user.create_test_user(uid=i)
        users.append(user1)

    # Add subentries.
    for i in range(nentries, nentries * 2):
        user1 = user.create_test_user(uid=i)
        user1.add("objectclass", "ldapsubentry")
        users.append(user1)

    def fin():
        for user in users:
            user.delete()


    request.addfinalizer(fin)


nentries = 5
search_entries = '(objectclass=inetorgperson)'
search_subentries = '(objectclass=ldapsubentry)'

# Test matrix parameters
searches = [
            # Search with subentries control visibility TRUE
            (search_entries, True, True, nentries, True),
            # Search with subentries control visibility FALSE
            (search_entries, True, False, nentries, False),
            # Search for normal entries
            (search_entries, False, None, nentries, False),
            # Search for subentries
            (search_subentries, False, None, nentries, True),
            # Search for normal entries and subentries
            (f'(|{search_entries}{search_subentries})', False, None,
                nentries * 2, None)
        ]

@pytest.mark.parametrize('search_filter, use_control, controlValue,'\
        'expected_nentries, expected_subentries', searches)
def test_subentries(topo, setup_test_entries, search_filter, use_control,
        controlValue, expected_nentries, expected_subentries):
    """Test LDAP Subentries control (RFC 3672)

    :id: 5cdb72eb-d227-49c8-9f7a-89314c717a85
    :setup: Standalone Instance
    :parametrized: yes
    :steps:
        1. Add test entries and subentries
        2. Search with subentries control visibility TRUE
        3. Search with subentries control visibility FALSE
        4. Search for normal entries
        5. Search for subentries
        6. Search for normal entries and subentries
    :expectedresults:
        1. Entries and subentries should be added
        2. Only subentries are visible
        3. Only normal entries are visible
        4. Only normal entries are visible
        5. Only subentries are visible
        6. Both normal entries and subentries are visible
    """
    if use_control and not has_subentries_control(topo):
        pytest.skip("This test is only required when LDAP Subentries "\
                "control is supported.")

    request_control = BooleanControl(
                controlType="1.3.6.1.4.1.4203.1.10.1",
                criticality=True, booleanValue=controlValue)

    if use_control:
        request_ctrl = [request_control]
    else:
        request_ctrl = None

    entries = topo.standalone.search_ext_s("ou=people," +
        DEFAULT_SUFFIX,
        ldap.SCOPE_SUBTREE,
        search_filter,
        serverctrls=request_ctrl)

    assert len(entries) == expected_nentries
    if expected_subentries is not None:
        if expected_subentries:
            for entry in entries:
                assert ensure_bytes("ldapsubentry") in \
                        entry.getValues("objectclass")
        else:
            for entry in entries:
                assert ensure_bytes("ldapsubentry") not in \
                        entry.getValues("objectclass")


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
