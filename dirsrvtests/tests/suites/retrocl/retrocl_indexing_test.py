# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import pytest
import os
import ldap
from lib389._constants import RETROCL_SUFFIX, DEFAULT_SUFFIX
from lib389.topologies import topology_st as topo
from lib389.topologies import topology_m1
from lib389.plugins import RetroChangelogPlugin
from lib389.idm.user import UserAccounts
from lib389._mapped_object import DSLdapObjects
from lib389.properties import TASK_WAIT, INDEX_TYPE
from lib389.index import Indexes


log = logging.getLogger(__name__)

TEST_REPL_DN = "cn=test_repl,{}".format(DEFAULT_SUFFIX)
ENTRY_DN = "cn=test_entry,{}".format(DEFAULT_SUFFIX)

OTHER_NAME = 'other_entry'
MAX_OTHERS = 100

ATTRIBUTES = ['street', 'countryName', 'description', 'postalAddress', 'postalCode', 'title', 'l', 'roomNumber']


def test_indexing_is_online(topo):
    """Test that the changenmumber index is online right after enabling the plugin

    :id: 16f4c001-9e0c-4448-a2b3-08ac1e85d40f
    :setup: Standalone Instance
    :steps:
        1. Enable retro cl
        2. Perform some updates
        3. Search for "(changenumber>=-1)", and it is not partially unindexed
        4. Search for "(&(changenumber>=-1)(targetuniqueid=*))", and it is not partially unindexed
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
    """

    # Enable plugin
    topo.standalone.config.set('nsslapd-accesslog-logbuffering',  'off')
    plugin = RetroChangelogPlugin(topo.standalone)
    plugin.enable()
    topo.standalone.restart()

    # Do a bunch of updates
    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    user_entry = users.create(properties={
        'sn': '1',
        'cn': 'user 1',
        'uid': 'user1',
        'uidNumber': '11',
        'gidNumber': '111',
        'givenname': 'user1',
        'homePhone': '0861234567',
        'carLicense': '131D16674',
        'mail': 'user1@whereever.com',
        'homeDirectory': '/home'
    })
    for count in range(0, 10):
        user_entry.replace('mail', f'test{count}@test.com')

    # Search the retro cl, and check for error messages
    filter_simple = '(changenumber>=-1)'
    filter_compound = '(&(changenumber>=-1)(targetuniqueid=*))'
    retro_changelog_suffix = DSLdapObjects(topo.standalone, basedn=RETROCL_SUFFIX)
    retro_changelog_suffix.filter(filter_simple)
    assert not topo.standalone.searchAccessLog('Partially Unindexed Filter')

    # Search the retro cl again with compound filter
    retro_changelog_suffix.filter(filter_compound)
    assert not topo.standalone.searchAccessLog('Partially Unindexed Filter')


def test_reindexing_retrochangelog(topology_m1):
    """ Test that the retro changelog can be successfully reindexed

        :id: 26d8df59-0886-4098-8d2c-b9337accc908
        :setup: Supplier instance
        :steps:
            1. Enable retro changelog and restart instance
            2. Create test entries
            3. Create index
            4. Reindex retro changelog
            5. Verify indexed search
            6. Clean up test entries
        :expectedresults:
            1. Success
            2. Success
            3. Success
            4. Success
            5. Success
            6. Success
        """

    log.info('Enable retro changelog and restart instance')
    retrocl = RetroChangelogPlugin(topology_m1.ms["supplier1"])
    retrocl.enable()
    topology_m1.ms["supplier1"].restart()

    log.info('Creating test entries')
    users = UserAccounts(topology_m1.ms["supplier1"], DEFAULT_SUFFIX)
    for count in range(MAX_OTHERS):
        name = "{}{}".format(OTHER_NAME, count)
        users.create(properties={
            'sn': name,
            'cn': name,
            'uid': name,
            'uidNumber': '1{}'.format(count),
            'gidNumber': '11{}'.format(count),
            'homeDirectory': '/home/{}'.format(name)
        })

    log.info('Test entries created')

    try:
        log.info('Verify number of created entries')
        check_entries = users.list()
        assert len(check_entries) == MAX_OTHERS

        log.info('Create index')
        indexes = Indexes(topology_m1.ms["supplier1"])
        for attr in ATTRIBUTES:
            indexes.create(properties={
                'cn': attr,
                'nsSystemIndex': 'false',
                'nsIndexType': 'eq'
            })
        topology_m1.ms["supplier1"].restart()

        log.info('Reindex retro changelog')
        args = {TASK_WAIT: True}
        for attr in ATTRIBUTES:
            rc = topology_m1.ms["supplier1"].tasks.reindex(suffix=RETROCL_SUFFIX, attrname=attr, args=args)
            log.info('Verify reindexing task was successful for attribute {}'.format(attr))
            assert rc == 0

        log.info('Check reindexed search')
        for attr in ATTRIBUTES:
            ents = topology_m1.ms["supplier1"].search_s(RETROCL_SUFFIX, ldap.SCOPE_SUBTREE, "({}=hello)".format(attr),
                                                        escapehatch='i am sure')
            assert len(ents) == 0
    finally:
        log.info('Clean up the test entries')
        entries = users.list()
        for entry in entries:
            entry.delete()


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
