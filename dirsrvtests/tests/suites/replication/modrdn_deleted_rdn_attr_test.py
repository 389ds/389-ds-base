# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
"""Regression test for issue #8063 - Stale RDN value state

A replicated MODRDN may need to restore the new RDN value on an entry whose
RDN attribute has no present values anymore but was deleted by a more recent
update (the attribute only lives on the entry deleted attribute list with a
deletion CSN newer than the MODRDN CSN).

entry_get_rdn_mods() looked up the attribute with slapi_entry_attr_find(),
which ignores deleted attributes, so the newer deletion CSN was not taken into
account: the RDN value was added with the (older) MODRDN CSN and the add was
dropped, leaving an entry without its RDN value and suppliers out of sync.
"""

import logging
import os
import time

import ldap
import pytest
from lib389._constants import DEFAULT_SUFFIX
from lib389.agreement import Agreements
from lib389.idm.user import UserAccount, UserAccounts
from lib389.replica import ReplicationManager
from test389.topologies import topology_m2 as topo

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
logging.getLogger(__name__).setLevel(logging.DEBUG if DEBUGGING else logging.INFO)
log = logging.getLogger(__name__)

NEW_VALUE = 'rdn_value'


def _get_agreement(supplier, consumer):
    for agmt in Agreements(supplier).list():
        if int(agmt.get_attr_val_utf8('nsDS5ReplicaPort')) == consumer.port:
            return agmt
    raise AssertionError(f'No agreement from {supplier.serverid} to {consumer.serverid}')


@pytest.mark.parametrize('value_deleted', [False, True],
                         ids=['attr_deleted', 'value_deleted_after'])
@pytest.mark.parametrize('rdn_attr', ['description', 'preferredLanguage'])
def test_modrdn_restores_rdn_on_deleted_attribute(topo, rdn_attr, value_deleted, request):
    """Check that a replicated MODRDN restores the new RDN value even if the
    RDN attribute was deleted by a more recent update

    :id: d9632d3b-021f-4607-9d8d-fab4a1439336
    :parametrized: yes
    :setup: Two suppliers replication topology
    :steps:
        1. Add a user without the future RDN attribute and wait for replication
        2. Pause the replication agreements between both suppliers
        3. On supplier1, rename the user to <rdn_attr>=<new value> keeping the old RDN
        4. On supplier2, later, replace the (absent) <rdn_attr> attribute with no value
           and, if value_deleted, then add and delete the RDN value so that its
           deletion CSN is more recent than the attribute deletion CSN
        5. Resume supplier1 -> supplier2 replication and wait for it
        6. Resume supplier2 -> supplier1 replication and wait for it
        7. Check that the renamed entry holds the RDN value on both suppliers
        8. Check that the entry is not flagged as a conflict on both suppliers
        9. Check that the server is still alive and both suppliers converged
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. The RDN value is present on both suppliers
        8. No nsds5ReplConflict attribute
        9. Success
    """
    s1 = topo.ms["supplier1"]
    s2 = topo.ms["supplier2"]
    repl = ReplicationManager(DEFAULT_SUFFIX)
    uid = f'user_8063_{rdn_attr.lower()}_{int(value_deleted)}'
    rdn_value = f'{NEW_VALUE}_{int(value_deleted)}'
    new_rdn = f'{rdn_attr}={rdn_value}'

    # Step 1
    users = UserAccounts(s1, DEFAULT_SUFFIX)
    user = users.create(properties={
        'uid': uid,
        'cn': uid,
        'sn': uid,
        'uidNumber': '8063',
        'gidNumber': '8063',
        'homeDirectory': f'/home/{uid}',
    })
    repl.wait_for_replication(s1, s2)
    old_dn = user.dn
    new_dn = f'{new_rdn},' + ','.join(ldap.explode_dn(old_dn)[1:])

    agmt_s1_s2 = _get_agreement(s1, s2)
    agmt_s2_s1 = _get_agreement(s2, s1)

    def fin():
        agmt_s1_s2.resume()
        agmt_s2_s1.resume()
        if not DEBUGGING:
            for dn in (old_dn, new_dn):
                entry = UserAccount(s1, dn)
                if entry.exists():
                    entry.delete()
            repl.wait_for_replication(s1, s2)

    request.addfinalizer(fin)

    # Step 2
    agmt_s1_s2.pause()
    agmt_s2_s1.pause()

    # Step 3: MODRDN gets the older CSN
    user.rename(new_rdn, deloldrdn=False)

    # Step 4: attribute deletion gets a more recent CSN
    time.sleep(2)
    # Replacing the (absent) attribute with no value leaves it on the deleted
    # attribute list of the entry with that deletion CSN, without giving it a
    # value that would compete with the RDN value on supplier1.
    s2_user = UserAccount(s2, old_dn)
    s2_user.set(rdn_attr, None, action=ldap.MOD_REPLACE)
    if value_deleted:
        # The RDN value lands on the deleted value list with a deletion
        # CSN more recent than the attribute deletion CSN.
        time.sleep(1)
        s2_user.add(rdn_attr, rdn_value)
        time.sleep(1)
        s2_user.remove(rdn_attr, rdn_value)
    assert not s2_user.present(rdn_attr)

    # Steps 5 and 6: supplier2 receives the MODRDN after its own deletion
    agmt_s1_s2.resume()
    repl.wait_for_replication(s1, s2)
    agmt_s2_s1.resume()
    repl.wait_for_replication(s2, s1)

    # Steps 7 and 8
    for inst in (s1, s2):
        entry = UserAccount(inst, new_dn)
        assert entry.exists(), f'{new_dn} is missing on {inst.serverid}'
        vals = [v.lower() for v in entry.get_attr_vals_utf8(rdn_attr)]
        log.info(f'{inst.serverid}: {rdn_attr} = {vals}')
        assert rdn_value.lower() in vals, \
            f'RDN value {new_rdn} is missing from {new_dn} on {inst.serverid}'
        assert not entry.present('nsds5ReplConflict'), \
            f'{new_dn} is flagged as a conflict on {inst.serverid}'

    # Step 9
    assert s1.status()
    assert s2.status()
    repl.test_replication(s1, s2)
    repl.test_replication(s2, s1)
    e1 = UserAccount(s1, new_dn)
    e2 = UserAccount(s2, new_dn)
    assert sorted(e1.get_attr_vals_utf8_l(rdn_attr)) == \
        sorted(e2.get_attr_vals_utf8_l(rdn_attr))


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
