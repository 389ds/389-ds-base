import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st


def test_purge_success(topology_st):
    """Verify that tombstones are created successfully

    :Feature: nsTombstone

    :Setup: Standalone instance

    :Steps: 1. Enable replication to unexisting instance
            2. Add an entry to the replicated suffix
            3. Delete the entry
            4. Check that tombstone entry exists (objectclass=nsTombstone)

    :Assert: Tombstone entry exist
    """

    log.info('Setting up replication...')
    topology_st.standalone.replica.enableReplication(suffix=DEFAULT_SUFFIX,
                                                     role=REPLICAROLE_MASTER,
                                                     replicaId=REPLICAID_MASTER_1)

    log.info("Add and then delete an entry to create a tombstone...")
    try:
        topology_st.standalone.add_s(Entry(('cn=entry1,dc=example,dc=com', {
            'objectclass': 'top person'.split(),
            'sn': 'user',
            'cn': 'entry1'})))
    except ldap.LDAPError as e:
        log.error('Failed to add entry: {}'.format(e.message['desc']))
        assert False

    try:
        topology_st.standalone.delete_s('cn=entry1,dc=example,dc=com')
    except ldap.LDAPError as e:
        log.error('Failed to delete entry: {}'.format(e.message['desc']))
        assert False

    log.info('Search for tombstone entries...')
    try:
        entries = topology_st.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE,
                                                  '(objectclass=nsTombstone)')
        assert entries
    except ldap.LDAPError as e:
        log.fatal('Search failed: {}'.format(e.message['desc']))
        assert False


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
