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
from lib389.idm.nscontainer import nsContainers
from lib389.idm.user import UserAccounts
from lib389.idm.group import Groups
from lib389._constants import DEFAULT_SUFFIX
from lib389.topologies import topology_st

pytestmark = pytest.mark.tier1

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)
MAIL_ATTR_VALUE = 'non-uniq@value.net'
MAIL_ATTR_VALUE_ALT = 'alt-mail@value.net'

EXCLUDED_CONTAINER_CN = "excluded_container"
EXCLUDED_CONTAINER_DN = "cn={},{}".format(EXCLUDED_CONTAINER_CN, DEFAULT_SUFFIX)

EXCLUDED_BIS_CONTAINER_CN = "excluded_bis_container"
EXCLUDED_BIS_CONTAINER_DN = "cn={},{}".format(EXCLUDED_BIS_CONTAINER_CN, DEFAULT_SUFFIX)

ENFORCED_CONTAINER_CN = "enforced_container"

USER_1_CN = "test_1"
USER_2_CN = "test_2"
USER_3_CN = "test_3"
USER_4_CN = "test_4"


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

    # Cleanup for next test
    user1.delete()
    user2.delete()
    attruniq.disable()
    attruniq.delete()


def test_multiple_attr_uniqueness(topology_st):
    """ Test that attribute uniqueness works properly with multiple attributes

    :id: c49aa5c1-7e65-45fd-b064-55e0b815e9bc
    :setup: Standalone instance
    :steps:
        1. Setup attribute uniqueness plugin to ensure uniqueness of attributes 'mail' and 'mailAlternateAddress'
        2. Add user with unique 'mail=non-uniq@value.net' and 'mailAlternateAddress=alt-mail@value.net'
        3. Try adding another user with 'mail=non-uniq@value.net'
        4. Try adding another user with 'mailAlternateAddress=alt-mail@value.net'
        5. Try adding another user with 'mail=alt-mail@value.net'
        6. Try adding another user with 'mailAlternateAddress=non-uniq@value.net'
    :expectedresults:
        1. Success
        2. Success
        3. Should raise CONSTRAINT_VIOLATION
        4. Should raise CONSTRAINT_VIOLATION
        5. Should raise CONSTRAINT_VIOLATION
        6. Should raise CONSTRAINT_VIOLATION
    """
    attruniq = AttributeUniquenessPlugin(topology_st.standalone, dn="cn=attruniq,cn=plugins,cn=config")

    try:
        log.debug(f'Setup PLUGIN_ATTR_UNIQUENESS plugin for {MAIL_ATTR_VALUE} attribute for the group2')
        attruniq.create(properties={'cn': 'attruniq'})
        attruniq.add_unique_attribute('mail')
        attruniq.add_unique_attribute('mailAlternateAddress')
        attruniq.add_unique_subtree(DEFAULT_SUFFIX)
        attruniq.enable_all_subtrees()
        log.debug(f'Enable PLUGIN_ATTR_UNIQUENESS plugin as "ON"')
        attruniq.enable()
    except ldap.LDAPError as e:
        log.fatal('test_multiple_attribute_uniqueness: Failed to configure plugin for "mail": error {}'.format(e.args[0]['desc']))
        assert False

    topology_st.standalone.restart()

    users = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX)

    testuser1 = users.create_test_user(100,100)
    testuser1.add('objectclass', 'extensibleObject')
    testuser1.add('mail', MAIL_ATTR_VALUE)
    testuser1.add('mailAlternateAddress', MAIL_ATTR_VALUE_ALT)

    testuser2 = users.create_test_user(200, 200)
    testuser2.add('objectclass', 'extensibleObject')

    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        testuser2.add('mail', MAIL_ATTR_VALUE)

    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        testuser2.add('mailAlternateAddress', MAIL_ATTR_VALUE_ALT)

    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        testuser2.add('mail', MAIL_ATTR_VALUE_ALT)

    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        testuser2.add('mailAlternateAddress', MAIL_ATTR_VALUE)

    # Cleanup
    testuser1.delete()
    testuser2.delete()
    attruniq.disable()
    attruniq.delete()


def test_exclude_subtrees(topology_st):
    """ Test attribute uniqueness with exclude scope

    :id: 43d29a60-40e1-4ebd-b897-6ef9f20e9f27
    :setup: Standalone instance
    :steps:
        1. Setup and enable attribute uniqueness plugin for telephonenumber unique attribute
        2. Create subtrees and test users
        3. Add a unique attribute to a user within uniqueness scope
        4. Add exclude subtree
        5. Try to add existing value attribute to an entry within uniqueness scope
        6. Try to add existing value attribute to an entry within exclude scope
        7. Remove the attribute from affected entries
        8. Add a unique attribute to a user within exclude scope
        9. Try to add existing value attribute to an entry within uniqueness scope
        10. Try to add existing value attribute to another entry within uniqueness scope
        11. Remove the attribute from affected entries
        12. Add another exclude subtree
        13. Add a unique attribute to a user within uniqueness scope
        14. Try to add existing value attribute to an entry within uniqueness scope
        15. Try to add existing value attribute to an entry within exclude scope
        16. Try to add existing value attribute to an entry within another exclude scope
        17. Clean up entries
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Should raise CONSTRAINT_VIOLATION
        6. Success
        7. Success
        8. Success
        9. Success
        10. Should raise CONSTRAINT_VIOLATION
        11. Success
        12. Success
        13. Success
        14. Should raise CONSTRAINT_VIOLATION
        15. Success
        16. Success
        17. Success
    """
    log.info('Setup attribute uniqueness plugin')
    attruniq = AttributeUniquenessPlugin(topology_st.standalone, dn="cn=attruniq,cn=plugins,cn=config")
    attruniq.create(properties={'cn': 'attruniq'})
    attruniq.add_unique_attribute('telephonenumber')
    attruniq.add_unique_subtree(DEFAULT_SUFFIX)
    attruniq.enable_all_subtrees()
    attruniq.enable()
    topology_st.standalone.restart()

    log.info('Create subtrees container')
    containers = nsContainers(topology_st.standalone, DEFAULT_SUFFIX)
    cont1 = containers.create(properties={'cn': EXCLUDED_CONTAINER_CN})
    cont2 = containers.create(properties={'cn': EXCLUDED_BIS_CONTAINER_CN})
    cont3 = containers.create(properties={'cn': ENFORCED_CONTAINER_CN})

    log.info('Create test users')
    users = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX,
                         rdn='cn={}'.format(ENFORCED_CONTAINER_CN))
    users_excluded = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX,
                                  rdn='cn={}'.format(EXCLUDED_CONTAINER_CN))
    users_excluded2 = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX,
                                   rdn='cn={}'.format(EXCLUDED_BIS_CONTAINER_CN))

    user1 = users.create(properties={'cn': USER_1_CN,
                                     'uid': USER_1_CN,
                                     'sn': USER_1_CN,
                                     'uidNumber': '1',
                                     'gidNumber': '11',
                                     'homeDirectory': '/home/{}'.format(USER_1_CN)})
    user2 = users.create(properties={'cn': USER_2_CN,
                                     'uid': USER_2_CN,
                                     'sn': USER_2_CN,
                                     'uidNumber': '2',
                                     'gidNumber': '22',
                                     'homeDirectory': '/home/{}'.format(USER_2_CN)})
    user3 = users_excluded.create(properties={'cn': USER_3_CN,
                                              'uid': USER_3_CN,
                                              'sn': USER_3_CN,
                                              'uidNumber': '3',
                                              'gidNumber': '33',
                                              'homeDirectory': '/home/{}'.format(USER_3_CN)})
    user4 = users_excluded2.create(properties={'cn': USER_4_CN,
                                               'uid': USER_4_CN,
                                               'sn': USER_4_CN,
                                               'uidNumber': '4',
                                               'gidNumber': '44',
                                               'homeDirectory': '/home/{}'.format(USER_4_CN)})

    UNIQUE_VALUE = '1234'

    log.info('Create user with unique attribute')
    user1.add('telephonenumber', UNIQUE_VALUE)
    assert user1.present('telephonenumber', UNIQUE_VALUE)

    log.info('Add exclude subtree')
    attruniq.add_exclude_subtree(EXCLUDED_CONTAINER_DN)
    topology_st.standalone.restart()

    log.info('Verify an already used attribute value cannot be added within the same subtree')
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        user2.add('telephonenumber', UNIQUE_VALUE)

    log.info('Verify an entry with same attribute value can be added within exclude subtree')
    user3.add('telephonenumber', UNIQUE_VALUE)
    assert user3.present('telephonenumber', UNIQUE_VALUE)

    log.info('Cleanup unique attribute values')
    user1.remove_all('telephonenumber')
    user3.remove_all('telephonenumber')

    log.info('Add a unique value to an entry in excluded scope')
    user3.add('telephonenumber', UNIQUE_VALUE)
    assert user3.present('telephonenumber', UNIQUE_VALUE)

    log.info('Verify the same value can be added to an entry within uniqueness scope')
    user1.add('telephonenumber', UNIQUE_VALUE)
    assert user1.present('telephonenumber', UNIQUE_VALUE)

    log.info('Verify that yet another same value cannot be added to another entry within uniqueness scope')
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        user2.add('telephonenumber', UNIQUE_VALUE)

    log.info('Cleanup unique attribute values')
    user1.remove_all('telephonenumber')
    user3.remove_all('telephonenumber')

    log.info('Add another exclude subtree')
    attruniq.add_exclude_subtree(EXCLUDED_BIS_CONTAINER_DN)
    topology_st.standalone.restart()

    user1.add('telephonenumber', UNIQUE_VALUE)
    log.info('Verify an already used attribute value cannot be added within the same subtree')
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        user2.add('telephonenumber', UNIQUE_VALUE)

    log.info('Verify an already used attribute can be added to an entry in exclude scope')
    user3.add('telephonenumber', UNIQUE_VALUE)
    assert user3.present('telephonenumber', UNIQUE_VALUE)
    user4.add('telephonenumber', UNIQUE_VALUE)
    assert user4.present('telephonenumber', UNIQUE_VALUE)

    log.info('Clean up users')
    user1.delete()
    user2.delete()
    user3.delete()
    user4.delete()
    cont1.delete()
    cont2.delete()
    cont3.delete()
    attruniq.disable()
    attruniq.delete()