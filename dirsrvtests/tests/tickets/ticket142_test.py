# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import sys
import time
import ldap
import logging
import pytest
import shutil
from lib389 import DirSrv, Entry, tools
from lib389 import DirSrvTools
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *

log = logging.getLogger(__name__)

installation_prefix = None

CONFIG_DN = 'cn=config'
OU_PEOPLE = 'ou=People,' + DEFAULT_SUFFIX
PWP_CONTAINER = 'nsPwPolicyContainer'
PWP_CONTAINER_DN = 'cn=' + PWP_CONTAINER + ',' + OU_PEOPLE
PWP_ENTRY_DN = 'cn=nsPwPolicyEntry,' + OU_PEOPLE
PWP_TEMPLATE_ENTRY_DN = 'cn=nsPwTemplateEntry,' + OU_PEOPLE
ATTR_INHERIT_GLOBAL = 'nsslapd-pwpolicy-inherit-global'

BN = 'uid=buser,' + DEFAULT_SUFFIX


class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):
    '''
    This fixture is used to standalone topology for the 'module'.
    '''
    global installation_prefix

    if installation_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation_prefix

    standalone = DirSrv(verbose=False)

    # Args for the standalone instance
    args_instance[SER_HOST] = HOST_STANDALONE
    args_instance[SER_PORT] = PORT_STANDALONE
    args_instance[SER_SERVERID_PROP] = SERVERID_STANDALONE
    args_standalone = args_instance.copy()
    standalone.allocate(args_standalone)

    # Get the status of the instance and restart it if it exists
    instance_standalone = standalone.exists()

    # Remove the instance
    if instance_standalone:
        standalone.delete()

    # Create the instance
    standalone.create()

    # Used to retrieve configuration information (dbdir, confdir...)
    standalone.open()

    def fin():
        standalone.delete()
    request.addfinalizer(fin)

    # Here we have standalone instance up and running
    return TopologyStandalone(standalone)


def _header(topology, label):
    topology.standalone.log.info("###############################################")
    topology.standalone.log.info("####### %s" % label)
    topology.standalone.log.info("###############################################")


def check_attr_val(topology, dn, attr, expected):
    try:
        centry = topology.standalone.search_s(dn, ldap.SCOPE_BASE, 'cn=*')
        if centry:
            val = centry[0].getValue(attr)
            if val == expected:
                log.info('Default value of %s is %s' % (attr, expected))
            else:
                log.info('Default value of %s is not %s, but %s' % (attr, expected, val))
                assert False
        else:
            log.fatal('Failed to get %s' % dn)
            assert False
    except ldap.LDAPError as e:
        log.fatal('Failed to search ' + dn + ': ' + e.message['desc'])
        assert False


def _142_init(topology):
    """
    Set global password policy.
    Then, set fine-grained subtree level password policy to ou=People with no password syntax.
    Note: do not touch nsslapd-pwpolicy-inherit-global -- off by default
    Also, adding an ordinary bind user.
    """
    _header(topology, 'Testing Ticket 142 - Default password syntax settings do not work with fine-grained policies')

    log.info("Setting global password policy with password syntax.")
    topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'nsslapd-pwpolicy-local', 'on')])

    log.info("Setting fine-grained password policy.")
    topology.standalone.add_s(Entry((PWP_CONTAINER_DN, {
                                     'objectclass': "top nsContainer".split()})))
    topology.standalone.add_s(Entry(('cn="%s",%s' % (PWP_ENTRY_DN, PWP_CONTAINER_DN), {
                                     'objectclass': "top ldapsubentry passwordpolicy".split()})))
    topology.standalone.add_s(Entry(('cn="%s",%s' % (PWP_TEMPLATE_ENTRY_DN, PWP_CONTAINER_DN), {
                                     'objectclass': "top ldapsubentry costemplate".split(),
                                     'pwdpolicysubentry': 'cn="%s",%s' % (PWP_ENTRY_DN, PWP_CONTAINER_DN)})))
    topology.standalone.add_s(Entry(('cn=nsPwPolicy_CoS,%s' % OU_PEOPLE, {
                                     'objectclass': "top ldapsubentry cosSuperDefinition cosPointerDefinition".split(),
                                     'cosTemplateDn': 'cn="%s",%s' % (PWP_TEMPLATE_ENTRY_DN, PWP_CONTAINER_DN),
                                     'cosAttribute': 'pwdpolicysubentry default operational-default'})))

    log.info("    with the default settings.")
    topology.standalone.modify_s('cn="%s",%s' % (PWP_ENTRY_DN, PWP_CONTAINER_DN),
                                 [(ldap.MOD_REPLACE, 'passwordMustChange', 'off'),
                                  (ldap.MOD_REPLACE, 'passwordExp', 'off'),
                                  (ldap.MOD_REPLACE, 'passwordMinAge', '0'),
                                  (ldap.MOD_REPLACE, 'passwordChange', 'off'),
                                  (ldap.MOD_REPLACE, 'passwordStorageScheme', 'ssha')])

    check_attr_val(topology, CONFIG_DN, ATTR_INHERIT_GLOBAL, 'off')
    check_attr_val(topology, CONFIG_DN, 'passwordCheckSyntax', 'off')

    log.info('Adding a bind user.')
    topology.standalone.add_s(Entry((BN,
                                     {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                      'cn': 'bind user',
                                      'sn': 'bind user',
                                      'userPassword': PASSWORD})))

    log.info('Adding an aci for the bind user.')
    topology.standalone.modify_s(OU_PEOPLE,
                                 [(ldap.MOD_ADD,
                                   'aci',
                                   '(targetattr="*")(version 3.0; acl "pwp test"; allow (all) userdn="ldap:///%s";)' % BN)])


def _142_run_0(topology):
    """
    Make sure an entry added to ou=people has no password syntax restrictions.
    """
    _header(topology, 'Case 0 - Make sure an entry added to ou=people has no password syntax restrictions.')

    topology.standalone.simple_bind_s(BN, PASSWORD)
    try:
        topology.standalone.add_s(Entry(('cn=test0,%s' % OU_PEOPLE,
                                         {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                          'cn': 'test0',
                                          'sn': 'test0',
                                          'userPassword': 'short'})))
    except ldap.LDAPError as e:
        log.fatal('Failed to add cn=test0 with userPassword: short: ' + e.message['desc'])
        assert False

    log.info('PASSED')


def _142_run_1(topology):
    """
    Set 'nsslapd-pwpolicy-inherit-global: on'
    But passwordCheckSyntax is still off.
    Make sure an entry added to ou=people has the global password syntax restrictions.
    """
    _header(topology, 'Case 1 - Make sure an entry added to ou=people has no password syntax restrictions.')

    topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, ATTR_INHERIT_GLOBAL, 'on')])
    check_attr_val(topology, CONFIG_DN, ATTR_INHERIT_GLOBAL, 'on')
    check_attr_val(topology, CONFIG_DN, 'passwordCheckSyntax', 'off')
    topology.standalone.simple_bind_s(BN, PASSWORD)
    try:
        topology.standalone.add_s(Entry(('cn=test1,%s' % OU_PEOPLE,
                                         {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                          'cn': 'test1',
                                          'sn': 'test1',
                                          'userPassword': 'short'})))
    except ldap.LDAPError as e:
        log.fatal('Failed to add cn=test1 with userPassword: short: ' + e.message['desc'])
        assert False

    log.info('PASSED')


def _142_run_2(topology):
    """
    Set 'passwordCheckSyntax: on'
    Set 'passwordMinLength: 9' for testing
    Make sure an entry added to ou=people has the global password syntax restrictions.
    """
    _header(topology, 'Case 2 - Make sure an entry added to ou=people has the global password syntax restrictions.')

    topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology.standalone.modify_s(CONFIG_DN,
                                 [(ldap.MOD_REPLACE, 'passwordCheckSyntax', 'on'),
                                  (ldap.MOD_REPLACE, 'passwordMinLength', '9')])
    check_attr_val(topology, CONFIG_DN, ATTR_INHERIT_GLOBAL, 'on')
    check_attr_val(topology, CONFIG_DN, 'passwordCheckSyntax', 'on')
    topology.standalone.simple_bind_s(BN, PASSWORD)
    failed_as_expected = False
    try:
        topology.standalone.add_s(Entry(('cn=test2,%s' % OU_PEOPLE,
                                         {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                          'cn': 'test2',
                                          'sn': 'test2',
                                          'userPassword': 'Abcd2345'})))
    except ldap.LDAPError as e:
        log.info('Adding cn=test2 with "userPassword: Abcd2345" was expectedly rejected: ' + e.message['desc'])
        failed_as_expected = True

    if not failed_as_expected:
        log.fatal('Adding cn=test2 with "userPassword: Abcd2345" was unexpectedly successful despite of short password.')
        assert False

    try:
        topology.standalone.add_s(Entry(('cn=test2,%s' % OU_PEOPLE,
                                         {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                          'cn': 'test2',
                                          'sn': 'test2',
                                          'userPassword': 'Abcd23456'})))
    except ldap.LDAPError as e:
        log.fatal('Adding cn=test2 with "userPassword: Abcd23456" failed: ' + e.message['desc'])
        assert False

    log.info('PASSED')


def _142_run_3(topology):
    """
    Set 'passwordCheckSyntax: on'
    Set 'nsslapd-pwpolicy-inherit-global: off'
    Make sure an entry added to ou=people has no syntax restrictions.
    """
    _header(topology, 'Case 3 - Make sure an entry added to ou=people has no password syntax restrictions.')

    topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology.standalone.modify_s(CONFIG_DN,
                                 [(ldap.MOD_REPLACE, ATTR_INHERIT_GLOBAL, 'off')])
    check_attr_val(topology, CONFIG_DN, ATTR_INHERIT_GLOBAL, 'off')
    check_attr_val(topology, CONFIG_DN, 'passwordCheckSyntax', 'on')
    topology.standalone.simple_bind_s(BN, PASSWORD)
    try:
        topology.standalone.add_s(Entry(('cn=test3,%s' % OU_PEOPLE,
                                         {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                          'cn': 'test3',
                                          'sn': 'test3',
                                          'userPassword': 'Abcd3456'})))
    except ldap.LDAPError as e:
        log.fatal('Adding cn=test3 with "userPassword: Abcd3456" failed: ' + e.message['desc'])
        assert False

    log.info('PASSED')


def _142_run_4(topology):
    """
    Set 'passwordCheckSyntax: on'
    Set 'nsslapd-pwpolicy-inherit-global: on'
    Set password syntax to fine-grained password policy to check it overrides the global settings.
    """
    _header(topology, 'Case 4 - Make sure an entry added to ou=people follows the fine-grained password syntax restrictions.')

    topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology.standalone.modify_s(CONFIG_DN,
                                 [(ldap.MOD_REPLACE, ATTR_INHERIT_GLOBAL, 'on')])
    check_attr_val(topology, CONFIG_DN, ATTR_INHERIT_GLOBAL, 'on')
    check_attr_val(topology, CONFIG_DN, 'passwordCheckSyntax', 'on')
    topology.standalone.modify_s('cn="%s",%s' % (PWP_ENTRY_DN, PWP_CONTAINER_DN),
                                 [(ldap.MOD_REPLACE, 'passwordMinLength', '5'),
                                  (ldap.MOD_REPLACE, 'passwordMinCategories', '2')])
    try:
        topology.standalone.add_s(Entry(('cn=test4,%s' % OU_PEOPLE,
                                         {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                          'cn': 'test4',
                                          'sn': 'test4',
                                          'userPassword': 'Abcd4'})))
    except ldap.LDAPError as e:
        log.fatal('Adding cn=test4 with "userPassword: Abcd4" failed: ' + e.message['desc'])
        assert False

    log.info('PASSED')


def test_ticket142(topology):
    '''
        run_isolated is used to run these test cases independently of a test scheduler (xunit, py.test..)
        To run isolated without py.test, you need to
            - edit this file and comment '@pytest.fixture' line before 'topology' function.
            - set the installation prefix
            - run this program
    '''
    global installation_prefix
    installation_prefix = None

    _142_init(topology)

    _142_run_0(topology)
    _142_run_1(topology)
    _142_run_2(topology)
    _142_run_3(topology)
    _142_run_4(topology)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
