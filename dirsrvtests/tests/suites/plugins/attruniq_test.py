# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2021 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
import ldap
import logging
from lib389.plugins import AttributeUniquenessPlugin
from lib389.idm.user import UserAccounts
from lib389.idm.group import Groups
from lib389._constants import DEFAULT_SUFFIX
from lib389.topologies import topology_st

pytestmark = pytest.mark.tier1

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)
MAIL_ATTR_VALUE = 'non-uniq@value.net'


def test_modrdn_attr_uniqueness(topology_st):
    """Test that we can not add two entries that have the same attr value that is
    defined by the plugin

    :id: dd763830-78b8-452e-888d-1d83d2e623f1

    :setup: Standalone instance

    :steps: 1. Create two groups
            2. Setup PLUGIN_ATTR_UNIQUENESS plugin for 'mail' attribute for the group2
            3. Enable PLUGIN_ATTR_UNIQUENESS plugin as "ON"
            4. Add two test users at group1 and add not uniq 'mail' attribute to each of them
            5. Move user1 to group2
            6. Move user2 to group2
            7. Move user2 back to group1

    :expectedresults:
            1. Success
            2. Success
            3. Success
            4. Success
            5. Success
            6. Modrdn operation should FAIL
            7. Success
    """
    log.debug('Create two groups')
    groups = Groups(topology_st.standalone, DEFAULT_SUFFIX)
    group1 = groups.create(properties={'cn': 'group1'})
    group2 = groups.create(properties={'cn': 'group2'})

    attruniq = AttributeUniquenessPlugin(topology_st.standalone, dn="cn=attruniq,cn=plugins,cn=config")
    log.debug(f'Setup PLUGIN_ATTR_UNIQUENESS plugin for {MAIL_ATTR_VALUE} attribute for the group2')
    attruniq.create(properties={'cn': 'attruniq'})
    attruniq.add_unique_attribute('mail')
    attruniq.add_unique_subtree(group2.dn)
    attruniq.enable_all_subtrees()
    log.debug(f'Enable PLUGIN_ATTR_UNIQUENESS plugin as "ON"')
    attruniq.enable()
    topology_st.standalone.restart()

    log.debug(f'Add two test users at group1 and add not uniq {MAIL_ATTR_VALUE} attribute to each of them')
    users = UserAccounts(topology_st.standalone, basedn=group1.dn, rdn=None)
    user1 = users.create_test_user(1)
    user2 = users.create_test_user(2)
    user1.add('mail', MAIL_ATTR_VALUE)
    user2.add('mail', MAIL_ATTR_VALUE)

    log.debug('Move user1 to group2')
    user1.rename(f'uid={user1.rdn}', group2.dn)

    log.debug('Move user2 to group2')
    with pytest.raises(ldap.CONSTRAINT_VIOLATION) as excinfo:
        user2.rename(f'uid={user2.rdn}', group2.dn)
        log.fatal(f'Failed: Attribute "mail" with {MAIL_ATTR_VALUE} is accepted')
    assert 'attribute value already exist' in str(excinfo.value)
    log.debug(excinfo.value)

    log.debug('Move user2 to group1')
    user2.rename(f'uid={user2.rdn}', group1.dn)