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
from lib389._constants import RETROCL_SUFFIX, DEFAULT_SUFFIX
from lib389.topologies import topology_st as topo
from lib389.plugins import RetroChangelogPlugin
from lib389.idm.user import UserAccounts
from lib389._mapped_object import DSLdapObjects
log = logging.getLogger(__name__)


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


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
