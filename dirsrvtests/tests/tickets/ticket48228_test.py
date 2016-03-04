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
from lib389 import DirSrv, Entry, tools, tasks
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *

log = logging.getLogger(__name__)

installation_prefix = None

# Assuming DEFAULT_SUFFIX is "dc=example,dc=com", otherwise it does not work... :(
SUBTREE_CONTAINER = 'cn=nsPwPolicyContainer,' + DEFAULT_SUFFIX
SUBTREE_PWPDN = 'cn=nsPwPolicyEntry,' + DEFAULT_SUFFIX
SUBTREE_PWP = 'cn=cn\3DnsPwPolicyEntry\2Cdc\3Dexample\2Cdc\3Dcom,' + SUBTREE_CONTAINER
SUBTREE_COS_TMPLDN = 'cn=nsPwTemplateEntry,' + DEFAULT_SUFFIX
SUBTREE_COS_TMPL = 'cn=cn\3DnsPwTemplateEntry\2Cdc\3Dexample\2Cdc\3Dcom,' + SUBTREE_CONTAINER
SUBTREE_COS_DEF = 'cn=nsPwPolicy_CoS,' + DEFAULT_SUFFIX

USER1_DN = 'uid=user1,' + DEFAULT_SUFFIX
USER2_DN = 'uid=user2,' + DEFAULT_SUFFIX


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

    # clear the tmp directory
    standalone.clearTmpDir(__file__)

    # Here we have standalone instance up and running
    return TopologyStandalone(standalone)


def set_global_pwpolicy(topology, inhistory):
    log.info("	+++++ Enable global password policy +++++\n")
    topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    # Enable password policy
    try:
        topology.standalone.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE, 'nsslapd-pwpolicy-local', 'on')])
    except ldap.LDAPError as e:
        log.error('Failed to set pwpolicy-local: error ' + e.message['desc'])
        assert False

    log.info("		Set global password history on\n")
    try:
        topology.standalone.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE, 'passwordHistory', 'on')])
    except ldap.LDAPError as e:
        log.error('Failed to set passwordHistory: error ' + e.message['desc'])
        assert False

    log.info("		Set global passwords in history\n")
    try:
        count = "%d" % inhistory
        topology.standalone.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE, 'passwordInHistory', count)])
    except ldap.LDAPError as e:
        log.error('Failed to set passwordInHistory: error ' + e.message['desc'])
        assert False


def set_subtree_pwpolicy(topology):
    log.info("	+++++ Enable subtree level password policy +++++\n")
    topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    log.info("		Add the container")
    try:
        topology.standalone.add_s(Entry((SUBTREE_CONTAINER, {'objectclass': 'top nsContainer'.split(),
                                                             'cn': 'nsPwPolicyContainer'})))
    except ldap.LDAPError as e:
        log.error('Failed to add subtree container: error ' + e.message['desc'])
        assert False

    log.info("		Add the password policy subentry {passwordHistory: on, passwordInHistory: 6}")
    try:
        topology.standalone.add_s(Entry((SUBTREE_PWP, {'objectclass': 'top ldapsubentry passwordpolicy'.split(),
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
        topology.standalone.add_s(Entry((SUBTREE_COS_TMPL, {'objectclass': 'top ldapsubentry costemplate extensibleObject'.split(),
                                                            'cn': SUBTREE_PWPDN,
                                                            'cosPriority': '1',
                                                            'cn': SUBTREE_COS_TMPLDN,
                                                            'pwdpolicysubentry': SUBTREE_PWP})))
    except ldap.LDAPError as e:
        log.error('Failed to add COS template: error ' + e.message['desc'])
        assert False

    log.info("		Add the COS definition")
    try:
        topology.standalone.add_s(Entry((SUBTREE_COS_DEF, {'objectclass': 'top ldapsubentry cosSuperDefinition cosPointerDefinition'.split(),
                                                           'cn': SUBTREE_PWPDN,
                                                           'costemplatedn': SUBTREE_COS_TMPL,
                                                           'cosAttribute': 'pwdpolicysubentry default operational-default'})))
    except ldap.LDAPError as e:
        log.error('Failed to add COS def: error ' + e.message['desc'])
        assert False


def check_passwd_inhistory(topology, user, cpw, passwd):
    inhistory = 0
    log.info("		Bind as {%s,%s}" % (user, cpw))
    topology.standalone.simple_bind_s(user, cpw)
    try:
        topology.standalone.modify_s(user, [(ldap.MOD_REPLACE, 'userpassword', passwd)])
    except ldap.LDAPError as e:
        log.info('		The password ' + passwd + ' of user' + USER1_DN + ' in history: error ' + e.message['desc'])
        inhistory = 1
    return inhistory


def update_passwd(topology, user, passwd, times):
    cpw = passwd
    loop = 0
    while loop < times:
        log.info("		Bind as {%s,%s}" % (user, cpw))
        topology.standalone.simple_bind_s(user, cpw)
        cpw = 'password%d' % loop
        try:
            topology.standalone.modify_s(user, [(ldap.MOD_REPLACE, 'userpassword', cpw)])
        except ldap.LDAPError as e:
            log.fatal('test_ticket48228: Failed to update the password ' + cpw + ' of user ' + user + ': error ' + e.message['desc'])
            assert False
        loop += 1

    # checking the first password, which is supposed to be in history
    inhistory = check_passwd_inhistory(topology, user, cpw, passwd)
    assert inhistory == 1


def test_ticket48228_test_global_policy(topology):
    """
    Check global password policy
    """

    log.info('	Set inhistory = 6')
    set_global_pwpolicy(topology, 6)

    log.info('	Bind as directory manager')
    log.info("Bind as %s" % DN_DM)
    topology.standalone.simple_bind_s(DN_DM, PASSWORD)

    log.info('	Add an entry' + USER1_DN)
    try:
        topology.standalone.add_s(Entry((USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
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
    update_passwd(topology, USER1_DN, 'password', 6)

    log.info('	Set inhistory = 4')
    set_global_pwpolicy(topology, 4)

    log.info('	checking the first password, which is supposed NOT to be in history any more')
    cpw = 'password%d' % 5
    tpw = 'password'
    inhistory = check_passwd_inhistory(topology, USER1_DN, cpw, tpw)
    assert inhistory == 0

    log.info('	checking the second password, which is supposed NOT to be in history any more')
    cpw = tpw
    tpw = 'password%d' % 0
    inhistory = check_passwd_inhistory(topology, USER1_DN, cpw, tpw)
    assert inhistory == 0

    log.info('	checking the second password, which is supposed NOT to be in history any more')
    cpw = tpw
    tpw = 'password%d' % 1
    inhistory = check_passwd_inhistory(topology, USER1_DN, cpw, tpw)
    assert inhistory == 0

    log.info('	checking the third password, which is supposed to be in history')
    cpw = tpw
    tpw = 'password%d' % 2
    inhistory = check_passwd_inhistory(topology, USER1_DN, cpw, tpw)
    assert inhistory == 1

    log.info("Global policy was successfully verified.")


def test_ticket48228_test_subtree_policy(topology):
    """
    Check subtree level password policy
    """

    log.info('	Set inhistory = 6')
    set_subtree_pwpolicy(topology)

    log.info('	Bind as directory manager')
    log.info("Bind as %s" % DN_DM)
    topology.standalone.simple_bind_s(DN_DM, PASSWORD)

    log.info('	Add an entry' + USER2_DN)
    try:
        topology.standalone.add_s(Entry((USER2_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
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
    update_passwd(topology, USER2_DN, 'password', 6)

    log.info('	Set inhistory = 4')
    topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    try:
        topology.standalone.modify_s(SUBTREE_PWP, [(ldap.MOD_REPLACE, 'passwordInHistory', '4')])
    except ldap.LDAPError as e:
        log.error('Failed to set pwpolicy-local: error ' + e.message['desc'])
        assert False

    log.info('	checking the first password, which is supposed NOT to be in history any more')
    cpw = 'password%d' % 5
    tpw = 'password'
    inhistory = check_passwd_inhistory(topology, USER2_DN, cpw, tpw)
    assert inhistory == 0

    log.info('	checking the second password, which is supposed NOT to be in history any more')
    cpw = tpw
    tpw = 'password%d' % 0
    inhistory = check_passwd_inhistory(topology, USER2_DN, cpw, tpw)
    assert inhistory == 0

    log.info('	checking the second password, which is supposed NOT to be in history any more')
    cpw = tpw
    tpw = 'password%d' % 1
    inhistory = check_passwd_inhistory(topology, USER2_DN, cpw, tpw)
    assert inhistory == 0

    log.info('	checking the third password, which is supposed to be in history')
    cpw = tpw
    tpw = 'password%d' % 2
    inhistory = check_passwd_inhistory(topology, USER2_DN, cpw, tpw)
    assert inhistory == 1

    log.info("Subtree level policy was successfully verified.")


def test_ticket48228_final(topology):
    topology.standalone.delete()
    log.info('Testcase PASSED')


def run_isolated():
    '''
        run_isolated is used to run these test cases independently of a test scheduler (xunit, py.test..)
        To run isolated without py.test, you need to
            - edit this file and comment '@pytest.fixture' line before 'topology' function.
            - set the installation prefix
            - run this program
    '''
    global installation_prefix
    installation_prefix = None

    topo = topology(True)
    log.info('Testing Ticket 48228 - wrong password check if passwordInHistory is decreased')

    test_ticket48228_test_global_policy(topo)

    test_ticket48228_test_subtree_policy(topo)

    test_ticket48228_final(topo)


if __name__ == '__main__':
    run_isolated()

