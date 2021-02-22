# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2018 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ----


import os, shutil, time, pytest, re, pwd, grp
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_m4 as topo_m4
from lib389.replica import *
from lib389.idm.user import UserAccounts
from lib389.agreement import *
from contextlib import suppress

pytestmark = pytest.mark.tier2

@pytest.fixture(scope="function")
def _cleanupentris(request, topo_m4):
    users = UserAccounts(topo_m4.ms["master1"], DEFAULT_SUFFIX)
    for i in range(10): users.create_test_user(uid=i)

    def fin():
        try:
            for i in users.list():
                i.delete()
        except: pass
    request.addfinalizer(fin)


def test_verify_trees(topo_m4):
    """All 4 masters should have consistent data

    :id: 01733ef8-e764-11e8-98f3-8c16451d917b
    :setup: 4 Instances with replication
    :steps:
        1. All 4 masters should have consistent data now
    :expected results:
        1. Should success
    """
    # all 4 masters should have consistent data now
    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.test_replication(
        topo_m4.ms["master1"], topo_m4.ms["master2"], 30
    )
    repl.test_replication(
        topo_m4.ms["master1"], topo_m4.ms["master3"], 30
    )
    repl.test_replication(
        topo_m4.ms["master1"], topo_m4.ms["master4"], 30
    )


def test_sync_through_to_all_4_masters(topo_m4, _cleanupentris):
    """Insert fresh data into Master 2 - about 10 entries

    :id: 10917e04-e764-11e8-8367-8c16451d917b
    :setup: 4 Instances with replication
    :steps:
        1. Insert fresh data into M2 - about 100 entries
        2. Begin verification process
    :expected results:
        1. Should success
        2. Should success
    """
    # Insert fresh data into M2 - about 100 entries
    # Wait for a minute for data to sync through to all 4 masters
    # Begin verification process
    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.test_replication(
        topo_m4.ms["master1"], topo_m4.ms["master2"], 30
    )
    repl.test_replication(
        topo_m4.ms["master1"], topo_m4.ms["master3"], 30
    )
    repl.test_replication(
        topo_m4.ms["master1"], topo_m4.ms["master4"], 30
    )


def test_modify_some_data_in_m3(topo_m4):
    """Modify some data in Master 3 , check trees on all 4 masters

    :id: 33583ff4-e764-11e8-8491-8c16451d917b
    :setup: 4 Instances with replication
    :steps:
        1. Modify some data in M3 , wait for 20 seconds ,check trees on all 4 masters
    :expected results:
        1. Should success
    """
    # modify some data in M3
    # wait for 20 seconds
    # check trees on all 4 masters
    users = UserAccounts(topo_m4.ms["master3"], DEFAULT_SUFFIX)
    repl = ReplicationManager(DEFAULT_SUFFIX)
    for i in range(15, 20):
        users.create_test_user(uid=i)
        time.sleep(1)
    for i in range(15, 20):users.list()[19-i].set("description", "description for user{} CHANGED".format(i))
    repl.test_replication(
        topo_m4.ms["master3"], topo_m4.ms["master1"], 30
    )
    repl.test_replication(
        topo_m4.ms["master3"], topo_m4.ms["master2"], 30
    )
    repl.test_replication(
        topo_m4.ms["master3"], topo_m4.ms["master4"], 30
    )
    for i in users.list():
        i.delete()


def test_delete_a_few_entries_in_m4(topo_m4, _cleanupentris):
    """Delete a few entries in Master 4 , verify trees.

    :id: 6ea94d78-e764-11e8-987f-8c16451d917b
    :setup: 4 Instances with replication
    :steps:
        1. Delete a few entries in M4 ,
        2. Wait for 60 seconds for them to propagate,
        3. Verify trees
    :expected results:
        1. Should success
        2. Should success
        3. Should success
    """
    # delete a few entries in M4
    # wait for 60 seconds for them to propagate
    # verify trees
    users = UserAccounts(topo_m4.ms["master1"], DEFAULT_SUFFIX)
    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.wait_for_replication(topo_m4.ms["master4"], topo_m4.ms["master1"])
    for i in users.list():
        i.delete()
    repl.test_replication(
        topo_m4.ms["master4"], topo_m4.ms["master1"], 30
    )
    repl.test_replication(
        topo_m4.ms["master4"], topo_m4.ms["master2"], 30
    )
    repl.test_replication(
        topo_m4.ms["master4"], topo_m4.ms["master3"], 30
    )


def test_replicated_multivalued_entries(topo_m4):
    """Replicated multivalued entries are ordered the same way on all consumers

    :id: 7bf9a34c-e764-11e8-928c-8c16451d917b
    :setup: 4 Instances with replication
    :steps:
        1. Replicated multivalued entries are ordered the same way on all consumers
    :expected results:
        1. Should success
    """
    # This test case checks that replicated multivalued entries are
    # ordered the same way on all consumers
    users = UserAccounts(topo_m4.ms["master1"], DEFAULT_SUFFIX)
    repl = ReplicationManager(DEFAULT_SUFFIX)
    user_properties = {
        "uid": "test_replicated_multivalued_entries",
        "cn": "test_replicated_multivalued_entries",
        "sn": "test_replicated_multivalued_entries",
        "userpassword": "test_replicated_multivalued_entries",
        "uidNumber": "1001",
        "gidNumber": "2002",
        "homeDirectory": "/home/{}".format("test_replicated_multivalued_entries"),
    }
    users.create(properties=user_properties)
    testuser = users.get("test_replicated_multivalued_entries")
    testuser.set("mail", ["test1", "test2", "test3"])
    # Now we check the entry on each consumer, making sure the order of the
    # multi-valued mail attribute is the same on all server instances
    repl.wait_for_replication(topo_m4.ms["master4"], topo_m4.ms["master1"])
    assert topo_m4.ms["master1"].search_s("uid=test_replicated_multivalued_entries,ou=People,dc=example,dc=com",
                                          ldap.SCOPE_SUBTREE, '(objectclass=*)', ['mail']) == topo_m4.ms[
               "master2"].search_s("uid=test_replicated_multivalued_entries,ou=People,dc=example,dc=com",
                                   ldap.SCOPE_SUBTREE, '(objectclass=*)', ['mail']) == topo_m4.ms["master3"].search_s(
        "uid=test_replicated_multivalued_entries,ou=People,dc=example,dc=com", ldap.SCOPE_SUBTREE, '(objectclass=*)',
        ['mail']) == topo_m4.ms["master4"].search_s(
        "uid=test_replicated_multivalued_entries,ou=People,dc=example,dc=com", ldap.SCOPE_SUBTREE, '(objectclass=*)',
        ['mail'])


@pytest.mark.bz157377
def test_bad_replication_agreement(topo_m4):
    """Create the bad replication agreement and try to add it

    :id: 9cf3daf4-e764-11e8-a132-8c16451d917b
    :setup: 4 Instances with replication
    :steps:
        1. Create the bad replication agreement and try to add it
    :expected results:
        1. Should not success
    """
    # The return code for adding a bad replication agreement to the directory server is now
    # correct. Check that the return code is zero ( unsuccessful ). What used to happen is
    # the directory server would not give the correct non-zero return code, add the bad replication
    # agreement and then core dump.
    # Stop the server and backup the dse.ldif so it can be restored later
    for inst in topo_m4: inst.stop()
    for i in range(1, 5):
        if os.path.exists(
            topo_m4.ms["master{}".format(i)].confdir
            + "/dse_test_bug157377.ldif"
        ):
            os.remove(
                topo_m4.ms["master{}".format(i)].confdir
                + "/dse_test_bug157377.ldif"
            )
        shutil.copy(
            topo_m4.ms["master{}".format(i)].confdir + "/dse.ldif",
            topo_m4.ms["master{}".format(i)].confdir
            + "/dse_test_bug157377.ldif",
        )
        with suppress(PermissionError):
            os.chown('{}/dse_test_bug157377.ldif'.format(topo_m4.all_insts.get('master{}'.format(i)).confdir),
                 pwd.getpwnam('dirsrv').pw_uid, grp.getgrnam('dirsrv').gr_gid)
    for i in ["master1", "master2", "master3", "master4"]:
        topo_m4.all_insts.get(i).start()
    # Create the bad replication agreement and try to add it
    # Its a agreement as Missing replica host and port information makes for a bad agreement.
    properties = {
        "basedn": "cn=Ze_bad_agreeemnt,cn=replica,cn=dc\=example\,dc\=com,cn=mapping tree,cn=config",
        "objectclass": ["top", "nsds5replicationagreement"],
        "cn": "Ze_bad_agreement",
        "nsds5replicabinddn": "{},{}".format("cn=replication manager", "o=fr"),
        "nsds5replicabindmethod": "SIMPLE",
        "nsds5replicaroot": DEFAULT_SUFFIX,
        "description": "Ze_bad_agreement",
        "nsds5replicacredentials": "Secret123",
    }
    for i in ["master1", "master2", "master3", "master4"]:
        with pytest.raises(ldap.UNWILLING_TO_PERFORM):
            Agreement(topo_m4.all_insts.get("{}".format(i))).create(
                properties=properties
            )
    for inst in topo_m4: inst.stop()
    # Now retore the original dse.ldif
    for i in range(1, 5):
        shutil.copy(
            topo_m4.ms["master{}".format(i)].confdir
            + "/dse_test_bug157377.ldif",
            topo_m4.ms["master{}".format(i)].confdir + "/dse.ldif",
        )
        with suppress(PermissionError):
            os.chown('{}/dse_test_bug157377.ldif'.format(topo_m4.all_insts.get('master{}'.format(i)).confdir),
                 pwd.getpwnam('dirsrv').pw_uid, grp.getgrnam('dirsrv').gr_gid)
    for inst in topo_m4: inst.start()


@pytest.mark.bz834074
def test_nsds5replicaenabled_verify(topo_m4):
    """Add the attribute nsds5ReplicaEnabled to cn=config

    :id: ba6dd634-e764-11e8-b158-8c16451d917b
    :setup: 4 Instances with replication
    :steps:
        1. Add the attribute nsds5ReplicaEnabled to cn=config
        2. Add data
        3. Delete data
        4. Very the replication
    :expected results:
        1. Should  success
        2. Should  success
        3. Should  success
        4. Should  success
    """
    # Add the attribute nsds5ReplicaEnabled to cn=config
    # Stop M3 and M4 instances, as not required for this test
    repl = ReplicationManager(DEFAULT_SUFFIX)
    for i in ["master3", "master4"]:
        topo_m4.all_insts.get(i).stop()
    # Adding nsds5ReplicaEnabled to M1
    topo_m4.ms["master1"].modify_s(
        topo_m4.ms["master1"].agreement.list(suffix=DEFAULT_SUFFIX)[0].dn,
        [(ldap.MOD_ADD, "nsds5ReplicaEnabled", b"on")],
    )
    topo_m4.ms["master1"].modify_s(
        topo_m4.ms["master1"].agreement.list(suffix=DEFAULT_SUFFIX)[0].dn,
        [(ldap.MOD_REPLACE, "nsds5ReplicaEnabled", b"off")],
    )
    # Adding data to Master1
    users = UserAccounts(topo_m4.ms["master1"], DEFAULT_SUFFIX)
    user_properties = {
        "uid": "test_bug834074",
        "cn": "test_bug834074",
        "sn": "test_bug834074",
        "userpassword": "test_bug834074",
        "uidNumber": "1000",
        "gidNumber": "2000",
        "homeDirectory": "/home/{}".format("test_bug834074"),
    }
    users.create(properties=user_properties)
    test_user_very = users.get("test_bug834074").dn
    # No replication no data in Master2
    with pytest.raises(Exception):
        repl.wait_for_replication(topo_m4.ms["master1"], topo_m4.ms["master2"])
    # Replication on
    topo_m4.ms["master1"].modify_s(
        topo_m4.ms["master1"].agreement.list(suffix=DEFAULT_SUFFIX)[0].dn,
        [(ldap.MOD_REPLACE, "nsds5ReplicaEnabled", b"on")],
    )
    repl.wait_for_replication(topo_m4.ms["master1"], topo_m4.ms["master2"])
    # Now data is available on master2
    assert len(topo_m4.ms['master2'].search_s(test_user_very, ldap.SCOPE_SUBTREE, 'objectclass=*')) == 1
    ## Stop replication to master2
    topo_m4.ms["master1"].modify_s(
        topo_m4.ms["master1"].agreement.list(suffix=DEFAULT_SUFFIX)[0].dn,
        [(ldap.MOD_REPLACE, "nsds5ReplicaEnabled", b"off")],
    )
    # Modify some data in master1
    topo_m4.ms["master1"].modrdn_s(test_user_very, 'uid=test_bug834075', 1)
    with pytest.raises(Exception):
        repl.wait_for_replication(topo_m4.ms["master1"], topo_m4.ms["master2"])
    # changes are not replicated in master2
        with pytest.raises(Exception): topo_m4.ms['master2'].search_s(
            'uid=test_bug834075,ou=People,{}'.format(DEFAULT_SUFFIX), ldap.SCOPE_SUBTREE, 'objectclass=*')
    # Turn on the replication
    topo_m4.ms["master1"].modify_s(
        topo_m4.ms["master1"].agreement.list(suffix=DEFAULT_SUFFIX)[0].dn,
        [(ldap.MOD_REPLACE, "nsds5ReplicaEnabled", b"on")],
    )
    repl.wait_for_replication(topo_m4.ms["master1"], topo_m4.ms["master2"])
    # Now same data is available in master2
    assert len(
        topo_m4.ms['master2'].search_s('uid=test_bug834075,ou=People,{}'.format(DEFAULT_SUFFIX), ldap.SCOPE_SUBTREE,
                                       'objectclass=*')) == 1
    # Turn off the replication from master1 to master2
    topo_m4.ms["master1"].modify_s(
        topo_m4.ms["master1"].agreement.list(suffix=DEFAULT_SUFFIX)[0].dn,
        [(ldap.MOD_REPLACE, "nsds5ReplicaEnabled", b"off")],
    )
    # delete some data in master1
    topo_m4.ms["master1"].delete_s(
        'uid=test_bug834075,ou=People,{}'.format(DEFAULT_SUFFIX)
    )
    with pytest.raises(Exception):
        repl.wait_for_replication(topo_m4.ms["master1"], topo_m4.ms["master2"])
    # deleted data from master1 is still there in master2 as repliaction is off
        assert len(
            topo_m4.ms['master2'].search_s('uid=test_bug834075,ou=People,{}'.format(DEFAULT_SUFFIX), ldap.SCOPE_SUBTREE,
                                           'objectclass=*')) == 1
    topo_m4.ms["master1"].modify_s(
        topo_m4.ms["master1"].agreement.list(suffix=DEFAULT_SUFFIX)[0].dn,
        [(ldap.MOD_REPLACE, "nsds5ReplicaEnabled", b"on")],
    )
    repl.wait_for_replication(topo_m4.ms["master1"], topo_m4.ms["master2"])
    # After repliction is on same is gone from master2 also.
    with pytest.raises(ldap.NO_SUCH_OBJECT):
        topo_m4.ms['master2'].search_s('uid=test_bug834075,ou=People,{}'.format(DEFAULT_SUFFIX), ldap.SCOPE_SUBTREE,
                                       'objectclass=*')
    with pytest.raises(ldap.OPERATIONS_ERROR):
        topo_m4.ms["master1"].modify_s(
            topo_m4.ms["master1"]
            .agreement.list(suffix=DEFAULT_SUFFIX)[0]
            .dn,
            [(ldap.MOD_REPLACE, "nsds5ReplicaEnabled", b"invalid")],
        )
    for i in ["master3", "master4"]:
        topo_m4.all_insts.get(i).start()


@pytest.mark.bz830344
def test_create_an_entry_on_the_supplier(topo_m4):
    """Shut down one instance and create an entry on the supplier

    :id: f57538d0-e764-11e8-94fc-8c16451d917b
    :setup: standalone
    :steps:
        1. Shut down one instance and create an entry on the supplier
    :expected results:
        1. Should not success
    """
    # Bug 830344: Shut down one instance and create an entry on the supplier
    topo_m4.ms["master1"].stop()
    users = UserAccounts(topo_m4.ms["master2"], DEFAULT_SUFFIX)
    users.create_test_user(uid=4)
    # ldapsearch output
    assert \
    topo_m4.ms["master2"].search_s('cn=replica,cn="dc=example,dc=com",cn=mapping tree,cn=config', ldap.SCOPE_SUBTREE,
                                   "(objectclass=*)", ["nsds5replicaLastUpdateStatus"], )[1].getValue(
        'nsds5replicalastupdatestatus')
    topo_m4.ms["master1"].start()


@pytest.mark.bz923502
def test_bob_acceptance_tests(topo_m4):
    """Run multiple modrdn_s operation on master1

    :id: 26eb87f2-e765-11e8-9698-8c16451d917b
    :setup: standalone
    :steps:
        1. Add entry
        2. Run multiple modrdn_s operation on master1
        3. Check everything is fine.
    :expected results:
        1. Should  success
        2. Should  success
        3. Should  success
    """
    # Bug description: run BOB acceptance tests...but it may be not systematic
    # Testing bug #923502: Crash in MODRDN
    users = UserAccounts(topo_m4.ms["master1"], DEFAULT_SUFFIX)
    repl = ReplicationManager(DEFAULT_SUFFIX)
    users.create_test_user()
    users.create_test_user(uid=2)
    for _ in range(100):
        topo_m4.ms["master1"].modrdn_s("uid=test_user_1000,ou=People,{}".format(DEFAULT_SUFFIX), "uid=userB", 1)
        topo_m4.ms["master1"].modrdn_s("uid=userB,ou=People,{}".format(DEFAULT_SUFFIX), "uid=test_user_1000", 1)
    repl.wait_for_replication(topo_m4.ms["master1"], topo_m4.ms["master2"])
    for i in range(100):
        topo_m4.ms["master2"].modrdn_s("uid=test_user_2,ou=People,{}".format(DEFAULT_SUFFIX), "uid=userB", 1)
        topo_m4.ms["master2"].modrdn_s("uid=userB,ou=People,{}".format(DEFAULT_SUFFIX), "uid=test_user_2", 1)
    assert topo_m4.ms["master1"].status() == True
    assert topo_m4.ms["master2"].status() == True


@pytest.mark.bz830335
def test_replica_backup_and_restore(topo_m4):
    """Test Backup and restore

    :id: 5ad1b85c-e765-11e8-9668-8c16451d917b
    :setup: standalone
    :steps:
        1. Add entries
        2. Take backup db2ldif on master1
        3. Delete entries on master1
        4. Restore entries ldif2db
        5. Check entries
    :expected results:
        1. Should success
        2. Should success
        3. Should success
        4. Should success
        5. Should success
    """
    # Testing bug #830335: Taking a replica backup and Restore on M1 after deleting few entries from M1 nad M2
    repl = ReplicationManager(DEFAULT_SUFFIX)
    users = UserAccounts(topo_m4.ms["master3"], DEFAULT_SUFFIX)
    for i in range(20, 25):
        users.create_test_user(uid=i)
        time.sleep(1)
    repl.wait_for_replication(topo_m4.ms["master1"], topo_m4.ms["master2"])
    repl.test_replication(topo_m4.ms["master1"], topo_m4.ms["master2"], 30)
    topo_m4.ms["master1"].stop()
    topo_m4.ms["master1"].db2ldif(
        bename=DEFAULT_BENAME,
        suffixes=[DEFAULT_SUFFIX],
        excludeSuffixes=[],
        encrypt=False,
        repl_data=True,
        outputfile="/tmp/output_file",
    )
    topo_m4.ms["master1"].start()
    for i in users.list(): topo_m4.ms["master1"].delete_s(i.dn)
    repl.wait_for_replication(topo_m4.ms["master1"], topo_m4.ms["master2"])
    repl.test_replication(topo_m4.ms["master1"], topo_m4.ms["master2"], 30)
    topo_m4.ms["master1"].stop()
    topo_m4.ms["master1"].ldif2db(
        bename=None,
        excludeSuffixes=None,
        encrypt=False,
        suffixes=[DEFAULT_SUFFIX],
        import_file="/tmp/output_file",
    )
    topo_m4.ms["master1"].start()
    for i in range(20, 25):
        users.create_test_user(uid=i)
        time.sleep(1)
    repl.wait_for_replication(topo_m4.ms["master1"], topo_m4.ms["master2"])
    repl.test_replication(topo_m4.ms["master1"], topo_m4.ms["master2"], 30)


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
