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

from lib389._constants import SUFFIX, DEFAULT_SUFFIX, PLUGIN_ATTR_UNIQUENESS

# Skip on older versions
pytestmark = [pytest.mark.tier2,
              pytest.mark.skipif(ds_is_older('1.3.4'), reason="Not implemented")]

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

EXCLUDED_CONTAINER_CN = "excluded_container"
EXCLUDED_CONTAINER_DN = "cn=%s,%s" % (EXCLUDED_CONTAINER_CN, SUFFIX)

EXCLUDED_BIS_CONTAINER_CN = "excluded_bis_container"
EXCLUDED_BIS_CONTAINER_DN = "cn=%s,%s" % (EXCLUDED_BIS_CONTAINER_CN, SUFFIX)

ENFORCED_CONTAINER_CN = "enforced_container"
ENFORCED_CONTAINER_DN = "cn=%s,%s" % (ENFORCED_CONTAINER_CN, SUFFIX)

USER_1_CN = "test_1"
USER_1_DN = "cn=%s,%s" % (USER_1_CN, ENFORCED_CONTAINER_DN)
USER_2_CN = "test_2"
USER_2_DN = "cn=%s,%s" % (USER_2_CN, ENFORCED_CONTAINER_DN)
USER_3_CN = "test_3"
USER_3_DN = "cn=%s,%s" % (USER_3_CN, EXCLUDED_CONTAINER_DN)
USER_4_CN = "test_4"
USER_4_DN = "cn=%s,%s" % (USER_4_CN, EXCLUDED_BIS_CONTAINER_DN)


def test_ticket47927_init(topology_st):
    topology_st.standalone.plugins.enable(name=PLUGIN_ATTR_UNIQUENESS)
    try:
        topology_st.standalone.modify_s('cn=' + PLUGIN_ATTR_UNIQUENESS + ',cn=plugins,cn=config',
                                        [(ldap.MOD_REPLACE, 'uniqueness-attribute-name', b'telephonenumber'),
                                         (ldap.MOD_REPLACE, 'uniqueness-subtrees', ensure_bytes(DEFAULT_SUFFIX)),
                                         ])
    except ldap.LDAPError as e:
        log.fatal('test_ticket47927: Failed to configure plugin for "telephonenumber": error ' + e.args[0]['desc'])
        assert False
    topology_st.standalone.restart(timeout=120)

    topology_st.standalone.add_s(Entry((EXCLUDED_CONTAINER_DN, {'objectclass': "top nscontainer".split(),
                                                                'cn': EXCLUDED_CONTAINER_CN})))
    topology_st.standalone.add_s(Entry((EXCLUDED_BIS_CONTAINER_DN, {'objectclass': "top nscontainer".split(),
                                                                    'cn': EXCLUDED_BIS_CONTAINER_CN})))
    topology_st.standalone.add_s(Entry((ENFORCED_CONTAINER_DN, {'objectclass': "top nscontainer".split(),
                                                                'cn': ENFORCED_CONTAINER_CN})))

    # adding an entry on a stage with a different 'cn'
    topology_st.standalone.add_s(Entry((USER_1_DN, {
        'objectclass': "top person".split(),
        'sn': USER_1_CN,
        'cn': USER_1_CN})))
    # adding an entry on a stage with a different 'cn'
    topology_st.standalone.add_s(Entry((USER_2_DN, {
        'objectclass': "top person".split(),
        'sn': USER_2_CN,
        'cn': USER_2_CN})))
    topology_st.standalone.add_s(Entry((USER_3_DN, {
        'objectclass': "top person".split(),
        'sn': USER_3_CN,
        'cn': USER_3_CN})))
    topology_st.standalone.add_s(Entry((USER_4_DN, {
        'objectclass': "top person".split(),
        'sn': USER_4_CN,
        'cn': USER_4_CN})))


def test_ticket47927_one(topology_st):
    '''
    Check that uniqueness is enforce on all SUFFIX
    '''
    UNIQUE_VALUE = b'1234'
    try:
        topology_st.standalone.modify_s(USER_1_DN,
                                        [(ldap.MOD_REPLACE, 'telephonenumber', UNIQUE_VALUE)])
    except ldap.LDAPError as e:
        log.fatal('test_ticket47927_one: Failed to set the telephonenumber for %s: %s' % (USER_1_DN, e.args[0]['desc']))
        assert False

    # we expect to fail because user1 is in the scope of the plugin
    try:
        topology_st.standalone.modify_s(USER_2_DN,
                                        [(ldap.MOD_REPLACE, 'telephonenumber', UNIQUE_VALUE)])
        log.fatal('test_ticket47927_one: unexpected success to set the telephonenumber for %s' % (USER_2_DN))
        assert False
    except ldap.LDAPError as e:
        log.fatal('test_ticket47927_one: Failed (expected) to set the telephonenumber for %s: %s' % (
        USER_2_DN, e.args[0]['desc']))
        pass

    # we expect to fail because user1 is in the scope of the plugin
    try:
        topology_st.standalone.modify_s(USER_3_DN,
                                        [(ldap.MOD_REPLACE, 'telephonenumber', UNIQUE_VALUE)])
        log.fatal('test_ticket47927_one: unexpected success to set the telephonenumber for %s' % (USER_3_DN))
        assert False
    except ldap.LDAPError as e:
        log.fatal('test_ticket47927_one: Failed (expected) to set the telephonenumber for %s: %s' % (
        USER_3_DN, e.args[0]['desc']))
        pass


def test_ticket47927_two(topology_st):
    '''
    Exclude the EXCLUDED_CONTAINER_DN from the uniqueness plugin
    '''
    try:
        topology_st.standalone.modify_s('cn=' + PLUGIN_ATTR_UNIQUENESS + ',cn=plugins,cn=config',
                                        [(ldap.MOD_REPLACE, 'uniqueness-exclude-subtrees', ensure_bytes(EXCLUDED_CONTAINER_DN))])
    except ldap.LDAPError as e:
        log.fatal('test_ticket47927_two: Failed to configure plugin for to exclude %s: error %s' % (
        EXCLUDED_CONTAINER_DN, e.args[0]['desc']))
        assert False
    topology_st.standalone.restart(timeout=120)


def test_ticket47927_three(topology_st):
    '''
    Check that uniqueness is enforced on full SUFFIX except EXCLUDED_CONTAINER_DN
    First case: it exists an entry (with the same attribute value) in the scope
    of the plugin and we set the value in an entry that is in an excluded scope
    '''
    UNIQUE_VALUE = b'9876'
    try:
        topology_st.standalone.modify_s(USER_1_DN,
                                        [(ldap.MOD_REPLACE, 'telephonenumber', UNIQUE_VALUE)])
    except ldap.LDAPError as e:
        log.fatal('test_ticket47927_three: Failed to set the telephonenumber ' + e.args[0]['desc'])
        assert False

    # we should not be allowed to set this value (because user1 is in the scope)
    try:
        topology_st.standalone.modify_s(USER_2_DN,
                                        [(ldap.MOD_REPLACE, 'telephonenumber', UNIQUE_VALUE)])
        log.fatal('test_ticket47927_three: unexpected success to set the telephonenumber for %s' % (USER_2_DN))
        assert False
    except ldap.LDAPError as e:
        log.fatal('test_ticket47927_three: Failed (expected) to set the telephonenumber for %s: %s' % (
        USER_2_DN, e.args[0]['desc']))

    # USER_3_DN is in EXCLUDED_CONTAINER_DN so update should be successful
    try:
        topology_st.standalone.modify_s(USER_3_DN,
                                        [(ldap.MOD_REPLACE, 'telephonenumber', UNIQUE_VALUE)])
        log.fatal('test_ticket47927_three: success to set the telephonenumber for %s' % (USER_3_DN))
    except ldap.LDAPError as e:
        log.fatal('test_ticket47927_three: Failed (unexpected) to set the telephonenumber for %s: %s' % (
        USER_3_DN, e.args[0]['desc']))
        assert False


def test_ticket47927_four(topology_st):
    '''
    Check that uniqueness is enforced on full SUFFIX except EXCLUDED_CONTAINER_DN
    Second case: it exists an entry (with the same attribute value) in an excluded scope
    of the plugin and we set the value in an entry is in the scope
    '''
    UNIQUE_VALUE = b'1111'
    # USER_3_DN is in EXCLUDED_CONTAINER_DN so update should be successful
    try:
        topology_st.standalone.modify_s(USER_3_DN,
                                        [(ldap.MOD_REPLACE, 'telephonenumber', UNIQUE_VALUE)])
        log.fatal('test_ticket47927_four: success to set the telephonenumber for %s' % USER_3_DN)
    except ldap.LDAPError as e:
        log.fatal('test_ticket47927_four: Failed (unexpected) to set the telephonenumber for %s: %s' % (
        USER_3_DN, e.args[0]['desc']))
        assert False

    # we should be allowed to set this value (because user3 is excluded from scope)
    try:
        topology_st.standalone.modify_s(USER_1_DN,
                                        [(ldap.MOD_REPLACE, 'telephonenumber', UNIQUE_VALUE)])
    except ldap.LDAPError as e:
        log.fatal(
            'test_ticket47927_four: Failed to set the telephonenumber for %s: %s' % (USER_1_DN, e.args[0]['desc']))
        assert False

    # we should not be allowed to set this value (because user1 is in the scope)
    try:
        topology_st.standalone.modify_s(USER_2_DN,
                                        [(ldap.MOD_REPLACE, 'telephonenumber', UNIQUE_VALUE)])
        log.fatal('test_ticket47927_four: unexpected success to set the telephonenumber %s' % USER_2_DN)
        assert False
    except ldap.LDAPError as e:
        log.fatal('test_ticket47927_four: Failed (expected) to set the telephonenumber for %s: %s' % (
        USER_2_DN, e.args[0]['desc']))
        pass


def test_ticket47927_five(topology_st):
    '''
    Exclude the EXCLUDED_BIS_CONTAINER_DN from the uniqueness plugin
    '''
    try:
        topology_st.standalone.modify_s('cn=' + PLUGIN_ATTR_UNIQUENESS + ',cn=plugins,cn=config',
                                        [(ldap.MOD_ADD, 'uniqueness-exclude-subtrees', ensure_bytes(EXCLUDED_BIS_CONTAINER_DN))])
    except ldap.LDAPError as e:
        log.fatal('test_ticket47927_five: Failed to configure plugin for to exclude %s: error %s' % (
        EXCLUDED_BIS_CONTAINER_DN, e.args[0]['desc']))
        assert False
    topology_st.standalone.restart(timeout=120)
    topology_st.standalone.getEntry('cn=' + PLUGIN_ATTR_UNIQUENESS + ',cn=plugins,cn=config', ldap.SCOPE_BASE)


def test_ticket47927_six(topology_st):
    '''
    Check that uniqueness is enforced on full SUFFIX except EXCLUDED_CONTAINER_DN
    and EXCLUDED_BIS_CONTAINER_DN
    First case: it exists an entry (with the same attribute value) in the scope
    of the plugin and we set the value in an entry that is in an excluded scope
    '''
    UNIQUE_VALUE = b'222'
    try:
        topology_st.standalone.modify_s(USER_1_DN,
                                        [(ldap.MOD_REPLACE, 'telephonenumber', UNIQUE_VALUE)])
    except ldap.LDAPError as e:
        log.fatal('test_ticket47927_six: Failed to set the telephonenumber ' + e.args[0]['desc'])
        assert False

    # we should not be allowed to set this value (because user1 is in the scope)
    try:
        topology_st.standalone.modify_s(USER_2_DN,
                                        [(ldap.MOD_REPLACE, 'telephonenumber', UNIQUE_VALUE)])
        log.fatal('test_ticket47927_six: unexpected success to set the telephonenumber for %s' % (USER_2_DN))
        assert False
    except ldap.LDAPError as e:
        log.fatal('test_ticket47927_six: Failed (expected) to set the telephonenumber for %s: %s' % (
        USER_2_DN, e.args[0]['desc']))

    # USER_3_DN is in EXCLUDED_CONTAINER_DN so update should be successful
    try:
        topology_st.standalone.modify_s(USER_3_DN,
                                        [(ldap.MOD_REPLACE, 'telephonenumber', UNIQUE_VALUE)])
        log.fatal('test_ticket47927_six: success to set the telephonenumber for %s' % (USER_3_DN))
    except ldap.LDAPError as e:
        log.fatal('test_ticket47927_six: Failed (unexpected) to set the telephonenumber for %s: %s' % (
        USER_3_DN, e.args[0]['desc']))
        assert False
    # USER_4_DN is in EXCLUDED_CONTAINER_DN so update should be successful
    try:
        topology_st.standalone.modify_s(USER_4_DN,
                                        [(ldap.MOD_REPLACE, 'telephonenumber', UNIQUE_VALUE)])
        log.fatal('test_ticket47927_six: success to set the telephonenumber for %s' % (USER_4_DN))
    except ldap.LDAPError as e:
        log.fatal('test_ticket47927_six: Failed (unexpected) to set the telephonenumber for %s: %s' % (
        USER_4_DN, e.args[0]['desc']))
        assert False


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
