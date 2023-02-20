# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import logging
import pytest
import os
import time
from lib389.topologies import topology_st as topo
from lib389.idm.user import UserAccount, UserAccounts
from lib389.idm.organizationalunit import OrganizationalUnit

log = logging.getLogger(__name__)

SUFFIX = "dc=Example,DC=COM"
SUBTREE = "ou=People,dc=Example,DC=COM"
NEW_SUBTREE = "ou=humans,dc=Example,DC=COM"
USER_DN = "uid=tUser,ou=People,dc=Example,DC=COM"
NEW_USER_DN = "uid=tUser,ou=humans,dc=Example,DC=COM"
NEW_USER_NORM_DN = "uid=tUser,ou=humans,dc=example,dc=com"


def test_dsentrydn(topo):
    """Test that the dsentrydn attribute is properly maintained and preserves the case of the DN

    :id: f0f2fe6b-c70d-4de1-a9a9-06dda74e7c30
    :setup: Standalone Instance
    :steps:
        1. Create user and make sure dsentrydn is set to the correct value/case
        2. Moddn of "ou=people" and check dsentrydn is correct for parent and the children
        3. Check the DN matches dsEntryDN
        4. Disable nsslapd-return-original-entrydn
        5. Check the DN matches normalized DN
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
    """

    inst = topo.standalone
    inst.config.replace('nsslapd-return-original-entrydn', 'on')

    # Create user and makes sure "dsEntryDN" is set correctly
    users = UserAccounts(inst, SUFFIX)
    user_properties = {
        'uid': 'tUser',
        'givenname': 'test',
        'cn': 'Test User',
        'sn': 'user',
        'userpassword': 'password',
        'uidNumber': '1000',
        'gidNumber': '2000',
        'homeDirectory': '/home/tUser'
    }
    user = users.create(properties=user_properties)
    assert user.get_attr_val_utf8('dsentrydn') == USER_DN

    # Move subtree ou=people to ou=humans
    ou = OrganizationalUnit(inst, SUBTREE)
    ou.rename("ou=humans", SUFFIX)  # NEW_SUBTREE

    # check dsEntryDN is properly updated to new subtree
    ou = OrganizationalUnit(inst, NEW_SUBTREE)
    assert ou.get_attr_val_utf8('dsentrydn') == NEW_SUBTREE

    user = UserAccount(inst, NEW_USER_DN)
    assert user.get_attr_val_utf8('dsentrydn') == NEW_USER_DN

    # Check DN returned to client matches "dsEntryDN"
    users = UserAccounts(inst, SUFFIX, rdn="ou=humans").list()
    for user in users:
        if user.rdn.startswith("tUser"):
            assert user.dn == NEW_USER_DN
            break

    # Disable 'nsslapd-return-original-entrydn' and check DN is normalized
    inst.config.replace('nsslapd-return-original-entrydn', 'off')
    inst.restart()
    users = UserAccounts(inst, SUFFIX, rdn="ou=humans").list()
    for user in users:
        if user.rdn.startswith("tUser"):
            assert user.dn == NEW_USER_NORM_DN
            break


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])

