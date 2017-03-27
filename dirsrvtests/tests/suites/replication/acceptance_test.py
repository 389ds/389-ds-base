# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_m4 as topo

TEST_ENTRY_NAME = 'mmrepl_test'
TEST_ENTRY_DN = 'uid={},{}'.format(TEST_ENTRY_NAME, DEFAULT_SUFFIX)

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


@pytest.fixture(scope="function")
def test_entry(topo, request):
    """Add test entry to master1"""

    log.info('Adding entry {}'.format(TEST_ENTRY_DN))
    try:
        topo.ms["master1"].add_s(Entry((TEST_ENTRY_DN, {
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

    def fin():
        log.info('Deleting entry {}'.format(TEST_ENTRY_DN))
        try:
            topo.ms["master1"].delete_s(TEST_ENTRY_DN)
        except ldap.NO_SUCH_OBJECT:
            log.info("Entry {} wasn't found".format(TEST_ENTRY_DN))

    request.addfinalizer(fin)


def get_repl_entries(topo, entry_name, attr_list):
    """Get a list of test entries from all masters"""

    entries_list = []

    log.info('Wait for replication to happen')
    time.sleep(3)

    for num in range(1, 5):
        entries = topo.ms['master{}'.format(num)].search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE,
                                                           "uid={}".format(entry_name), attr_list)
        entries_list += entries

    return entries_list


def test_add_entry(topo, test_entry):
    """Check that entries are replicated after add operation

    :ID: 024250f1-5f7e-4f3b-a9f5-27741e6fd405
    :feature: Multi master replication
    :setup: Four masters replication setup
    :steps: 1. Add entry to master1
            2. Wait for replication to happen
            3. Check entry on all other masters
    :expectedresults: Entry should be replicated
    """

    entries = get_repl_entries(topo, TEST_ENTRY_NAME, ["uid"])
    assert all(entries), "Entry {} wasn't replicated successfully".format(TEST_ENTRY_DN)


def test_modify_entry(topo, test_entry):
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
        topo.ms["master1"].modify_s(TEST_ENTRY_DN, [(ldap.MOD_ADD,
                                                     'mail', '{}@redhat.com'.format(TEST_ENTRY_NAME))])
    except ldap.LDAPError as e:
        log.error('Failed to modify entry (%s): error (%s)' % (TEST_ENTRY_DN,
                                                               e.message['desc']))
        raise e

    entries = get_repl_entries(topo, TEST_ENTRY_NAME, ["mail"])
    assert all(entry["mail"] == "{}@redhat.com".format(TEST_ENTRY_NAME)
               for entry in entries), "Entry attr {} wasn't replicated successfully".format(TEST_ENTRY_DN)

    log.info('Modifying entry {} - replace operation'.format(TEST_ENTRY_DN))
    try:
        topo.ms["master1"].modify_s(TEST_ENTRY_DN, [(ldap.MOD_REPLACE,
                                                     'mail', '{}@greenhat.com'.format(TEST_ENTRY_NAME))])
    except ldap.LDAPError as e:
        log.error('Failed to modify entry (%s): error (%s)' % (TEST_ENTRY_DN,
                                                               e.message['desc']))
        raise e

    entries = get_repl_entries(topo, TEST_ENTRY_NAME, ["mail"])
    assert all(entry["mail"] == "{}@greenhat.com".format(TEST_ENTRY_NAME)
               for entry in entries), "Entry attr {} wasn't replicated successfully".format(TEST_ENTRY_DN)

    log.info('Modifying entry {} - delete operation'.format(TEST_ENTRY_DN))
    try:
        topo.ms["master1"].modify_s(TEST_ENTRY_DN, [(ldap.MOD_DELETE,
                                                     'mail', '{}@greenhat.com'.format(TEST_ENTRY_NAME))])
    except ldap.LDAPError as e:
        log.error('Failed to modify entry (%s): error (%s)' % (TEST_ENTRY_DN,
                                                               e.message['desc']))
        raise e

    entries = get_repl_entries(topo, TEST_ENTRY_NAME, ["mail"])
    assert all(not entry["mail"] for entry in entries), "Entry attr {} wasn't replicated successfully".format(
        TEST_ENTRY_DN)


def test_delete_entry(topo, test_entry):
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
    topo.ms["master1"].delete_s(TEST_ENTRY_DN)

    entries = get_repl_entries(topo, TEST_ENTRY_NAME, ["uid"])
    assert not entries, "Entry deletion {} wasn't replicated successfully".format(TEST_ENTRY_DN)


@pytest.mark.parametrize("delold", [0, 1])
def test_modrdn_entry(topo, test_entry, delold):
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
        topo.ms["master1"].modrdn_s(TEST_ENTRY_DN, 'uid={}'.format(newrdn_name), delold)
    except ldap.LDAPError as e:
        log.error('Failed to modrdn entry (%s): error (%s)' % (TEST_ENTRY_DN,
                                                               e.message['desc']))
        raise e

    try:
        entries_new = get_repl_entries(topo, newrdn_name, ["uid"])
        assert all(entries_new), "Entry {} wasn't replicated successfully".format(TEST_ENTRY_DN)
        if delold == 0:
            entries_old = get_repl_entries(topo, TEST_ENTRY_NAME, ["uid"])
            assert all(entries_old), "Entry with old rdn {} wasn't replicated successfully".format(TEST_ENTRY_DN)
        else:
            entries_old = get_repl_entries(topo, TEST_ENTRY_NAME, ["uid"])
            assert not entries_old, "Entry with old rdn {} wasn't removed in replicas successfully".format(
                TEST_ENTRY_DN)
    finally:
        log.info('Remove entry with new RDN {}'.format(newrdn_dn))
        topo.ms["master1"].delete_s(newrdn_dn)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
