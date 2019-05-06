# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st

from lib389._constants import DEFAULT_SUFFIX, DN_CONFIG, DN_DM, PASSWORD, DEFAULT_SUFFIX_ESCAPED

# Skip on older versions
pytestmark = [pytest.mark.tier2,
              pytest.mark.skipif(ds_is_older('1.3.6'), reason="Not implemented")]

log = logging.getLogger(__name__)

# Assuming DEFAULT_SUFFIX is "dc=example,dc=com", otherwise it does not work... :(
SUBTREE_CONTAINER = 'cn=nsPwPolicyContainer,' + DEFAULT_SUFFIX
SUBTREE_PWPDN = 'cn=nsPwPolicyEntry,' + DEFAULT_SUFFIX
SUBTREE_PWP = 'cn=cn\3DnsPwPolicyEntry\2C' + DEFAULT_SUFFIX_ESCAPED + ',' + SUBTREE_CONTAINER
SUBTREE_COS_TMPLDN = 'cn=nsPwTemplateEntry,' + DEFAULT_SUFFIX
SUBTREE_COS_TMPL = 'cn=cn\3DnsPwTemplateEntry\2C' + DEFAULT_SUFFIX_ESCAPED + ',' + SUBTREE_CONTAINER
SUBTREE_COS_DEF = 'cn=nsPwPolicy_CoS,' + DEFAULT_SUFFIX

USER1_DN = 'uid=user1,' + DEFAULT_SUFFIX
USER2_DN = 'uid=user2,' + DEFAULT_SUFFIX
USER3_DN = 'uid=user3,' + DEFAULT_SUFFIX
USER_PW = 'password'


def days_to_secs(days):
    # Value of 60 * 60 * 24
    return days * 86400


# Values are in days
def set_global_pwpolicy(topology_st, min_=1, max_=10, warn=3):
    log.info("	+++++ Enable global password policy +++++\n")
    # Enable password policy
    try:
        topology_st.standalone.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE, 'nsslapd-pwpolicy-local', b'on')])
    except ldap.LDAPError as e:
        log.error('Failed to set pwpolicy-local: error ' + e.message['desc'])
        assert False

    # Convert our values to seconds
    min_secs = days_to_secs(min_)
    max_secs = days_to_secs(max_)
    warn_secs = days_to_secs(warn)

    log.info("		Set global password Min Age -- %s day\n" % min_)
    try:
        topology_st.standalone.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE, 'passwordMinAge', ('%s' % min_secs).encode())])
    except ldap.LDAPError as e:
        log.error('Failed to set passwordMinAge: error ' + e.message['desc'])
        assert False

    log.info("		Set global password Expiration -- on\n")
    try:
        topology_st.standalone.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE, 'passwordExp', b'on')])
    except ldap.LDAPError as e:
        log.error('Failed to set passwordExp: error ' + e.message['desc'])
        assert False

    log.info("		Set global password Max Age -- %s days\n" % max_)
    try:
        topology_st.standalone.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE, 'passwordMaxAge', ('%s' % max_secs).encode())])
    except ldap.LDAPError as e:
        log.error('Failed to set passwordMaxAge: error ' + e.message['desc'])
        assert False

    log.info("		Set global password Warning -- %s days\n" % warn)
    try:
        topology_st.standalone.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE, 'passwordWarning', ('%s' % warn_secs).encode())])
    except ldap.LDAPError as e:
        log.error('Failed to set passwordWarning: error ' + e.message['desc'])
        assert False


def set_subtree_pwpolicy(topology_st, min_=2, max_=20, warn=6):
    log.info("	+++++ Enable subtree level password policy +++++\n")

    # Convert our values to seconds
    min_secs = days_to_secs(min_)
    max_secs = days_to_secs(max_)
    warn_secs = days_to_secs(warn)

    log.info("		Add the container")
    try:
        topology_st.standalone.add_s(Entry((SUBTREE_CONTAINER, {'objectclass': 'top nsContainer'.split(),
                                                                'cn': 'nsPwPolicyContainer'})))
    except ldap.ALREADY_EXISTS:
        pass
    except ldap.LDAPError as e:
        log.error('Failed to add subtree container: error ' + e.message['desc'])
        # assert False

    try:
        # Purge the old policy
        topology_st.standalone.delete_s(SUBTREE_PWP)
    except:
        pass

    log.info(
        "		Add the password policy subentry {passwordMustChange: on, passwordMinAge: %s, passwordMaxAge: %s, passwordWarning: %s}" % (
            min_, max_, warn))
    try:
        topology_st.standalone.add_s(Entry((SUBTREE_PWP, {'objectclass': 'top ldapsubentry passwordpolicy'.split(),
                                                          'cn': SUBTREE_PWPDN,
                                                          'passwordMustChange': 'on',
                                                          'passwordExp': 'on',
                                                          'passwordMinAge': '%s' % min_secs,
                                                          'passwordMaxAge': '%s' % max_secs,
                                                          'passwordWarning': '%s' % warn_secs,
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
    except ldap.ALREADY_EXISTS:
        pass
    except ldap.LDAPError as e:
        log.error('Failed to add COS template: error ' + e.message['desc'])
        # assert False

    log.info("		Add the COS definition")
    try:
        topology_st.standalone.add_s(
            Entry((SUBTREE_COS_DEF, {'objectclass': 'top ldapsubentry cosSuperDefinition cosPointerDefinition'.split(),
                                     'cn': SUBTREE_PWPDN,
                                     'costemplatedn': SUBTREE_COS_TMPL,
                                     'cosAttribute': 'pwdpolicysubentry default operational-default'})))
    except ldap.ALREADY_EXISTS:
        pass
    except ldap.LDAPError as e:
        log.error('Failed to add COS def: error ' + e.message['desc'])
        # assert False

    time.sleep(1)


def update_passwd(topology_st, user, passwd, newpasswd):
    log.info("		Bind as {%s,%s}" % (user, passwd))
    topology_st.standalone.simple_bind_s(user, passwd)
    try:
        topology_st.standalone.modify_s(user, [(ldap.MOD_REPLACE, 'userpassword', newpasswd.encode())])
    except ldap.LDAPError as e:
        log.fatal('test_ticket548: Failed to update the password ' + cpw + ' of user ' + user + ': error ' + e.message[
            'desc'])
        assert False

    time.sleep(1)


def check_shadow_attr_value(entry, attr_type, expected, dn):
    if entry.hasAttr(attr_type):
        actual = entry.getValue(attr_type)
        if int(actual) == expected:
            log.info('%s of entry %s has expected value %s' % (attr_type, dn, actual))
            assert True
        else:
            log.fatal('%s %s of entry %s does not have expected value %s' % (attr_type, actual, dn, expected))
            assert False
    else:
        log.fatal('entry %s does not have %s attr' % (dn, attr_type))
        assert False


def test_ticket548_test_with_no_policy(topology_st):
    """
    Check shadowAccount under no password policy
    """
    log.info("Case 1. No password policy")

    log.info("Bind as %s" % DN_DM)
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)

    log.info('Add an entry' + USER1_DN)
    try:
        topology_st.standalone.add_s(
            Entry((USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson shadowAccount".split(),
                              'sn': '1',
                              'cn': 'user 1',
                              'uid': 'user1',
                              'givenname': 'user',
                              'mail': 'user1@' + DEFAULT_SUFFIX,
                              'userpassword': USER_PW})))
    except ldap.LDAPError as e:
        log.fatal('test_ticket548: Failed to add user' + USER1_DN + ': error ' + e.message['desc'])
        assert False

    edate = int(time.time() / (60 * 60 * 24))
    log.info('Search entry %s' % USER1_DN)

    log.info("Bind as %s" % USER1_DN)
    topology_st.standalone.simple_bind_s(USER1_DN, USER_PW)
    entry = topology_st.standalone.getEntry(USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)", ['shadowLastChange'])
    check_shadow_attr_value(entry, 'shadowLastChange', edate, USER1_DN)

    log.info("Check shadowAccount with no policy was successfully verified.")


def test_ticket548_test_global_policy(topology_st):
    """
    Check shadowAccount with global password policy
    """

    log.info("Case 2.  Check shadowAccount with global password policy")

    log.info("Bind as %s" % DN_DM)
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)

    set_global_pwpolicy(topology_st)

    log.info('Add an entry' + USER2_DN)
    try:
        topology_st.standalone.add_s(
            Entry((USER2_DN, {'objectclass': "top person organizationalPerson inetOrgPerson shadowAccount".split(),
                              'sn': '2',
                              'cn': 'user 2',
                              'uid': 'user2',
                              'givenname': 'user',
                              'mail': 'user2@' + DEFAULT_SUFFIX,
                              'userpassword': USER_PW})))
    except ldap.LDAPError as e:
        log.fatal('test_ticket548: Failed to add user' + USER2_DN + ': error ' + e.message['desc'])
        assert False

    edate = int(time.time() / (60 * 60 * 24))

    log.info("Bind as %s" % USER1_DN)
    topology_st.standalone.simple_bind_s(USER1_DN, USER_PW)

    log.info('Search entry %s' % USER1_DN)
    entry = topology_st.standalone.getEntry(USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    check_shadow_attr_value(entry, 'shadowLastChange', edate, USER1_DN)

    # passwordMinAge -- 1 day
    check_shadow_attr_value(entry, 'shadowMin', 1, USER1_DN)

    # passwordMaxAge -- 10 days
    check_shadow_attr_value(entry, 'shadowMax', 10, USER1_DN)

    # passwordWarning -- 3 days
    check_shadow_attr_value(entry, 'shadowWarning', 3, USER1_DN)

    log.info("Bind as %s" % USER2_DN)
    topology_st.standalone.simple_bind_s(USER2_DN, USER_PW)

    log.info('Search entry %s' % USER2_DN)
    entry = topology_st.standalone.getEntry(USER2_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    check_shadow_attr_value(entry, 'shadowLastChange', edate, USER2_DN)

    # passwordMinAge -- 1 day
    check_shadow_attr_value(entry, 'shadowMin', 1, USER2_DN)

    # passwordMaxAge -- 10 days
    check_shadow_attr_value(entry, 'shadowMax', 10, USER2_DN)

    # passwordWarning -- 3 days
    check_shadow_attr_value(entry, 'shadowWarning', 3, USER2_DN)

    # Bind as DM again, change policy
    log.info("Bind as %s" % DN_DM)
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    set_global_pwpolicy(topology_st, 3, 30, 9)

    # change the user password, then check again.
    log.info("Bind as %s" % USER2_DN)
    topology_st.standalone.simple_bind_s(USER2_DN, USER_PW)

    newpasswd = USER_PW + '2'
    update_passwd(topology_st, USER2_DN, USER_PW, newpasswd)

    log.info("Re-bind as %s with new password" % USER2_DN)
    topology_st.standalone.simple_bind_s(USER2_DN, newpasswd)

    ## This tests if we update the shadow values on password change.
    log.info('Search entry %s' % USER2_DN)
    entry = topology_st.standalone.getEntry(USER2_DN, ldap.SCOPE_BASE, "(objectclass=*)")

    # passwordMinAge -- 1 day
    check_shadow_attr_value(entry, 'shadowMin', 3, USER2_DN)

    # passwordMaxAge -- 10 days
    check_shadow_attr_value(entry, 'shadowMax', 30, USER2_DN)

    # passwordWarning -- 3 days
    check_shadow_attr_value(entry, 'shadowWarning', 9, USER2_DN)

    log.info("Check shadowAccount with global policy was successfully verified.")


def test_ticket548_test_subtree_policy(topology_st):
    """
    Check shadowAccount with subtree level password policy
    """

    log.info("Case 3.  Check shadowAccount with subtree level password policy")

    log.info("Bind as %s" % DN_DM)
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
    # Check the global policy values

    set_subtree_pwpolicy(topology_st, 2, 20, 6)

    log.info('Add an entry' + USER3_DN)
    try:
        topology_st.standalone.add_s(
            Entry((USER3_DN, {'objectclass': "top person organizationalPerson inetOrgPerson shadowAccount".split(),
                              'sn': '3',
                              'cn': 'user 3',
                              'uid': 'user3',
                              'givenname': 'user',
                              'mail': 'user3@' + DEFAULT_SUFFIX,
                              'userpassword': USER_PW})))
    except ldap.LDAPError as e:
        log.fatal('test_ticket548: Failed to add user' + USER3_DN + ': error ' + e.message['desc'])
        assert False

    log.info('Search entry %s' % USER3_DN)
    entry0 = topology_st.standalone.getEntry(USER3_DN, ldap.SCOPE_BASE, "(objectclass=*)")

    log.info('Expecting shadowLastChange 0 since passwordMustChange is on')
    check_shadow_attr_value(entry0, 'shadowLastChange', 0, USER3_DN)

    # passwordMinAge -- 2 day
    check_shadow_attr_value(entry0, 'shadowMin', 2, USER3_DN)

    # passwordMaxAge -- 20 days
    check_shadow_attr_value(entry0, 'shadowMax', 20, USER3_DN)

    # passwordWarning -- 6 days
    check_shadow_attr_value(entry0, 'shadowWarning', 6, USER3_DN)

    log.info("Bind as %s" % USER3_DN)
    topology_st.standalone.simple_bind_s(USER3_DN, USER_PW)

    log.info('Search entry %s' % USER3_DN)
    try:
        entry1 = topology_st.standalone.getEntry(USER3_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    except ldap.UNWILLING_TO_PERFORM:
        log.info('test_ticket548: Search by' + USER3_DN + ' failed by UNWILLING_TO_PERFORM as expected')
    except ldap.LDAPError as e:
        log.fatal('test_ticket548: Failed to serch user' + USER3_DN + ' by self: error ' + e.message['desc'])
        assert False

    log.info("Bind as %s and updating the password with a new one" % USER3_DN)
    topology_st.standalone.simple_bind_s(USER3_DN, USER_PW)

    # Bind as DM again, change policy
    log.info("Bind as %s" % DN_DM)
    topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)

    set_subtree_pwpolicy(topology_st, 4, 40, 12)

    newpasswd = USER_PW + '0'
    update_passwd(topology_st, USER3_DN, USER_PW, newpasswd)

    log.info("Re-bind as %s with new password" % USER3_DN)
    topology_st.standalone.simple_bind_s(USER3_DN, newpasswd)

    try:
        entry2 = topology_st.standalone.getEntry(USER3_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    except ldap.LDAPError as e:
        log.fatal('test_ticket548: Failed to serch user' + USER3_DN + ' by self: error ' + e.message['desc'])
        assert False

    edate = int(time.time() / (60 * 60 * 24))

    log.info('Expecting shadowLastChange %d once userPassword is updated', edate)
    check_shadow_attr_value(entry2, 'shadowLastChange', edate, USER3_DN)

    log.info('Search entry %s' % USER3_DN)
    entry = topology_st.standalone.getEntry(USER3_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    check_shadow_attr_value(entry, 'shadowLastChange', edate, USER3_DN)

    # passwordMinAge -- 1 day
    check_shadow_attr_value(entry, 'shadowMin', 4, USER3_DN)

    # passwordMaxAge -- 10 days
    check_shadow_attr_value(entry, 'shadowMax', 40, USER3_DN)

    # passwordWarning -- 3 days
    check_shadow_attr_value(entry, 'shadowWarning', 12, USER3_DN)

    log.info("Check shadowAccount with subtree level policy was successfully verified.")


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
