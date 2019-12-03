# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import time

import ldap
import pytest
from lib389.utils import *
from lib389._constants import *
from lib389.pwpolicy import PwPolicyManager
from lib389.topologies import topology_st
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.idm.user import UserAccounts, TEST_USER_PROPERTIES

pytestmark = pytest.mark.tier1

logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

OU_PEOPLE = 'ou=People,' + DEFAULT_SUFFIX
ATTR_INHERIT_GLOBAL = 'nsslapd-pwpolicy-inherit-global'
ATTR_CHECK_SYNTAX = 'passwordCheckSyntax'

BN = 'uid=buser,' + OU_PEOPLE
TEMP_USER = 'cn=test{}'
TEMP_USER_DN = '%s,%s' % (TEMP_USER, OU_PEOPLE)


@pytest.fixture(scope="module")
def create_user(topology_st, request):
    """User for binding operation"""

    log.info('Adding user {}'.format(BN))

    users = UserAccounts(topology_st.standalone, OU_PEOPLE, rdn=None)
    user_props = TEST_USER_PROPERTIES.copy()
    user_props.update({'uid': 'buser', 'cn': 'buser', 'userpassword': PASSWORD})
    user = users.create(properties=user_props)

    log.info('Adding an aci for the bind user')
    BN_ACI = '(targetattr="*")(version 3.0; acl "pwp test"; allow (all) userdn="ldap:///%s";)' % user.dn
    ous = OrganizationalUnits(topology_st.standalone, DEFAULT_SUFFIX)
    ou_people = ous.get('people')
    ou_people.add('aci', BN_ACI)

    def fin():
        log.info('Deleting user {}'.format(BN))
        user.delete()
        ous = OrganizationalUnits(topology_st.standalone, DEFAULT_SUFFIX)
        ou_people = ous.get('people')
        ou_people.remove('aci', BN_ACI)

    request.addfinalizer(fin)


@pytest.fixture(scope="module")
def password_policy(topology_st, create_user):
    """Set global password policy.
    Then, set fine-grained subtree level password policy
    to ou=People with no password syntax.

    Note: do not touch nsslapd-pwpolicy-inherit-global -- off by default
    """

    log.info('Enable fine-grained policy')
    pwp = PwPolicyManager(topology_st.standalone)
    policy_props = {
        'passwordMustChange': 'off',
        'passwordExp': 'off',
        'passwordMinAge': '0',
        'passwordChange': 'off',
        'passwordStorageScheme': 'ssha'
    }
    pwp.create_subtree_policy(OU_PEOPLE, policy_props)
    check_attr_val(topology_st.standalone, ATTR_INHERIT_GLOBAL, 'off')
    check_attr_val(topology_st.standalone, ATTR_CHECK_SYNTAX, 'off')


def check_attr_val(inst, attr, expected):
    """Check that entry has the value"""

    val = inst.config.get_attr_val_utf8(attr)
    assert val == expected, 'Default value of %s is not %s, but %s' % (
            attr, expected, val)

    log.info('Default value of %s is %s' % (attr, expected))


@pytest.mark.parametrize('inherit_value,checksyntax_value',
                         [('off', 'off'), ('on', 'off'), ('off', 'on')])
def test_entry_has_no_restrictions(topology_st, password_policy, create_user,
                                   inherit_value, checksyntax_value):
    """Make sure an entry added to ou=people has no password syntax restrictions

    :id: 2f07ff40-76ca-45a9-a556-331c94084945
    :parametrized: yes
    :setup: Standalone instance, test user,
            password policy entries for a subtree
    :steps:
        1. Bind as test user
        2. Set 'nsslapd-pwpolicy-inherit-global' and
           'passwordCheckSyntax' accordingly:
           'off' and 'off'; 'on' and 'off'; 'off' and 'on'
        3. Try to add user with a short password
        4. Cleanup - remove temp user bound as DM
    :expectedresults:
        1. Bind should be successful
        2. Attributes should be successfully set
        3. No exceptions should occur
        4. Operation should be successful
    """

    log.info('Set {} to {}'.format(ATTR_INHERIT_GLOBAL, inherit_value))
    log.info('Set {} to {}'.format(ATTR_CHECK_SYNTAX, checksyntax_value))
    topology_st.standalone.config.set(ATTR_INHERIT_GLOBAL, inherit_value)
    topology_st.standalone.config.set(ATTR_CHECK_SYNTAX, checksyntax_value)

    # Wait a second for cn=config to apply
    time.sleep(1)
    check_attr_val(topology_st.standalone, ATTR_INHERIT_GLOBAL, inherit_value)
    check_attr_val(topology_st.standalone, ATTR_CHECK_SYNTAX, checksyntax_value)

    log.info('Bind as test user')
    topology_st.standalone.simple_bind_s(BN, PASSWORD)

    log.info('Make sure an entry added to ou=people has '
             'no password syntax restrictions.')

    users = UserAccounts(topology_st.standalone, OU_PEOPLE, rdn=None)
    user_props = TEST_USER_PROPERTIES.copy()
    user_props.update({'cn': 'test0', 'userpassword': 'short'})
    user = users.create(properties=user_props)

    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)

    # Remove test user
    user.delete()


def test_entry_has_restrictions(topology_st, password_policy, create_user):
    """Set 'nsslapd-pwpolicy-inherit-global: on' and 'passwordCheckSyntax: on'.
    Make sure that syntax rules work, if set them at both: cn=config and
    ou=people policy container.

    :id: 4bb0f474-17c1-40f7-aab4-4ddc17d019e8
    :setup: Standalone instance, test user,
            password policy entries for a subtree
    :steps:
        1. Bind as test user
        2. Switch 'nsslapd-pwpolicy-inherit-global: on'
        3. Switch 'passwordCheckSyntax: on'
        4. Set 'passwordMinLength: 9' to:
           cn=config and ou=people policy container
        5. Try to add user with a short password (<9)
        6. Try to add user with a long password (>9)
        7. Cleanup - remove temp users bound as DM
    :expectedresults:
        1. Bind should be successful
        2. nsslapd-pwpolicy-inherit-global should be successfully set
        3. passwordCheckSyntax should be successfully set
        4. passwordMinLength should be successfully set
        5. User should be rejected
        6. User should be rejected
        7. Operation should be successful
    """

    log.info('Set {} to {}'.format(ATTR_INHERIT_GLOBAL, 'on'))
    log.info('Set {} to {}'.format(ATTR_CHECK_SYNTAX, 'on'))
    topology_st.standalone.config.set(ATTR_INHERIT_GLOBAL, 'on')
    topology_st.standalone.config.set(ATTR_CHECK_SYNTAX, 'on')

    pwp = PwPolicyManager(topology_st.standalone)
    policy = pwp.get_pwpolicy_entry(OU_PEOPLE)
    policy.set('passwordMinLength', '9')

    # Wait a second for cn=config to apply
    time.sleep(1)
    check_attr_val(topology_st.standalone, ATTR_INHERIT_GLOBAL, 'on')
    check_attr_val(topology_st.standalone, ATTR_CHECK_SYNTAX, 'on')

    log.info('Bind as test user')
    topology_st.standalone.simple_bind_s(BN, PASSWORD)
    users = UserAccounts(topology_st.standalone, OU_PEOPLE, rdn=None)
    user_props = TEST_USER_PROPERTIES.copy()

    log.info('Try to add user with a short password (<9)')
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        user_props.update({'cn': 'test0', 'userpassword': 'short'})
        user = users.create(properties=user_props)

    log.info('Try to add user with a long password (>9)')
    user_props.update({'cn': 'test1', 'userpassword': 'Reallylong1'})
    user = users.create(properties=user_props)

    log.info('Bind as DM user')
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)

    # Remove test user 1
    user.delete()


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
