# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import pytest
import logging
from lib389.tasks import *
from test389.topologies import topology_m1 as topo
from lib389._constants import *
from lib389.idm.user import UserAccounts

pytestmark = pytest.mark.tier1

log = logging.getLogger(__name__)


def test_ldif_import_state_info(topo):
    """Test that importing an LDIF with complex replication state information
    correctly handles that state information in nscpentrywsi.

    :id: 15da0302-601d-4a6f-b006-6711e1669cf9
    :setup: Single supplier replication instance
    :steps:
        1. Disable schema checking
        2. Create an LDIF file with 777 permissions in the instance LDIF directory
           containing complex state info in attribute names (vucsn, adcsn, deletedattribute)
        3. Import the LDIF file
        4. Search for the entry and retrieve nscpentrywsi
        5. Verify that:
           - uidNumber state info is preserved
           - gidNumber state info is preserved
           - homeDirectory state info is corrected to remove bogus subtypes
           - description state info is removed from wsi values
    :expectedresults:
        1. Success
        2. LDIF file created with 777 permissions
        3. Import completes successfully
        4. Entry is found with nscpentrywsi
        5. State information for uidNumber/gidNumber is unchanged, while
           description state is removed from wsi and homeDirectory is corrected
           to remove bogus subtypes
    """
    inst = topo.ms['supplier1']

    # Disable schema checking to ensure the import of complex state info succeeds
    inst.config.set('nsslapd-schemacheck', 'off')

    ldif_dir = inst.get_ldif_dir()
    ldif_file = os.path.join(ldif_dir, 'state_import.ldif')

    # Create the LDIF file with the specified content
    ldif_content = f"""dn: {DEFAULT_SUFFIX}
objectClass: top
objectClass: domain
dc: example
description: {DEFAULT_SUFFIX}

dn: ou=people,{DEFAULT_SUFFIX}
objectClass: top
objectClass: organizationalunit
ou: people

dn: uid=state_test_user,ou=People,{DEFAULT_SUFFIX}
objectClass;vucsn-6aabdd6a000000010000: top
objectClass;vucsn-6aabdd6a000000010000: person
objectClass;vucsn-6aabdd6a000000010000: organizationalPerson
objectClass;vucsn-6aabdd6a000000010000: inetOrgPerson
objectClass;vucsn-6aabdd6a000000010000: posixAccount
objectClass;vucsn-6aabdd6a000000010000: account
uid;vucsn-6aabdd6a000000010000;mdcsn-6aabdd6a000000010000: state_test_user
cn;vucsn-6aabdd6a000000010000: state_test_user
sn;vucsn-6aabdd6a000000010000: state_test_user
uidNumber;vucsn-6aabdd6a000000010000: 1002
description;vucsn-6aa27901000300020000;000;deletedattribute;123;456
homeDirectory;vucsn-6aabdd6a000000010000;123;456: /home/state_test_user
gidNumber;adcsn-6aabdd6b000000010000;vdcsn-6aabdd6b000000010000;deletedattribute;deleted:
"""

    with open(ldif_file, 'w') as f:
        f.write(ldif_content)
    os.chmod(ldif_file, 0o777)

    log.info(f"Created LDIF file: {ldif_file}")

    # Import the LDIF file
    # We use import_suffix_from_ldif which uses the ImportTask
    inst.tasks.importLDIF(suffix=DEFAULT_SUFFIX, input_file=ldif_file, args={TASK_WAIT: True})
    log.info("Imported LDIF file")

    # Search for the entry and retrieve nscpentrywsi
    user_dn = f"uid=state_test_user,ou=People,{DEFAULT_SUFFIX}"
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    tuser = users.get(dn=user_dn)
    wsi_values = tuser.get_attr_vals('nscpentrywsi')
    entrywsi_vals = [ensure_str(val).lower() for val in wsi_values]

    log.info(f"MARK entrywsi_vals: {entrywsi_vals}")
    found_uid = any('uidnumber;vucsn-6aabdd6a000000010000' in val for val in entrywsi_vals)
    expected_desc = 'description;vucsn-6aa27901000300020000;000;deletedattribute;123;456'
    found_desc = any(expected_desc in val for val in entrywsi_vals)
    expected_gid = 'gidnumber;adcsn-6aabdd6b000000010000;vdcsn-6aabdd6b000000010000;deletedattribute;deleted'
    found_gid = any(expected_gid in val for val in entrywsi_vals)
    expected_home = 'homedirectory;vucsn-6aabdd6a000000010000;123;456'
    found_home = any(expected_home in val for val in entrywsi_vals)

    # assert entry_wsi values are correct
    assert found_uid, "uidNumber state info was not correctly preserved"
    assert found_gid, "gidNumber state info was not correctly preserved"
    assert not found_home, "homeDirectory state info was not correctly adjusted"
    assert not found_desc, "description incorrectly found"

    # assert entry attributes are correct
    assert len(tuser.get_attr_vals('description')) == 0, "description should be deleted"
    assert len(tuser.get_attr_vals('gidNumber')) == 0, "gidNumber should be deleted"
    assert len(tuser.get_attr_vals('uidNumber')) == 1, "uidNumber should be 1"


if __name__ == "__main__":
    # Run isolated
    # -s for DEBUG mode
    import os
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
