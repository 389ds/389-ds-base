# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import logging
import os
import pytest
from lib389.topologies import topology_st as topo
from lib389.dbgen import dbgen_users, get_index
from lib389.idm.organizationalunit import OrganizationalUnit
from lib389.idm.user import UserAccount, UserAccounts
from lib389.tasks import ImportTask


log = logging.getLogger(__name__)

SUFFIX = "dc=Example,DC=COM"
SUBTREE = "ou=People,dc=Example,DC=COM"
NEW_SUBTREE = "ou=humans,dc=Example,DC=COM"
USER_DN = "uid=tUser,ou=People,dc=Example,DC=COM"
NEW_USER_DN = "uid=tUser,ou=humans,dc=Example,DC=COM"
NEW_USER_NORM_DN = "uid=tUser,ou=humans,dc=example,dc=com"

IMPORT_LDIF_NAME = "ds_entrydn_import.ldif"
NUM_IMPORT_ENTRIES = 10
IMPORT_ENTRY_NAME = "importuser"
IMPORT_PARENT = f"ou=people,{SUFFIX}"


def _import_user_dns():
    """DNs produced by dbgen_users(generic=True, entry_name=IMPORT_ENTRY_NAME)."""
    return [
        f"uid={IMPORT_ENTRY_NAME}{get_index(i, NUM_IMPORT_ENTRIES)},{IMPORT_PARENT}"
        for i in range(1, NUM_IMPORT_ENTRIES + 1)
    ]


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


def test_dsentrydn_import_ldif(topo, request):
    """Imported entries receive dsEntryDN matching their DN

    :id: 8457793e-8374-4435-b6b7-701b58246d91
    :setup: Standalone Instance
    :steps:
        1. Generate an LDIF with 10 users under ou=people using dbgen_users
        2. Import the LDIF into the suffix
        3. Verify each imported entry has dsentrydn set to its DN
    :expectedresults:
        1. LDIF file is created with 10 user entries
        2. Online import completes successfully
        3. All 10 entries have dsentrydn matching their DN
    """
    inst = topo.standalone

    ldif_path = os.path.join(inst.get_ldif_dir(), IMPORT_LDIF_NAME)
    dbgen_users(
        inst,
        NUM_IMPORT_ENTRIES,
        ldif_path,
        SUFFIX,
        generic=True,
        entry_name=IMPORT_ENTRY_NAME,
        parent=IMPORT_PARENT,
    )

    import_task = ImportTask(inst)
    import_task.import_suffix_from_ldif(ldiffile=ldif_path, suffix=SUFFIX)
    import_task.wait()

    for dn in _import_user_dns():
        user = UserAccount(inst, dn)
        assert user.exists(), dn
        assert user.get_attr_val_utf8('dsentrydn') == dn


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])

