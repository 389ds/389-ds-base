# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_m2 as topo_m2
from lib389.topologies import topology_m1c1 as m1c1
from lib389.topologies import topology_st as topo
from lib389.idm.user import UserAccount, UserAccounts
from lib389.plugins import USNPlugin
from lib389.replica import ReplicationManager
from lib389.tombstone import Tombstones
from lib389.agreement import Agreements
from lib389._constants import *


pytestmark = pytest.mark.tier1


@pytest.fixture(scope="function")
def _delete_after(request, topo_m2):
    def last():
        m1 = topo_m2.ms["supplier1"]
        if UserAccounts(m1, DEFAULT_SUFFIX, rdn=None).list():
            for user in UserAccounts(m1, DEFAULT_SUFFIX, rdn=None).list():
                user.delete()

    request.addfinalizer(last)


@pytest.mark.bz830337
def test_deletions_are_not_replicated(topo_m2):
    """usn + mmr = deletions are not replicated

    :id: aa4f67ce-a64c-11ea-a6fd-8c16451d917b
    :setup: MMR with 2 suppliers
    :steps:
        1. Enable USN plugin on both servers
        2. Enable USN plugin on Supplier 2
        3. Add user
        4. Check that user propagated to Supplier 2
        5. Check user`s USN on Supplier 1
        6. Check user`s USN on Supplier 2
        7. Delete user
        8. Check that deletion of user propagated to Supplier 1
    :expected results:
        1. Should succeeds
        2. Should succeeds
        3. Should succeeds
        4. Should succeeds
        5. Should succeeds
        6. Should succeeds
        7. Should succeeds
        8. Should succeeds
    """
    m1 = topo_m2.ms["supplier1"]
    m2 = topo_m2.ms["supplier2"]
    # Enable USN plugin on both servers
    usn1 = USNPlugin(m1)
    usn2 = USNPlugin(m2)
    for usn_usn in [usn1, usn2]:
        usn_usn.enable()
    for instance in [m1, m2]:
        instance.restart()
    # Add user
    user = UserAccounts(m1, DEFAULT_SUFFIX, rdn=None).create_test_user(uid=1, gid=1)
    repl_manager = ReplicationManager(DEFAULT_SUFFIX)
    repl_manager.wait_for_replication(m1, m2, timeout=100)
    # Check that user propagated to Supplier 2
    assert user.dn in [i.dn for i in UserAccounts(m2, DEFAULT_SUFFIX, rdn=None).list()]
    user2 = UserAccount(m2, f'uid=test_user_1,{DEFAULT_SUFFIX}')
    # Check user`s USN on Supplier 1
    assert user.get_attr_val_utf8('entryusn')
    # Check user`s USN on Supplier 2
    assert user2.get_attr_val_utf8('entryusn')
    # Delete user
    user2.delete()
    repl_manager.wait_for_replication(m1, m2, timeout=100)
    # Check that deletion of user propagated to Supplier 1
    with pytest.raises(ldap.NO_SUCH_OBJECT):
        user.status()


@pytest.mark.bz891866
def test_error_20(topo_m2, _delete_after):
    """DS returns error 20 when replacing values of a multi-valued attribute (only when replication is enabled)

    :id: a55bccc6-a64c-11ea-bac8-8c16451d917b
    :setup: MMR with 2 suppliers
    :steps:
        1. Add user
        2. Change multivalue attribute
    :expected results:
        1. Should succeeds
        2. Should succeeds
    """
    m1 = topo_m2.ms["supplier1"]
    m2 = topo_m2.ms["supplier2"]
    # Add user
    user = UserAccounts(m1, DEFAULT_SUFFIX, rdn=None).create_test_user(uid=1, gid=1)
    repl_manager = ReplicationManager(DEFAULT_SUFFIX)
    repl_manager.wait_for_replication(m1, m2, timeout=100)
    # Change multivalue attribute
    assert user.replace_many(('cn', 'BUG 891866'), ('cn', 'Test'))


@pytest.mark.bz1955658
def test_enable_repl_w_master(topo):
    """Check that enabling replication with the role "master" succeeds.

    :id: 074fbb38-069e-11ec-98ca-fa163ec212ff
    :customerscenario: True
    :setup: Create DS standalone instance
    :steps:
        1. Create DS standalone instance
        2. Enable replication on supplier with role='master' attribute OR Display appropriate message.
        3. Disable role created above if it was created.
        4. Re-enable replication on supplier with role='supplier' attribute

    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
    """
    _err_unknown_role = 'Error: Unknown replication role (master), you must use "supplier", "hub", or "consumer"'
    log.info("Enabling replication on supplier with role='master' attribute")
    cmd = ('dsconf -D "' + DN_DM + '" standalone1 ' + ' -w ' + PW_DM + ' replication enable --suffix="' + DEFAULT_SUFFIX +
           '" --role="master" --replica-id=1 ')
    if os.system(cmd) == 0:
        log.info("Replication role enabled successfully")
        cmd = ('dsconf -D "' + DN_DM + '" standalone1 ' + ' -w ' + PW_DM + ' replication disable --suffix="' + DEFAULT_SUFFIX+' "')
        os.system(cmd)
        log.info("Disabling replication on supplier with role='master' attribute")
        time.sleep(.5)
    elif topo.logcap.contains(_err_unknown_role):
        log.info("Replication role provided is not supported")
    log.info("Enabling replication on supplier with role='supplier' attribute")
    cmd = ('dsconf -D "' + DN_DM + '" standalone1 ' + ' -w ' + PW_DM + ' replication enable --suffix="' + DEFAULT_SUFFIX +
           '" --role="supplier" --replica-id=1 ')
    assert os.system(cmd) == 0


@pytest.mark.bz914305
def test_segfaults(topo_m2, _delete_after):
    """ns-slapd segfaults while trying to delete a tombstone entry

    :id: 9f8f7388-a64c-11ea-b5f7-8c16451d917b
    :setup: MMR with 2 suppliers
    :steps:
        1. Add new user
        2. Delete user - should leave tombstone entry
        3. Search for tombstone entry
        4. Try to delete tombstone entry
        5. Check if server is still alive
    :expected results:
        1. Should succeeds
        2. Should succeeds
        3. Should succeeds
        4. Should succeeds
        5. Should succeeds
    """
    m1 = topo_m2.ms["supplier1"]
    # Add user
    user = UserAccounts(m1, DEFAULT_SUFFIX, rdn=None).create_test_user(uid=10, gid=1)
    # Delete user - should leave tombstone entry
    user.delete()
    tombstones = Tombstones(m1, DEFAULT_SUFFIX)
    # Search for tombstone entry
    fil = tombstones.filter("(&(objectClass=nstombstone)(uid=test_user_10))")
    assert fil
    # Try to delete tombstone entry
    for user in fil:
        user.delete()
    # Check if server is still alive
    assert m1.status()


def test_adding_deleting(topo_m2, _delete_after):
    """Adding attribute with 11 values to entry

    :id: 99842b1e-a64c-11ea-b8e3-8c16451d917b
    :setup: MMR with 2 suppliers
    :steps:
        1. Adding entry
        2. Adding attribute with 11 values to entry
        3. Removing 4 values from the attribute in the entry
    :expected results:
        1. Should succeeds
        2. Should succeeds
        3. Should succeeds
    """
    m1 = topo_m2.ms["supplier1"]
    # Adding entry
    user = UserAccounts(m1, DEFAULT_SUFFIX, rdn=None).create_test_user(uid=1, gid=1)
    # Adding attribute with 11 values to entry
    for val1, val2 in [('description', 'first description'),
                       ('description', 'second description'),
                       ('description', 'third description'),
                       ('description', 'fourth description'),
                       ('description', 'fifth description'),
                       ('description', 'sixth description'),
                       ('description', 'seventh description'),
                       ('description', 'eighth description'),
                       ('description', 'nineth description'),
                       ('description', 'tenth description'),
                       ('description', 'eleventh description')]:
        user.add(val1, val2)
    # Removing 4 values from the attribute in the entry
    for val1, val2 in [('description', 'first description'),
                       ('description', 'second description'),
                       ('description', 'third description'),
                       ('description', 'fourth description')]:
        user.remove(val1, val2)


def test_deleting_twice(topo_m2):
    """Deleting entry twice crashed a server

    :id: 94045560-a64c-11ea-93d6-8c16451d917b
    :setup: MMR with 2 suppliers
    :steps:
        1. Adding entry
        2. Deleting the same entry from s1
        3. Deleting the same entry from s2 after some seconds
    :expected results:
        1. Should succeeds
        2. Should succeeds
        3. Should succeeds
    """
    m1 = topo_m2.ms["supplier1"]
    m2 = topo_m2.ms["supplier2"]
    # Adding entry
    user1 = UserAccounts(m1, DEFAULT_SUFFIX, rdn=None).create_test_user(uid=1, gid=1)
    repl_manager = ReplicationManager(DEFAULT_SUFFIX)
    repl_manager.wait_for_replication(m1, m2, timeout=100)
    user2 = UserAccount(m2, f'uid=test_user_1,{DEFAULT_SUFFIX}')
    assert user2.status()
    # Deleting the same entry from s1
    user1.delete()
    repl_manager.wait_for_replication(m1, m2, timeout=100)
    # Deleting the same entry from s2 after some seconds
    with pytest.raises(ldap.NO_SUCH_OBJECT):
        user2.delete()
    assert m1.status()
    assert m2.status()


def test_rename_entry(topo_m2, _delete_after):
    """Rename entry crashed a server

    :id: 3866f9d6-a946-11ea-a3f8-8c16451d917b
    :setup: MMR with 2 suppliers
    :steps:
        1. Adding entry
        2. Stop Agreement for both
        3. Change description
        4. Change will not reflect on other supplier
        5. Turn on agreement on both
        6. Change will reflect on other supplier
    :expected results:
        1. Should succeeds
        2. Should succeeds
        3. Should succeeds
        4. Should succeeds
        5. Should succeeds
        6. Should succeeds
    """
    m1 = topo_m2.ms["supplier1"]
    m2 = topo_m2.ms["supplier2"]
    # Adding entry
    user1 = UserAccounts(m1, DEFAULT_SUFFIX, rdn=None).create_test_user(uid=1, gid=1)
    repl_manager = ReplicationManager(DEFAULT_SUFFIX)
    repl_manager.wait_for_replication(m1, m2, timeout=100)
    user2 = UserAccount(m2, user1.dn)
    assert user2.status()
    # Stop Agreement for both
    agree1 = Agreements(m1).list()[0]
    agree2 = Agreements(m2).list()[0]
    for agree in [agree1, agree2]:
        agree.pause()
    # change description
    user1.replace('description', 'New Des')
    assert user1.get_attr_val_utf8('description')
    # Change will not reflect on other supplier
    with pytest.raises(AssertionError):
        assert user2.get_attr_val_utf8('description')
    # Turn on agreement on both
    for agree in [agree1, agree2]:
        agree.resume()
    repl_manager.wait_for_replication(m1, m2, timeout=100)
    for instance in [user1, user2]:
        assert instance.get_attr_val_utf8('description')


def test_userpassword_attribute(topo_m2, _delete_after):
    """Modifications of userpassword attribute in an MMR environment were successful
        however a error message was displayed in the error logs which was curious.

    :id: bdcf0464-a947-11ea-9f0d-8c16451d917b
    :setup: MMR with 2 suppliers
    :steps:
        1. Add the test user to S1
        2. Check that user's  has been propogated to Supplier 2
        3. modify user's userpassword attribute on supplier 2
        4. check the error logs on suppler 1 to make sure the error message is not there
    :expected results:
        1. Should succeeds
        2. Should succeeds
        3. Should succeeds
        4. Should succeeds
    """
    m1 = topo_m2.ms["supplier1"]
    m2 = topo_m2.ms["supplier2"]
    # Add the test user to S1
    user1 = UserAccounts(m1, DEFAULT_SUFFIX, rdn=None).create_test_user(uid=1, gid=1)
    repl_manager = ReplicationManager(DEFAULT_SUFFIX)
    repl_manager.wait_for_replication(m1, m2, timeout=100)
    # Check that user's  has been propogated to Supplier 2
    user2 = UserAccount(m2, user1.dn)
    assert user2.status()
    # modify user's userpassword attribute on supplier 2
    user2.replace('userpassword', 'fred1')
    repl_manager.wait_for_replication(m1, m2, timeout=100)
    assert user1.get_attr_val_utf8('userpassword')
    # check the error logs on suppler 1 to make sure the error message is not there
    assert not m1.searchErrorsLog("can\'t add a change for uid=")


def _create_and_delete_tombstone(topo_m2, id):
    m1 = topo_m2.ms["supplier1"]
    # Add new user
    user1 = UserAccounts(m1, DEFAULT_SUFFIX, rdn=None).create_test_user(uid=id, gid=id)
    # Delete user - should leave tombstone entry
    user1.delete()
    tombstones = Tombstones(m1, DEFAULT_SUFFIX)
    # Search for tombstone entry
    fil = tombstones.filter("(&(objectClass=nstombstone)(uid=test_user_{}*))".format(id))[0]
    assert fil
    fil.rename("uid=engineer")
    assert m1


def test_tombstone_modrdn(topo_m2):
    """rhds90 crash on tombstone modrdn

    :id: 846f5042-a948-11ea-ade2-8c16451d917b
    :setup: MMR with 2 suppliers
    :steps:
        1. Add new user
        2. Delete user - should leave tombstone entry
        3. Search for tombstone entry
        4. Try to modrdn with deleteoldrdn
    :expected results:
        1. Should succeeds
        2. Should succeeds
        3. Should succeeds
        4. Should succeeds
    """
    for id_id in [11, 12, 13, 14]:
        _create_and_delete_tombstone(topo_m2, id_id)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
