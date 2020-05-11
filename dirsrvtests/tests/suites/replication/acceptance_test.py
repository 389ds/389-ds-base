# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
from lib389.replica import Replicas
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_m4 as topo_m4
from . import get_repl_entries
from lib389.idm.user import UserAccount
from lib389.replica import ReplicationManager
from lib389._constants import *

pytestmark = pytest.mark.tier0

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
def create_entry(topo_m4, request):
    """Add test entry to master1"""

    log.info('Adding entry {}'.format(TEST_ENTRY_DN))

    test_user = UserAccount(topo_m4.ms["master1"], TEST_ENTRY_DN)
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


def test_add_entry(topo_m4, create_entry):
    """Check that entries are replicated after add operation

    :id: 024250f1-5f7e-4f3b-a9f5-27741e6fd405
    :setup: Four masters replication setup, an entry
    :steps:
        1. Check entry on all other masters
    :expectedresults:
        1. The entry should be replicated to all masters
    """

    entries = get_repl_entries(topo_m4, TEST_ENTRY_NAME, ["uid"])
    assert all(entries), "Entry {} wasn't replicated successfully".format(TEST_ENTRY_DN)


def test_modify_entry(topo_m4, create_entry):
    """Check that entries are replicated after modify operation

    :id: 36764053-622c-43c2-a132-d7a3ab7d9aaa
    :setup: Four masters replication setup, an entry
    :steps:
        1. Modify the entry on master1 - add attribute
        2. Wait for replication to happen
        3. Check entry on all other masters
        4. Modify the entry on master1 - replace attribute
        5. Wait for replication to happen
        6. Check entry on all other masters
        7. Modify the entry on master1 - delete attribute
        8. Wait for replication to happen
        9. Check entry on all other masters
    :expectedresults:
        1. Attribute should be successfully added
        2. Some time should pass
        3. The change should be present on all masters
        4. Attribute should be successfully replaced
        5. Some time should pass
        6. The change should be present on all masters
        4. Attribute should be successfully deleted
        8. Some time should pass
        9. The change should be present on all masters
    """

    log.info('Modifying entry {} - add operation'.format(TEST_ENTRY_DN))

    test_user = UserAccount(topo_m4.ms["master1"], TEST_ENTRY_DN)
    test_user.add('mail', '{}@redhat.com'.format(TEST_ENTRY_NAME))
    time.sleep(1)

    all_user = topo_m4.all_get_dsldapobject(TEST_ENTRY_DN, UserAccount)
    for u in all_user:
        assert "{}@redhat.com".format(TEST_ENTRY_NAME) in u.get_attr_vals_utf8('mail')

    log.info('Modifying entry {} - replace operation'.format(TEST_ENTRY_DN))
    test_user.replace('mail', '{}@greenhat.com'.format(TEST_ENTRY_NAME))
    time.sleep(1)

    all_user = topo_m4.all_get_dsldapobject(TEST_ENTRY_DN, UserAccount)
    for u in all_user:
        assert "{}@greenhat.com".format(TEST_ENTRY_NAME) in u.get_attr_vals_utf8('mail')

    log.info('Modifying entry {} - delete operation'.format(TEST_ENTRY_DN))
    test_user.remove('mail', '{}@greenhat.com'.format(TEST_ENTRY_NAME))
    time.sleep(1)

    all_user = topo_m4.all_get_dsldapobject(TEST_ENTRY_DN, UserAccount)
    for u in all_user:
        assert "{}@greenhat.com".format(TEST_ENTRY_NAME) not in u.get_attr_vals_utf8('mail')


def test_delete_entry(topo_m4, create_entry):
    """Check that entry deletion is replicated after delete operation

    :id: 18437262-9d6a-4b98-a47a-6182501ab9bc
    :setup: Four masters replication setup, an entry
    :steps:
        1. Delete the entry from master1
        2. Check entry on all other masters
    :expectedresults:
        1. The entry should be deleted
        2. The change should be present on all masters
    """

    log.info('Deleting entry {} during the test'.format(TEST_ENTRY_DN))
    topo_m4.ms["master1"].delete_s(TEST_ENTRY_DN)

    entries = get_repl_entries(topo_m4, TEST_ENTRY_NAME, ["uid"])
    assert not entries, "Entry deletion {} wasn't replicated successfully".format(TEST_ENTRY_DN)


@pytest.mark.parametrize("delold", [0, 1])
def test_modrdn_entry(topo_m4, create_entry, delold):
    """Check that entries are replicated after modrdn operation

    :id: 02558e6d-a745-45ae-8d88-34fe9b16adc9
    :parametrized: yes
    :setup: Four masters replication setup, an entry
    :steps:
        1. Make modrdn operation on entry on master1 with both delold 1 and 0
        2. Check entry on all other masters
    :expectedresults:
        1. Modrdn operation should be successful
        2. The change should be present on all masters
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

    :id: 6271dc9c-a993-4a9e-9c6d-05650cdab282
    :setup: Four masters replication setup, an entry
    :steps:
        1. Pause all replicas
        2. Make modrdn operation on entry on master1
        3. Resume all replicas
        4. Wait for replication to happen
        5. Check entry on all other masters
    :expectedresults:
        1. Replicas should be paused
        2. Modrdn operation should be successful
        3. Replicas should be resumed
        4. Some time should pass
        5. The change should be present on all masters
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


@pytest.mark.bz842441
def test_modify_stripattrs(topo_m4):
    """Check that we can modify nsds5replicastripattrs

    :id: f36abed8-e262-4f35-98aa-71ae55611aaa
    :setup: Four masters replication setup
    :steps:
        1. Modify nsds5replicastripattrs attribute on any agreement
        2. Search for the modified attribute
    :expectedresults: It should be contain the value
        1. nsds5replicastripattrs should be successfully set
        2. The modified attribute should be the one we set
    """

    m1 = topo_m4.ms["master1"]
    agreement = m1.agreement.list(suffix=DEFAULT_SUFFIX)[0].dn
    attr_value = b'modifiersname modifytimestamp'

    log.info('Modify nsds5replicastripattrs with {}'.format(attr_value))
    m1.modify_s(agreement, [(ldap.MOD_REPLACE, 'nsds5replicastripattrs', [attr_value])])

    log.info('Check nsds5replicastripattrs for {}'.format(attr_value))
    entries = m1.search_s(agreement, ldap.SCOPE_BASE, "objectclass=*", ['nsds5replicastripattrs'])
    assert attr_value in entries[0].data['nsds5replicastripattrs']


def test_new_suffix(topo_m4, new_suffix):
    """Check that we can enable replication on a new suffix

    :id: d44a9ed4-26b0-4189-b0d0-b2b336ddccbd
    :setup: Four masters replication setup, a new suffix
    :steps:
        1. Enable replication on the new suffix
        2. Check if replication works
        3. Disable replication on the new suffix
    :expectedresults:
        1. Replication on the new suffix should be enabled
        2. Replication should work
        3. Replication on the new suffix should be disabled
    """
    m1 = topo_m4.ms["master1"]
    m2 = topo_m4.ms["master2"]

    repl = ReplicationManager(NEW_SUFFIX)

    repl.create_first_master(m1)

    repl.join_master(m1, m2)

    repl.test_replication(m1, m2)
    repl.test_replication(m2, m1)

    repl.remove_master(m1)
    repl.remove_master(m2)

def test_many_attrs(topo_m4, create_entry):
    """Check a replication with many attributes (add and delete)

    :id: d540b358-f67a-43c6-8df5-7c74b3cb7523
    :setup: Four masters replication setup, a test entry
    :steps:
        1. Add 10 new attributes to the entry
        2. Delete few attributes: one from the beginning,
           two from the middle and one from the end
        3. Check that the changes were replicated in the right order
    :expectedresults:
        1. The attributes should be successfully added
        2. Delete operations should be successful
        3. The changes should be replicated in the right order
    """

    m1 = topo_m4.ms["master1"]
    add_list = ensure_list_bytes(map(lambda x: "test{}".format(x), range(10)))
    delete_list = ensure_list_bytes(map(lambda x: "test{}".format(x), [0, 4, 7, 9]))
    test_user = UserAccount(topo_m4.ms["master1"], TEST_ENTRY_DN)

    log.info('Modifying entry {} - 10 add operations'.format(TEST_ENTRY_DN))
    for add_name in add_list:
        test_user.add('description', add_name)

    log.info('Check that everything was properly replicated after an add operation')
    entries = get_repl_entries(topo_m4, TEST_ENTRY_NAME, ["description"])
    for entry in entries:
        assert all(entry.getValues("description")[i] == add_name for i, add_name in enumerate(add_list))

    log.info('Modifying entry {} - 4 delete operations for {}'.format(TEST_ENTRY_DN, str(delete_list)))
    for delete_name in delete_list:
        test_user.remove('description', delete_name)

    log.info('Check that everything was properly replicated after a delete operation')
    entries = get_repl_entries(topo_m4, TEST_ENTRY_NAME, ["description"])
    for entry in entries:
        for i, value in enumerate(entry.getValues("description")):
            assert value == [name for name in add_list if name not in delete_list][i]
            assert value not in delete_list


def test_double_delete(topo_m4, create_entry):
    """Check that double delete of the entry doesn't crash server

    :id: 5b85a5af-df29-42c7-b6cb-965ec5aa478e
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


def test_password_repl_error(topo_m4, create_entry):
    """Check that error about userpassword replication is properly logged

    :id: d4f12dc0-cd2c-4b92-9b8d-d764a60f0698
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
    m2.config.loglevel((ErrorLog.REPLICA,))

    log.info('Modifying entry {} - change userpassword on master 2'.format(TEST_ENTRY_DN))
    test_user_m1 = UserAccount(topo_m4.ms["master1"], TEST_ENTRY_DN)
    test_user_m2 = UserAccount(topo_m4.ms["master2"], TEST_ENTRY_DN)
    test_user_m3 = UserAccount(topo_m4.ms["master3"], TEST_ENTRY_DN)
    test_user_m4 = UserAccount(topo_m4.ms["master4"], TEST_ENTRY_DN)

    test_user_m1.set('userpassword', TEST_ENTRY_NEW_PASS)

    log.info('Restart the servers to flush the logs')
    for num in range(1, 5):
        topo_m4.ms["master{}".format(num)].restart(timeout=10)

    m1_conn = test_user_m1.bind(TEST_ENTRY_NEW_PASS)
    m2_conn = test_user_m2.bind(TEST_ENTRY_NEW_PASS)
    m3_conn = test_user_m3.bind(TEST_ENTRY_NEW_PASS)
    m4_conn = test_user_m4.bind(TEST_ENTRY_NEW_PASS)

    log.info('Check the error log for the error with {}'.format(TEST_ENTRY_DN))
    assert not m2.ds_error_log.match('.*can.t add a change for uid={}.*'.format(TEST_ENTRY_NAME))


def test_invalid_agmt(topo_m4):
    """Test adding that an invalid agreement is properly rejected and does not crash the server

    :id: 92f10f46-1be1-49ca-9358-784359397bc2
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


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
