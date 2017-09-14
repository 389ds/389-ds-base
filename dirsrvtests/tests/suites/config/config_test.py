# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging

import pytest
from lib389.tasks import *
from lib389.topologies import topology_m2

from lib389._constants import DN_CONFIG, DEFAULT_SUFFIX

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

    :id: ad57de60-7d56-4323-bbca-5556e5cdb126
    :setup: MMR with two masters, test user,
            1 MiB big value for any attribute
    :steps:
        1. Set maxbersize attribute to a small value (20KiB) on master2
        2. Add the big value to master2
        3. Add the big value to master1
        4. Check if the big value was successfully replicated to master2
    :expectedresults:
        1. maxbersize should be successfully set
        2. Adding the big value to master2 failed
        3. Adding the big value to master1 succeed
        4. The big value is successfully replicated to master2
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
    """Check that nsslapd-listen-backlog-size acted as expected

    :id: a4385d58-a6ab-491e-a604-6df0e8ed91cd
    :setup: MMR with two masters
    :steps:
        1. Search for nsslapd-listen-backlog-size
        2. Set nsslapd-listen-backlog-size to a positive value
        3. Set nsslapd-listen-backlog-size to a negative value
        4. Set nsslapd-listen-backlog-size to an invalid value
        5. Set nsslapd-listen-backlog-size back to a default value
    :expectedresults:
        1. Search should be successful
        2. nsslapd-listen-backlog-size should be successfully set
        3. nsslapd-listen-backlog-size should be successfully set
        4. Modification with an invalid value should throw an error
        5. nsslapd-listen-backlog-size should be successfully set
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
    """Check that nsslapd-db-deadlock-policy acted as expected

    :ID: a24e25fd-bc15-47fa-b018-372f6a2ec59c
    :setup: MMR with two masters
    :steps:
        1. Search for nsslapd-db-deadlock-policy and check if
           it contains a default value
        2. Set nsslapd-db-deadlock-policy to a positive value
        3. Set nsslapd-db-deadlock-policy to a negative value
        4. Set nsslapd-db-deadlock-policy to an invalid value
        5. Set nsslapd-db-deadlock-policy back to a default value
    :expectedresults:
        1. Search should be a successful and should contain a default value
        2. nsslapd-db-deadlock-policy should be successfully set
        3. nsslapd-db-deadlock-policy should be successfully set
        4. Modification with an invalid value should throw an error
        5. nsslapd-db-deadlock-policy should be successfully set
    """

    LDBM_DN = 'cn=config,cn=ldbm database,cn=plugins,cn=config'
    default_val = '9'

    try:
        entry = topology_m2.ms["master1"].search_s(LDBM_DN, ldap.SCOPE_BASE, 'objectclass=top',
                                                   ['nsslapd-db-deadlock-policy'])
        val = entry[0].data['nsslapd-db-deadlock-policy'][0]
        assert val, 'Failed to get nsslapd-db-deadlock-policy from config'
        assert val == default_val, 'The wrong default value was present'
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
