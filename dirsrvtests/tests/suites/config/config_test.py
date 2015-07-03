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

DEBUGGING = False
USER_DN = 'uid=test_user,%s' % DEFAULT_SUFFIX

if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)

log = logging.getLogger(__name__)


class TopologyReplication(object):
    """The Replication Topology Class"""
    def __init__(self, master1, master2):
        """Init"""
        master1.open()
        self.master1 = master1
        master2.open()
        self.master2 = master2


@pytest.fixture(scope="module")
def topology(request):
    """Create Replication Deployment"""

    # Creating master 1...
    if DEBUGGING:
        master1 = DirSrv(verbose=True)
    else:
        master1 = DirSrv(verbose=False)
    args_instance[SER_HOST] = HOST_MASTER_1
    args_instance[SER_PORT] = PORT_MASTER_1
    args_instance[SER_SERVERID_PROP] = SERVERID_MASTER_1
    args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
    args_master = args_instance.copy()
    master1.allocate(args_master)
    instance_master1 = master1.exists()
    if instance_master1:
        master1.delete()
    master1.create()
    master1.open()
    master1.replica.enableReplication(suffix=SUFFIX, role=REPLICAROLE_MASTER, replicaId=REPLICAID_MASTER_1)

    # Creating master 2...
    if DEBUGGING:
        master2 = DirSrv(verbose=True)
    else:
        master2 = DirSrv(verbose=False)
    args_instance[SER_HOST] = HOST_MASTER_2
    args_instance[SER_PORT] = PORT_MASTER_2
    args_instance[SER_SERVERID_PROP] = SERVERID_MASTER_2
    args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
    args_master = args_instance.copy()
    master2.allocate(args_master)
    instance_master2 = master2.exists()
    if instance_master2:
        master2.delete()
    master2.create()
    master2.open()
    master2.replica.enableReplication(suffix=SUFFIX, role=REPLICAROLE_MASTER, replicaId=REPLICAID_MASTER_2)

    #
    # Create all the agreements
    #
    # Creating agreement from master 1 to master 2
    properties = {RA_NAME: 'meTo_' + master2.host + ':' + str(master2.port),
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m1_m2_agmt = master1.agreement.create(suffix=SUFFIX, host=master2.host, port=master2.port, properties=properties)
    if not m1_m2_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m1_m2_agmt)

    # Creating agreement from master 2 to master 1
    properties = {RA_NAME: 'meTo_' + master1.host + ':' + str(master1.port),
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m2_m1_agmt = master2.agreement.create(suffix=SUFFIX, host=master1.host, port=master1.port, properties=properties)
    if not m2_m1_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m2_m1_agmt)

    # Allow the replicas to get situated with the new agreements...
    time.sleep(5)

    #
    # Initialize all the agreements
    #
    master1.agreement.init(SUFFIX, HOST_MASTER_2, PORT_MASTER_2)
    master1.waitForReplInit(m1_m2_agmt)

    # Check replication is working...
    if master1.testReplication(DEFAULT_SUFFIX, master2):
        log.info('Replication is working.')
    else:
        log.fatal('Replication is not working.')
        raise

    def fin():
        """If we are debugging just stop the instances, otherwise remove
        them
        """
        if DEBUGGING:
            master1.stop()
            master2.stop()
        else:
            master1.delete()
            master2.delete()

    request.addfinalizer(fin)

    # Clear out the tmp dir
    master1.clearTmpDir(__file__)

    return TopologyReplication(master1, master2)


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
def test_user(topology):
    """Add and remove test user"""

    try:
        topology.master1.add_s(Entry((USER_DN, {
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
            topology.master1.delete_s(USER_DN)
            time.sleep(1)
        except ldap.LDAPError as e:
            log.fatal('Failed to delete user (%s): error (%s)' % (
                                                            USER_DN,
                                                            e.message['desc']))
            raise


def test_maxbersize_repl(topology, test_user, big_file):
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
        topology.master2.modify_s("cn=config", [(ldap.MOD_REPLACE,
                                                 'nsslapd-maxbersize', '20480')])
    except ldap.LDAPError as e:
        log.error('Failed to set nsslapd-maxbersize == 20480: error ' +
                                                             e.message['desc'])
        raise

    topology.master2.restart(20)

    log.info('Try to add attribute with a big value to master2 - expect to FAIL')
    with pytest.raises(ldap.SERVER_DOWN):
        topology.master2.modify_s(USER_DN, [(ldap.MOD_REPLACE,
                                            'jpegphoto', big_file)])

    topology.master2.restart(20)
    topology.master1.restart(20)

    log.info('Try to add attribute with a big value to master1 - expect to PASS')
    try:
        topology.master1.modify_s(USER_DN, [(ldap.MOD_REPLACE,
                                            'jpegphoto', big_file)])
    except ldap.SERVER_DOWN as e:
        log.fatal('Failed to add a big attribute, error: ' + e.message['desc'])
        raise

    time.sleep(1)

    log.info('Check if a big value was successfully added to master1')
    try:
        entries = topology.master1.search_s(USER_DN, ldap.SCOPE_BASE,
                                            '(cn=*)',
                                            ['jpegphoto'])
        assert entries[0].data['jpegphoto']
    except ldap.LDAPError as e:
            log.fatal('Search failed, error: ' + e.message['desc'])
            raise

    log.info('Check if a big value was successfully replicated to master2')
    try:
        entries = topology.master2.search_s(USER_DN, ldap.SCOPE_BASE,
                                            '(cn=*)',
                                            ['jpegphoto'])
        assert entries[0].data['jpegphoto']
    except ldap.LDAPError as e:
            log.fatal('Search failed, error: ' + e.message['desc'])
            raise

    log.info("Set nsslapd-maxbersize: 2097152 (default) to master2")
    try:
        topology.master2.modify_s("cn=config", [(ldap.MOD_REPLACE,
                                                 'nsslapd-maxbersize', '2097152')])
    except ldap.LDAPError as e:
        log.error('Failed to set nsslapd-maxbersize == 2097152 error ' +
                                                             e.message['desc'])
        raise


def test_config_listen_backport_size(topology):
    """We need to check that we can search on nsslapd-listen-backlog-size,
    and change its value: to a psoitive number and a negative number.
    Verify invalid value is rejected.
    """

    try:
        entry = topology.master1.search_s(DN_CONFIG, ldap.SCOPE_BASE, 'objectclass=top',
                                          ['nsslapd-listen-backlog-size'])
        default_val = entry[0].data['nsslapd-listen-backlog-size'][0]
        assert default_val, 'Failed to get nsslapd-listen-backlog-size from config'
    except ldap.LDAPError as e:
        log.fatal('Failed to search config, error: ' + e.message('desc'))
        raise

    try:
        topology.master1.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE,
                                               'nsslapd-listen-backlog-size',
                                               '256')])
    except ldap.LDAPError as e:
        log.fatal('Failed to modify config, error: ' + e.message('desc'))
        raise

    try:
        topology.master1.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE,
                                               'nsslapd-listen-backlog-size',
                                               '-1')])
    except ldap.LDAPError as e:
        log.fatal('Failed to modify config(negative value), error: ' +
                  e.message('desc'))
        raise

    with pytest.raises(ldap.LDAPError):
        topology.master1.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE,
                                               'nsslapd-listen-backlog-size',
                                               'ZZ')])
        log.fatal('Invalid value was successfully added')

    # Cleanup - undo what we've done
    try:
        topology.master1.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE,
                                              'nsslapd-listen-backlog-size',
                                               default_val)])
    except ldap.LDAPError as e:
        log.fatal('Failed to reset config, error: ' + e.message('desc'))
        raise


def test_config_deadlock_policy(topology):
    """We need to check that nsslapd-db-deadlock-policy exists, that we can
    change the value, and invalid values are rejected
    """

    LDBM_DN = 'cn=config,cn=ldbm database,cn=plugins,cn=config'
    default_val = '9'

    try:
        entry = topology.master1.search_s(LDBM_DN, ldap.SCOPE_BASE, 'objectclass=top',
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
            topology.master1.modify_s(LDBM_DN, [(ldap.MOD_REPLACE,
                                                 'nsslapd-db-deadlock-policy',
                                                 val)])
        except ldap.LDAPError as e:
            log.fatal('Failed to modify config: nsslapd-db-deadlock-policy to (%s), error: %s' %
                      (val, e.message('desc')))
            raise

    # Try a range of invalid values
    for val in ('-1', '10'):
        with pytest.raises(ldap.LDAPError):
            topology.master1.modify_s(LDBM_DN, [(ldap.MOD_REPLACE,
                                                 'nsslapd-db-deadlock-policy',
                                                 val)])
            log.fatal('Able to add invalid value to nsslapd-db-deadlock-policy(%s)' % (val))

    # Cleanup - undo what we've done
    try:
        topology.master1.modify_s(LDBM_DN, [(ldap.MOD_REPLACE,
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
