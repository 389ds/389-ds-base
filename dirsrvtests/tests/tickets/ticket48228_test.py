# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import time

import pytest
from lib389.tasks import *
from lib389.topologies import topology_st

from lib389._constants import DEFAULT_SUFFIX, DN_DM, PASSWORD, DN_CONFIG

pytestmark = pytest.mark.tier2

log = logging.getLogger(__name__)

# Assuming DEFAULT_SUFFIX is "dc=example,dc=com", otherwise it does not work... :(
SUBTREE_CONTAINER = 'cn=nsPwPolicyContainer,' + DEFAULT_SUFFIX
SUBTREE_PWPDN = 'cn=nsPwPolicyEntry,' + DEFAULT_SUFFIX
SUBTREE_PWP = 'cn=cn\3DnsPwPolicyEntry\2Cdc\3Dexample\2Cdc\3Dcom,' + SUBTREE_CONTAINER
SUBTREE_COS_TMPLDN = 'cn=nsPwTemplateEntry,' + DEFAULT_SUFFIX
SUBTREE_COS_TMPL = 'cn=cn\3DnsPwTemplateEntry\2Cdc\3Dexample\2Cdc\3Dcom,' + SUBTREE_CONTAINER
SUBTREE_COS_DEF = 'cn=nsPwPolicy_CoS,' + DEFAULT_SUFFIX

USER1_DN = 'uid=user1,' + DEFAULT_SUFFIX
USER2_DN = 'uid=user2,' + DEFAULT_SUFFIX


def set_global_pwpolicy(topology_st, inhistory):
    log.info("	+++++ Enable global password policy +++++\n")
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    # Enable password policy
    try:
        topology_st.standalone.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE, 'nsslapd-pwpolicy-local', b'on')])
    except ldap.LDAPError as e:
        log.error('Failed to set pwpolicy-local: error ' + e.message['desc'])
        assert False

    log.info("		Set global password history on\n")
    try:
        topology_st.standalone.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE, 'passwordHistory', b'on')])
    except ldap.LDAPError as e:
        log.error('Failed to set passwordHistory: error ' + e.message['desc'])
        assert False

    log.info("		Set global passwords in history\n")
    try:
        count = "%d" % inhistory
        topology_st.standalone.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE, 'passwordInHistory', count.encode())])
    except ldap.LDAPError as e:
        log.error('Failed to set passwordInHistory: error ' + e.message['desc'])
        assert False
    time.sleep(1)


def set_subtree_pwpolicy(topology_st):
    log.info("	+++++ Enable subtree level password policy +++++\n")
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    log.info("		Add the container")
    try:
        topology_st.standalone.add_s(Entry((SUBTREE_CONTAINER, {'objectclass': 'top nsContainer'.split(),
                                                                'cn': 'nsPwPolicyContainer'})))
    except ldap.LDAPError as e:
        log.error('Failed to add subtree container: error ' + e.message['desc'])
        assert False

    log.info("		Add the password policy subentry {passwordHistory: on, passwordInHistory: 6}")
    try:
        topology_st.standalone.add_s(Entry((SUBTREE_PWP, {'objectclass': 'top ldapsubentry passwordpolicy'.split(),
                                                          'cn': SUBTREE_PWPDN,
                                                          'passwordMustChange': 'off',
                                                          'passwordExp': 'off',
                                                          'passwordHistory': 'on',
                                                          'passwordInHistory': '6',
                                                          'passwordMinAge': '0',
                                                          'passwordChange': 'on',
                                                          'passwordStorageScheme': 'clear'})))
    except ldap.LDAPError as e:
        log.error('Failed to add passwordpolicy: error ' + e.message['desc'])
        assert False

    log.info("		Add the COS template")
    try:
        topology_st.standalone.add_s(
            Entry((SUBTREE_COS_TMPL, {'objectclass': 'top ldapsubentry costemplate extensibleObject'.split(),
                                      'cn': SUBTREE_PWPDN,
                                      'cosPriority': '1',
                                      'cn': SUBTREE_COS_TMPLDN,
                                      'pwdpolicysubentry': SUBTREE_PWP})))
    except ldap.LDAPError as e:
        log.error('Failed to add COS template: error ' + e.message['desc'])
        assert False

    log.info("		Add the COS definition")
    try:
        topology_st.standalone.add_s(
            Entry((SUBTREE_COS_DEF, {'objectclass': 'top ldapsubentry cosSuperDefinition cosPointerDefinition'.split(),
                                     'cn': SUBTREE_PWPDN,
                                     'costemplatedn': SUBTREE_COS_TMPL,
                                     'cosAttribute': 'pwdpolicysubentry default operational-default'})))
    except ldap.LDAPError as e:
        log.error('Failed to add COS def: error ' + e.message['desc'])
        assert False
    time.sleep(1)


def check_passwd_inhistory(topology_st, user, cpw, passwd):

    inhistory = 0
    log.info("		Bind as {%s,%s}" % (user, cpw))
    topology_st.standalone.simple_bind_s(user, cpw)
    time.sleep(1)
    try:
        topology_st.standalone.modify_s(user, [(ldap.MOD_REPLACE, 'userpassword', passwd.encode())])
    except ldap.LDAPError as e:
        log.info('		The password ' + passwd + ' of user' + USER1_DN + ' in history: error {0}'.format(e))
        inhistory = 1
    time.sleep(1)
    return inhistory


def update_passwd(topology_st, user, passwd, times):
    # Set the default value
    cpw = passwd
    for i in range(times):
        log.info("		Bind as {%s,%s}" % (user, cpw))
        topology_st.standalone.simple_bind_s(user, cpw)
        # Now update the value for this iter.
        cpw = 'password%d' % i
        try:
            topology_st.standalone.modify_s(user, [(ldap.MOD_REPLACE, 'userpassword', cpw.encode())])
        except ldap.LDAPError as e:
            log.fatal(
                'test_ticket48228: Failed to update the password ' + cpw + ' of user ' + user + ': error ' + e.message[
                    'desc'])
            assert False

    # checking the first password, which is supposed to be in history
    inhistory = check_passwd_inhistory(topology_st, user, cpw, passwd)
    assert inhistory == 1


def test_ticket48228_test_global_policy(topology_st):
    """
    Check global password policy
    """
    log.info('	Set inhistory = 6')
    set_global_pwpolicy(topology_st, 6)

    log.info('	Bind as directory manager')
    log.info("Bind as %s" % DN_DM)
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)

    log.info('	Add an entry' + USER1_DN)
    try:
        topology_st.standalone.add_s(
            Entry((USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                              'sn': '1',
                              'cn': 'user 1',
                              'uid': 'user1',
                              'givenname': 'user',
                              'mail': 'user1@example.com',
                              'userpassword': 'password'})))
    except ldap.LDAPError as e:
        log.fatal('test_ticket48228: Failed to add user' + USER1_DN + ': error ' + e.message['desc'])
        assert False

    log.info('	Update the password of ' + USER1_DN + ' 6 times')
    update_passwd(topology_st, USER1_DN, 'password', 6)

    log.info('	Set inhistory = 4')
    set_global_pwpolicy(topology_st, 4)

    log.info('	checking the first password, which is supposed NOT to be in history any more')
    cpw = 'password%d' % 5
    tpw = 'password'
    inhistory = check_passwd_inhistory(topology_st, USER1_DN, cpw, tpw)
    assert inhistory == 0

    log.info('	checking the second password, which is supposed NOT to be in history any more')
    cpw = tpw
    tpw = 'password%d' % 0
    inhistory = check_passwd_inhistory(topology_st, USER1_DN, cpw, tpw)
    assert inhistory == 0

    log.info('	checking the third password, which is supposed NOT to be in history any more')
    cpw = tpw
    tpw = 'password%d' % 1
    inhistory = check_passwd_inhistory(topology_st, USER1_DN, cpw, tpw)
    assert inhistory == 0

    log.info('	checking the sixth password, which is supposed to be in history')
    cpw = tpw
    tpw = 'password%d' % 5
    inhistory = check_passwd_inhistory(topology_st, USER1_DN, cpw, tpw)
    assert inhistory == 1

    log.info("Global policy was successfully verified.")


def text_ticket48228_text_subtree_policy(topology_st):
    """
    Check subtree level password policy
    """

    log.info('	Set inhistory = 6')
    set_subtree_pwpolicy(topology_st)

    log.info('	Bind as directory manager')
    log.info("Bind as %s" % DN_DM)
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)

    log.info('	Add an entry' + USER2_DN)
    try:
        topology_st.standalone.add_s(
            Entry((USER2_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                              'sn': '2',
                              'cn': 'user 2',
                              'uid': 'user2',
                              'givenname': 'user',
                              'mail': 'user2@example.com',
                              'userpassword': 'password'})))
    except ldap.LDAPError as e:
        log.fatal('test_ticket48228: Failed to add user' + USER2_DN + ': error ' + e.message['desc'])
        assert False

    log.info('	Update the password of ' + USER2_DN + ' 6 times')
    update_passwd(topology_st, USER2_DN, 'password', 6)

    log.info('	Set inhistory = 4')
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    try:
        topology_st.standalone.modify_s(SUBTREE_PWP, [(ldap.MOD_REPLACE, 'passwordInHistory', b'4')])
    except ldap.LDAPError as e:
        log.error('Failed to set pwpolicy-local: error ' + e.message['desc'])
        assert False

    log.info('	checking the first password, which is supposed NOT to be in history any more')
    cpw = 'password%d' % 5
    tpw = 'password'
    inhistory = check_passwd_inhistory(topology_st, USER2_DN, cpw, tpw)
    assert inhistory == 0

    log.info('	checking the second password, which is supposed NOT to be in history any more')
    cpw = tpw
    tpw = 'password%d' % 1
    inhistory = check_passwd_inhistory(topology_st, USER2_DN, cpw, tpw)
    assert inhistory == 0

    log.info('	checking the third password, which is supposed NOT to be in history any more')
    cpw = tpw
    tpw = 'password%d' % 2
    inhistory = check_passwd_inhistory(topology_st, USER2_DN, cpw, tpw)
    assert inhistory == 0

    log.info('	checking the six password, which is supposed to be in history')
    cpw = tpw
    tpw = 'password%d' % 5
    inhistory = check_passwd_inhistory(topology_st, USER2_DN, cpw, tpw)
    assert inhistory == 1

    log.info("Subtree level policy was successfully verified.")


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
