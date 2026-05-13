# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import os
import pytest
import ldap
from lib389.topologies import topology_st
from lib389._constants import DEFAULT_SUFFIX
from lib389.plugins import LinkedAttributesPlugin, LinkedAttributesConfigs, USNPlugin
from lib389.idm.user import UserAccounts

pytestmark = pytest.mark.tier1

log = logging.getLogger(__name__)

LINKTYPE = 'directReport'
MANAGEDTYPE = 'manager'


@pytest.fixture(scope='function')
def setup_linked_attributes(topology_st, request):
    """Fixture to set up the Linked Attributes plugin."""
    log.info('Setting up Linked Attributes plugin')

    log.info('Enable Linked Attributes plugin')
    linkedattr = LinkedAttributesPlugin(topology_st.standalone)
    linkedattr.enable()
    topology_st.standalone.restart()

    log.info('Add the plugin config entry')
    la_configs = LinkedAttributesConfigs(topology_st.standalone)
    config = la_configs.create(properties={'cn': 'Manager Link',
                                  'linkType': LINKTYPE,
                                  'managedType': MANAGEDTYPE})

    def fin():
        log.info('Cleaning up Linked Attributes plugin')
        config.delete()
        linkedattr.disable()
        topology_st.standalone.restart()

    request.addfinalizer(fin)


def test_replace_linktype_no_spurious_managedtype_mods(topology_st, setup_linked_attributes, request):
    """A linktype MOD_REPLACE must not re-modify overlap targets.

    :id: 405f7dab-1c36-458d-a2bf-abf3284c7c41
    :setup: Standalone Instance, USN + Linked Attributes enabled
    :steps:
        1. Set linkType=[t0, t3] on a source entry
        2. Snapshot entryUSN of every target
        3. MOD_REPLACE linkType to [t0, t1, t2, t3]
    :expectedresults:
        1. Success
        2. Success
        3. t0 and t3 entryUSN unchanged (overlap), t1 and t2 advanced
    """
    inst = topology_st.standalone
    USNPlugin(inst).enable()
    inst.restart()

    users = UserAccounts(inst, DEFAULT_SUFFIX)
    targets = [users.create_test_user(uid=500000 + i, gid=500000 + i)
               for i in range(4)]
    source = users.create_test_user(uid=500099, gid=500099)
    source.add('objectclass', 'extensibleObject')

    def fin():
        for u in targets + [source]:
            try:
                u.delete()
            except ldap.LDAPError:
                pass
    request.addfinalizer(fin)

    source.replace(LINKTYPE, [targets[0].dn, targets[3].dn])
    assert targets[0].present(MANAGEDTYPE, source.dn)
    assert targets[3].present(MANAGEDTYPE, source.dn)

    usn_before = [t.get_attr_val_int('entryusn') for t in targets]
    source.replace(LINKTYPE, [t.dn for t in targets])
    usn_after = [t.get_attr_val_int('entryusn') for t in targets]

    for i in (0, 3):
        assert usn_after[i] == usn_before[i], (
            f't{i} entryUSN advanced ({usn_before[i]} -> {usn_after[i]}); '
            f'it was already linked before and after the replace')
    for i in (1, 2):
        assert usn_after[i] > usn_before[i], f't{i} should have been linked'


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
