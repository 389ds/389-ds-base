# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import pytest
import os
import time
from lib389._constants import DEFAULT_SUFFIX
from test389.topologies import topology_st as topo
from lib389.plugins import MemberOfPlugin
from lib389.idm.user import UserAccounts
from lib389.idm.group import Groups
from lib389.idm.nscontainer import nsContainers
from lib389.utils import get_default_db_lib

log = logging.getLogger(__name__)

EXCLUDED_SUBTREE = f'cn=excluded,{DEFAULT_SUFFIX}'


def configure_memberof(inst, scope=None, exclude=None):
    memberof = MemberOfPlugin(inst)
    memberof.enable()
    memberof.set_autoaddoc('nsMemberOf')
    memberof.set_memberofdeferredupdate('on')
    memberof.remove_all('memberOfEntryScope')
    memberof.remove_all('memberOfEntryScopeExcludeSubtree')
    if scope:
        memberof.set('memberOfEntryScope', scope)
    if exclude:
        memberof.set('memberOfEntryScopeExcludeSubtree', exclude)
    inst.restart()


def wait_for_memberof(user, group_dn, timeout=10):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        values = {v.lower() for v in user.get_attr_vals_utf8('memberOf')}
        if group_dn.lower() in values:
            return True
        time.sleep(0.5)
    return False


@pytest.mark.skipif(get_default_db_lib() == "mdb", reason="Not supported over mdb")
def test_deferred_update_in_entry_scope(topo):
    """Deferred memberOf updates are applied when memberOfEntryScope is set

    :id: 2a4877f8-1e63-4a6c-9f35-3c6b0f9d7b41
    :setup: Standalone Instance
    :steps:
        1. Enable memberOf with deferred updates, memberOfEntryScope set to
           the suffix and one exclude subtree
        2. Create a user and a group inside the scope
        3. Add the user as a member of the group
        4. Check the user's memberOf attribute
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. memberOf contains the group DN
    """
    inst = topo.standalone
    configure_memberof(inst, scope=DEFAULT_SUFFIX, exclude=EXCLUDED_SUBTREE)

    user = UserAccounts(inst, DEFAULT_SUFFIX).create_test_user(uid=1001)
    group = Groups(inst, DEFAULT_SUFFIX).create(properties={
        'cn': 'deferred_scope_group',
        'description': 'group inside the entry scope',
    })
    group.add_member(user.dn)

    assert wait_for_memberof(user, group.dn), \
        'memberOf was not applied by the deferred update with entry scope set'


@pytest.mark.skipif(get_default_db_lib() == "mdb", reason="Not supported over mdb")
def test_deferred_update_exclude_subtree_honored(topo):
    """Deferred memberOf updates honor memberOfEntryScopeExcludeSubtree

    :id: 8c9f4b02-55d1-4f7e-b6a9-70d3e2c1a9d4
    :setup: Standalone Instance
    :steps:
        1. Enable memberOf with deferred updates and only an exclude subtree
        2. Create a group inside the excluded subtree, a control group in
           scope and a user outside the excluded subtree
        3. Add the user to the excluded group, then to the control group
        4. Wait for the control group's memberOf, then check the user
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. memberOf contains the control group but not the excluded group
    """
    inst = topo.standalone
    configure_memberof(inst, exclude=EXCLUDED_SUBTREE)

    containers = nsContainers(inst, DEFAULT_SUFFIX)
    if not containers.exists('excluded'):
        containers.create(properties={'cn': 'excluded'})
    user = UserAccounts(inst, DEFAULT_SUFFIX).create_test_user(uid=1002)
    excluded_group = Groups(inst, EXCLUDED_SUBTREE, rdn=None).create(properties={
        'cn': 'deferred_excluded_group',
        'description': 'group inside the excluded subtree',
    })
    control_group = Groups(inst, DEFAULT_SUFFIX).create(properties={
        'cn': 'deferred_control_group',
        'description': 'control group inside the scope',
    })
    excluded_group.add_member(user.dn)
    control_group.add_member(user.dn)

    # The deferred list is FIFO: once the control group's update is applied,
    # the excluded group's update has already been processed
    assert wait_for_memberof(user, control_group.dn), \
        'memberOf was not applied for the in-scope control group'
    values = {v.lower() for v in user.get_attr_vals_utf8('memberOf')}
    assert excluded_group.dn.lower() not in values, \
        'memberOf was applied for a group inside an excluded subtree'


if __name__ == '__main__':
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
