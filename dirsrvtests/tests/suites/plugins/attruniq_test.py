# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import subprocess
import shutil
import re
import pytest
import ldap
import logging
from time import sleep
from lib389.plugins import AttributeUniquenessPlugin
from lib389.idm.nscontainer import nsContainers
from lib389.idm.user import UserAccounts
from lib389.idm.group import Groups
from lib389._constants import DEFAULT_SUFFIX
from lib389.topologies import topology_st
from lib389.utils import ds_is_older

pytestmark = [pytest.mark.tier1,
              pytest.mark.skipif(ds_is_older('1.3.3'), reason="Not implemented")]

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

PROVISIONING_CN = "provisioning"
PROVISIONING_DN = f"cn={PROVISIONING_CN},{DEFAULT_SUFFIX}"

ACTIVE_CN = "accounts"
STAGE_CN = "staged users"
DELETE_CN = "deleted users"
ACTIVE_DN = f"cn={ACTIVE_CN},{DEFAULT_SUFFIX}"
STAGE_DN = f"cn={STAGE_CN},{PROVISIONING_DN}"

ACTIVE_USER_1_CN = "active_1"
ACTIVE_USER_2_CN = "active_2"

STAGE_USER_1_CN = 'stage_1'
STAGE_USER_2_CN = 'stage_2'


@pytest.fixture(scope='function')
def containers(topology_st, request):
    """Create containers for the tests"""
    log.info('Create containers for the tests')
    containers = nsContainers(topology_st.standalone, DEFAULT_SUFFIX)
    cont_provisioning = containers.create(properties={'cn': PROVISIONING_CN})
    cont_active = containers.create(properties={'cn': ACTIVE_CN})

    containers_provisioning = nsContainers(topology_st.standalone, PROVISIONING_DN)
    cont_stage = containers_provisioning.create(properties={'cn': STAGE_CN})
    cont_delete = containers_provisioning.create(properties={'cn': DELETE_CN})

    def fin():
        log.info('Delete containers')
        cont_stage.delete()
        cont_delete.delete()
        cont_provisioning.delete()
        cont_active.delete()

    request.addfinalizer(fin)


@pytest.fixture(scope='function')
def attruniq(topology_st, request):
    log.info('Setup attribute uniqueness plugin')
    attruniq = AttributeUniquenessPlugin(topology_st.standalone, dn="cn=attruniq,cn=plugins,cn=config")
    attruniq.create(properties={'cn': 'attruniq'})
    attruniq.add_unique_attribute('cn')
    topology_st.standalone.restart()

    def fin():
        if attruniq.exists():
            attruniq.disable()
            attruniq.delete()

    request.addfinalizer(fin)

    return attruniq


@pytest.fixture(scope='function')
def active_user_1(topology_st, request):
    log.info('Create active user 1')
    active_users = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX,
                                rdn='cn={}'.format(ACTIVE_CN))
    if active_users.exists(ACTIVE_USER_1_CN):
        active_users.get(ACTIVE_USER_1_CN).delete()

    active_user_1 = active_users.create(properties={'cn': ACTIVE_USER_1_CN,
                                               'uid': ACTIVE_USER_1_CN,
                                               'sn': ACTIVE_USER_1_CN,
                                               'uidNumber': '1',
                                               'gidNumber': '11',
                                               'homeDirectory': f'/home/{ACTIVE_USER_1_CN}'})

    def fin():
        if active_user_1.exists():
            log.info('Delete active user 1')
            active_user_1.delete()

    request.addfinalizer(fin)

    return active_user_1


@pytest.fixture(scope='function')
def active_user_2(topology_st, request):
    log.info('Create active user 2')
    active_users = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX,
                                rdn='cn={}'.format(ACTIVE_CN))
    if active_users.exists(ACTIVE_USER_2_CN):
        active_users.get(ACTIVE_USER_2_CN).delete()

    active_user_2 = active_users.create(properties={'cn': ACTIVE_USER_2_CN,
                                               'uid': ACTIVE_USER_2_CN,
                                               'sn': ACTIVE_USER_2_CN,
                                               'uidNumber': '2',
                                               'gidNumber': '22',
                                               'homeDirectory': f'/home/{ACTIVE_USER_2_CN}'})

    def fin():
        if active_user_2.exists():
            log.info('Delete active user 2')
            active_user_2.delete()

    request.addfinalizer(fin)

    return active_user_2


@pytest.fixture(scope='function')
def stage_user_1(topology_st, request):
    log.info('Create stage user 1')
    stage_users = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX,
                               rdn=f'cn={STAGE_CN},cn={PROVISIONING_CN}')
    if stage_users.exists(STAGE_USER_1_CN):
        stage_users.get(STAGE_USER_1_CN).delete()

    stage_user_1 = stage_users.create(properties={'cn': STAGE_USER_1_CN,
                                              'uid': STAGE_USER_1_CN,
                                              'sn': STAGE_USER_1_CN,
                                              'uidNumber': '2',
                                              'gidNumber': '22',
                                              'homeDirectory': f'/home/{STAGE_USER_1_CN}'})

    def fin():
        if stage_user_1.exists():
            log.info('Delete stage user 1')
            stage_user_1.delete()

    request.addfinalizer(fin)

    return stage_user_1


@pytest.fixture(scope='function')
def stage_user_2(topology_st, request):
    log.info('Create stage user 1')
    stage_users = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX,
                               rdn=f'cn={STAGE_CN},cn={PROVISIONING_CN}')
    if stage_users.exists(STAGE_USER_2_CN):
        stage_users.get(STAGE_USER_2_CN).delete()

    stage_user_2 = stage_users.create(properties={'cn': STAGE_USER_2_CN,
                                              'uid': STAGE_USER_2_CN,
                                              'sn': STAGE_USER_2_CN,
                                              'uidNumber': '2',
                                              'gidNumber': '22',
                                              'homeDirectory': f'/home/{STAGE_USER_2_CN}'})

    def fin():
        if stage_user_2.exists():
            log.info('Delete stage user 2')
            stage_user_2.delete()

    request.addfinalizer(fin)

    return stage_user_2


def create_active_user(topology_st, uid=ACTIVE_USER_2_CN, cn=ACTIVE_USER_2_CN):
    active_users = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX,
                                rdn=f'cn={ACTIVE_CN}')
    return active_users.create(properties={'cn': cn,
                                        'uid': uid,
                                        'sn': uid,
                                        'uidNumber': '2',
                                        'gidNumber': '22',
                                        'homeDirectory': f'/home/{uid}'})


def create_stage_user(topology_st, uid=STAGE_USER_2_CN, cn=STAGE_USER_2_CN):
    stage_users = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX,
                                rdn=f'cn={STAGE_CN},cn={PROVISIONING_CN}')
    return stage_users.create(properties={'cn': cn,
                                        'uid': uid,
                                        'sn': uid,
                                        'uidNumber': '3',
                                        'gidNumber': '33',
                                        'homeDirectory': f'/home/{uid}'})


def test_modrdn_attr_uniqueness(topology_st, attruniq):
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

    user1.delete()
    user2.delete()


def test_multiple_attr_uniqueness(topology_st, attruniq):
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

    try:
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

    testuser1.delete()
    testuser2.delete()


def test_exclude_subtrees(topology_st, attruniq):
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

    try:
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

    finally:
        log.info('Clean up users, containers and attribute uniqueness plugin')
        user1.delete()
        user2.delete()
        user3.delete()
        user4.delete()
        cont1.delete()
        cont2.delete()
        cont3.delete()
        attruniq.disable()
        attruniq.delete()


def test_matchingrule_attr(topology_st):
    """ Test list extension MR attribute. Check for "cn" using CES (versus it
    being defined as CIS)

    :id: 5cde4342-6fa3-4225-b23d-0af918981075
    :setup: Standalone instance
    :steps:
        1. Setup and enable attribute uniqueness plugin to use CN attribute
           with a matching rule of CaseExactMatch.
        2. Add user with CN value is lowercase
        3. Add second user with same lowercase CN which should be rejected
        4. Add second user with same CN value but with mixed case
        5. Modify second user replacing CN value to lc which should be rejected
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
    """

    inst = topology_st.standalone

    attruniq = AttributeUniquenessPlugin(inst,
                                         dn="cn=attribute uniqueness,cn=plugins,cn=config")
    attruniq.add_unique_attribute('cn:CaseExactMatch:')
    attruniq.enable_all_subtrees()
    attruniq.enable()
    inst.restart()

    users = UserAccounts(inst, DEFAULT_SUFFIX)
    users.create(properties={'cn': "common_name",
                             'uid': "uid_name",
                             'sn': "uid_name",
                             'uidNumber': '1',
                             'gidNumber': '11',
                             'homeDirectory': '/home/uid_name'})

    log.info('Add entry with the exact CN value which should be rejected')
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        users.create(properties={'cn': "common_name",
                                 'uid': "uid_name2",
                                 'sn': "uid_name2",
                                 'uidNumber': '11',
                                 'gidNumber': '111',
                                 'homeDirectory': '/home/uid_name2'})

    log.info('Add entry with the mixed case CN value which should be allowed')
    user = users.create(properties={'cn': "Common_Name",
                                    'uid': "uid_name2",
                                    'sn': "uid_name2",
                                    'uidNumber': '11',
                                    'gidNumber': '111',
                                    'homeDirectory': '/home/uid_name2'})

    log.info('Mod entry with exact case CN value which should be rejected')
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        user.replace('cn', 'common_name')

    attruniq.disable()
    attruniq.delete()
    inst.restart()


def test_one_container_add(topology_st, attruniq, containers, active_user_1):
    """Test ADD operations with attribute uniqueness in a single container.

    This test verifies that the attribute uniqueness plugin correctly prevents
    the addition of entries with duplicate attribute values within a single
    container when the plugin is enabled.

    :id: 05960fb7-3ef8-4b53-b616-126f7ac737c4
    :setup: Standalone instance with containers and one active user
    :steps:
        1. Verify user with duplicate 'cn' attribute can be added
           without enforced uniqueness
        2. Remove the duplicate user
        3. Configure and enable attribute uniqueness plugin for 'cn' in active container
        4. Try to add user with duplicate 'cn' attribute (should fail)
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Duplicate entry creation fails with CONSTRAINT_VIOLATION
    """

    log.info('Verify that there is no uniqueness rule enforced')
    active_users = UserAccounts(topology_st.standalone, DEFAULT_SUFFIX,
                                rdn='cn={}'.format(ACTIVE_CN))
    active_2 = active_users.create(properties={'cn': [ACTIVE_USER_1_CN, ACTIVE_USER_2_CN],
                                               'uid': ACTIVE_USER_2_CN,
                                               'sn': ACTIVE_USER_2_CN,
                                               'uidNumber': '2',
                                               'gidNumber': '22',
                                               'homeDirectory': f'/home/{ACTIVE_USER_2_CN}'})

    log.info('Remove the second user')
    active_2.delete()

    log.info('Setup attribute uniqueness plugin for "cn" attribute')
    attruniq.add_unique_subtree(ACTIVE_DN)
    attruniq.enable()
    topology_st.standalone.restart()

    log.info('Verify that uniqueness rule is enforced')
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        active_users.create(properties={'cn': [ACTIVE_USER_1_CN, ACTIVE_USER_2_CN],
                                                   'uid': ACTIVE_USER_2_CN,
                                                   'sn': ACTIVE_USER_2_CN,
                                                   'uidNumber': '2',
                                                   'gidNumber': '22',
                                                   'homeDirectory': f'/home/{ACTIVE_USER_2_CN}'})


def test_one_container_mod(topology_st, attruniq, containers,
                           active_user_1, active_user_2):
    """Test MOD operations with attribute uniqueness in a single container.

    This test verifies that the attribute uniqueness plugin correctly prevents
    modification of entries that would create duplicate attribute values within
    a single container.

    :id: c566f279-6073-4fb7-8956-f30d1bdda8e2
    :setup: Standalone instance with containers and two active users
    :steps:
        1. Configure and enable attribute uniqueness plugin for "cn" attribute
        2. Try to modify user2's "cn" to match user1's "cn" (should fail)
    :expectedresults:
        1. Plugin configured and enabled successfully
        2. Modification fails with CONSTRAINT_VIOLATION
    """

    log.info('Setup attribute uniqueness plugin for "cn" attribute')
    attruniq.add_unique_subtree(ACTIVE_DN)
    attruniq.enable()
    topology_st.standalone.restart()

    log.info('Verify that uniqueness rule is enforced')
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        active_user_2.replace('cn', ACTIVE_USER_1_CN)


def test_one_container_modrdn(topology_st, attruniq, containers,
                              active_user_1, active_user_2):
    """Test MODRDN operations with attribute uniqueness in a single container.

    This test verifies that the attribute uniqueness plugin correctly prevents
    ModRDN operations that would create duplicate attribute values within
    a single container.

    :id: cabfec46-bea7-4175-913e-2f7ab84a2919
    :setup: Standalone instance with containers and two active users
    :steps:
        1. Configure and enable attribute uniqueness plugin for "cn" attribute
        2. Try to rename user2 to have the same "cn" as user1 (should fail)
    :expectedresults:
        1. Plugin configured and enabled successfully
        2. ModRDN operation fails with CONSTRAINT_VIOLATION
    """

    log.info('Setup attribute uniqueness plugin for "cn" attribute')
    attruniq.add_unique_subtree(ACTIVE_DN)
    attruniq.enable()
    topology_st.standalone.restart()

    log.info('Verify that uniqueness rule is enforced')
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        active_user_2.rename(f'cn={ACTIVE_USER_1_CN}', deloldrdn=False)


def test_multiple_containers_add(topology_st, attruniq, containers,
                                  active_user_1, stage_user_1):
    """Test ADD operations with attribute uniqueness across multiple containers.

    This test verifies that when multiple containers are configured for
    attribute uniqueness, the plugin enforces uniqueness within each container
    separately, but allows duplicate values across different containers.

    :id: 400cdf54-499a-4c23-91f5-835f44835e3e
    :setup: Standalone instance with containers and users in different containers
    :steps:
        1. Configure plugin for 'cn' attribute in both active and stage containers
        2. Try to add user with duplicate 'cn' in active container (should fail)
        3. Try to add user with duplicate 'cn' in stage container (should fail)
        4. Try to add user in active container with 'cn' matching stage user (should succeed)
        5. Try to add user in stage container with 'cn' matching active user (should succeed)
    :expectedresults:
        1. Plugin configured successfully
        2. Duplicate in active container fails with CONSTRAINT_VIOLATION
        3. Duplicate in stage container fails with CONSTRAINT_VIOLATION
        4. Cross-container duplicate in active succeeds
        5. Cross-container duplicate in stage succeeds
    """

    log.info('Setup attribute uniqueness plugin for "cn" attribute')
    attruniq.add_unique_subtree(ACTIVE_DN)
    attruniq.add_unique_subtree(STAGE_DN)
    attruniq.enable()
    topology_st.standalone.restart()

    log.info("Verify both subtrees apply attruniqueness separately")
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        create_active_user(topology_st, uid=ACTIVE_USER_2_CN, cn=[ACTIVE_USER_1_CN, ACTIVE_USER_2_CN])
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        create_stage_user(topology_st, uid=STAGE_USER_2_CN, cn=[STAGE_USER_1_CN, STAGE_USER_2_CN])

    log.info("Verify that the subtrees do not enforce uniqueness across each other")
    try:
        active_user_2 = create_active_user(topology_st, uid=ACTIVE_USER_2_CN,
                                           cn=[STAGE_USER_1_CN, ACTIVE_USER_2_CN])
        stage_user_2 = create_stage_user(topology_st, uid=STAGE_USER_2_CN,
                                         cn=[ACTIVE_USER_1_CN, STAGE_USER_2_CN])
    except:
        assert False, "Failed to create users necessary for the test"
    finally:
        active_user_2.delete()
        stage_user_2.delete()


def test_multiple_containers_add_across_subtrees(topology_st, attruniq, containers,
                           active_user_1, stage_user_1):
    """Test ADD operations with attribute uniqueness across multiple containers with cross-subtree enforcement.

    This test verifies that when the 'across-all-subtrees' option is enabled,
    the attribute uniqueness plugin enforces uniqueness across all configured
    containers, preventing duplicate values anywhere in the configured scope.

    :id: 7b58c46a-9ef1-4bbc-929d-578dbd3f4910
    :setup: Standalone instance with containers and users in different containers
    :steps:
        1. Configure plugin for 'cn' attribute in both containers with across-subtrees enabled
        2. Try to add user with duplicate 'cn' in active container (should fail)
        3. Try to add user with duplicate 'cn' in stage container (should fail)
        4. Try to add user in stage container with 'cn' matching active user (should fail)
        5. Try to add user in active container with 'cn' matching stage user (should fail)
    :expectedresults:
        1. Plugin configured successfully with across-subtrees enabled
        2. Duplicate in active container fails with CONSTRAINT_VIOLATION
        3. Duplicate in stage container fails with CONSTRAINT_VIOLATION
        4. Cross-container duplicate in stage fails with CONSTRAINT_VIOLATION
        5. Cross-container duplicate in active fails with CONSTRAINT_VIOLATION
    """

    log.info('Setup attribute uniqueness plugin for "cn" attribute')
    attruniq.add_unique_subtree(ACTIVE_DN)
    attruniq.add_unique_subtree(STAGE_DN)
    attruniq.enable_all_subtrees()
    attruniq.enable()
    topology_st.standalone.restart()

    # Inserting sleep to prevent repeatedly occuring 'Server is busy' errors
    sleep(5)

    log.info("Verify both subtrees apply attruniqueness separately")
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        create_active_user(topology_st, uid=ACTIVE_USER_2_CN, cn=[ACTIVE_USER_1_CN, ACTIVE_USER_2_CN])
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        create_stage_user(topology_st, uid=STAGE_USER_2_CN, cn=[STAGE_USER_1_CN, STAGE_USER_2_CN])

    log.info('Verify that uniqueness rule is enforced')
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        create_stage_user(topology_st, uid=STAGE_USER_2_CN, cn=[ACTIVE_USER_1_CN, STAGE_USER_2_CN])
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        create_active_user(topology_st, uid=ACTIVE_USER_2_CN, cn=[STAGE_USER_1_CN, ACTIVE_USER_2_CN])


def test_multiple_containers_mod(topology_st, attruniq, containers,
                           active_user_1, active_user_2, stage_user_1, stage_user_2):
    """Test MOD operations with attribute uniqueness across multiple containers.

    This test verifies that when multiple containers are configured for
    attribute uniqueness without cross-subtree enforcement, modifications
    are constrained within each container but allowed across containers.

    :id: 29ad0e41-bb6f-4ec0-bf79-0d6a4402a5db
    :setup: Standalone instance with containers and users in different containers
    :steps:
        1. Configure plugin for 'cn' attribute in both containers
        2. Try to modify user in active container to match another active user (should fail)
        3. Try to modify user in stage container to match another stage user (should fail)
        4. Try to modify user in active container to match stage user (should succeed)
        5. Try to modify user in stage container to match active user (should succeed)
    :expectedresults:
        1. Plugin configured successfully
        2. Intra-container modification fails with CONSTRAINT_VIOLATION
        3. Intra-container modification fails with CONSTRAINT_VIOLATION
        4. Cross-container modification succeeds
        5. Cross-container modification succeeds
    """

    log.info('Setup attribute uniqueness plugin for "cn" attribute')
    attruniq.add_unique_subtree(ACTIVE_DN)
    attruniq.add_unique_subtree(STAGE_DN)
    attruniq.enable()
    topology_st.standalone.restart()

    log.info('Verify that uniqueness is enforced in individual subtrees')
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        active_user_1.replace('cn', ACTIVE_USER_2_CN)
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        stage_user_1.replace('cn', STAGE_USER_2_CN)

    log.info('Verify that uniqueness rule is not enforced across subtrees')
    active_user_1.replace('cn', STAGE_USER_1_CN)
    stage_user_1.replace('cn', ACTIVE_USER_1_CN)


def test_multiple_containers_mod_across_subtrees(topology_st, attruniq, containers,
                           active_user_1, active_user_2, stage_user_1, stage_user_2):
    """Test MOD operations with attribute uniqueness across multiple containers with cross-subtree enforcement.

    This test verifies that when the 'across-all-subtrees' option is enabled,
    modification operations are constrained across all configured containers,
    preventing duplicate values anywhere in the configured scope.

    :id: f5272114-8869-4c61-96e2-0241500c47c2
    :setup: Standalone instance with containers and users in different containers
    :steps:
        1. Configure plugin for 'cn' attribute in both containers with across-subtrees enabled
        2. Try to modify user in active container to match another active user (should fail)
        3. Try to modify user in stage container to match another stage user (should fail)
        4. Try to modify user in active container to match stage user (should fail)
        5. Try to modify user in stage container to match active user (should fail)
    :expectedresults:
        1. Plugin configured successfully with across-subtrees enabled
        2. Intra-container modification fails with CONSTRAINT_VIOLATION
        3. Intra-container modification fails with CONSTRAINT_VIOLATION
        4. Cross-container modification fails with CONSTRAINT_VIOLATION
        5. Cross-container modification fails with CONSTRAINT_VIOLATION
    """

    log.info('Setup attribute uniqueness plugin for "cn" attribute')
    attruniq.add_unique_subtree(ACTIVE_DN)
    attruniq.add_unique_subtree(STAGE_DN)
    attruniq.enable_all_subtrees()
    attruniq.enable()
    topology_st.standalone.restart()

    log.info('Verify that uniqueness is enforced in individual subtrees')
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        active_user_1.replace('cn', ACTIVE_USER_2_CN)
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        stage_user_1.replace('cn', STAGE_USER_2_CN)

    log.info('Verify that uniqueness rule is enforced across subtrees')
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        active_user_1.replace('cn', STAGE_USER_1_CN)
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        stage_user_1.replace('cn', ACTIVE_USER_1_CN)


def test_multiple_containers_modrdn(topology_st, attruniq, containers,
                           active_user_1, active_user_2, stage_user_1, stage_user_2):
    """Test MODRDN operations with attribute uniqueness across multiple containers.

    This test verifies that when multiple containers are configured for
    attribute uniqueness without cross-subtree enforcement, ModRDN operations
    are constrained within each container but allowed across containers.

    :id: 4b835bfe-d116-418a-beb9-943e23b7cd2a
    :setup: Standalone instance with containers and users in different containers
    :steps:
        1. Configure plugin for 'cn' attribute in both containers
        2. Try to rename user in active container to match another active user (should fail)
        3. Try to rename user in stage container to match another stage user (should fail)
        4. Try to rename user in active container to match stage user (should succeed)
        5. Try to rename user in stage container to match active user (should succeed)
    :expectedresults:
        1. Plugin configured successfully
        2. Intra-container ModRDN fails with CONSTRAINT_VIOLATION
        3. Intra-container ModRDN fails with CONSTRAINT_VIOLATION
        4. Cross-container ModRDN succeeds
        5. Cross-container ModRDN succeeds
    """

    log.info('Setup attribute uniqueness plugin for "cn" attribute')
    attruniq.add_unique_subtree(ACTIVE_DN)
    attruniq.add_unique_subtree(STAGE_DN)
    attruniq.enable()
    topology_st.standalone.restart()

    log.info('Verify that uniqueness is enforced in individual subtrees')
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        active_user_1.rename(f'cn={ACTIVE_USER_2_CN}', deloldrdn=False)
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        stage_user_1.rename(f'cn={STAGE_USER_2_CN}', deloldrdn=False)

    log.info('Verify that uniqueness rule is not enforced across subtrees')
    active_user_2.rename(f'cn={STAGE_USER_1_CN}', deloldrdn=False)
    stage_user_2.rename(f'cn={ACTIVE_USER_1_CN}', deloldrdn=False)


def test_multiple_containers_modrdn_across_subtrees(topology_st, attruniq, containers,
                                        active_user_1, active_user_2, stage_user_1, stage_user_2):
        """Test MODRDN operations with attribute uniqueness across multiple containers with cross-subtree enforcement.

        This test verifies that when the 'across-all-subtrees' option is enabled,
        ModRDN operations are constrained across all configured containers,
        preventing duplicate values anywhere in the configured scope.

        :id: 8d2c4af9-1b45-4c72-9523-7f8b39c74e21
        :setup: Standalone instance with containers and users in different containers
        :steps:
            1. Configure plugin for 'cn' attribute in both containers with across-subtrees enabled
            2. Try to rename user in active container to match another active user (should fail)
            3. Try to rename user in stage container to match another stage user (should fail)
            4. Try to rename user in active container to match stage user (should fail)
            5. Try to rename user in stage container to match active user (should fail)
        :expectedresults:
            1. Plugin configured successfully with across-subtrees enabled
            2. Intra-container ModRDN fails with CONSTRAINT_VIOLATION
            3. Intra-container ModRDN fails with CONSTRAINT_VIOLATION
            4. Cross-container ModRDN fails with CONSTRAINT_VIOLATION
            5. Cross-container ModRDN fails with CONSTRAINT_VIOLATION
        """

        log.info('Setup attribute uniqueness plugin for "cn" attribute')
        attruniq.add_unique_subtree(ACTIVE_DN)
        attruniq.add_unique_subtree(STAGE_DN)
        attruniq.enable_all_subtrees()
        attruniq.enable()
        topology_st.standalone.restart()

        log.info('Verify that uniqueness is enforced in individual subtrees')
        with pytest.raises(ldap.CONSTRAINT_VIOLATION):
            active_user_1.rename(f'cn={ACTIVE_USER_2_CN}', deloldrdn=False)
        with pytest.raises(ldap.CONSTRAINT_VIOLATION):
            stage_user_1.rename(f'cn={STAGE_USER_2_CN}', deloldrdn=False)

        log.info('Verify that uniqueness rule is enforced across subtrees')
        with pytest.raises(ldap.CONSTRAINT_VIOLATION):
            active_user_2.rename(f'cn={STAGE_USER_1_CN}', deloldrdn=False)
        with pytest.raises(ldap.CONSTRAINT_VIOLATION):
            stage_user_2.rename(f'cn={ACTIVE_USER_1_CN}', deloldrdn=False)


def _config_file(topology_st, action='save'):
    dse_ldif = topology_st.standalone.confdir + '/dse.ldif'
    sav_file = topology_st.standalone.confdir + '/dse.ldif.ticket47823'
    if action == 'save':
        shutil.copy(dse_ldif, sav_file)
    else:
        shutil.copy(sav_file, dse_ldif)


def _pattern_errorlog(file, log_pattern):
    try:
        _pattern_errorlog.last_pos += 1
    except AttributeError:
        _pattern_errorlog.last_pos = 0

    found = None
    log.debug("_pattern_errorlog: start at offset %d" % _pattern_errorlog.last_pos)
    file.seek(_pattern_errorlog.last_pos)

    while True:
        line = file.readline()
        log.debug("_pattern_errorlog: [%d] %s" % (file.tell(), line))
        found = log_pattern.search(line)
        if ((line == '') or (found)):
            break

    log.debug("_pattern_errorlog: end at offset %d" % file.tell())
    _pattern_errorlog.last_pos = file.tell()
    return found


def test_invalid_config_missing_attr_name(topology_st):
    """Test that invalid plugin configuration is properly detected when attribute name is missing.

    This test verifies that the attribute uniqueness plugin correctly fails
    to start when the required attribute name configuration is missing,
    and that appropriate error messages are logged.

    :id: 2ff3aa56-304e-41b0-9614-eeb481ef6c55
    :setup: Standalone instance
    :steps:
        1. Save current configuration
        2. Create plugin configuration without uniqueness attribute name
        3. Enable the plugin
        4. Try to restart the server (should fail)
        5. Verify expected error message in logs
        6. Restore configuration and verify server can restart
    :expectedresults:
        1. Configuration saved successfully
        2. Invalid plugin configuration created
        3. Plugin enabled successfully
        4. Server restart fails with appropriate error
        5. Error message "Attribute name not defined" found in logs
        6. Server restarts successfully with restored configuration
    """

    _config_file(topology_st, action='save')

    attruniq = AttributeUniquenessPlugin(topology_st.standalone, dn="cn=attruniq,cn=plugins,cn=config")
    attruniq.create(properties={'cn': 'attruniq'})
    attruniq.enable()

    topology_st.standalone.errorlog_file = open(topology_st.standalone.errlog, "r")

    with pytest.raises(subprocess.CalledProcessError):
        log.info("Server is down as expected due to invalid config")
        topology_st.standalone.restart()

    log.info("Check the expected error message")
    regex = re.compile("[A|a]ttribute name not defined")
    res = _pattern_errorlog(topology_st.standalone.errorlog_file, regex)
    assert res

    log.info("Restore the configuration and verify the server can be restarted")
    _config_file(topology_st, action='restore')
    topology_st.standalone.restart()


def test_invalid_config_invalid_subtree(topology_st):
    """Test that invalid plugin configuration is properly detected when subtree is invalid.

    This test verifies that the attribute uniqueness plugin correctly fails
    to start when an invalid subtree DN is specified in the configuration,
    and that appropriate error messages are logged.

    :id: b7e51131-8542-4110-a90a-3e5c88f75355
    :setup: Standalone instance
    :steps:
        1. Save current configuration
        2. Create plugin configuration with invalid subtree DN
        3. Enable the plugin
        4. Try to restart the server (should fail)
        5. Verify expected error message in logs
        6. Restore configuration and verify server can restart
    :expectedresults:
        1. Configuration saved successfully
        2. Invalid plugin configuration created with bad subtree
        3. Plugin enabled successfully
        4. Server restart fails with appropriate error
        5. Error message "No valid subtree is defined" found in logs
        6. Server restarts successfully with restored configuration
    """

    _config_file(topology_st, action='save')

    attruniq = AttributeUniquenessPlugin(topology_st.standalone, dn="cn=attruniq,cn=plugins,cn=config")
    attruniq.create(properties={'cn': 'attruniq'})
    attruniq.add_unique_attribute('cn')
    attruniq.add_unique_subtree('invalid_subtree')
    attruniq.enable()

    topology_st.standalone.errorlog_file = open(topology_st.standalone.errlog, "r")

    with pytest.raises(subprocess.CalledProcessError):
        log.info("Server is down as expected due to invalid config")
        topology_st.standalone.restart()

    log.info("Check the expected error message")
    regex = re.compile("No valid subtree is defined")
    res = _pattern_errorlog(topology_st.standalone.errorlog_file, regex)
    assert res

    log.info("Restore the configuration and verify the server can be restarted")
    _config_file(topology_st, action='restore')
    topology_st.standalone.restart()
