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
from lib389.utils import *

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

installation1_prefix = None

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


class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):
    global installation1_prefix

    # Creating standalone instance ...
    standalone = DirSrv(verbose=False)
    if installation1_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation1_prefix
    args_instance[SER_HOST] = HOST_STANDALONE
    args_instance[SER_PORT] = PORT_STANDALONE
    args_instance[SER_SERVERID_PROP] = SERVERID_STANDALONE
    args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
    args_standalone = args_instance.copy()
    standalone.allocate(args_standalone)
    instance_standalone = standalone.exists()
    if instance_standalone:
        standalone.delete()
    standalone.create()
    standalone.open()

    # Clear out the tmp dir
    standalone.clearTmpDir(__file__)

    return TopologyStandalone(standalone)

def test_ticket47927_init(topology):
    topology.standalone.plugins.enable(name=PLUGIN_ATTR_UNIQUENESS)
    try:
        topology.standalone.modify_s('cn=' + PLUGIN_ATTR_UNIQUENESS + ',cn=plugins,cn=config',
                      [(ldap.MOD_REPLACE, 'uniqueness-attribute-name', 'telephonenumber'),
                       (ldap.MOD_REPLACE, 'uniqueness-subtrees', DEFAULT_SUFFIX),
                      ])
    except ldap.LDAPError, e:
        log.fatal('test_ticket47927: Failed to configure plugin for "telephonenumber": error ' + e.message['desc'])
        assert False
    topology.standalone.restart(timeout=120)

    topology.standalone.add_s(Entry((EXCLUDED_CONTAINER_DN, {'objectclass': "top nscontainer".split(),
                                                             'cn': EXCLUDED_CONTAINER_CN})))
    topology.standalone.add_s(Entry((EXCLUDED_BIS_CONTAINER_DN, {'objectclass': "top nscontainer".split(),
                                                                 'cn': EXCLUDED_BIS_CONTAINER_CN})))
    topology.standalone.add_s(Entry((ENFORCED_CONTAINER_DN, {'objectclass': "top nscontainer".split(),
                                                             'cn': ENFORCED_CONTAINER_CN})))

        # adding an entry on a stage with a different 'cn'
    topology.standalone.add_s(Entry((USER_1_DN, {
                                    'objectclass': "top person".split(),
                                    'sn':           USER_1_CN,
                                    'cn':           USER_1_CN})))
        # adding an entry on a stage with a different 'cn'
    topology.standalone.add_s(Entry((USER_2_DN, {
                                    'objectclass': "top person".split(),
                                    'sn':           USER_2_CN,
                                    'cn':           USER_2_CN})))
    topology.standalone.add_s(Entry((USER_3_DN, {
                                    'objectclass': "top person".split(),
                                    'sn':           USER_3_CN,
                                    'cn':           USER_3_CN})))
    topology.standalone.add_s(Entry((USER_4_DN, {
                                    'objectclass': "top person".split(),
                                    'sn':           USER_4_CN,
                                    'cn':           USER_4_CN})))
    
def test_ticket47927_one(topology):
    '''
    Check that uniqueness is enforce on all SUFFIX
    '''
    UNIQUE_VALUE='1234'
    try:
        topology.standalone.modify_s(USER_1_DN,
                      [(ldap.MOD_REPLACE, 'telephonenumber', UNIQUE_VALUE)])
    except ldap.LDAPError, e:
        log.fatal('test_ticket47927_one: Failed to set the telephonenumber for %s: %s' % (USER_1_DN, e.message['desc']))
        assert False

    # we expect to fail because user1 is in the scope of the plugin
    try:
        topology.standalone.modify_s(USER_2_DN,
                      [(ldap.MOD_REPLACE, 'telephonenumber', UNIQUE_VALUE)])
        log.fatal('test_ticket47927_one: unexpected success to set the telephonenumber for %s' % (USER_2_DN))
        assert False
    except ldap.LDAPError, e:
        log.fatal('test_ticket47927_one: Failed (expected) to set the telephonenumber for %s: %s' % (USER_2_DN, e.message['desc']))
        pass


    # we expect to fail because user1 is in the scope of the plugin
    try:
        topology.standalone.modify_s(USER_3_DN,
                      [(ldap.MOD_REPLACE, 'telephonenumber', UNIQUE_VALUE)])
        log.fatal('test_ticket47927_one: unexpected success to set the telephonenumber for %s' % (USER_3_DN))
        assert False
    except ldap.LDAPError, e:
        log.fatal('test_ticket47927_one: Failed (expected) to set the telephonenumber for %s: %s' % (USER_3_DN, e.message['desc']))
        pass


def test_ticket47927_two(topology):
    '''
    Exclude the EXCLUDED_CONTAINER_DN from the uniqueness plugin
    '''
    try:
        topology.standalone.modify_s('cn=' + PLUGIN_ATTR_UNIQUENESS + ',cn=plugins,cn=config',
                      [(ldap.MOD_REPLACE, 'uniqueness-exclude-subtrees', EXCLUDED_CONTAINER_DN)])
    except ldap.LDAPError, e:
        log.fatal('test_ticket47927_two: Failed to configure plugin for to exclude %s: error %s' % (EXCLUDED_CONTAINER_DN, e.message['desc']))
        assert False
    topology.standalone.restart(timeout=120)

def test_ticket47927_three(topology):
    '''
    Check that uniqueness is enforced on full SUFFIX except EXCLUDED_CONTAINER_DN
    First case: it exists an entry (with the same attribute value) in the scope
    of the plugin and we set the value in an entry that is in an excluded scope
    '''
    UNIQUE_VALUE='9876'
    try:
        topology.standalone.modify_s(USER_1_DN,
                      [(ldap.MOD_REPLACE, 'telephonenumber', UNIQUE_VALUE)])
    except ldap.LDAPError, e:
        log.fatal('test_ticket47927_three: Failed to set the telephonenumber ' + e.message['desc'])
        assert False
    
    # we should not be allowed to set this value (because user1 is in the scope)
    try:
        topology.standalone.modify_s(USER_2_DN,
                      [(ldap.MOD_REPLACE, 'telephonenumber', UNIQUE_VALUE)])
        log.fatal('test_ticket47927_three: unexpected success to set the telephonenumber for %s' % (USER_2_DN))
        assert False
    except ldap.LDAPError, e:
        log.fatal('test_ticket47927_three: Failed (expected) to set the telephonenumber for %s: %s' % (USER_2_DN , e.message['desc']))


    # USER_3_DN is in EXCLUDED_CONTAINER_DN so update should be successful
    try:
        topology.standalone.modify_s(USER_3_DN,
                      [(ldap.MOD_REPLACE, 'telephonenumber', UNIQUE_VALUE)])
        log.fatal('test_ticket47927_three: success to set the telephonenumber for %s' % (USER_3_DN))
    except ldap.LDAPError, e:
        log.fatal('test_ticket47927_three: Failed (unexpected) to set the telephonenumber for %s: %s' % (USER_3_DN, e.message['desc']))
        assert False


def test_ticket47927_four(topology):
    '''
    Check that uniqueness is enforced on full SUFFIX except EXCLUDED_CONTAINER_DN
    Second case: it exists an entry (with the same attribute value) in an excluded scope
    of the plugin and we set the value in an entry is in the scope
    '''
    UNIQUE_VALUE='1111'
    # USER_3_DN is in EXCLUDED_CONTAINER_DN so update should be successful
    try:
        topology.standalone.modify_s(USER_3_DN,
                      [(ldap.MOD_REPLACE, 'telephonenumber', UNIQUE_VALUE)])
        log.fatal('test_ticket47927_four: success to set the telephonenumber for %s' % USER_3_DN)
    except ldap.LDAPError, e:
        log.fatal('test_ticket47927_four: Failed (unexpected) to set the telephonenumber for %s: %s' % (USER_3_DN, e.message['desc']))
        assert False


    # we should be allowed to set this value (because user3 is excluded from scope)
    try:
        topology.standalone.modify_s(USER_1_DN,
                      [(ldap.MOD_REPLACE, 'telephonenumber', UNIQUE_VALUE)])
    except ldap.LDAPError, e:
        log.fatal('test_ticket47927_four: Failed to set the telephonenumber for %s: %s' % (USER_1_DN, e.message['desc']))
        assert False

    # we should not be allowed to set this value (because user1 is in the scope)
    try:
        topology.standalone.modify_s(USER_2_DN,
                      [(ldap.MOD_REPLACE, 'telephonenumber', UNIQUE_VALUE)])
        log.fatal('test_ticket47927_four: unexpected success to set the telephonenumber %s' % USER_2_DN)
        assert False
    except ldap.LDAPError, e:
        log.fatal('test_ticket47927_four: Failed (expected) to set the telephonenumber for %s: %s' % (USER_2_DN, e.message['desc']))
        pass

def test_ticket47927_five(topology):
    '''
    Exclude the EXCLUDED_BIS_CONTAINER_DN from the uniqueness plugin
    '''
    try:
        topology.standalone.modify_s('cn=' + PLUGIN_ATTR_UNIQUENESS + ',cn=plugins,cn=config',
                      [(ldap.MOD_ADD, 'uniqueness-exclude-subtrees', EXCLUDED_BIS_CONTAINER_DN)])
    except ldap.LDAPError, e:
        log.fatal('test_ticket47927_five: Failed to configure plugin for to exclude %s: error %s' % (EXCLUDED_BIS_CONTAINER_DN, e.message['desc']))
        assert False
    topology.standalone.restart(timeout=120)
    topology.standalone.getEntry('cn=' + PLUGIN_ATTR_UNIQUENESS + ',cn=plugins,cn=config', ldap.SCOPE_BASE)

def test_ticket47927_six(topology):
    '''
    Check that uniqueness is enforced on full SUFFIX except EXCLUDED_CONTAINER_DN
    and EXCLUDED_BIS_CONTAINER_DN
    First case: it exists an entry (with the same attribute value) in the scope
    of the plugin and we set the value in an entry that is in an excluded scope
    '''
    UNIQUE_VALUE='222'
    try:
        topology.standalone.modify_s(USER_1_DN,
                      [(ldap.MOD_REPLACE, 'telephonenumber', UNIQUE_VALUE)])
    except ldap.LDAPError, e:
        log.fatal('test_ticket47927_six: Failed to set the telephonenumber ' + e.message['desc'])
        assert False
    
    # we should not be allowed to set this value (because user1 is in the scope)
    try:
        topology.standalone.modify_s(USER_2_DN,
                      [(ldap.MOD_REPLACE, 'telephonenumber', UNIQUE_VALUE)])
        log.fatal('test_ticket47927_six: unexpected success to set the telephonenumber for %s' % (USER_2_DN))
        assert False
    except ldap.LDAPError, e:
        log.fatal('test_ticket47927_six: Failed (expected) to set the telephonenumber for %s: %s' % (USER_2_DN , e.message['desc']))


    # USER_3_DN is in EXCLUDED_CONTAINER_DN so update should be successful
    try:
        topology.standalone.modify_s(USER_3_DN,
                      [(ldap.MOD_REPLACE, 'telephonenumber', UNIQUE_VALUE)])
        log.fatal('test_ticket47927_six: success to set the telephonenumber for %s' % (USER_3_DN))
    except ldap.LDAPError, e:
        log.fatal('test_ticket47927_six: Failed (unexpected) to set the telephonenumber for %s: %s' % (USER_3_DN, e.message['desc']))
        assert False
    # USER_4_DN is in EXCLUDED_CONTAINER_DN so update should be successful
    try:
        topology.standalone.modify_s(USER_4_DN,
                      [(ldap.MOD_REPLACE, 'telephonenumber', UNIQUE_VALUE)])
        log.fatal('test_ticket47927_six: success to set the telephonenumber for %s' % (USER_4_DN))
    except ldap.LDAPError, e:
        log.fatal('test_ticket47927_six: Failed (unexpected) to set the telephonenumber for %s: %s' % (USER_4_DN, e.message['desc']))
        assert False


def test_ticket47927_final(topology):
    topology.standalone.delete()
    log.info('Testcase PASSED')


def run_isolated():
    global installation1_prefix
    installation1_prefix = None

    topo = topology(True)
    test_ticket47927_init(topo)
    test_ticket47927_one(topo)
    test_ticket47927_two(topo)
    test_ticket47927_three(topo)
    test_ticket47927_four(topo)
    test_ticket47927_five(topo)
    test_ticket47927_six(topo)
    test_ticket47927_final(topo)


if __name__ == '__main__':
    run_isolated()

