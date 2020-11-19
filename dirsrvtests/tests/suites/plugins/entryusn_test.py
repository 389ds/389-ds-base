# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import ldap
import logging
import pytest
import time
from lib389._constants import DEFAULT_SUFFIX
from lib389.config import Config
from lib389.plugins import USNPlugin, MemberOfPlugin
from lib389.idm.group import Groups
from lib389.idm.user import UserAccounts
from lib389.idm.organizationalunit import OrganizationalUnit
from lib389.tombstone import Tombstones
from lib389.rootdse import RootDSE
from lib389.topologies import topology_st, topology_m2

log = logging.getLogger(__name__)

USER_NUM = 10
GROUP_NUM = 3


def check_entryusn_no_duplicates(entryusn_list):
    """Check that all values in the list are unique"""

    if len(entryusn_list) > len(set(entryusn_list)):
        raise AssertionError(f"EntryUSN values have duplicates, please, check logs")


def check_lastusn_after_restart(inst):
    """Check that last usn is the same after restart"""

    root_dse = RootDSE(inst)
    last_usn_before = root_dse.get_attr_val_int("lastusn;userroot")
    inst.restart()
    last_usn_after = root_dse.get_attr_val_int("lastusn;userroot")
    assert last_usn_after == last_usn_before


@pytest.fixture(scope="module")
def setup(topology_st, request):
    """
    Enable USN plug-in
    Enable MEMBEROF plugin
    Add test entries
    """

    inst = topology_st.standalone

    log.info("Enable the USN plugin...")
    plugin = USNPlugin(inst)
    plugin.enable()

    log.info("Enable the MEMBEROF plugin...")
    plugin = MemberOfPlugin(inst)
    plugin.enable()

    inst.restart()

    users_list = []
    log.info("Adding test entries...")
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    for id in range(USER_NUM):
        user = users.create_test_user(uid=id)
        users_list.append(user)

    groups_list = []
    log.info("Adding test groups...")
    groups = Groups(inst, DEFAULT_SUFFIX)
    for id in range(GROUP_NUM):
        group = groups.create(properties={'cn': f'test_group{id}'})
        groups_list.append(group)

    def fin():
        for user in users_list:
            try:
                user.delete()
            except ldap.NO_SUCH_OBJECT:
                pass
        for group in groups_list:
            try:
                group.delete()
            except ldap.NO_SUCH_OBJECT:
                pass
    request.addfinalizer(fin)

    return {"users": users_list,
            "groups": groups_list}


def test_entryusn_no_duplicates(topology_st, setup):
    """Verify that entryUSN is not duplicated after memberOf operation

    :id: 1a7d382d-1214-4d56-b9c2-9c4ed57d1683
    :setup: Standalone instance, Groups and Users, USN and memberOf are enabled
    :steps:
        1. Add a member to group 1
        2. Add a member to group 1 and 2
        3. Check that entryUSNs are different
        4. Check that lastusn before and after a restart are the same
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
    """

    inst = topology_st.standalone
    config = Config(inst)
    config.replace('nsslapd-accesslog-level', '260')  # Internal op
    config.replace('nsslapd-errorlog-level', '65536')
    config.replace('nsslapd-plugin-logging', 'on')
    entryusn_list = []

    users = setup["users"]
    groups = setup["groups"]

    groups[0].replace('member', users[0].dn)
    entryusn_list.append(users[0].get_attr_val_int('entryusn'))
    log.info(f"{users[0].dn}_1: {entryusn_list[-1:]}")
    entryusn_list.append(groups[0].get_attr_val_int('entryusn'))
    log.info(f"{groups[0].dn}_1: {entryusn_list[-1:]}")
    check_entryusn_no_duplicates(entryusn_list)

    groups[1].replace('member', [users[0].dn, users[1].dn])
    entryusn_list.append(users[0].get_attr_val_int('entryusn'))
    log.info(f"{users[0].dn}_2: {entryusn_list[-1:]}")
    entryusn_list.append(users[1].get_attr_val_int('entryusn'))
    log.info(f"{users[1].dn}_2: {entryusn_list[-1:]}")
    entryusn_list.append(groups[1].get_attr_val_int('entryusn'))
    log.info(f"{groups[1].dn}_2: {entryusn_list[-1:]}")
    check_entryusn_no_duplicates(entryusn_list)

    check_lastusn_after_restart(inst)


def test_entryusn_is_same_after_failure(topology_st, setup):
    """Verify that entryUSN is the same after failed operation

    :id: 1f227533-370a-48c1-b920-9b3b0bcfc32e
    :setup: Standalone instance, Groups and Users, USN and memberOf are enabled
    :steps:
        1. Get current group's entryUSN value
        2. Try to modify the group with an invalid syntax
        3. Get new group's entryUSN value and compare with old
        4. Check that lastusn before and after a restart are the same
    :expectedresults:
        1. Success
        2. Invalid Syntax error
        3. Should be the same
        4. Success
    """

    inst = topology_st.standalone
    users = setup["users"]

    # We need this update so we get the latest USN pointed to our entry
    users[0].replace('description', 'update')

    entryusn_before = users[0].get_attr_val_int('entryusn')
    users[0].replace('description', 'update')
    try:
        users[0].replace('uid', 'invalid update')
    except ldap.NOT_ALLOWED_ON_RDN:
        pass
    users[0].replace('description', 'second update')
    entryusn_after = users[0].get_attr_val_int('entryusn')

    # entryUSN should be OLD + 2 (only two user updates)
    assert entryusn_after == (entryusn_before + 2)

    check_lastusn_after_restart(inst)


def test_entryusn_after_repl_delete(topology_m2):
    """Verify that entryUSN is incremented on 1 after delete operation which creates a tombstone

    :id: 1704cf65-41bc-4347-bdaf-20fc2431b218
    :setup: An instance with replication, Users, USN enabled
    :steps:
        1. Try to delete a user
        2. Check the tombstone has the incremented USN
        3. Try to delete ou=People with users
        4. Check the entry has a not incremented entryUSN
    :expectedresults:
        1. Success
        2. Success
        3. Should fail with Not Allowed On Non-leaf error
        4. Success
    """

    inst = topology_m2.ms["master1"]
    plugin = USNPlugin(inst)
    plugin.enable()
    inst.restart()
    users = UserAccounts(inst, DEFAULT_SUFFIX)

    try:
        user_1 = users.create_test_user()
        user_rdn = user_1.rdn
        tombstones = Tombstones(inst, DEFAULT_SUFFIX)

        user_1.replace('description', 'update_ts')
        user_usn = user_1.get_attr_val_int('entryusn')

        user_1.delete()
        time.sleep(1)  # Gives a little time for tombstone creation to complete

        ts = tombstones.get(user_rdn)
        ts_usn = ts.get_attr_val_int('entryusn')

        assert (user_usn + 1) == ts_usn

        user_1 = users.create_test_user()
        org = OrganizationalUnit(inst, f"ou=People,{DEFAULT_SUFFIX}")
        org.replace('description', 'update_ts')
        ou_usn_before = org.get_attr_val_int('entryusn')
        try:
            org.delete()
        except ldap.NOT_ALLOWED_ON_NONLEAF:
            pass
        ou_usn_after = org.get_attr_val_int('entryusn')
        assert ou_usn_before == ou_usn_after

    finally:
        try:
            user_1.delete()
        except ldap.NO_SUCH_OBJECT:
            pass


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
