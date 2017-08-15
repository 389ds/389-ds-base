# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016-2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import pytest

from lib389.topologies import topology_st
from lib389.plugins import ReferentialIntegrityPlugin
from lib389.tests.plugins.utils import (
        create_test_user, create_test_group, delete_objects)


@pytest.fixture(scope="module")
def plugin(request):
    return ReferentialIntegrityPlugin(topology_st(request).standalone)


def test_referint_enable_disable(plugin):
    """
    Test that the plugin doesn't do anything while disabled, and functions
    properly when enabled.

    NOTICE: This test case leaves the plugin enabled for the following tests.
    """
    # assert plugin is disabled (by default)
    assert plugin.status() == False

    user1 = create_test_user(plugin._instance)
    group = create_test_group(plugin._instance)
    group.add_member(user1.dn)

    user1.delete()
    # assert that user was not removed from group because the plugin is disabled
    assert group.present(attr='member', value=user1.dn)

    # enable the plugin and restart the server for the action to take effect
    plugin.enable()
    plugin._instance.restart()
    assert plugin.status() == True

    user2 = create_test_user(plugin._instance)
    group.add_member(user2.dn)

    user2.delete()
    # assert that user was removed from the group as well
    assert not group.present(attr='member', value=user2.dn)

    # clean up for subsequent test cases
    delete_objects([group])

def test_membership_attr(plugin):
    """
    Test that the plugin performs integrity updates based on the attributes
    defined by referint-membership-attr.
    """
    # remove a membership attribute
    plugin.remove_membership_attr('uniquemember')

    user1 = create_test_user(plugin._instance)
    group = create_test_group(plugin._instance, unique_group=True)
    group.add_member(user1.dn)

    user1.delete()
    # assert that the user was not removed from the group
    assert group.present(attr='uniquemember', value=user1.dn)

    # now put this membership attribute back and try again
    plugin.add_membership_attr('uniquemember')

    user2 = create_test_user(plugin._instance)
    group.add_member(user2.dn)

    user2.delete()
    # assert that user was removed from the group as well
    assert not group.present(attr='uniquemember', value=user2.dn)

    # clean up for subsequent test cases
    delete_objects([group])
