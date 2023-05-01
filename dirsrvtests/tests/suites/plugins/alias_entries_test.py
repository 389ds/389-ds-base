# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2023 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import pytest
import os
import ldap
import time
from lib389.utils import ensure_str
from lib389.plugins import AliasEntriesPlugin
from lib389.idm.user import UserAccount
from lib389._constants import DEFAULT_SUFFIX
from lib389.topologies import topology_st as topo

log = logging.getLogger(__name__)

TEST_ENTRY_NAME = "entry"
TEST_ENTRY_DN = "cn=entry," + DEFAULT_SUFFIX
TEST_ALIAS_NAME = "alias entry"
TEST_ALIAS_DN = "cn=alias_entry," + DEFAULT_SUFFIX
TEST_ALIAS_DN_WRONG = "cn=alias_entry_not_there," + DEFAULT_SUFFIX
EXPECTED_UIDNUM = "1000"


def test_entry_alias(topo):
    """Test that deref search for alias entry works

    :id: 454e85af-0e20-4a36-9b3a-02562b1db53d
    :setup: Standalone Instance
    :steps:
        1. Enable alias entry plugin
        2. Create entry and alias entry
        3. Set deref option and do a base search
        4. Test non-base scope ssearch returns error
        5. Test invalid alias DN returns error
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
    """
    inst = topo.standalone

    # Enable Alias Entries plugin
    alias_plugin = AliasEntriesPlugin(inst)
    alias_plugin.enable()
    inst.restart()

    # Add entry
    test_user = UserAccount(inst, TEST_ENTRY_DN)
    test_user.create(properties={
        'uid': TEST_ENTRY_NAME,
        'cn': TEST_ENTRY_NAME,
        'sn': TEST_ENTRY_NAME,
        'userPassword': TEST_ENTRY_NAME,
        'uidNumber': '1000',
        'gidNumber': '2000',
        'homeDirectory': '/home/alias_test',
    })

    # Add entry that has an alias set to the first entry
    test_alias = UserAccount(inst, TEST_ALIAS_DN)
    test_alias.create(properties={
        'uid': TEST_ALIAS_NAME,
        'cn': TEST_ALIAS_NAME,
        'sn': TEST_ALIAS_NAME,
        'userPassword': TEST_ALIAS_NAME,
        'uidNumber': '1001',
        'gidNumber': '2001',
        'homeDirectory': '/home/alias_test',
        'objectclass': ['alias', 'extensibleObject'],
        'aliasedObjectName': TEST_ENTRY_DN,
    })

    # Set the deref "finding" option
    inst.set_option(ldap.OPT_DEREF, ldap.DEREF_FINDING)

    # Do base search which could map entry to the aliased one
    log.info("Test alias")
    deref_user = UserAccount(inst, TEST_ALIAS_DN)
    result = deref_user.search(scope="base")
    assert result[0].dn == TEST_ENTRY_DN
    assert ensure_str(result[0].getValue('uidNumber')) == EXPECTED_UIDNUM

    # Do non-base search which could raise an error
    log.info("Test unsupported search scope")
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        deref_user.search(scope="subtree")

    # Reset the aliasObjectname to a DN that does not exist, and try again
    log.info("Test invalid alias")
    test_alias.replace('aliasedObjectName', TEST_ALIAS_DN_WRONG)
    try:
        deref_user.search(scope="base")
        assert False
    except ldap.LDAPError as e:
        msg = e.args[0]['info']
        assert msg.startswith("Failed to dereference alias object")


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
