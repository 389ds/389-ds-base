# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 William Brown <william@blackhats.net.au>
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import pytest
import time
from lib389._constants import DEFAULT_SUFFIX
from lib389.topologies import topology_m2

from lib389.replica import ReplicationManager
from lib389.idm.group import Groups
from lib389.idm.user import nsUserAccounts
from lib389.idm.organizationalunit import OrganizationalUnit as OrganisationalUnit

from lib389.plugins import AutoMembershipPlugin, ReferentialIntegrityPlugin, AutoMembershipDefinitions, MemberOfPlugin

pytestmark = pytest.mark.tier2

UCOUNT = 400

def _enable_plugins(inst, group_dn):
    # Enable automember
    amp = AutoMembershipPlugin(inst)
    amp.enable()

    # Create the automember definition
    automembers = AutoMembershipDefinitions(inst)

    automember = automembers.create(properties={
        'cn': 'testgroup_definition',
        'autoMemberScope': DEFAULT_SUFFIX,
        'autoMemberFilter': 'objectclass=nsAccount',
        'autoMemberDefaultGroup': group_dn,
        'autoMemberGroupingAttr': 'member:dn',
    })

    # Enable MemberOf
    mop = MemberOfPlugin(inst)
    mop.enable()

    # Enable referint
    rip = ReferentialIntegrityPlugin(inst)
    # We only need to enable the plugin, the default configuration is sane and
    # correctly coveres member as an enforced attribute.
    rip.enable()

    # Restart to make sure it's enabled and good to go.
    inst.restart()

def test_rename_large_subtree(topology_m2):
    """
    A report stated that the following configuration would lead
    to an operation failure:

    ou=int,ou=account,dc=...
    ou=s1,ou=int,ou=account,dc=...
    ou=s2,ou=int,ou=account,dc=...

    rename ou=s1 to re-parent to ou=account, leaving:

    ou=int,ou=account,dc=...
    ou=s1,ou=account,dc=...
    ou=s2,ou=account,dc=...

    The ou=s1 if it has < 100 entries below, is able to be reparented.

    If ou=s1 has > 400 entries, it fails.

    Other conditions was the presence of referential integrity - so one would
    assume that all users under s1 are a member of some group external to this.

    :id: 5915c38d-b3c2-4b7c-af76-8a1e002e27f7

    :setup: standalone instance

    :steps: 1. Enable automember plugin
            2. Add UCOUNT users, and ensure they are members of a group.
            3. Enable refer-int plugin
            4. Move ou=s1 to a new parent

    :expectedresults:
        1. The plugin is enabled
        2. The users are members of the group
        3. The plugin is enabled
        4. The rename operation of ou=s1 succeeds
    """

    st = topology_m2.ms["supplier1"]
    m2 = topology_m2.ms["supplier2"]

    # Create a default group
    gps = Groups(st, DEFAULT_SUFFIX)
    # Keep the group so we can get it's DN out.
    group = gps.create(properties={
        'cn': 'default_group'
    })

    _enable_plugins(st, group.dn)
    _enable_plugins(m2, group.dn)

    # Now unlike normal, we bypass the plural-create method, because we need control
    # over the exact DN of the OU to create.
    # Create the ou=account

    # We don't need to set a DN here because ...
    ou_account = OrganisationalUnit(st)

    # It's set in the .create step.
    ou_account.create(
        basedn = DEFAULT_SUFFIX,
        properties={
            'ou': 'account'
        })
    # create the ou=int,ou=account
    ou_int = OrganisationalUnit(st)
    ou_int.create(
        basedn = ou_account.dn,
        properties={
            'ou': 'int'
        })
    # Create the ou=s1,ou=int,ou=account
    ou_s1 = OrganisationalUnit(st)
    ou_s1.create(
        basedn = ou_int.dn,
        properties={
            'ou': 's1'
        })

    # Pause replication
    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.disable_to_supplier(m2, [st, ])

    # Create the users 1 -> UCOUNT in ou=s1
    nsu = nsUserAccounts(st, basedn=ou_s1.dn, rdn=None)
    for i in range(1000, 1000 + UCOUNT):
        nsu.create_test_user(uid=i)

    # Enable replication

    repl.enable_to_supplier(m2, [st, ])

    # Assert they are in the group as we expect
    members = group.get_attr_vals_utf8('member')
    assert len(members) == UCOUNT

    # Wait for replication
    repl.wait_for_replication(st, m2, timeout=60)

    for i in range(0, 5):
        # Move ou=s1 to ou=account as parent. We have to provide the rdn,
        # even though it's not changing.
        ou_s1.rename('ou=s1', newsuperior=ou_account.dn)
        time.sleep(2)

        members = group.get_attr_vals_utf8('member')
        assert len(members) == UCOUNT
        # Check that we really did refer-int properly, and ou=int is not in the members.
        for member in members:
            assert 'ou=int' not in member

        # Now move it back
        ou_s1.rename('ou=s1', newsuperior=ou_int.dn)
        time.sleep(2)
        members = group.get_attr_vals_utf8('member')
        assert len(members) == UCOUNT
        for member in members:
            assert 'ou=int' in member

    # Check everythig on the other side is good.
    repl.wait_for_replication(st, m2, timeout=60)

    group2 = Groups(m2, DEFAULT_SUFFIX).get('default_group')

    members = group2.get_attr_vals_utf8('member')
    assert len(members) == UCOUNT
    for member in members:
        assert 'ou=int' in member
