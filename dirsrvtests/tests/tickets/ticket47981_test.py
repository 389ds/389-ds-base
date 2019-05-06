# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging

import ldap.sasl
import pytest
from lib389.tasks import *
from lib389.topologies import topology_st

from lib389._constants import DEFAULT_SUFFIX, BACKEND_NAME, DN_CONFIG

pytestmark = pytest.mark.tier2

log = logging.getLogger(__name__)

BRANCH = 'ou=people,' + DEFAULT_SUFFIX
USER_DN = 'uid=user1,%s' % (BRANCH)
BRANCH_CONTAINER = 'cn=nsPwPolicyContainer,ou=people,dc=example,dc=com'
BRANCH_COS_DEF = 'cn=nsPwPolicy_CoS,ou=people,dc=example,dc=com'
BRANCH_PWP = 'cn=cn\\3DnsPwPolicyEntry\\2Cou\\3DPeople\\2Cdc\\3Dexample\\2Cdc\\3Dcom,' + \
             'cn=nsPwPolicyContainer,ou=People,dc=example,dc=com'
BRANCH_COS_TMPL = 'cn=cn\\3DnsPwTemplateEntry\\2Cou\\3DPeople\\2Cdc\\3Dexample\\2Cdc\\3Dcom,' + \
                  'cn=nsPwPolicyContainer,ou=People,dc=example,dc=com'
SECOND_SUFFIX = 'o=netscaperoot'
BE_NAME = 'netscaperoot'


def addSubtreePwPolicy(inst):
    #
    # Add subtree policy to the people branch
    #
    try:
        inst.add_s(Entry((BRANCH_CONTAINER, {
            'objectclass': 'top nsContainer'.split(),
            'cn': 'nsPwPolicyContainer'
        })))
    except ldap.LDAPError as e:
        log.error('Failed to add subtree container for ou=people: error ' + e.args[0]['desc'])
        assert False

    # Add the password policy subentry
    try:
        inst.add_s(Entry((BRANCH_PWP, {
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
        log.error('Failed to add passwordpolicy: error ' + e.args[0]['desc'])
        assert False

    # Add the COS template
    try:
        inst.add_s(Entry((BRANCH_COS_TMPL, {
            'objectclass': 'top ldapsubentry costemplate extensibleObject'.split(),
            'cn': 'cn=nsPwPolicyEntry,ou=people,dc=example,dc=com',
            'cosPriority': '1',
            'cn': 'cn=nsPwTemplateEntry,ou=people,dc=example,dc=com',
            'pwdpolicysubentry': BRANCH_PWP
        })))
    except ldap.LDAPError as e:
        log.error('Failed to add COS template: error ' + e.args[0]['desc'])
        assert False

    # Add the COS definition
    try:
        inst.add_s(Entry((BRANCH_COS_DEF, {
            'objectclass': 'top ldapsubentry cosSuperDefinition cosPointerDefinition'.split(),
            'cn': 'cn=nsPwPolicyEntry,ou=people,dc=example,dc=com',
            'costemplatedn': BRANCH_COS_TMPL,
            'cosAttribute': 'pwdpolicysubentry default operational-default'
        })))
    except ldap.LDAPError as e:
        log.error('Failed to add COS def: error ' + e.args[0]['desc'])
        assert False
    time.sleep(1)


def delSubtreePwPolicy(inst):
    try:
        inst.delete_s(BRANCH_COS_DEF)
    except ldap.LDAPError as e:
        log.error('Failed to delete COS def: error ' + e.args[0]['desc'])
        assert False

    try:
        inst.delete_s(BRANCH_COS_TMPL)
    except ldap.LDAPError as e:
        log.error('Failed to delete COS template: error ' + e.args[0]['desc'])
        assert False

    try:
        inst.delete_s(BRANCH_PWP)
    except ldap.LDAPError as e:
        log.error('Failed to delete COS password policy: error ' + e.args[0]['desc'])
        assert False

    try:
        inst.delete_s(BRANCH_CONTAINER)
    except ldap.LDAPError as e:
        log.error('Failed to delete COS container: error ' + e.args[0]['desc'])
        assert False
    time.sleep(1)


def test_ticket47981(topology_st):
    """
        If there are multiple suffixes, and the last suffix checked does not contain any COS entries,
        while other suffixes do, then the vattr cache is not invalidated as it should be.  Then any
        cached entries will still contain the old COS attributes/values.
    """

    log.info('Testing Ticket 47981 - Test that COS def changes are correctly reflected in affected users')

    #
    # Create a second backend that does not have any COS entries
    #
    log.info('Adding second suffix that will not contain any COS entries...\n')

    topology_st.standalone.backend.create(SECOND_SUFFIX, {BACKEND_NAME: BE_NAME})
    topology_st.standalone.mappingtree.create(SECOND_SUFFIX, bename=BE_NAME)
    try:
        topology_st.standalone.add_s(Entry((SECOND_SUFFIX, {
            'objectclass': 'top organization'.split(),
            'o': BE_NAME})))
    except ldap.ALREADY_EXISTS:
        pass
    except ldap.LDAPError as e:
        log.error('Failed to create suffix entry: error ' + e.args[0]['desc'])
        assert False

    #
    # Add People branch, it might already exist
    #
    log.info('Add our test entries to the default suffix, and proceed with the test...')

    try:
        topology_st.standalone.add_s(Entry((BRANCH, {
            'objectclass': 'top extensibleObject'.split(),
            'ou': 'level4'
        })))
    except ldap.ALREADY_EXISTS:
        pass
    except ldap.LDAPError as e:
        log.error('Failed to add ou=people: error ' + e.args[0]['desc'])
        assert False

    #
    # Add a user to the branch
    #
    try:
        topology_st.standalone.add_s(Entry((USER_DN, {
            'objectclass': 'top extensibleObject'.split(),
            'uid': 'user1'
        })))
    except ldap.LDAPError as e:
        log.error('Failed to add user1: error ' + e.args[0]['desc'])
        assert False

    #
    # Enable password policy and add the subtree policy
    #
    try:
        topology_st.standalone.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE, 'nsslapd-pwpolicy-local', b'on')])
    except ldap.LDAPError as e:
        log.error('Failed to set pwpolicy-local: error ' + e.args[0]['desc'])
        assert False

    addSubtreePwPolicy(topology_st.standalone)

    #
    # Now check the user has its expected passwordPolicy subentry
    #
    try:
        entries = topology_st.standalone.search_s(USER_DN,
                                                  ldap.SCOPE_BASE,
                                                  '(objectclass=top)',
                                                  ['pwdpolicysubentry', 'dn'])
        if not entries[0].hasAttr('pwdpolicysubentry'):
            log.fatal('User does not have expected pwdpolicysubentry!')
            assert False
    except ldap.LDAPError as e:
        log.fatal('Unable to search for entry %s: error %s' % (USER_DN, e.args[0]['desc']))
        assert False

    #
    # Delete the password policy and make sure it is removed from the same user
    #
    delSubtreePwPolicy(topology_st.standalone)
    try:
        entries = topology_st.standalone.search_s(USER_DN, ldap.SCOPE_BASE, '(objectclass=top)', ['pwdpolicysubentry'])
        if entries[0].hasAttr('pwdpolicysubentry'):
            log.fatal('User unexpectedly does have the pwdpolicysubentry!')
            assert False
    except ldap.LDAPError as e:
        log.fatal('Unable to search for entry %s: error %s' % (USER_DN, e.args[0]['desc']))
        assert False

    #
    # Add the subtree policvy back and see if the user now has it
    #
    addSubtreePwPolicy(topology_st.standalone)
    try:
        entries = topology_st.standalone.search_s(USER_DN, ldap.SCOPE_BASE, '(objectclass=top)', ['pwdpolicysubentry'])
        if not entries[0].hasAttr('pwdpolicysubentry'):
            log.fatal('User does not have expected pwdpolicysubentry!')
            assert False
    except ldap.LDAPError as e:
        log.fatal('Unable to search for entry %s: error %s' % (USER_DN, e.args[0]['desc']))
        assert False


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
