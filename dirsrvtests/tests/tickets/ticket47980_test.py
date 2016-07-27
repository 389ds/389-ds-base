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
import ldap.sasl
import logging
import pytest
from lib389 import DirSrv, Entry, tools, tasks
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *

log = logging.getLogger(__name__)

installation_prefix = None

BRANCH1 = 'ou=level1,' + DEFAULT_SUFFIX
BRANCH2 = 'ou=level2,ou=level1,' + DEFAULT_SUFFIX
BRANCH3 = 'ou=level3,ou=level2,ou=level1,' + DEFAULT_SUFFIX
BRANCH4 = 'ou=people,' + DEFAULT_SUFFIX
BRANCH5 = 'ou=lower,ou=people,' + DEFAULT_SUFFIX
BRANCH6 = 'ou=lower,ou=lower,ou=people,' + DEFAULT_SUFFIX
USER1_DN = 'uid=user1,%s' % (BRANCH1)
USER2_DN = 'uid=user2,%s' % (BRANCH2)
USER3_DN = 'uid=user3,%s' % (BRANCH3)
USER4_DN = 'uid=user4,%s' % (BRANCH4)
USER5_DN = 'uid=user5,%s' % (BRANCH5)
USER6_DN = 'uid=user6,%s' % (BRANCH6)

BRANCH1_CONTAINER = 'cn=nsPwPolicyContainer,ou=level1,dc=example,dc=com'
BRANCH1_PWP = 'cn=cn\3DnsPwPolicyEntry\2Cou\3Dlevel1\2Cdc\3Dexample\2Cdc\3Dcom,' + \
                  'cn=nsPwPolicyContainer,ou=level1,dc=example,dc=com'
BRANCH1_COS_TMPL = 'cn=cn\3DnsPwTemplateEntry\2Cou\3Dlevel1\2Cdc\3Dexample\2Cdc\3Dcom,' + \
                  'cn=nsPwPolicyContainer,ou=level1,dc=example,dc=com'
BRANCH1_COS_DEF = 'cn=nsPwPolicy_CoS,ou=level1,dc=example,dc=com'

BRANCH2_CONTAINER = 'cn=nsPwPolicyContainer,ou=level2,ou=level1,dc=example,dc=com'
BRANCH2_PWP = 'cn=cn\3DnsPwPolicyEntry\2Cou\3Dlevel2\2Cou\3Dlevel1\2Cdc\3Dexample\2Cdc\3Dcom,' + \
             'cn=nsPwPolicyContainer,ou=level2,ou=level1,dc=example,dc=com'
BRANCH2_COS_TMPL = 'cn=cn\3DnsPwTemplateEntry\2Cou\3Dlevel2\2Cou\3Dlevel1\2Cdc\3Dexample\2Cdc\3Dcom,' + \
                  'cn=nsPwPolicyContainer,ou=level2,ou=level1,dc=example,dc=com'
BRANCH2_COS_DEF = 'cn=nsPwPolicy_CoS,ou=level2,ou=level1,dc=example,dc=com'

BRANCH3_CONTAINER = 'cn=nsPwPolicyContainer,ou=level3,ou=level2,ou=level1,dc=example,dc=com'
BRANCH3_PWP = 'cn=cn\3DnsPwPolicyEntry\2Cou\3Dlevel3\2Cou\3Dlevel2\2Cou\3Dlevel1\2Cdc\3Dexample\2Cdc\3Dcom,' + \
             'cn=nsPwPolicyContainer,ou=level3,ou=level2,ou=level1,dc=example,dc=com'
BRANCH3_COS_TMPL = 'cn=cn\3DnsPwTemplateEntry\2Cou\3Dlevel3\2Cou\3Dlevel2\2Cou\3Dlevel1\2Cdc\3Dexample\2Cdc\3Dcom,' + \
                  'cn=nsPwPolicyContainer,ou=level3,ou=level2,ou=level1,dc=example,dc=com'
BRANCH3_COS_DEF = 'cn=nsPwPolicy_CoS,ou=level3,ou=level2,ou=level1,dc=example,dc=com'

BRANCH4_CONTAINER = 'cn=nsPwPolicyContainer,ou=people,dc=example,dc=com'
BRANCH4_PWP = 'cn=cn\3DnsPwPolicyEntry\2Cou\3DPeople\2Cdc\3Dexample\2Cdc\3Dcom,' + \
             'cn=nsPwPolicyContainer,ou=People,dc=example,dc=com'
BRANCH4_COS_TMPL = 'cn=cn\3DnsPwTemplateEntry\2Cou\3DPeople\2Cdc\3Dexample\2Cdc\3Dcom,' + \
                  'cn=nsPwPolicyContainer,ou=People,dc=example,dc=com'
BRANCH4_COS_DEF = 'cn=nsPwPolicy_CoS,ou=people,dc=example,dc=com'

BRANCH5_CONTAINER = 'cn=nsPwPolicyContainer,ou=lower,ou=people,dc=example,dc=com'
BRANCH5_PWP = 'cn=cn\3DnsPwPolicyEntry\2Cou\3Dlower\2Cou\3DPeople\2Cdc\3Dexample\2Cdc\3Dcom,' + \
             'cn=nsPwPolicyContainer,ou=lower,ou=People,dc=example,dc=com'
BRANCH5_COS_TMPL = 'cn=cn\3DnsPwTemplateEntry\2Cou\3Dlower\2Cou\3DPeople\2Cdc\3Dexample\2Cdc\3Dcom,' + \
                  'cn=nsPwPolicyContainer,ou=lower,ou=People,dc=example,dc=com'
BRANCH5_COS_DEF = 'cn=nsPwPolicy_CoS,ou=lower,ou=People,dc=example,dc=com'

BRANCH6_CONTAINER = 'cn=nsPwPolicyContainer,ou=lower,ou=lower,ou=People,dc=example,dc=com'
BRANCH6_PWP = 'cn=cn\3DnsPwPolicyEntry\2Cou\3Dlower\2Cou\3Dlower\2Cou\3DPeople\2Cdc\3Dexample\2Cdc\3Dcom,' + \
             'cn=nsPwPolicyContainer,ou=lower,ou=lower,ou=People,dc=example,dc=com'
BRANCH6_COS_TMPL = 'cn=cn\3DnsPwTemplateEntry\2Cou\3Dlower\2Cou\3Dlower\2Cou\3DPeople\2Cdc\3Dexample\2Cdc\3Dcom,' + \
                  'cn=nsPwPolicyContainer,ou=lower,ou=lower,ou=People,dc=example,dc=com'
BRANCH6_COS_DEF = 'cn=nsPwPolicy_CoS,ou=lower,ou=lower,ou=People,dc=example,dc=com'


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


def test_ticket47980(topology):
    """
        Multiple COS pointer definitions that use the same attribute are not correctly ordered.
        The cos plugin was incorrectly sorting the attribute indexes based on subtree, which lead
        to the wrong cos attribute value being applied to the entry.
    """

    log.info('Testing Ticket 47980 - Testing multiple nested COS pointer definitions are processed correctly')

    # Add our nested branches
    try:
        topology.standalone.add_s(Entry((BRANCH1, {
                          'objectclass': 'top extensibleObject'.split(),
                          'ou': 'level1'
                          })))
    except ldap.LDAPError as e:
        log.error('Failed to add level1: error ' + e.message['desc'])
        assert False

    try:
        topology.standalone.add_s(Entry((BRANCH2, {
                          'objectclass': 'top extensibleObject'.split(),
                          'ou': 'level2'
                          })))
    except ldap.LDAPError as e:
        log.error('Failed to add level2: error ' + e.message['desc'])
        assert False

    try:
        topology.standalone.add_s(Entry((BRANCH3, {
                          'objectclass': 'top extensibleObject'.split(),
                          'uid': 'level3'
                          })))
    except ldap.LDAPError as e:
        log.error('Failed to add level3: error ' + e.message['desc'])
        assert False

    # People branch, might already exist
    try:
        topology.standalone.add_s(Entry((BRANCH4, {
                          'objectclass': 'top extensibleObject'.split(),
                          'ou': 'level4'
                          })))
    except ldap.ALREADY_EXISTS:
        pass
    except ldap.LDAPError as e:
        log.error('Failed to add level4: error ' + e.message['desc'])
        assert False

    try:
        topology.standalone.add_s(Entry((BRANCH5, {
                          'objectclass': 'top extensibleObject'.split(),
                          'ou': 'level5'
                          })))
    except ldap.LDAPError as e:
        log.error('Failed to add level5: error ' + e.message['desc'])
        assert False

    try:
        topology.standalone.add_s(Entry((BRANCH6, {
                          'objectclass': 'top extensibleObject'.split(),
                          'uid': 'level6'
                          })))
    except ldap.LDAPError as e:
        log.error('Failed to add level6: error ' + e.message['desc'])
        assert False

    # Add users to each branch
    try:
        topology.standalone.add_s(Entry((USER1_DN, {
                          'objectclass': 'top extensibleObject'.split(),
                          'uid': 'user1'
                          })))
    except ldap.LDAPError as e:
        log.error('Failed to add user1: error ' + e.message['desc'])
        assert False

    try:
        topology.standalone.add_s(Entry((USER2_DN, {
                          'objectclass': 'top extensibleObject'.split(),
                          'uid': 'user2'
                          })))
    except ldap.LDAPError as e:
        log.error('Failed to add user2: error ' + e.message['desc'])
        assert False

    try:
        topology.standalone.add_s(Entry((USER3_DN, {
                          'objectclass': 'top extensibleObject'.split(),
                          'uid': 'user3'
                          })))
    except ldap.LDAPError as e:
        log.error('Failed to add user3: error ' + e.message['desc'])
        assert False

    try:
        topology.standalone.add_s(Entry((USER4_DN, {
                          'objectclass': 'top extensibleObject'.split(),
                          'uid': 'user4'
                          })))
    except ldap.LDAPError as e:
        log.error('Failed to add user4: error ' + e.message['desc'])
        assert False

    try:
        topology.standalone.add_s(Entry((USER5_DN, {
                          'objectclass': 'top extensibleObject'.split(),
                          'uid': 'user5'
                          })))
    except ldap.LDAPError as e:
        log.error('Failed to add user5: error ' + e.message['desc'])
        assert False

    try:
        topology.standalone.add_s(Entry((USER6_DN, {
                          'objectclass': 'top extensibleObject'.split(),
                          'uid': 'user6'
                          })))
    except ldap.LDAPError as e:
        log.error('Failed to add user6: error ' + e.message['desc'])
        assert False

    # Enable password policy
    try:
        topology.standalone.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE, 'nsslapd-pwpolicy-local', 'on')])
    except ldap.LDAPError as e:
        log.error('Failed to set pwpolicy-local: error ' + e.message['desc'])
        assert False

    #
    # Add subtree policy to branch 1
    #
    # Add the container
    try:
        topology.standalone.add_s(Entry((BRANCH1_CONTAINER, {
                          'objectclass': 'top nsContainer'.split(),
                          'cn': 'nsPwPolicyContainer'
                          })))
    except ldap.LDAPError as e:
        log.error('Failed to add subtree container for level1: error ' + e.message['desc'])
        assert False

    # Add the password policy subentry
    try:
        topology.standalone.add_s(Entry((BRANCH1_PWP, {
                          'objectclass': 'top ldapsubentry passwordpolicy'.split(),
                          'cn': 'cn=nsPwPolicyEntry,ou=level1,dc=example,dc=com',
                          'passwordMustChange': 'off',
                          'passwordExp': 'off',
                          'passwordHistory': 'off',
                          'passwordMinAge': '0',
                          'passwordChange': 'off',
                          'passwordStorageScheme': 'ssha'
                          })))
    except ldap.LDAPError as e:
        log.error('Failed to add passwordpolicy for level1: error ' + e.message['desc'])
        assert False

    # Add the COS template
    try:
        topology.standalone.add_s(Entry((BRANCH1_COS_TMPL, {
                          'objectclass': 'top ldapsubentry costemplate extensibleObject'.split(),
                          'cn': 'cn=nsPwPolicyEntry,ou=level1,dc=example,dc=com',
                          'cosPriority': '1',
                          'cn': 'cn=nsPwTemplateEntry,ou=level1,dc=example,dc=com',
                          'pwdpolicysubentry': BRANCH1_PWP
                          })))
    except ldap.LDAPError as e:
        log.error('Failed to add COS template for level1: error ' + e.message['desc'])
        assert False

    # Add the COS definition
    try:
        topology.standalone.add_s(Entry((BRANCH1_COS_DEF, {
                          'objectclass': 'top ldapsubentry cosSuperDefinition cosPointerDefinition'.split(),
                          'cn': 'cn=nsPwPolicyEntry,ou=level1,dc=example,dc=com',
                          'costemplatedn': BRANCH1_COS_TMPL,
                          'cosAttribute': 'pwdpolicysubentry default operational-default'
                          })))
    except ldap.LDAPError as e:
        log.error('Failed to add COS def for level1: error ' + e.message['desc'])
        assert False

    #
    # Add subtree policy to branch 2
    #
    # Add the container
    try:
        topology.standalone.add_s(Entry((BRANCH2_CONTAINER, {
                          'objectclass': 'top nsContainer'.split(),
                          'cn': 'nsPwPolicyContainer'
                          })))
    except ldap.LDAPError as e:
        log.error('Failed to add subtree container for level2: error ' + e.message['desc'])
        assert False

    # Add the password policy subentry
    try:
        topology.standalone.add_s(Entry((BRANCH2_PWP, {
                          'objectclass': 'top ldapsubentry passwordpolicy'.split(),
                          'cn': 'cn=nsPwPolicyEntry,ou=level2,dc=example,dc=com',
                          'passwordMustChange': 'off',
                          'passwordExp': 'off',
                          'passwordHistory': 'off',
                          'passwordMinAge': '0',
                          'passwordChange': 'off',
                          'passwordStorageScheme': 'ssha'
                          })))
    except ldap.LDAPError as e:
        log.error('Failed to add passwordpolicy for level2: error ' + e.message['desc'])
        assert False

    # Add the COS template
    try:
        topology.standalone.add_s(Entry((BRANCH2_COS_TMPL, {
                          'objectclass': 'top ldapsubentry costemplate extensibleObject'.split(),
                          'cn': 'cn=nsPwPolicyEntry,ou=level2,dc=example,dc=com',
                          'cosPriority': '1',
                          'cn': 'cn=nsPwTemplateEntry,ou=level2,dc=example,dc=com',
                          'pwdpolicysubentry': BRANCH2_PWP
                          })))
    except ldap.LDAPError as e:
        log.error('Failed to add COS template for level2: error ' + e.message['desc'])
        assert False

    # Add the COS definition
    try:
        topology.standalone.add_s(Entry((BRANCH2_COS_DEF, {
                          'objectclass': 'top ldapsubentry cosSuperDefinition cosPointerDefinition'.split(),
                          'cn': 'cn=nsPwPolicyEntry,ou=level2,dc=example,dc=com',
                          'costemplatedn': BRANCH2_COS_TMPL,
                          'cosAttribute': 'pwdpolicysubentry default operational-default'
                          })))
    except ldap.LDAPError as e:
        log.error('Failed to add COS def for level2: error ' + e.message['desc'])
        assert False

    #
    # Add subtree policy to branch 3
    #
    # Add the container
    try:
        topology.standalone.add_s(Entry((BRANCH3_CONTAINER, {
                          'objectclass': 'top nsContainer'.split(),
                          'cn': 'nsPwPolicyContainer'
                          })))
    except ldap.LDAPError as e:
        log.error('Failed to add subtree container for level3: error ' + e.message['desc'])
        assert False

    # Add the password policy subentry
    try:
        topology.standalone.add_s(Entry((BRANCH3_PWP, {
                          'objectclass': 'top ldapsubentry passwordpolicy'.split(),
                          'cn': 'cn=nsPwPolicyEntry,ou=level3,dc=example,dc=com',
                          'passwordMustChange': 'off',
                          'passwordExp': 'off',
                          'passwordHistory': 'off',
                          'passwordMinAge': '0',
                          'passwordChange': 'off',
                          'passwordStorageScheme': 'ssha'
                          })))
    except ldap.LDAPError as e:
        log.error('Failed to add passwordpolicy for level3: error ' + e.message['desc'])
        assert False

    # Add the COS template
    try:
        topology.standalone.add_s(Entry((BRANCH3_COS_TMPL, {
                          'objectclass': 'top ldapsubentry costemplate extensibleObject'.split(),
                          'cn': 'cn=nsPwPolicyEntry,ou=level3,dc=example,dc=com',
                          'cosPriority': '1',
                          'cn': 'cn=nsPwTemplateEntry,ou=level3,dc=example,dc=com',
                          'pwdpolicysubentry': BRANCH3_PWP
                          })))
    except ldap.LDAPError as e:
        log.error('Failed to add COS template for level3: error ' + e.message['desc'])
        assert False

    # Add the COS definition
    try:
        topology.standalone.add_s(Entry((BRANCH3_COS_DEF, {
                          'objectclass': 'top ldapsubentry cosSuperDefinition cosPointerDefinition'.split(),
                          'cn': 'cn=nsPwPolicyEntry,ou=level3,dc=example,dc=com',
                          'costemplatedn': BRANCH3_COS_TMPL,
                          'cosAttribute': 'pwdpolicysubentry default operational-default'
                          })))
    except ldap.LDAPError as e:
        log.error('Failed to add COS def for level3: error ' + e.message['desc'])
        assert False

    #
    # Add subtree policy to branch 4
    #
    # Add the container
    try:
        topology.standalone.add_s(Entry((BRANCH4_CONTAINER, {
                          'objectclass': 'top nsContainer'.split(),
                          'cn': 'nsPwPolicyContainer'
                          })))
    except ldap.LDAPError as e:
        log.error('Failed to add subtree container for level3: error ' + e.message['desc'])
        assert False

    # Add the password policy subentry
    try:
        topology.standalone.add_s(Entry((BRANCH4_PWP, {
                          'objectclass': 'top ldapsubentry passwordpolicy'.split(),
                          'cn': 'cn=nsPwPolicyEntry,ou=people,dc=example,dc=com',
                          'passwordMustChange': 'off',
                          'passwordExp': 'off',
                          'passwordHistory': 'off',
                          'passwordMinAge': '0',
                          'passwordChange': 'off',
                          'passwordStorageScheme': 'ssha'
                          })))
    except ldap.LDAPError as e:
        log.error('Failed to add passwordpolicy for branch4: error ' + e.message['desc'])
        assert False

    # Add the COS template
    try:
        topology.standalone.add_s(Entry((BRANCH4_COS_TMPL, {
                          'objectclass': 'top ldapsubentry costemplate extensibleObject'.split(),
                          'cn': 'cn=nsPwPolicyEntry,ou=people,dc=example,dc=com',
                          'cosPriority': '1',
                          'cn': 'cn=nsPwTemplateEntry,ou=people,dc=example,dc=com',
                          'pwdpolicysubentry': BRANCH4_PWP
                          })))
    except ldap.LDAPError as e:
        log.error('Failed to add COS template for level3: error ' + e.message['desc'])
        assert False

    # Add the COS definition
    try:
        topology.standalone.add_s(Entry((BRANCH4_COS_DEF, {
                          'objectclass': 'top ldapsubentry cosSuperDefinition cosPointerDefinition'.split(),
                          'cn': 'cn=nsPwPolicyEntry,ou=people,dc=example,dc=com',
                          'costemplatedn': BRANCH4_COS_TMPL,
                          'cosAttribute': 'pwdpolicysubentry default operational-default'
                          })))
    except ldap.LDAPError as e:
        log.error('Failed to add COS def for branch4: error ' + e.message['desc'])
        assert False

    #
    # Add subtree policy to branch 5
    #
    # Add the container
    try:
        topology.standalone.add_s(Entry((BRANCH5_CONTAINER, {
                          'objectclass': 'top nsContainer'.split(),
                          'cn': 'nsPwPolicyContainer'
                          })))
    except ldap.LDAPError as e:
        log.error('Failed to add subtree container for branch5: error ' + e.message['desc'])
        assert False

    # Add the password policy subentry
    try:
        topology.standalone.add_s(Entry((BRANCH5_PWP, {
                          'objectclass': 'top ldapsubentry passwordpolicy'.split(),
                          'cn': 'cn=nsPwPolicyEntry,ou=lower,ou=people,dc=example,dc=com',
                          'passwordMustChange': 'off',
                          'passwordExp': 'off',
                          'passwordHistory': 'off',
                          'passwordMinAge': '0',
                          'passwordChange': 'off',
                          'passwordStorageScheme': 'ssha'
                          })))
    except ldap.LDAPError as e:
        log.error('Failed to add passwordpolicy for branch5: error ' + e.message['desc'])
        assert False

    # Add the COS template
    try:
        topology.standalone.add_s(Entry((BRANCH5_COS_TMPL, {
                          'objectclass': 'top ldapsubentry costemplate extensibleObject'.split(),
                          'cn': 'cn=nsPwPolicyEntry,ou=lower,ou=people,dc=example,dc=com',
                          'cosPriority': '1',
                          'cn': 'cn=nsPwTemplateEntry,ou=lower,ou=people,dc=example,dc=com',
                          'pwdpolicysubentry': BRANCH5_PWP
                          })))
    except ldap.LDAPError as e:
        log.error('Failed to add COS template for branch5: error ' + e.message['desc'])
        assert False

    # Add the COS definition
    try:
        topology.standalone.add_s(Entry((BRANCH5_COS_DEF, {
                          'objectclass': 'top ldapsubentry cosSuperDefinition cosPointerDefinition'.split(),
                          'cn': 'cn=nsPwPolicyEntry,ou=lower,ou=people,dc=example,dc=com',
                          'costemplatedn': BRANCH5_COS_TMPL,
                          'cosAttribute': 'pwdpolicysubentry default operational-default'
                          })))
    except ldap.LDAPError as e:
        log.error('Failed to add COS def for level3: error ' + e.message['desc'])
        assert False

    #
    # Add subtree policy to branch 6
    #
    # Add the container
    try:
        topology.standalone.add_s(Entry((BRANCH6_CONTAINER, {
                          'objectclass': 'top nsContainer'.split(),
                          'cn': 'nsPwPolicyContainer'
                          })))
    except ldap.LDAPError as e:
        log.error('Failed to add subtree container for branch6: error ' + e.message['desc'])
        assert False

    # Add the password policy subentry
    try:
        topology.standalone.add_s(Entry((BRANCH6_PWP, {
                          'objectclass': 'top ldapsubentry passwordpolicy'.split(),
                          'cn': 'cn=nsPwPolicyEntry,ou=level3,dc=example,dc=com',
                          'passwordMustChange': 'off',
                          'passwordExp': 'off',
                          'passwordHistory': 'off',
                          'passwordMinAge': '0',
                          'passwordChange': 'off',
                          'passwordStorageScheme': 'ssha'
                          })))
    except ldap.LDAPError as e:
        log.error('Failed to add passwordpolicy for branch6: error ' + e.message['desc'])
        assert False

    # Add the COS template
    try:
        topology.standalone.add_s(Entry((BRANCH6_COS_TMPL, {
                          'objectclass': 'top ldapsubentry costemplate extensibleObject'.split(),
                          'cn': 'cn=nsPwPolicyEntry,ou=lower,ou=lower,ou=people,dc=example,dc=com',
                          'cosPriority': '1',
                          'cn': 'cn=nsPwTemplateEntry,ou=lower,ou=lower,ou=people,dc=example,dc=com',
                          'pwdpolicysubentry': BRANCH6_PWP
                          })))
    except ldap.LDAPError as e:
        log.error('Failed to add COS template for branch6: error ' + e.message['desc'])
        assert False

    # Add the COS definition
    try:
        topology.standalone.add_s(Entry((BRANCH6_COS_DEF, {
                          'objectclass': 'top ldapsubentry cosSuperDefinition cosPointerDefinition'.split(),
                          'cn': 'cn=nsPwPolicyEntry,ou=lower,ou=lower,ou=people,dc=example,dc=com',
                          'costemplatedn': BRANCH6_COS_TMPL,
                          'cosAttribute': 'pwdpolicysubentry default operational-default'
                          })))
    except ldap.LDAPError as e:
        log.error('Failed to add COS def for branch6: error ' + e.message['desc'])
        assert False

    time.sleep(2)

    #
    # Now check that each user has its expected passwordPolicy subentry
    #
    try:
        entries = topology.standalone.search_s(USER1_DN, ldap.SCOPE_BASE, '(objectclass=top)', ['pwdpolicysubentry'])
        if not entries[0].hasValue('pwdpolicysubentry', BRANCH1_PWP):
            log.fatal('User %s does not have expected pwdpolicysubentry!')
            assert False
    except ldap.LDAPError as e:
        log.fatal('Unable to search for entry %s: error %s' % (USER1_DN, e.message['desc']))
        assert False

    try:
        entries = topology.standalone.search_s(USER2_DN, ldap.SCOPE_BASE, '(objectclass=top)', ['pwdpolicysubentry'])
        if not entries[0].hasValue('pwdpolicysubentry', BRANCH2_PWP):
            log.fatal('User %s does not have expected pwdpolicysubentry!' % USER2_DN)
            assert False
    except ldap.LDAPError as e:
        log.fatal('Unable to search for entry %s: error %s' % (USER2_DN, e.message['desc']))
        assert False

    try:
        entries = topology.standalone.search_s(USER3_DN, ldap.SCOPE_BASE, '(objectclass=top)', ['pwdpolicysubentry'])
        if not entries[0].hasValue('pwdpolicysubentry', BRANCH3_PWP):
            log.fatal('User %s does not have expected pwdpolicysubentry!' % USER3_DN)
            assert False
    except ldap.LDAPError as e:
        log.fatal('Unable to search for entry %s: error %s' % (USER3_DN, e.message['desc']))
        assert False

    try:
        entries = topology.standalone.search_s(USER4_DN, ldap.SCOPE_BASE, '(objectclass=top)', ['pwdpolicysubentry'])
        if not entries[0].hasValue('pwdpolicysubentry', BRANCH4_PWP):
            log.fatal('User %s does not have expected pwdpolicysubentry!' % USER4_DN)
            assert False
    except ldap.LDAPError as e:
        log.fatal('Unable to search for entry %s: error %s' % (USER4_DN, e.message['desc']))
        assert False

    try:
        entries = topology.standalone.search_s(USER5_DN, ldap.SCOPE_BASE, '(objectclass=top)', ['pwdpolicysubentry'])
        if not entries[0].hasValue('pwdpolicysubentry', BRANCH5_PWP):
            log.fatal('User %s does not have expected pwdpolicysubentry!' % USER5_DN)
            assert False
    except ldap.LDAPError as e:
        log.fatal('Unable to search for entry %s: error %s' % (USER5_DN, e.message['desc']))
        assert False

    try:
        entries = topology.standalone.search_s(USER6_DN, ldap.SCOPE_BASE, '(objectclass=top)', ['pwdpolicysubentry'])
        if not entries[0].hasValue('pwdpolicysubentry', BRANCH6_PWP):
            log.fatal('User %s does not have expected pwdpolicysubentry!' % USER6_DN)
            assert False
    except ldap.LDAPError as e:
        log.fatal('Unable to search for entry %s: error %s' % (USER6_DN, e.message['desc']))
        assert False


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
