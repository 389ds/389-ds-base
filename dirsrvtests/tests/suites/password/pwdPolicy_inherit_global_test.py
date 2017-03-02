# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import subprocess
import time

import ldap
import pytest
from lib389 import Entry
from lib389._constants import *
from lib389.topologies import topology_st

logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

CONFIG_DN = 'cn=config'
OU_PEOPLE = 'ou=People,' + DEFAULT_SUFFIX
PWP_CONTAINER = 'nsPwPolicyContainer'
PWP_CONTAINER_DN = 'cn=' + PWP_CONTAINER + ',' + OU_PEOPLE
PWP_ENTRY_DN = 'cn=nsPwPolicyEntry,' + OU_PEOPLE
PWP_CONTAINER_PEOPLE = 'cn="%s",%s' % (PWP_ENTRY_DN, PWP_CONTAINER_DN)
PWP_TEMPLATE_ENTRY_DN = 'cn=nsPwTemplateEntry,' + OU_PEOPLE
ATTR_INHERIT_GLOBAL = 'nsslapd-pwpolicy-inherit-global'
ATTR_CHECK_SYNTAX = 'passwordCheckSyntax'

BN = 'uid=buser,' + DEFAULT_SUFFIX
TEMP_USER = 'cn=test{}'
TEMP_USER_DN = '%s,%s' % (TEMP_USER, OU_PEOPLE)


@pytest.fixture(scope="module")
def test_user(topology_st, request):
    """User for binding operation"""

    log.info('Adding user {}'.format(BN))
    try:
        topology_st.standalone.add_s(Entry((BN,
                                            {'objectclass': ['top',
                                                             'person',
                                                             'organizationalPerson',
                                                             'inetOrgPerson'],
                                             'cn': 'bind user',
                                             'sn': 'bind user',
                                             'userPassword': PASSWORD})))
        log.info('Adding an aci for the bind user')
        BN_ACI = '(targetattr="*")(version 3.0; acl "pwp test"; allow (all) userdn="ldap:///%s";)' % BN
        topology_st.standalone.modify_s(OU_PEOPLE, [(ldap.MOD_ADD, 'aci', BN_ACI)])

    except ldap.LDAPError as e:
        log.error('Failed to add user (%s): error (%s)' % (BN,
                                                           e.message['desc']))
        raise e

    def fin():
        log.info('Deleting user {}'.format(BN))
        topology_st.standalone.delete_s(BN)
        topology_st.standalone.modify_s(OU_PEOPLE, [(ldap.MOD_DELETE, 'aci', BN_ACI)])

    request.addfinalizer(fin)


@pytest.fixture(scope="module")
def password_policy(topology_st, test_user):
    """Set global password policy.
    Then, set fine-grained subtree level password policy
    to ou=People with no password syntax.

    Note: do not touch nsslapd-pwpolicy-inherit-global -- off by default
    """

    log.info('Enable fine-grained policy')
    try:
        topology_st.standalone.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE,
                                                     'nsslapd-pwpolicy-local',
                                                     'on')])
    except ldap.LDAPError as e:
        log.error('Failed to set fine-grained policy: error {}'.format(
            e.message['desc']))
        raise e

    log.info('Create password policy for subtree {}'.format(OU_PEOPLE))
    try:
        subprocess.call(['%s/ns-newpwpolicy.pl' % topology_st.standalone.get_sbin_dir(),
                         '-D', DN_DM, '-w', PASSWORD,
                         '-p', str(PORT_STANDALONE), '-h', HOST_STANDALONE,
                         '-S', OU_PEOPLE, '-Z', SERVERID_STANDALONE])
    except subprocess.CalledProcessError as e:
        log.error('Failed to create pw policy policy for {}: error {}'.format(
            OU_PEOPLE, e.message['desc']))
        raise e

    log.info('Add pwdpolicysubentry attribute to {}'.format(OU_PEOPLE))
    try:
        topology_st.standalone.modify_s(OU_PEOPLE, [(ldap.MOD_REPLACE,
                                                     'pwdpolicysubentry',
                                                     PWP_CONTAINER_PEOPLE)])
    except ldap.LDAPError as e:
        log.error('Failed to pwdpolicysubentry pw policy ' \
                  'policy for {}: error {}'.format(OU_PEOPLE,
                                                   e.message['desc']))
        raise e

    log.info("Set the default settings for the policy container.")
    topology_st.standalone.modify_s(PWP_CONTAINER_PEOPLE,
                                    [(ldap.MOD_REPLACE, 'passwordMustChange', 'off'),
                                     (ldap.MOD_REPLACE, 'passwordExp', 'off'),
                                     (ldap.MOD_REPLACE, 'passwordMinAge', '0'),
                                     (ldap.MOD_REPLACE, 'passwordChange', 'off'),
                                     (ldap.MOD_REPLACE, 'passwordStorageScheme', 'ssha')])

    check_attr_val(topology_st, CONFIG_DN, ATTR_INHERIT_GLOBAL, 'off')
    check_attr_val(topology_st, CONFIG_DN, ATTR_CHECK_SYNTAX, 'off')


def check_attr_val(topology_st, dn, attr, expected):
    """Check that entry has the value"""

    try:
        centry = topology_st.standalone.search_s(dn, ldap.SCOPE_BASE, 'cn=*')
        assert centry[0], 'Failed to get %s' % dn

        val = centry[0].getValue(attr)
        assert val == expected, 'Default value of %s is not %s, but %s' % (
            attr, expected, val)

        log.info('Default value of %s is %s' % (attr, expected))
    except ldap.LDAPError as e:
        log.fatal('Failed to search ' + dn + ': ' + e.message['desc'])
        raise e


@pytest.mark.parametrize('inherit_value,checksyntax_value',
                         [('off', 'off'), ('on', 'off'), ('off', 'on')])
def test_entry_has_no_restrictions(topology_st, password_policy, test_user,
                                   inherit_value, checksyntax_value):
    """Make sure an entry added to ou=people
    has no password syntax restrictions when:
    - 'passwordCheckSyntax' is 'off' for 'nsslapd-pwpolicy-inherit-global'
    equaled 'off' and 'on'
    - 'passwordCheckSyntax' is 'on' for  'nsslapd-pwpolicy-inherit-global'
    equaled 'off'

    :ID: 2f07ff40-76ca-45a9-a556-331c94084945
    :feature: Password policy
    :setup: Standalone instance, test user,
            password policy entries for a subtree
    :steps: 1. Bind as test user
            2. Set 'nsslapd-pwpolicy-inherit-global' and
               'passwordCheckSyntax' accordingly:
               a) 'off' and 'off'
               b) 'on' and 'off'
               c) 'off' and 'on'
            3. Try to add user with a short password
    :assert: No exception should occure
    """

    log.info('Set {} to {}'.format(ATTR_INHERIT_GLOBAL, inherit_value))
    log.info('Set {} to {}'.format(ATTR_CHECK_SYNTAX, checksyntax_value))
    topology_st.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE,
                                                 ATTR_INHERIT_GLOBAL, inherit_value)])
    topology_st.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE,
                                                 ATTR_CHECK_SYNTAX, checksyntax_value)])

    # Wait a second for cn=config to apply
    time.sleep(1)
    check_attr_val(topology_st, CONFIG_DN, ATTR_INHERIT_GLOBAL, inherit_value)
    check_attr_val(topology_st, CONFIG_DN, ATTR_CHECK_SYNTAX, checksyntax_value)

    log.info('Bind as test user')
    topology_st.standalone.simple_bind_s(BN, PASSWORD)

    log.info('Make sure an entry added to ou=people has '
             'no password syntax restrictions.')
    try:
        topology_st.standalone.add_s(Entry((TEMP_USER_DN.format('0'),
                                            {'objectclass': ['top',
                                                             'person',
                                                             'organizationalPerson',
                                                             'inetOrgPerson'],
                                             'cn': TEMP_USER.format('0'),
                                             'sn': TEMP_USER.format('0'),
                                             'userPassword': 'short'})))
    except ldap.LDAPError as e:
        log.fatal('Failed to add cn=test0 with userPassword: short: ' +
                  e.message['desc'])
        raise e
    finally:
        log.info('Bind as DM user')
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
        log.info('Remove {}'.format(TEMP_USER_DN.format('0')))
        try:
            topology_st.standalone.delete_s(TEMP_USER_DN.format('0'))
        except ldap.NO_SUCH_OBJECT as e:
            log.fatal('There is no {}, it is a problem'.format(TEMP_USER_DN.format('0')))
            raise e


@pytest.mark.parametrize('container', [DN_CONFIG, PWP_CONTAINER_PEOPLE])
def test_entry_has_restrictions(topology_st, password_policy, test_user, container):
    """Set 'nsslapd-pwpolicy-inherit-global: on'
    and 'passwordCheckSyntax: on'. Make sure that
    syntax rules work, if set them at both: cn=config and
    ou=people policy container.

    :ID: 4bb0f474-17c1-40f7-aab4-4ddc17d019e8
    :feature: Password policy
    :setup: Standalone instance, test user,
            password policy entries for a subtree
    :steps: 1. Bind as test user
            2. Switch 'nsslapd-pwpolicy-inherit-global: on'
            3. Switch 'passwordCheckSyntax: on'
            4. Set 'passwordMinLength: 9' to:
               a) cn=config
               b) ou=people policy container
            5. Try to add user with a short password (<9)
            6. Try to add user with a long password (>9)
    :assert: User should be rejected
    """

    log.info('Set {} to {}'.format(ATTR_INHERIT_GLOBAL, 'on'))
    log.info('Set {} to {}'.format(ATTR_CHECK_SYNTAX, 'on'))
    topology_st.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE,
                                                 ATTR_INHERIT_GLOBAL, 'on')])
    topology_st.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE,
                                                 ATTR_CHECK_SYNTAX, 'on')])
    topology_st.standalone.modify_s(container, [(ldap.MOD_REPLACE,
                                                 'passwordMinLength', '9')])

    # Wait a second for cn=config to apply
    time.sleep(1)
    check_attr_val(topology_st, CONFIG_DN, ATTR_INHERIT_GLOBAL, 'on')
    check_attr_val(topology_st, CONFIG_DN, ATTR_CHECK_SYNTAX, 'on')

    log.info('Bind as test user')
    topology_st.standalone.simple_bind_s(BN, PASSWORD)

    log.info('Try to add user with a short password (<9)')
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        topology_st.standalone.add_s(Entry((TEMP_USER_DN.format('0'),
                                            {'objectclass': ['top',
                                                             'person',
                                                             'organizationalPerson',
                                                             'inetOrgPerson'],
                                             'cn': TEMP_USER.format('0'),
                                             'sn': TEMP_USER.format('0'),
                                             'userPassword': 'short'})))

    log.info('Try to add user with a long password (>9)')
    try:
        topology_st.standalone.add_s(Entry((TEMP_USER_DN.format('1'),
                                            {'objectclass': ['top',
                                                             'person',
                                                             'organizationalPerson',
                                                             'inetOrgPerson'],
                                             'cn': TEMP_USER.format('1'),
                                             'sn': TEMP_USER.format('1'),
                                             'userPassword': 'Reallylong1'})))
    except ldap.LDAPError as e:
        log.fatal('Failed to add cn=test1 with userPassword: short: '
                  + e.message['desc'])
        raise e
    finally:
        log.info('Bind as DM user')
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
        log.info('Remove {}'.format(TEMP_USER_DN.format('0')))
        try:
            topology_st.standalone.delete_s(TEMP_USER_DN.format('0'))
        except ldap.NO_SUCH_OBJECT as e:
            log.info('There is no {}, it is okay'.format(TEMP_USER_DN.format('0')))
        try:
            topology_st.standalone.delete_s(TEMP_USER_DN.format('1'))
        except ldap.NO_SUCH_OBJECT as e:
            log.fatal('There is no {}, it is a problem'.format(TEMP_USER_DN.format('1')))
            raise e


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
