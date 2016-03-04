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

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

installation1_prefix = None


class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):
    global installation1_prefix
    if installation1_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation1_prefix

    # Creating standalone instance ...
    standalone = DirSrv(verbose=False)
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


def test_config_init(topology):
    '''
    Initialization function
    '''
    return


def test_config_listen_backport_size(topology):
    '''
    We need to check that we can search on nsslapd-listen-backlog-size,
    and change its value: to a psoitive number and a negative number.
    Verify invalid value is rejected.
    '''

    log.info('Running test_config_listen_backport_size...')

    try:
        entry = topology.standalone.search_s(DN_CONFIG, ldap.SCOPE_BASE, 'objectclass=top',
                                             ['nsslapd-listen-backlog-size'])
        default_val = entry[0].getValue('nsslapd-listen-backlog-size')
        if not default_val:
            log.fatal('test_config_listen_backport_size: Failed to get nsslapd-listen-backlog-size from config')
            assert False
    except ldap.LDAPError, e:
        log.fatal('test_config_listen_backport_size: Failed to search config, error: ' + e.message('desc'))
        assert False

    try:
        topology.standalone.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE, 'nsslapd-listen-backlog-size', '256')])
    except ldap.LDAPError, e:
        log.fatal('test_config_listen_backport_size: Failed to modify config, error: ' + e.message('desc'))
        assert False

    try:
        topology.standalone.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE, 'nsslapd-listen-backlog-size', '-1')])
    except ldap.LDAPError, e:
        log.fatal('test_config_listen_backport_size: Failed to modify config(negative value), error: ' + e.message('desc'))
        assert False

    try:
        topology.standalone.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE, 'nsslapd-listen-backlog-size', 'ZZ')])
        log.fatal('test_config_listen_backport_size: Invalid value was successfully added')
        assert False
    except ldap.LDAPError, e:
        pass

    #
    # Cleanup - undo what we've done
    #
    try:
        topology.standalone.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE, 'nsslapd-listen-backlog-size', default_val)])
    except ldap.LDAPError, e:
        log.fatal('test_config_listen_backport_size: Failed to reset config, error: ' + e.message('desc'))
        assert False

    log.info('test_config_listen_backport_size: PASSED')


def test_config_deadlock_policy(topology):
    '''
    We need to check that nsslapd-db-deadlock-policy exists, that we can
    change the value, and invalid values are rejected
    '''

    log.info('Running test_config_deadlock_policy...')

    LDBM_DN = 'cn=config,cn=ldbm database,cn=plugins,cn=config'
    default_val = '9'

    try:
        entry = topology.standalone.search_s(LDBM_DN, ldap.SCOPE_BASE, 'objectclass=top',
                                             ['nsslapd-db-deadlock-policy'])
        val = entry[0].getValue('nsslapd-db-deadlock-policy')
        if not val:
            log.fatal('test_config_deadlock_policy: Failed to get nsslapd-db-deadlock-policy from config')
            assert False
        if val != default_val:
            log.fatal('test_config_deadlock_policy: The wrong derfualt value was present:  (%s) but expected (%s)' %
                      (val, default_val))
            assert False
    except ldap.LDAPError, e:
        log.fatal('test_config_deadlock_policy: Failed to search config, error: ' + e.message('desc'))
        assert False

    # Try a range of valid values
    for val in ('0', '5', '9'):
        try:
            topology.standalone.modify_s(LDBM_DN, [(ldap.MOD_REPLACE, 'nsslapd-db-deadlock-policy', val)])
        except ldap.LDAPError, e:
            log.fatal('test_config_deadlock_policy: Failed to modify config: nsslapd-db-deadlock-policy to (%s), error: %s' %
                      (val, e.message('desc')))
            assert False

    # Try a range of invalid values
    for val in ('-1', '10'):
        try:
            topology.standalone.modify_s(LDBM_DN, [(ldap.MOD_REPLACE, 'nsslapd-db-deadlock-policy', val)])
            log.fatal('test_config_deadlock_policy: Able to add invalid value to nsslapd-db-deadlock-policy(%s)' % (val))
            assert False
        except ldap.LDAPError, e:
            pass
    #
    # Cleanup - undo what we've done
    #
    try:
        topology.standalone.modify_s(LDBM_DN, [(ldap.MOD_REPLACE, 'nsslapd-db-deadlock-policy', default_val)])
    except ldap.LDAPError, e:
        log.fatal('test_config_deadlock_policy: Failed to reset nsslapd-db-deadlock-policy to the default value(%s), error: %s' %
                  (default_val, e.message('desc')))

    log.info('test_config_deadlock_policy: PASSED')


def test_config_final(topology):
    topology.standalone.delete()
    log.info('Testcase PASSED')


def run_isolated():
    '''
    This test suite is designed to test all things cn=config Like, the core cn=config settings,
    or the ldbm database settings, etc.  This suite shoud not test individual plugins - there
    should be individual suites for each plugin.
    '''
    global installation1_prefix
    installation1_prefix = None

    topo = topology(True)
    test_config_init(topo)

    test_config_listen_backport_size(topo)
    test_config_deadlock_policy(topo)

    test_config_final(topo)


if __name__ == '__main__':
    run_isolated()

