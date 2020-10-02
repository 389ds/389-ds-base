# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
import logging
from lib389.replica import Replicas
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_m4 as topo_m4

from lib389._constants import (BACKEND_NAME, DEFAULT_SUFFIX, LOG_REPLICA, REPLICA_RUV_FILTER,
                              ReplicaRole, REPLICATION_BIND_DN, REPLICATION_BIND_PW,
                              REPLICATION_BIND_METHOD, REPLICATION_TRANSPORT, defaultProperties,
                              RA_NAME, RA_BINDDN, RA_BINDPW, RA_METHOD, RA_TRANSPORT_PROT,
                              DN_DM, PASSWORD, LOG_DEFAULT, RA_ENABLED, RA_SCHEDULE)

TEST_ENTRY_NAME = 'mmrepl_test'
TEST_ENTRY_DN = 'uid={},{}'.format(TEST_ENTRY_NAME, DEFAULT_SUFFIX)
NEW_SUFFIX_NAME = 'test_repl'
NEW_SUFFIX = 'o={}'.format(NEW_SUFFIX_NAME)
NEW_BACKEND = 'repl_base'

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


@pytest.fixture(scope="function")
def test_entry(topo_m4, request):
    """Add test entry to master1"""

    log.info('Adding entry {}'.format(TEST_ENTRY_DN))
    try:
        topo_m4.ms["master1"].add_s(Entry((TEST_ENTRY_DN, {
            'objectclass': 'top person'.split(),
            'objectclass': 'organizationalPerson',
            'objectclass': 'inetorgperson',
            'cn': TEST_ENTRY_NAME,
            'sn': TEST_ENTRY_NAME,
            'uid': TEST_ENTRY_NAME,
            'userpassword': TEST_ENTRY_NAME
        })))
    except ldap.LDAPError as e:
        log.error('Failed to add entry (%s): error (%s)' % (TEST_ENTRY_DN,
                                                            e.message['desc']))
        raise e

    def fin():
        log.info('Deleting entry {}'.format(TEST_ENTRY_DN))
        try:
            topo_m4.ms["master1"].delete_s(TEST_ENTRY_DN)
        except ldap.NO_SUCH_OBJECT:
            log.info("Entry {} wasn't found".format(TEST_ENTRY_DN))

    request.addfinalizer(fin)


@pytest.fixture(scope="function")
def new_suffix(topo_m4, request):
    """Add a new suffix and enable a replication on it"""

    for num in range(1, 5):
        log.info('Adding suffix:{} and backend: {} to master{}'.format(NEW_SUFFIX, NEW_BACKEND, num))
        topo_m4.ms["master{}".format(num)].backend.create(NEW_SUFFIX, {BACKEND_NAME: NEW_BACKEND})
        topo_m4.ms["master{}".format(num)].mappingtree.create(NEW_SUFFIX, NEW_BACKEND)

        try:
            topo_m4.ms["master{}".format(num)].add_s(Entry((NEW_SUFFIX, {
                'objectclass': 'top',
                'objectclass': 'organization',
                'o': NEW_SUFFIX_NAME,
                'description': NEW_SUFFIX_NAME
            })))
        except ldap.LDAPError as e:
            log.error('Failed to add suffix ({}): error ({})'.format(NEW_SUFFIX, e.message['desc']))
            raise

    def fin():
        for num in range(1, 5):
            log.info('Deleting suffix:{} and backend: {} from master{}'.format(NEW_SUFFIX, NEW_BACKEND, num))
            topo_m4.ms["master{}".format(num)].mappingtree.delete(NEW_SUFFIX)
            topo_m4.ms["master{}".format(num)].backend.delete(NEW_SUFFIX)

    request.addfinalizer(fin)


def get_repl_entries(topo, entry_name, attr_list):
    """Get a list of test entries from all masters"""

    entries_list = []
    num_of_masters = len({name: inst for name, inst in topo.ms.items() if not name.endswith('agmts')})

    log.info('Wait for replication to happen')
    time.sleep(10)

    for num in range(1, num_of_masters + 1):
        entries = topo.ms['master{}'.format(num)].search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE,
                                                           "uid={}".format(entry_name), attr_list)
        entries_list += entries

    return entries_list


def test_add_entry(topo_m4, test_entry):
    """Check that entries are replicated after add operation

    :ID: 024250f1-5f7e-4f3b-a9f5-27741e6fd405
    :feature: Multi master replication
    :setup: Four masters replication setup
    :steps: 1. Add entry to master1
            2. Wait for replication to happen
            3. Check entry on all other masters
    :expectedresults: Entry should be replicated
    """

    entries = get_repl_entries(topo_m4, TEST_ENTRY_NAME, ["uid"])
    assert all(entries), "Entry {} wasn't replicated successfully".format(TEST_ENTRY_DN)


def test_modify_entry(topo_m4, test_entry):
    """Check that entries are replicated after modify operation

    :ID: 36764053-622c-43c2-a132-d7a3ab7d9aaa
    :feature: Multi master replication
    :setup: Four masters replication setup, an entry
    :steps: 1. Modify the entry on master1 (try add, modify and delete operations)
            2. Wait for replication to happen
            3. Check entry on all other masters
    :expectedresults: Entry attr should be replicated
    """

    log.info('Modifying entry {} - add operation'.format(TEST_ENTRY_DN))
    try:
        topo_m4.ms["master1"].modify_s(TEST_ENTRY_DN, [(ldap.MOD_ADD,
                                                        'mail', '{}@redhat.com'.format(TEST_ENTRY_NAME))])
    except ldap.LDAPError as e:
        log.error('Failed to modify entry (%s): error (%s)' % (TEST_ENTRY_DN,
                                                               e.message['desc']))
        raise e
    time.sleep(1)

    entries = get_repl_entries(topo_m4, TEST_ENTRY_NAME, ["mail"])
    assert all(entry["mail"] == "{}@redhat.com".format(TEST_ENTRY_NAME)
               for entry in entries), "Entry attr {} wasn't replicated successfully".format(TEST_ENTRY_DN)

    log.info('Modifying entry {} - replace operation'.format(TEST_ENTRY_DN))
    try:
        topo_m4.ms["master1"].modify_s(TEST_ENTRY_DN, [(ldap.MOD_REPLACE,
                                                        'mail', '{}@greenhat.com'.format(TEST_ENTRY_NAME))])
    except ldap.LDAPError as e:
        log.error('Failed to modify entry (%s): error (%s)' % (TEST_ENTRY_DN,
                                                               e.message['desc']))
        raise e
    time.sleep(1)

    entries = get_repl_entries(topo_m4, TEST_ENTRY_NAME, ["mail"])
    assert all(entry["mail"] == "{}@greenhat.com".format(TEST_ENTRY_NAME)
               for entry in entries), "Entry attr {} wasn't replicated successfully".format(TEST_ENTRY_DN)

    log.info('Modifying entry {} - delete operation'.format(TEST_ENTRY_DN))
    try:
        topo_m4.ms["master1"].modify_s(TEST_ENTRY_DN, [(ldap.MOD_DELETE,
                                                        'mail', '{}@greenhat.com'.format(TEST_ENTRY_NAME))])
    except ldap.LDAPError as e:
        log.error('Failed to modify entry (%s): error (%s)' % (TEST_ENTRY_DN,
                                                               e.message['desc']))
        raise e
    time.sleep(1)

    entries = get_repl_entries(topo_m4, TEST_ENTRY_NAME, ["mail"])
    assert all(not entry["mail"] for entry in entries), "Entry attr {} wasn't replicated successfully".format(
        TEST_ENTRY_DN)


def test_delete_entry(topo_m4, test_entry):
    """Check that entry deletion is replicated after delete operation

    :ID: 18437262-9d6a-4b98-a47a-6182501ab9bc
    :feature: Multi master replication
    :setup: Four masters replication setup, an entry
    :steps: 1. Delete the entry from master1
            2. Wait for replication to happen
            3. Check entry on all other masters
    :expectedresults: Entry deletion should be replicated
    """

    log.info('Deleting entry {} during the test'.format(TEST_ENTRY_DN))
    topo_m4.ms["master1"].delete_s(TEST_ENTRY_DN)

    entries = get_repl_entries(topo_m4, TEST_ENTRY_NAME, ["uid"])
    assert not entries, "Entry deletion {} wasn't replicated successfully".format(TEST_ENTRY_DN)


@pytest.mark.parametrize("delold", [0, 1])
def test_modrdn_entry(topo_m4, test_entry, delold):
    """Check that entries are replicated after modrdn operation

    :ID: 02558e6d-a745-45ae-8d88-34fe9b16adc9
    :feature: Multi master replication
    :setup: Four masters replication setup, an entry
    :steps: 1. Make modrdn operation on entry on master1 with both delold 1 and 0
            2. Wait for replication to happen
            3. Check entry on all other masters
    :expectedresults: Entry with new RDN should be replicated.
                     If delold was specified, entry with old RDN shouldn't exist
    """

    newrdn_name = 'newrdn'
    newrdn_dn = 'uid={},{}'.format(newrdn_name, DEFAULT_SUFFIX)
    log.info('Modify entry RDN {}'.format(TEST_ENTRY_DN))
    try:
        topo_m4.ms["master1"].modrdn_s(TEST_ENTRY_DN, 'uid={}'.format(newrdn_name), delold)
    except ldap.LDAPError as e:
        log.error('Failed to modrdn entry (%s): error (%s)' % (TEST_ENTRY_DN,
                                                               e.message['desc']))
        raise e

    try:
        entries_new = get_repl_entries(topo_m4, newrdn_name, ["uid"])
        assert all(entries_new), "Entry {} wasn't replicated successfully".format(newrdn_name)
        if delold == 0:
            entries_old = get_repl_entries(topo_m4, TEST_ENTRY_NAME, ["uid"])
            assert all(entries_old), "Entry with old rdn {} wasn't replicated successfully".format(TEST_ENTRY_DN)
        else:
            entries_old = get_repl_entries(topo_m4, TEST_ENTRY_NAME, ["uid"])
            assert not entries_old, "Entry with old rdn {} wasn't removed in replicas successfully".format(
                TEST_ENTRY_DN)
    finally:
        log.info('Remove entry with new RDN {}'.format(newrdn_dn))
        topo_m4.ms["master1"].delete_s(newrdn_dn)


def test_modrdn_after_pause(topo_m4):
    """Check that changes are properly replicated after replica pause

    :ID: 6271dc9c-a993-4a9e-9c6d-05650cdab282
    :feature: Multi master replication
    :setup: Four masters replication setup, an entry
    :steps: 1. Pause all replicas
            2. Make modrdn operation on entry on master1
            3. Resume all replicas
            4. Wait for replication to happen
            5. Check entry on all other masters
    :expectedresults: Entry with new RDN should be replicated.
    """

    newrdn_name = 'newrdn'
    newrdn_dn = 'uid={},{}'.format(newrdn_name, DEFAULT_SUFFIX)

    log.info('Adding entry {}'.format(TEST_ENTRY_DN))
    try:
        topo_m4.ms["master1"].add_s(Entry((TEST_ENTRY_DN, {
            'objectclass': 'top person'.split(),
            'objectclass': 'organizationalPerson',
            'objectclass': 'inetorgperson',
            'cn': TEST_ENTRY_NAME,
            'sn': TEST_ENTRY_NAME,
            'uid': TEST_ENTRY_NAME
        })))
    except ldap.LDAPError as e:
        log.error('Failed to add entry (%s): error (%s)' % (TEST_ENTRY_DN,
                                                            e.message['desc']))
        raise e

    log.info('Pause all replicas')
    topo_m4.pause_all_replicas()

    log.info('Modify entry RDN {}'.format(TEST_ENTRY_DN))
    try:
        topo_m4.ms["master1"].modrdn_s(TEST_ENTRY_DN, 'uid={}'.format(newrdn_name))
    except ldap.LDAPError as e:
        log.error('Failed to modrdn entry (%s): error (%s)' % (TEST_ENTRY_DN,
                                                               e.message['desc']))
        raise e

    log.info('Resume all replicas')
    topo_m4.resume_all_replicas()

    log.info('Wait for replication to happen')
    time.sleep(3)

    try:
        entries_new = get_repl_entries(topo_m4, newrdn_name, ["uid"])
        assert all(entries_new), "Entry {} wasn't replicated successfully".format(newrdn_name)
    finally:
        log.info('Remove entry with new RDN {}'.format(newrdn_dn))
        topo_m4.ms["master1"].delete_s(newrdn_dn)


# Bugzilla 842441
def test_modify_stripattrs(topo_m4):
    """Check that we can modify nsds5replicastripattrs

    :ID: f36abed8-e262-4f35-98aa-71ae55611aaa
    :feature: Multi master replication
    :setup: Four masters replication setup
    :steps: 1. Modify nsds5replicastripattrs attribute on any agreement
            2. Search for the modified attribute
    :expectedresults: It should be contain the value
    """

    m1 = topo_m4.ms["master1"]
    agreement = m1.agreement.list(suffix=DEFAULT_SUFFIX)[0].dn
    attr_value = 'modifiersname modifytimestamp'

    log.info('Modify nsds5replicastripattrs with {}'.format(attr_value))
    m1.modify_s(agreement, [(ldap.MOD_REPLACE, 'nsds5replicastripattrs', attr_value)])

    log.info('Check nsds5replicastripattrs for {}'.format(attr_value))
    entries = m1.search_s(agreement, ldap.SCOPE_BASE, "objectclass=*", ['nsds5replicastripattrs'])
    assert attr_value in entries[0].data['nsds5replicastripattrs']


def test_new_suffix(topo_m4, new_suffix):
    """Check that we can enable replication on a new suffix

    :ID: d44a9ed4-26b0-4189-b0d0-b2b336ddccbd
    :feature: Multi master replication
    :setup: Four masters replication setup, new suffix
    :steps: 1. Enable replication on the new suffix
            2. Check if it works
            3. Disable replication on the new suffix
    :expectedresults: Replication works on the new suffix
    """

    m1 = topo_m4.ms["master1"]
    m2 = topo_m4.ms["master2"]
    log.info('Enable replication for new suffix {} on two masters'.format(NEW_SUFFIX))
    m1.replica.enableReplication(NEW_SUFFIX, ReplicaRole.MASTER, 101)
    m2.replica.enableReplication(NEW_SUFFIX, ReplicaRole.MASTER, 102)

    log.info("Creating agreement from master1 to master2")
    properties = {RA_NAME: 'newMeTo_{}:{}'.format(m2.host, str(m2.port)),
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m1_m2_agmt = m1.agreement.create(NEW_SUFFIX, m2.host, m2.port, properties)

    if not m1_m2_agmt:
        log.fatal("Fail to create a hub -> consumer replica agreement")
        sys.exit(1)
    log.info("{} is created".format(m1_m2_agmt))

    # Allow the replicas to get situated with the new agreements...
    time.sleep(2)

    log.info("Initialize the agreement")
    m1.agreement.init(NEW_SUFFIX, m2.host, m2.port)
    m1.waitForReplInit(m1_m2_agmt)

    log.info("Check the replication is working")
    assert m1.testReplication(NEW_SUFFIX, m2), 'Replication for new suffix {} is not working.'.format(NEW_SUFFIX)

    log.info("Delete the agreement")
    m1.agreement.delete(NEW_SUFFIX, m2.host, m2.port, m1_m2_agmt)

    log.info("Disable replication for the new suffix")
    m1.replica.disableReplication(NEW_SUFFIX)
    m2.replica.disableReplication(NEW_SUFFIX)


def test_many_attrs(topo_m4, test_entry):
    """Check a replication with many attributes (add and delete)

    :ID: d540b358-f67a-43c6-8df5-7c74b3cb7523
    :feature: Multi master replication
    :setup: Four masters replication setup, a test entry
    :steps: 1. Add 10 new attributes to the entry
            2. Delete one from the beginning, two from the middle
               and one from the end
            3. Check that the changes were replicated in the right order
    :expectedresults: All changes are successfully replicated in the right order
    """

    m1 = topo_m4.ms["master1"]
    add_list = map(lambda x: "test{}".format(x), range(10))
    delete_list = map(lambda x: "test{}".format(x), [0, 4, 7, 9])

    log.info('Modifying entry {} - 10 add operations'.format(TEST_ENTRY_DN))
    for add_name in add_list:
        try:
            m1.modify_s(TEST_ENTRY_DN, [(ldap.MOD_ADD, 'description', add_name)])
        except ldap.LDAPError as e:
            log.error('Failed to modify entry (%s): error (%s)' % (TEST_ENTRY_DN, e.message['desc']))
            raise e

    log.info('Check that everything was properly replicated after an add operation')
    entries = get_repl_entries(topo_m4, TEST_ENTRY_NAME, ["description"])
    for entry in entries:
        assert all(entry.getValues("description")[i] == add_name for i, add_name in enumerate(add_list))

    log.info('Modifying entry {} - 4 delete operations for {}'.format(TEST_ENTRY_DN, str(delete_list)))
    for delete_name in delete_list:
        try:
            m1.modify_s(TEST_ENTRY_DN, [(ldap.MOD_DELETE, 'description', delete_name)])
        except ldap.LDAPError as e:
            log.error('Failed to modify entry (%s): error (%s)' % (TEST_ENTRY_DN, e.message['desc']))
            raise e

    log.info('Check that everything was properly replicated after a delete operation')
    entries = get_repl_entries(topo_m4, TEST_ENTRY_NAME, ["description"])
    for entry in entries:
        for i, value in enumerate(entry.getValues("description")):
            assert value == [name for name in add_list if name not in delete_list][i]
            assert value not in delete_list


def test_double_delete(topo_m4, test_entry):
    """Check that double delete of the entry doesn't crash server

    :ID: 3496c82d-636a-48c9-973c-2455b12164cc
    :feature: Multi master replication
    :setup: Four masters replication setup, a test entry
    :steps: 1. Delete the entry
            2. Delete the entry on the second master
            3. Check that server is alive
    :expectedresults: Server hasn't crash
    """

    log.info('Deleting entry {} from master1'.format(TEST_ENTRY_DN))
    topo_m4.ms["master1"].delete_s(TEST_ENTRY_DN)

    log.info('Deleting entry {} from master2'.format(TEST_ENTRY_DN))
    try:
        topo_m4.ms["master2"].delete_s(TEST_ENTRY_DN)
    except ldap.NO_SUCH_OBJECT:
        log.info("Entry {} wasn't found master2. It is expected.".format(TEST_ENTRY_DN))

    log.info('Make searches to check if server is alive')
    entries = get_repl_entries(topo_m4, TEST_ENTRY_NAME, ["uid"])
    assert not entries, "Entry deletion {} wasn't replicated successfully".format(TEST_ENTRY_DN)


def test_password_repl_error(topo_m4, test_entry):
    """Check that error about userpassword replication is properly logged

    :ID: 714130ff-e4f0-4633-9def-c1f4b24abfef
    :feature: Multi master replication
    :setup: Four masters replication setup, a test entry
    :steps: 1. Change userpassword on master 1
            2. Restart the servers to flush the logs
            3. Check the error log for an replication error
    :expectedresults: We don't have a replication error in the error log
    """

    m1 = topo_m4.ms["master1"]
    m2 = topo_m4.ms["master2"]
    TEST_ENTRY_NEW_PASS = 'new_{}'.format(TEST_ENTRY_NAME)

    log.info('Clean the error log')
    m2.deleteErrorLogs()

    log.info('Set replication loglevel')
    m2.setLogLevel(LOG_REPLICA)

    log.info('Modifying entry {} - change userpassword on master 2'.format(TEST_ENTRY_DN))
    try:
        m1.modify_s(TEST_ENTRY_DN, [(ldap.MOD_REPLACE, 'userpassword', 'new_{}'.format(TEST_ENTRY_NAME))])
    except ldap.LDAPError as e:
        log.error('Failed to modify entry (%s): error (%s)' % (TEST_ENTRY_DN,
                                                               e.message['desc']))
        raise e

    log.info('Restart the servers to flush the logs')
    for num in range(1, 5):
        topo_m4.ms["master{}".format(num)].restart(timeout=10)

    try:
        log.info('Check that password works on master 2')
        m2.simple_bind_s(TEST_ENTRY_DN, TEST_ENTRY_NEW_PASS)
        m2.simple_bind_s(DN_DM, PASSWORD)

        log.info('Check the error log for the error with {}'.format(TEST_ENTRY_DN))
        assert not m2.ds_error_log.match('.*can.t add a change for uid={}.*'.format(TEST_ENTRY_NAME))
    finally:
        log.info('Reset bind DN to Directory manager')
        for num in range(1, 5):
            topo_m4.ms["master{}".format(num)].simple_bind_s(DN_DM, PASSWORD)
        log.info('Set the default loglevel')
        m2.setLogLevel(LOG_DEFAULT)


def test_invalid_agmt(topo_m4):
    """Test adding that an invalid agreement is properly rejected and does not crash the server

    :id: 6c3b2a7e-edcd-4327-a003-6bd878ff722b
    :setup: MMR with four masters
    :steps:
        1. Add invalid agreement (nsds5ReplicaEnabled set to invalid value)
        2. Verify the server is still running
    :expectedresults:
        1. Invalid repl agreement should be rejected
        2. Server should be still running
    """
    m1 = topo_m4.ms["master1"]

    # Add invalid agreement (nsds5ReplicaEnabled set to invalid value)
    AGMT_DN = 'cn=whatever,cn=replica,cn="dc=example,dc=com",cn=mapping tree,cn=config'
    try:
        invalid_props = {RA_ENABLED: 'True',  # Invalid value
                         RA_SCHEDULE: '0001-2359 0123456'}
        m1.agreement.create(suffix=DEFAULT_SUFFIX, host='localhost', port=389, properties=invalid_props)
    except ldap.UNWILLING_TO_PERFORM:
        m1.log.info('Invalid repl agreement correctly rejected')
    except ldap.LDAPError as e:
        m1.log.fatal('Got unexpected error adding invalid agreement: ' + str(e))
        assert False
    else:
        m1.log.fatal('Invalid agreement was incorrectly accepted by the server')
        assert False

    # Verify the server is still running
    try:
        m1.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        m1.log.fatal('Failed to bind: ' + str(e))
        assert False


def test_warining_for_invalid_replica(topo_m4):
    """Testing logs to indicate the inconsistency when configuration is performed.

    :id: dd689d03-69b8-4bf9-a06e-2acd19d5e2c8
    :setup: MMR with four masters
    :steps:
        1. Setup nsds5ReplicaBackoffMin to 20
        2. Setup nsds5ReplicaBackoffMax to 10
    :expectedresults:
        1. nsds5ReplicaBackoffMin should set to 20
        2. An error should be generated and also logged in the error logs.
    """
    replicas = Replicas(topo_m4.ms["master1"])
    replica = replicas.list()[0]
    log.info('Set nsds5ReplicaBackoffMin to 20')
    replica.set('nsds5ReplicaBackoffMin', '20')
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        log.info('Set nsds5ReplicaBackoffMax to 10')
        replica.set('nsds5ReplicaBackoffMax', '10')
    log.info('Resetting configuration: nsds5ReplicaBackoffMin')
    replica.remove_all('nsds5ReplicaBackoffMin')
    log.info('Check the error log for the error')
    assert topo_m4.ms["master1"].ds_error_log.match('.*nsds5ReplicaBackoffMax.*10.*invalid.*')

@pytest.mark.skipif(ds_is_older('1.4.4'), reason="Not implemented")
def test_csngen_task(topo_m2):
    """Test csn generator test

    :id: b976849f-dbed-447e-91a7-c877d5d71fd0
    :setup: MMR with 2 masters
    :steps:
        1. Create a csngen_test task
        2. Check that debug messages "_csngen_gen_tester_main" are in errors logs
    :expectedresults:
        1. Should succeeds
        2. Should succeeds
    """
    m1 = topo_m2.ms["master1"]
    csngen_task = csngenTestTask(m1)
    csngen_task.create(properties={
        'ttl': '300'
    })
    time.sleep(10)
    log.info('Check the error log contains strings showing csn generator is tested')
    assert m1.searchErrorsLog("_csngen_gen_tester_main")

@pytest.mark.ds51082
def test_csnpurge_large_valueset(topo_m2):
    """Test csn generator test

    :id: 63e2bdb2-0a8f-4660-9465-7b80a9f72a74
    :setup: MMR with 2 masters
    :steps:
        1. Create a test_user
        2. add a large set of values (more than 10)
        3. delete all the values (more than 10)
        4. configure the replica to purge those values (purgedelay=5s)
        5. Waiting for 6 second
        6. do a series of update
    :expectedresults:
        1. Should succeeds
        2. Should succeeds
        3. Should succeeds
        4. Should succeeds
        5. Should succeeds
        6. Should not crash
    """
    m1 = topo_m2.ms["master2"]

    test_user = UserAccount(m1, TEST_ENTRY_DN)
    if test_user.exists():
        log.info('Deleting entry {}'.format(TEST_ENTRY_DN))
        test_user.delete()
    test_user.create(properties={
        'uid': TEST_ENTRY_NAME,
        'cn': TEST_ENTRY_NAME,
        'sn': TEST_ENTRY_NAME,
        'userPassword': TEST_ENTRY_NAME,
        'uidNumber' : '1000',
        'gidNumber' : '2000',
        'homeDirectory' : '/home/mmrepl_test',
    })

    # create a large value set so that it is sorted
    for i in range(1,20):
        test_user.add('description', 'value {}'.format(str(i)))

    # delete all values of the valueset
    for i in range(1,20):
        test_user.remove('description', 'value {}'.format(str(i)))

    # set purging delay to 5 second and wait more that 5second
    replicas = Replicas(m1)
    replica = replicas.list()[0]
    log.info('nsds5ReplicaPurgeDelay to 5')
    replica.set('nsds5ReplicaPurgeDelay', '5')
    time.sleep(6)

    # add some new values to the valueset containing entries that should be purged
    for i in range(21,25):
        test_user.add('description', 'value {}'.format(str(i)))

@pytest.mark.ds51244
def test_urp_trigger_substring_search(topo_m2):
    """Test that a ADD of a entry with a '*' in its DN, triggers
    an internal search with a escaped DN

    :id: 9869bb39-419f-42c3-a44b-c93eb0b77667
    :setup: MMR with 2 masters
    :steps:
        1. enable internal operation loggging for plugins
        2. Create on M1 a test_user with a '*' in its DN
        3. Check the test_user is replicated
        4. Check in access logs that the internal search does not contain '*'
    :expectedresults:
        1. Should succeeds
        2. Should succeeds
        3. Should succeeds
        4. Should succeeds
    """
    m1 = topo_m2.ms["master1"]
    m2 = topo_m2.ms["master2"]

    # Enable loggging of internal operation logging to capture URP intop
    log.info('Set nsslapd-plugin-logging to on')
    for inst in (m1, m2):
        inst.config.loglevel([AccessLog.DEFAULT, AccessLog.INTERNAL], service='access')
        inst.config.set('nsslapd-plugin-logging', 'on')
        inst.restart()

    # add a user with a DN containing '*'
    test_asterisk_uid = 'asterisk_*_in_value'
    test_asterisk_dn = 'uid={},{}'.format(test_asterisk_uid, DEFAULT_SUFFIX)

    test_user = UserAccount(m1, test_asterisk_dn)
    if test_user.exists():
        log.info('Deleting entry {}'.format(test_asterisk_dn))
        test_user.delete()
    test_user.create(properties={
        'uid': test_asterisk_uid,
        'cn': test_asterisk_uid,
        'sn': test_asterisk_uid,
        'userPassword': test_asterisk_uid,
        'uidNumber' : '1000',
        'gidNumber' : '2000',
        'homeDirectory' : '/home/asterisk',
    })

    # check that the ADD was replicated on M2
    test_user_m2 = UserAccount(m2, test_asterisk_dn)
    for i in range(1,5):
        if test_user_m2.exists():
            break
        else:
            log.info('Entry not yet replicated on M2, wait a bit')
            time.sleep(2)

    # check that M2 access logs does not "(&(objectclass=nstombstone)(nscpentrydn=uid=asterisk_*_in_value,dc=example,dc=com))"
    log.info('Check that on M2, URP as not triggered such internal search')
    pattern = ".*\(Internal\).*SRCH.*\(&\(objectclass=nstombstone\)\(nscpentrydn=uid=asterisk_\*_in_value,dc=example,dc=com.*"
    found = m2.ds_access_log.match(pattern)
    log.info("found line: %s" % found)
    assert not found


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
