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
from lib389.plugins import USNPlugin
from lib389.rootdse import RootDSE
from lib389._mapped_object import DSLdapObjects
from lib389.backend import Backend
from lib389.tests.plugins.utils import create_test_user, create_test_ou, delete_objects
from lib389._constants import DEFAULT_SUFFIX, DN_LDBM
from lib389.properties import BACKEND_NAME, BACKEND_SUFFIX


def reset_USN(plugin, objects):
    """
    Delete objects containing an entryusn attr, clean USN tombstone entries
    and restart the instance. This makes USN to reset lastusn back to -1.
    """
    delete_objects(objects)
    task = plugin.cleanup(suffix=DEFAULT_SUFFIX)
    task.wait()
    plugin._instance.restart()


@pytest.fixture(scope="module")
def plugin(request):
    topology = topology_st(request)
    plugin = USNPlugin(topology.standalone)
    return plugin


def test_usn_enable_disable(plugin):
    """
    Test that the plugin doesn't do anything while disabled, but stores
    entryusn values properly when enabled.

    NOTICE: This test case leaves the plugin enabled for the following tests.
    """
    # assert plugin is disabled (by default)
    assert plugin.status() == False

    root_dse = RootDSE(plugin._instance)
    # create a function for returning lastusn every time is called
    lastusn = lambda : root_dse.get_attr_val_int("lastusn;userroot")

    user1 = create_test_user(plugin._instance)

    # assert no entryusn,lastusn values were created while the plugin was disabled
    assert not "entryusn" in user1.get_all_attrs()
    assert not "lastusn;userroot" in root_dse.get_all_attrs()

    # enable the plugin and restart the server for the action to take effect
    plugin.enable()
    plugin._instance.restart()
    assert plugin.status() == True

    assert lastusn() == -1
    user2 = create_test_user(plugin._instance)
    # assert that a new entry now contains the entryusn value
    assert user2.get_attr_val_int("entryusn") == lastusn() == 0

    # assert that USNs are properly assigned after any write operation
    # write operations include add, modify, modrdn and delete operations
    user3 = create_test_user(plugin._instance)
    assert user3.get_attr_val_int("entryusn") == lastusn() == 1
    user2.delete()
    assert lastusn() == 2
    user3.replace('sn', 'another surname')
    assert user3.get_attr_val_int("entryusn") == lastusn() == 3

    # reset USN for subsequent test cases
    reset_USN(plugin, [user1, user3])

def test_usn_local_mode(plugin):
    """
    Test that when USN is operating in local mode, each backend has an instance
    of the USN Plug-in with a USN counter specific to that backend database.
    """
    # assert local mode
    assert not plugin.is_global_mode_set()

    ou_value = "People2"
    ou_suffix = DEFAULT_SUFFIX

    # create a new backend
    plugin._instance.backends.create(
        None, properties={
            BACKEND_NAME: "People2Data",
            BACKEND_SUFFIX: "ou=" + ou_value + "," + ou_suffix,
    })

    # create a new sub-suffix stored in the new backend
    ou2 = create_test_ou(plugin._instance, ou=ou_value, suffix=ou_suffix)

    root_dse = RootDSE(plugin._instance)
    lastusn_b1 = lambda : root_dse.get_attr_val_int("lastusn;userroot")
    lastusn_b2 = lambda : root_dse.get_attr_val_int("lastusn;people2data")

    assert lastusn_b1() == lastusn_b2() == -1

    # add a user in the default backend; trigger the plugin
    user_b1 = create_test_user(plugin._instance)
    user_b1.replace('sn', 'surname2')
    user_b1.replace('sn', 'surname3')

    # add a user in the new backend
    user_b2 = create_test_user(plugin._instance, suffix=ou2.dn)

    # assert the USN counter is different for each backend
    assert user_b1.get_attr_val_int("entryusn") == lastusn_b1() == 2
    assert user_b2.get_attr_val_int("entryusn") == lastusn_b2() == 0

    # reset USN for subsequent test cases
    b = Backend(plugin._instance, dn="cn=people2data," + DN_LDBM)
    reset_USN(plugin, [user_b1, user_b2, ou2, b])

def test_tombstone_cleanup(plugin):
    """
    Assert that the USN plugin removes tombstone entries when the cleanup task
    is run. Test removal for a specific backend, a specific suffix, and up to
    a given USN number.
    """
    # create a new backend and a new sub-suffix stored in the new backend
    ou_value = "People3"
    ou_suffix = DEFAULT_SUFFIX
    plugin._instance.backends.create(
        None, properties={
            BACKEND_NAME: "People3Data",
            BACKEND_SUFFIX: "ou=" + ou_value + "," + ou_suffix,
    })

    ou2 = create_test_ou(plugin._instance, ou=ou_value, suffix=ou_suffix)

    tombstones_b1 = DSLdapObjects(plugin._instance)
    tombstones_b1._basedn = "ou=People," + DEFAULT_SUFFIX
    tombstones_b1._objectclasses = ['nsTombstone']

    tombstones_b2 = DSLdapObjects(plugin._instance)
    tombstones_b2._basedn = ou2.dn
    tombstones_b2._objectclasses = ['nsTombstone']

    root_dse = RootDSE(plugin._instance)
    lastusn_b1 = lambda : root_dse.get_attr_val_int("lastusn;userroot")
    assert lastusn_b1() == -1

    user1_b1 = create_test_user(plugin._instance)
    user2_b1 = create_test_user(plugin._instance)
    user3_b1 = create_test_user(plugin._instance)

    user1_b2 = create_test_user(plugin._instance, suffix=ou2.dn)
    user2_b2 = create_test_user(plugin._instance, suffix=ou2.dn)

    # assert no tombstones exist at this point
    assert not tombstones_b1.list()
    assert not tombstones_b2.list()

    # create 3 tombstone entries on default backend
    user1_b1.delete()
    user2_b1.delete()
    user3_b1.delete()

    # assert there are 3 tombstone entries indeed on default backend
    assert len(tombstones_b1.list()) == 3
    assert not tombstones_b2.list()

    assert lastusn_b1() == 5

    # remove all tombstone entries from default backend, with a USN value up to 4
    task = plugin.cleanup(suffix=DEFAULT_SUFFIX, max_usn=lastusn_b1()-1)
    task.wait()

    # assert all tombstone entries were deleted but the last one on default backend
    assert len(tombstones_b1.list()) == 1
    assert not tombstones_b2.list()

    # create 2 tombstone entries on new backend
    user1_b2.delete()
    user2_b2.delete()

    # assert there are 2 tombstone entries indeed on new backend
    assert len(tombstones_b2.list()) == 2
    assert len(tombstones_b1.list()) == 1

    # remove all tombstone entries from ou2 suffix
    task = plugin.cleanup(suffix=ou2.dn)
    task.wait()

    # assert there are no tombstone entries stored on ou2 suffix
    assert not tombstones_b2.list()
    assert len(tombstones_b1.list()) == 1

    # reset USN for subsequent test cases
    b = Backend(plugin._instance, dn="cn=people3data," + DN_LDBM)
    reset_USN(plugin, [ou2, b])

def test_usn_global_mode(plugin):
    """
    Test that when USN is operating in global mode, there is a global instance
    of the USN Plug-in with a global USN counter that applies to changes made
    to the entire directory (all backends).
    """
    plugin.enable_global_mode()
    plugin._instance.restart()
    assert plugin.is_global_mode_set()

    # create a new backend and a new sub-suffix stored in the new backend
    ou_value = "People4"
    ou_suffix = DEFAULT_SUFFIX
    plugin._instance.backends.create(
        None, properties={
            BACKEND_NAME: "People4Data",
            BACKEND_SUFFIX: "ou=" + ou_value + "," + ou_suffix,
    })

    root_dse = RootDSE(plugin._instance)
    assert "lastusn" in root_dse.get_all_attrs()

    ou2 = create_test_ou(plugin._instance, ou=ou_value, suffix=ou_suffix)
    assert ou2.get_attr_val_int("entryusn") == 0

    # add a user in the default backend
    user_b1 = create_test_user(plugin._instance)
    # add a user in the new backend
    user_b2 = create_test_user(plugin._instance, suffix=ou2.dn)

    # assert that a global USN counter is used for all backends
    assert user_b1.get_attr_val_int("entryusn") == 1
    assert user_b2.get_attr_val_int("entryusn") == 2

    # reset USN for subsequent test cases
    b = Backend(plugin._instance, dn="cn=people4data," + DN_LDBM)
    reset_USN(plugin, [user_b1, user_b2, ou2, b])
