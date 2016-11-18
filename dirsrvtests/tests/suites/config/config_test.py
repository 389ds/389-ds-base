# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
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
from lib389.topologies import topology_m2

USER_DN = 'uid=test_user,%s' % DEFAULT_SUFFIX

logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


@pytest.fixture(scope="module")
def big_file():
    TEMP_BIG_FILE = ''
    # 1024*1024=1048576
    # B for 1 MiB
    # Big for 3 MiB
    for x in range(1048576):
        TEMP_BIG_FILE += '+'

    return TEMP_BIG_FILE


@pytest.fixture
def test_user(topology_m2):
    """Add and remove test user"""

    try:
        topology_m2.ms["master1"].add_s(Entry((USER_DN, {
            'uid': 'test_user',
            'givenName': 'test_user',
            'objectclass': ['top', 'person',
                            'organizationalPerson',
                            'inetorgperson'],
            'cn': 'test_user',
            'sn': 'test_user'})))
        time.sleep(1)
    except ldap.LDAPError as e:
        log.fatal('Failed to add user (%s): error (%s)' % (USER_DN,
                                                           e.message['desc']))
        raise

    def fin():
        try:
            topology_m2.ms["master1"].delete_s(USER_DN)
            time.sleep(1)
        except ldap.LDAPError as e:
            log.fatal('Failed to delete user (%s): error (%s)' % (
                USER_DN,
                e.message['desc']))
            raise


def test_maxbersize_repl(topology_m2, test_user, big_file):
    """maxbersize is ignored in the replicated operations.

    :Feature: Config

    :Setup: MMR with two masters, test user,
            1 MiB big value for attribute

    :Steps: 1. Set 20KiB small maxbersize on master2
            2. Add big value to master2
            3. Add big value to master1

    :Assert: Adding the big value to master2 is failed,
             adding the big value to master1 is succeed,
             the big value is successfully replicated to master2
    """
    log.info("Set nsslapd-maxbersize: 20K to master2")
    try:
        topology_m2.ms["master2"].modify_s("cn=config", [(ldap.MOD_REPLACE,
                                                          'nsslapd-maxbersize', '20480')])
    except ldap.LDAPError as e:
        log.error('Failed to set nsslapd-maxbersize == 20480: error ' +
                  e.message['desc'])
        raise

    topology_m2.ms["master2"].restart(20)

    log.info('Try to add attribute with a big value to master2 - expect to FAIL')
    with pytest.raises(ldap.SERVER_DOWN):
        topology_m2.ms["master2"].modify_s(USER_DN, [(ldap.MOD_REPLACE,
                                                      'jpegphoto', big_file)])

    topology_m2.ms["master2"].restart(20)
    topology_m2.ms["master1"].restart(20)

    log.info('Try to add attribute with a big value to master1 - expect to PASS')
    try:
        topology_m2.ms["master1"].modify_s(USER_DN, [(ldap.MOD_REPLACE,
                                                      'jpegphoto', big_file)])
    except ldap.SERVER_DOWN as e:
        log.fatal('Failed to add a big attribute, error: ' + e.message['desc'])
        raise

    time.sleep(1)

    log.info('Check if a big value was successfully added to master1')
    try:
        entries = topology_m2.ms["master1"].search_s(USER_DN, ldap.SCOPE_BASE,
                                                     '(cn=*)',
                                                     ['jpegphoto'])
        assert entries[0].data['jpegphoto']
    except ldap.LDAPError as e:
        log.fatal('Search failed, error: ' + e.message['desc'])
        raise

    log.info('Check if a big value was successfully replicated to master2')
    try:
        entries = topology_m2.ms["master2"].search_s(USER_DN, ldap.SCOPE_BASE,
                                                     '(cn=*)',
                                                     ['jpegphoto'])
        assert entries[0].data['jpegphoto']
    except ldap.LDAPError as e:
        log.fatal('Search failed, error: ' + e.message['desc'])
        raise

    log.info("Set nsslapd-maxbersize: 2097152 (default) to master2")
    try:
        topology_m2.ms["master2"].modify_s("cn=config", [(ldap.MOD_REPLACE,
                                                          'nsslapd-maxbersize', '2097152')])
    except ldap.LDAPError as e:
        log.error('Failed to set nsslapd-maxbersize == 2097152 error ' +
                  e.message['desc'])
        raise


def test_config_listen_backport_size(topology_m2):
    """We need to check that we can search on nsslapd-listen-backlog-size,
    and change its value: to a psoitive number and a negative number.
    Verify invalid value is rejected.
    """

    try:
        entry = topology_m2.ms["master1"].search_s(DN_CONFIG, ldap.SCOPE_BASE, 'objectclass=top',
                                                   ['nsslapd-listen-backlog-size'])
        default_val = entry[0].data['nsslapd-listen-backlog-size'][0]
        assert default_val, 'Failed to get nsslapd-listen-backlog-size from config'
    except ldap.LDAPError as e:
        log.fatal('Failed to search config, error: ' + e.message('desc'))
        raise

    try:
        topology_m2.ms["master1"].modify_s(DN_CONFIG, [(ldap.MOD_REPLACE,
                                                        'nsslapd-listen-backlog-size',
                                                        '256')])
    except ldap.LDAPError as e:
        log.fatal('Failed to modify config, error: ' + e.message('desc'))
        raise

    try:
        topology_m2.ms["master1"].modify_s(DN_CONFIG, [(ldap.MOD_REPLACE,
                                                        'nsslapd-listen-backlog-size',
                                                        '-1')])
    except ldap.LDAPError as e:
        log.fatal('Failed to modify config(negative value), error: ' +
                  e.message('desc'))
        raise

    with pytest.raises(ldap.LDAPError):
        topology_m2.ms["master1"].modify_s(DN_CONFIG, [(ldap.MOD_REPLACE,
                                                        'nsslapd-listen-backlog-size',
                                                        'ZZ')])
        log.fatal('Invalid value was successfully added')

    # Cleanup - undo what we've done
    try:
        topology_m2.ms["master1"].modify_s(DN_CONFIG, [(ldap.MOD_REPLACE,
                                                        'nsslapd-listen-backlog-size',
                                                        default_val)])
    except ldap.LDAPError as e:
        log.fatal('Failed to reset config, error: ' + e.message('desc'))
        raise


def test_config_deadlock_policy(topology_m2):
    """We need to check that nsslapd-db-deadlock-policy exists, that we can
    change the value, and invalid values are rejected
    """

    LDBM_DN = 'cn=config,cn=ldbm database,cn=plugins,cn=config'
    default_val = '9'

    try:
        entry = topology_m2.ms["master1"].search_s(LDBM_DN, ldap.SCOPE_BASE, 'objectclass=top',
                                                   ['nsslapd-db-deadlock-policy'])
        val = entry[0].data['nsslapd-db-deadlock-policy'][0]
        assert val, 'Failed to get nsslapd-db-deadlock-policy from config'
        assert val == default_val, 'The wrong derfualt value was present'
    except ldap.LDAPError as e:
        log.fatal('Failed to search config, error: ' + e.message('desc'))
        raise

    # Try a range of valid values
    for val in ('0', '5', '9'):
        try:
            topology_m2.ms["master1"].modify_s(LDBM_DN, [(ldap.MOD_REPLACE,
                                                          'nsslapd-db-deadlock-policy',
                                                          val)])
        except ldap.LDAPError as e:
            log.fatal('Failed to modify config: nsslapd-db-deadlock-policy to (%s), error: %s' %
                      (val, e.message('desc')))
            raise

    # Try a range of invalid values
    for val in ('-1', '10'):
        with pytest.raises(ldap.LDAPError):
            topology_m2.ms["master1"].modify_s(LDBM_DN, [(ldap.MOD_REPLACE,
                                                          'nsslapd-db-deadlock-policy',
                                                          val)])
            log.fatal('Able to add invalid value to nsslapd-db-deadlock-policy(%s)' % (val))

    # Cleanup - undo what we've done
    try:
        topology_m2.ms["master1"].modify_s(LDBM_DN, [(ldap.MOD_REPLACE,
                                                      'nsslapd-db-deadlock-policy',
                                                      default_val)])
    except ldap.LDAPError as e:
        log.fatal('Failed to reset nsslapd-db-deadlock-policy to the default value(%s), error: %s' %
                  (default_val, e.message('desc')))
        raise


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
