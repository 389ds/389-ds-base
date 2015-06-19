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
import threading
from lib389 import DirSrv, Entry, tools, tasks
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from lib389.utils import *
logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

installation1_prefix = None


class AddUsers(threading.Thread):
    def __init__(self, inst, num_users):
        threading.Thread.__init__(self)
        self.daemon = True
        self.inst = inst
        self.num_users = num_users

    def openConnection(self, inst):
        # Open a new connection to our LDAP server
        server = DirSrv(verbose=False)
        args_instance[SER_HOST] = inst.host
        args_instance[SER_PORT] = inst.port
        args_instance[SER_SERVERID_PROP] = inst.serverid
        args_standalone = args_instance.copy()
        server.allocate(args_standalone)
        server.open()
        return server

    def run(self):
        # Start adding users
        conn = self.openConnection(self.inst)
        idx = 0

        while idx < self.num_users:
            USER_DN = 'uid=' + self.inst.serverid + '_' + str(idx) + ',' + DEFAULT_SUFFIX
            try:
                conn.add_s(Entry((USER_DN, {'objectclass': 'top extensibleObject'.split(),
                           'uid': 'user' + str(idx)})))
            except ldap.UNWILLING_TO_PERFORM:
                # One of the masters was probably put into read only mode - just break out
                break
            except ldap.LDAPError, e:
                log.error('AddUsers: failed to add (' + USER_DN + ') error: ' + e.message['desc'])
                assert False
            idx += 1

        conn.close()


class TopologyReplication(object):
    def __init__(self, master1, master2, master3, master4, m1_m2_agmt, m1_m3_agmt, m1_m4_agmt):
        master1.open()
        self.master1 = master1
        master2.open()
        self.master2 = master2
        master3.open()
        self.master3 = master3
        master4.open()
        self.master4 = master4

        # Store the agreement dn's for future initializations
        self.m1_m2_agmt = m1_m2_agmt
        self.m1_m3_agmt = m1_m3_agmt
        self.m1_m4_agmt = m1_m4_agmt


@pytest.fixture(scope="module")
def topology(request):
    global installation1_prefix
    if installation1_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation1_prefix

    # Creating master 1...
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
    master1.log = log

    # Creating master 2...
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

    # Creating master 3...
    master3 = DirSrv(verbose=False)
    args_instance[SER_HOST] = HOST_MASTER_3
    args_instance[SER_PORT] = PORT_MASTER_3
    args_instance[SER_SERVERID_PROP] = SERVERID_MASTER_3
    args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
    args_master = args_instance.copy()
    master3.allocate(args_master)
    instance_master3 = master3.exists()
    if instance_master3:
        master3.delete()
    master3.create()
    master3.open()
    master3.replica.enableReplication(suffix=SUFFIX, role=REPLICAROLE_MASTER, replicaId=REPLICAID_MASTER_3)

    # Creating master 4...
    master4 = DirSrv(verbose=False)
    args_instance[SER_HOST] = HOST_MASTER_4
    args_instance[SER_PORT] = PORT_MASTER_4
    args_instance[SER_SERVERID_PROP] = SERVERID_MASTER_4
    args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
    args_master = args_instance.copy()
    master4.allocate(args_master)
    instance_master4 = master4.exists()
    if instance_master4:
        master4.delete()
    master4.create()
    master4.open()
    master4.replica.enableReplication(suffix=SUFFIX, role=REPLICAROLE_MASTER, replicaId=REPLICAID_MASTER_4)

    #
    # Create all the agreements
    #
    # Creating agreement from master 1 to master 2
    properties = {RA_NAME:      r'meTo_$host:$port',
                  RA_BINDDN:    defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW:    defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD:    defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m1_m2_agmt = master1.agreement.create(suffix=SUFFIX, host=master2.host, port=master2.port, properties=properties)
    if not m1_m2_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m1_m2_agmt)

    # Creating agreement from master 1 to master 3
    properties = {RA_NAME:      r'meTo_$host:$port',
                  RA_BINDDN:    defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW:    defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD:    defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m1_m3_agmt = master1.agreement.create(suffix=SUFFIX, host=master3.host, port=master3.port, properties=properties)
    if not m1_m3_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m1_m3_agmt)

    # Creating agreement from master 1 to master 4
    properties = {RA_NAME:      r'meTo_$host:$port',
                  RA_BINDDN:    defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW:    defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD:    defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m1_m4_agmt = master1.agreement.create(suffix=SUFFIX, host=master4.host, port=master4.port, properties=properties)
    if not m1_m4_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m1_m4_agmt)

    # Creating agreement from master 2 to master 1
    properties = {RA_NAME:      r'meTo_$host:$port',
                  RA_BINDDN:    defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW:    defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD:    defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m2_m1_agmt = master2.agreement.create(suffix=SUFFIX, host=master1.host, port=master1.port, properties=properties)
    if not m2_m1_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m2_m1_agmt)

    # Creating agreement from master 2 to master 3
    properties = {RA_NAME:      r'meTo_$host:$port',
                  RA_BINDDN:    defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW:    defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD:    defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m2_m3_agmt = master2.agreement.create(suffix=SUFFIX, host=master3.host, port=master3.port, properties=properties)
    if not m2_m3_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m2_m3_agmt)

    # Creating agreement from master 2 to master 4
    properties = {RA_NAME:      r'meTo_$host:$port',
                  RA_BINDDN:    defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW:    defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD:    defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m2_m4_agmt = master2.agreement.create(suffix=SUFFIX, host=master4.host, port=master4.port, properties=properties)
    if not m2_m4_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m2_m4_agmt)

    # Creating agreement from master 3 to master 1
    properties = {RA_NAME:      r'meTo_$host:$port',
                  RA_BINDDN:    defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW:    defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD:    defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m3_m1_agmt = master3.agreement.create(suffix=SUFFIX, host=master1.host, port=master1.port, properties=properties)
    if not m3_m1_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m3_m1_agmt)

    # Creating agreement from master 3 to master 2
    properties = {RA_NAME:      r'meTo_$host:$port',
                  RA_BINDDN:    defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW:    defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD:    defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m3_m2_agmt = master3.agreement.create(suffix=SUFFIX, host=master2.host, port=master2.port, properties=properties)
    if not m3_m2_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m3_m2_agmt)

    # Creating agreement from master 3 to master 4
    properties = {RA_NAME:      r'meTo_$host:$port',
                  RA_BINDDN:    defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW:    defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD:    defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m3_m4_agmt = master3.agreement.create(suffix=SUFFIX, host=master4.host, port=master4.port, properties=properties)
    if not m3_m4_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m3_m4_agmt)

    # Creating agreement from master 4 to master 1
    properties = {RA_NAME:      r'meTo_$host:$port',
                  RA_BINDDN:    defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW:    defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD:    defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m4_m1_agmt = master4.agreement.create(suffix=SUFFIX, host=master1.host, port=master1.port, properties=properties)
    if not m4_m1_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m4_m1_agmt)

    # Creating agreement from master 4 to master 2
    properties = {RA_NAME:      r'meTo_$host:$port',
                  RA_BINDDN:    defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW:    defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD:    defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m4_m2_agmt = master4.agreement.create(suffix=SUFFIX, host=master2.host, port=master2.port, properties=properties)
    if not m4_m2_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m4_m2_agmt)

    # Creating agreement from master 4 to master 3
    properties = {RA_NAME:      r'meTo_$host:$port',
                  RA_BINDDN:    defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW:    defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD:    defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m4_m3_agmt = master4.agreement.create(suffix=SUFFIX, host=master3.host, port=master3.port, properties=properties)
    if not m4_m3_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m4_m3_agmt)

    # Allow the replicas to get situated with the new agreements
    time.sleep(5)

    #
    # Initialize all the agreements
    #
    master1.agreement.init(SUFFIX, HOST_MASTER_2, PORT_MASTER_2)
    master1.waitForReplInit(m1_m2_agmt)
    master1.agreement.init(SUFFIX, HOST_MASTER_3, PORT_MASTER_3)
    master1.waitForReplInit(m1_m3_agmt)
    master1.agreement.init(SUFFIX, HOST_MASTER_4, PORT_MASTER_4)
    master1.waitForReplInit(m1_m4_agmt)

    # Check replication is working...
    if master1.testReplication(DEFAULT_SUFFIX, master2):
        log.info('Replication is working.')
    else:
        log.fatal('Replication is not working.')
        assert False

    # Clear out the tmp dir
    master1.clearTmpDir(__file__)

    return TopologyReplication(master1, master2, master3, master4, m1_m2_agmt, m1_m3_agmt, m1_m4_agmt)


def restore_master4(topology):
    '''
    In our tests will always be removing master 4, so we need a common
    way to restore it for another test
    '''

    log.info('Restoring master 4...')

    # Enable replication on master 4
    topology.master4.replica.enableReplication(suffix=SUFFIX, role=REPLICAROLE_MASTER, replicaId=REPLICAID_MASTER_4)

    #
    # Create agreements from master 4 -> m1, m2 ,m3
    #
    # Creating agreement from master 4 to master 1
    properties = {RA_NAME:      r'meTo_$host:$port',
                  RA_BINDDN:    defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW:    defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD:    defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m4_m1_agmt = topology.master4.agreement.create(suffix=SUFFIX, host=topology.master1.host,
                                                   port=topology.master1.port, properties=properties)
    if not m4_m1_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m4_m1_agmt)

    # Creating agreement from master 4 to master 2
    properties = {RA_NAME:      r'meTo_$host:$port',
                  RA_BINDDN:    defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW:    defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD:    defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m4_m2_agmt = topology.master4.agreement.create(suffix=SUFFIX, host=topology.master2.host,
                                                   port=topology.master2.port, properties=properties)
    if not m4_m2_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m4_m2_agmt)

    # Creating agreement from master 4 to master 3
    properties = {RA_NAME:      r'meTo_$host:$port',
                  RA_BINDDN:    defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW:    defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD:    defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m4_m3_agmt = topology.master4.agreement.create(suffix=SUFFIX, host=topology.master3.host,
                                                   port=topology.master3.port, properties=properties)
    if not m4_m3_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m4_m3_agmt)

    #
    # Create agreements from m1, m2, m3 to master 4
    #
    # Creating agreement from master 1 to master 4
    properties = {RA_NAME:      r'meTo_$host:$port',
                  RA_BINDDN:    defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW:    defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD:    defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m1_m4_agmt = topology.master1.agreement.create(suffix=SUFFIX, host=topology.master4.host,
                                                   port=topology.master4.port, properties=properties)
    if not m1_m4_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m1_m4_agmt)

    # Creating agreement from master 2 to master 4
    properties = {RA_NAME:      r'meTo_$host:$port',
                  RA_BINDDN:    defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW:    defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD:    defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m2_m4_agmt = topology.master2.agreement.create(suffix=SUFFIX, host=topology.master4.host,
                                                   port=topology.master4.port, properties=properties)
    if not m2_m4_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m2_m4_agmt)

    # Creating agreement from master 3 to master 4
    properties = {RA_NAME:      r'meTo_$host:$port',
                  RA_BINDDN:    defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW:    defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD:    defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m3_m4_agmt = topology.master3.agreement.create(suffix=SUFFIX, host=topology.master4.host,
                                                   port=topology.master4.port, properties=properties)
    if not m3_m4_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m3_m4_agmt)

    #
    # Restart the other servers - this allows the rid(for master4) to be used again/valid
    #
    topology.master1.restart(timeout=30)
    topology.master2.restart(timeout=30)
    topology.master3.restart(timeout=30)
    topology.master4.restart(timeout=30)

    #
    # Initialize the agreements
    #
    topology.master1.agreement.init(SUFFIX, HOST_MASTER_2, PORT_MASTER_2)
    topology.master1.waitForReplInit(topology.m1_m2_agmt)
    topology.master1.agreement.init(SUFFIX, HOST_MASTER_3, PORT_MASTER_3)
    topology.master1.waitForReplInit(topology.m1_m3_agmt)
    topology.master1.agreement.init(SUFFIX, HOST_MASTER_4, PORT_MASTER_4)
    topology.master1.waitForReplInit(topology.m1_m4_agmt)

    #
    # Test Replication is working
    #
    # Check replication is working with previous working master(m1 -> m2)
    if topology.master1.testReplication(DEFAULT_SUFFIX, topology.master2):
        log.info('Replication is working m1 -> m2.')
    else:
        log.fatal('restore_master4: Replication is not working from m1 -> m2.')
        assert False

    # Check replication is working from master 1 to  master 4...
    if topology.master1.testReplication(DEFAULT_SUFFIX, topology.master4):
        log.info('Replication is working m1 -> m4.')
    else:
        log.fatal('restore_master4: Replication is not working from m1 -> m4.')
        assert False

    # Check replication is working from master 4 to master1...
    if topology.master4.testReplication(DEFAULT_SUFFIX, topology.master1):
        log.info('Replication is working m4 -> m1.')
    else:
        log.fatal('restore_master4: Replication is not working from m4 -> 1.')
        assert False

    log.info('Master 4 has been successfully restored.')


def test_cleanallruv_init(topology):
    '''
    Make updates on each master to make sure we have the all master RUVs on
    each master.
    '''

    log.info('Initializing cleanAllRUV test suite...')

    # Master 1
    if not topology.master1.testReplication(DEFAULT_SUFFIX, topology.master2):
        log.fatal('test_cleanallruv_init: Replication is not working between master 1 and master 2.')
        assert False

    if not topology.master1.testReplication(DEFAULT_SUFFIX, topology.master3):
        log.fatal('test_cleanallruv_init: Replication is not working between master 1 and master 3.')
        assert False

    if not topology.master1.testReplication(DEFAULT_SUFFIX, topology.master4):
        log.fatal('test_cleanallruv_init: Replication is not working between master 1 and master 4.')
        assert False

    # Master 2
    if not topology.master2.testReplication(DEFAULT_SUFFIX, topology.master1):
        log.fatal('test_cleanallruv_init: Replication is not working between master 2 and master 1.')
        assert False

    if not topology.master2.testReplication(DEFAULT_SUFFIX, topology.master3):
        log.fatal('test_cleanallruv_init: Replication is not working between master 2 and master 3.')
        assert False

    if not topology.master2.testReplication(DEFAULT_SUFFIX, topology.master4):
        log.fatal('test_cleanallruv_init: Replication is not working between master 2 and master 4.')
        assert False

    # Master 3
    if not topology.master3.testReplication(DEFAULT_SUFFIX, topology.master1):
        log.fatal('test_cleanallruv_init: Replication is not working between master 2 and master 1.')
        assert False

    if not topology.master3.testReplication(DEFAULT_SUFFIX, topology.master2):
        log.fatal('test_cleanallruv_init: Replication is not working between master 2 and master 2.')
        assert False

    if not topology.master3.testReplication(DEFAULT_SUFFIX, topology.master4):
        log.fatal('test_cleanallruv_init: Replication is not working between master 2 and master 4.')
        assert False

    # Master 4
    if not topology.master4.testReplication(DEFAULT_SUFFIX, topology.master1):
        log.fatal('test_cleanallruv_init: Replication is not working between master 2 and master 1.')
        assert False

    if not topology.master4.testReplication(DEFAULT_SUFFIX, topology.master2):
        log.fatal('test_cleanallruv_init: Replication is not working between master 2 and master 2.')
        assert False

    if not topology.master4.testReplication(DEFAULT_SUFFIX, topology.master3):
        log.fatal('test_cleanallruv_init: Replication is not working between master 2 and master 3.')
        assert False

    log.info('Initialized cleanAllRUV test suite.')


def test_cleanallruv_clean(topology):
    '''
    Disable a master, remove agreements to that master, and clean the RUVs on
    the remaining replicas
    '''

    log.info('Running test_cleanallruv_clean...')

    # Disable master 4
    log.info('test_cleanallruv_clean: disable master 4...')
    try:
        topology.master4.replica.disableReplication(DEFAULT_SUFFIX)
    except:
        log.fatal('error!')
        assert False

    # Remove the agreements from the other masters that point to master 4
    log.info('test_cleanallruv_clean: remove all the agreements to master 4...')
    try:
        topology.master1.agreement.delete(DEFAULT_SUFFIX, topology.master4)
    except ldap.LDAPError, e:
        log.fatal('test_cleanallruv_clean: Failed to delete agmt(m1 -> m4), error: ' +
                  e.message['desc'])
        assert False
    try:
        topology.master2.agreement.delete(DEFAULT_SUFFIX, topology.master4)
    except ldap.LDAPError, e:
        log.fatal('test_cleanallruv_clean: Failed to delete agmt(m2 -> m4), error: ' +
                  e.message['desc'])
        assert False
    try:
        topology.master3.agreement.delete(DEFAULT_SUFFIX, topology.master4)
    except ldap.LDAPError, e:
        log.fatal('test_cleanallruv_clean: Failed to delete agmt(m3 -> m4), error: ' +
                  e.message['desc'])
        assert False

    # Run the task
    log.info('test_cleanallruv_clean: run the cleanAllRUV task...')
    try:
        topology.master1.tasks.cleanAllRUV(suffix=DEFAULT_SUFFIX, replicaid='4',
                                           args={TASK_WAIT: True})
    except ValueError, e:
        log.fatal('test_cleanallruv_clean: Problem running cleanAllRuv task: ' +
                  e.message('desc'))
        assert False

    # Check the other master's RUV for 'replica 4'
    log.info('test_cleanallruv_clean: check all the masters have been cleaned...')
    clean = False
    count = 0
    while not clean and count < 5:
        clean = True

        # Check master 1
        try:
            entry = topology.master1.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, REPLICA_RUV_FILTER)
            if not entry:
                log.error('test_cleanallruv_clean: Failed to find db tombstone entry from master')
                repl_fail(replica_inst)
            elements = entry[0].getValues('nsds50ruv')
            for ruv in elements:
                if 'replica 4' in ruv:
                    # Not cleaned
                    log.error('test_cleanallruv_clean: Master 1 not cleaned!')
                    clean = False
            if clean:
                log.info('test_cleanallruv_clean: Master 1 is cleaned.')
        except ldap.LDAPError, e:
            log.fatal('test_cleanallruv_clean: Unable to search master 1 for db tombstone: ' + e.message['desc'])

        # Check master 2
        try:
            entry = topology.master2.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, REPLICA_RUV_FILTER)
            if not entry:
                log.error('test_cleanallruv_clean: Failed to find db tombstone entry from master')
                repl_fail(replica_inst)
            elements = entry[0].getValues('nsds50ruv')
            for ruv in elements:
                if 'replica 4' in ruv:
                    # Not cleaned
                    log.error('test_cleanallruv_clean: Master 2 not cleaned!')
                    clean = False
            if clean:
                log.info('test_cleanallruv_clean: Master 2 is cleaned.')
        except ldap.LDAPError, e:
            log.fatal('Unable to search master 2 for db tombstone: ' + e.message['desc'])

        # Check master 3
        try:
            entry = topology.master3.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, REPLICA_RUV_FILTER)
            if not entry:
                log.error('test_cleanallruv_clean: Failed to find db tombstone entry from master')
                repl_fail(replica_inst)
            elements = entry[0].getValues('nsds50ruv')
            for ruv in elements:
                if 'replica 4' in ruv:
                    # Not cleaned
                    log.error('test_cleanallruv_clean: Master 3 not cleaned!')
                    clean = False
            if clean:
                log.info('test_cleanallruv_clean: Master 3 is cleaned.')
        except ldap.LDAPError, e:
            log.fatal('test_cleanallruv_clean: Unable to search master 3 for db tombstone: ' + e.message['desc'])

        # Sleep a bit and give it chance to clean up...
        time.sleep(5)
        count += 1

    if not clean:
        log.fatal('test_cleanallruv_clean: Failed to clean replicas')
        assert False

    log.info('Allow cleanallruv threads to finish...')
    time.sleep(30)

    log.info('test_cleanallruv_clean PASSED, restoring master 4...')

    #
    # Cleanup - restore master 4
    #
    restore_master4(topology)


def test_cleanallruv_clean_restart(topology):
    '''
    Test that if a master istopped during the clean process, that it
    resumes and finishes when its started.
    '''

    log.info('Running test_cleanallruv_clean_restart...')

    # Disable master 4
    log.info('test_cleanallruv_clean_restart: disable master 4...')
    try:
        topology.master4.replica.disableReplication(DEFAULT_SUFFIX)
    except:
        log.fatal('error!')
        assert False

    # Remove the agreements from the other masters that point to master 4
    log.info('test_cleanallruv_clean: remove all the agreements to master 4...')
    try:
        topology.master1.agreement.delete(DEFAULT_SUFFIX, topology.master4)
    except ldap.LDAPError, e:
        log.fatal('test_cleanallruv_clean_restart: Failed to delete agmt(m1 -> m4), error: ' +
                  e.message['desc'])
        assert False
    try:
        topology.master2.agreement.delete(DEFAULT_SUFFIX, topology.master4)
    except ldap.LDAPError, e:
        log.fatal('test_cleanallruv_clean_restart: Failed to delete agmt(m2 -> m4), error: ' +
                  e.message['desc'])
        assert False
    try:
        topology.master3.agreement.delete(DEFAULT_SUFFIX, topology.master4)
    except ldap.LDAPError, e:
        log.fatal('test_cleanallruv_clean_restart: Failed to delete agmt(m3 -> m4), error: ' +
                  e.message['desc'])
        assert False

    # Stop master 3 to keep the task running, so we can stop master 1...
    topology.master3.stop(timeout=30)

    # Run the task
    log.info('test_cleanallruv_clean_restart: run the cleanAllRUV task...')
    try:
        topology.master1.tasks.cleanAllRUV(suffix=DEFAULT_SUFFIX, replicaid='4',
                                           args={TASK_WAIT: False})
    except ValueError, e:
        log.fatal('test_cleanallruv_clean_restart: Problem running cleanAllRuv task: ' +
                  e.message('desc'))
        assert False

    # Sleep a bit, then stop master 1
    time.sleep(3)
    topology.master1.stop(timeout=30)

    # Now start master 3 & 1, and make sure we didn't crash
    topology.master3.start(timeout=30)
    if topology.master3.detectDisorderlyShutdown():
        log.fatal('test_cleanallruv_clean_restart: Master 3 previously crashed!')
        assert False

    topology.master1.start(timeout=30)
    if topology.master1.detectDisorderlyShutdown():
        log.fatal('test_cleanallruv_clean_restart: Master 1 previously crashed!')
        assert False

    # Wait a little for agmts/cleanallruv to wake up
    time.sleep(5)

    # Check the other master's RUV for 'replica 4'
    log.info('test_cleanallruv_clean_restart: check all the masters have been cleaned...')
    clean = False
    count = 0
    while not clean and count < 10:
        clean = True

        # Check master 1
        try:
            entry = topology.master1.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, REPLICA_RUV_FILTER)
            if not entry:
                log.error('test_cleanallruv_clean_restart: Failed to find db tombstone entry from master')
                repl_fail(replica_inst)
            elements = entry[0].getValues('nsds50ruv')
            for ruv in elements:
                if 'replica 4' in ruv:
                    # Not cleaned
                    log.error('test_cleanallruv_clean_restart: Master 1 not cleaned!')
                    clean = False
            if clean:
                log.info('test_cleanallruv_clean_restart: Master 1 is cleaned.')
        except ldap.LDAPError, e:
            log.fatal('test_cleanallruv_clean_restart: Unable to search master 1 for db tombstone: ' +
                      e.message['desc'])

        # Check master 2
        try:
            entry = topology.master2.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, REPLICA_RUV_FILTER)
            if not entry:
                log.error('test_cleanallruv_clean_restart: Failed to find db tombstone entry from master')
                repl_fail(replica_inst)
            elements = entry[0].getValues('nsds50ruv')
            for ruv in elements:
                if 'replica 4' in ruv:
                    # Not cleaned
                    log.error('test_cleanallruv_clean_restart: Master 2 not cleaned!')
                    clean = False
            if clean:
                log.info('test_cleanallruv_clean_restart: Master 2 is cleaned.')
        except ldap.LDAPError, e:
            log.fatal('test_cleanallruv_clean_restart: Unable to search master 2 for db tombstone: ' +
                      e.message['desc'])

        # Check master 3
        try:
            entry = topology.master3.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, REPLICA_RUV_FILTER)
            if not entry:
                log.error('test_cleanallruv_clean_restart: Failed to find db tombstone entry from master')
                repl_fail(replica_inst)
            elements = entry[0].getValues('nsds50ruv')
            for ruv in elements:
                if 'replica 4' in ruv:
                    # Not cleaned
                    log.error('test_cleanallruv_clean_restart: Master 3 not cleaned!')
                    clean = False
            if clean:
                log.info('test_cleanallruv_clean_restart: Master 3 is cleaned.')
        except ldap.LDAPError, e:
            log.fatal('test_cleanallruv_clean_restart: Unable to search master 3 for db tombstone: ' +
                      e.message['desc'])

        # Sleep a bit and give it chance to clean up...
        time.sleep(5)
        count += 1

    if not clean:
        log.fatal('Failed to clean replicas')
        assert False

    log.info('Allow cleanallruv threads to finish...')
    time.sleep(30)

    log.info('test_cleanallruv_clean_restart PASSED, restoring master 4...')

    #
    # Cleanup - restore master 4
    #
    restore_master4(topology)


def test_cleanallruv_clean_force(topology):
    '''
    Disable a master, remove agreements to that master, and clean the RUVs on
    the remaining replicas
    '''

    log.info('Running test_cleanallruv_clean_force...')

    # Stop master 3, while we update master 4, so that 3 is behind the other masters
    topology.master3.stop(timeout=10)

    # Add a bunch of updates to master 4
    m4_add_users = AddUsers(topology.master4, 1500)
    m4_add_users.start()
    m4_add_users.join()

    # Disable master 4
    log.info('test_cleanallruv_clean_force: disable master 4...')
    try:
        topology.master4.replica.disableReplication(DEFAULT_SUFFIX)
    except:
        log.fatal('error!')
        assert False

    # Start master 3, it should be out of sync with the other replicas...
    topology.master3.start(timeout=10)

    # Remove the agreements from the other masters that point to master 4
    log.info('test_cleanallruv_clean_force: remove all the agreements to master 4...')
    try:
        topology.master1.agreement.delete(DEFAULT_SUFFIX, topology.master4)
    except ldap.LDAPError, e:
        log.fatal('test_cleanallruv_clean_force: Failed to delete agmt(m1 -> m4), error: ' +
                  e.message['desc'])
        assert False
    try:
        topology.master2.agreement.delete(DEFAULT_SUFFIX, topology.master4)
    except ldap.LDAPError, e:
        log.fatal('test_cleanallruv_clean_force: Failed to delete agmt(m2 -> m4), error: ' +
                  e.message['desc'])
        assert False
    try:
        topology.master3.agreement.delete(DEFAULT_SUFFIX, topology.master4)
    except ldap.LDAPError, e:
        log.fatal('test_cleanallruv_clean_force: Failed to delete agmt(m3 -> m4), error: ' +
                  e.message['desc'])
        assert False

    # Run the task, use "force" because master 3 is not in sync with the other replicas
    # in regards to the replica 4 RUV
    log.info('test_cleanallruv_clean_force: run the cleanAllRUV task...')
    try:
        topology.master1.tasks.cleanAllRUV(suffix=DEFAULT_SUFFIX, replicaid='4',
                                           force=True, args={TASK_WAIT: True})
    except ValueError, e:
        log.fatal('test_cleanallruv_clean_force: Problem running cleanAllRuv task: ' +
                  e.message('desc'))
        assert False

    # Check the other master's RUV for 'replica 4'
    log.info('test_cleanallruv_clean_force: check all the masters have been cleaned...')
    clean = False
    count = 0
    while not clean and count < 5:
        clean = True

        # Check master 1
        try:
            entry = topology.master1.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, REPLICA_RUV_FILTER)
            if not entry:
                log.error('test_cleanallruv_clean_force: Failed to find db tombstone entry from master')
                repl_fail(replica_inst)
            elements = entry[0].getValues('nsds50ruv')
            for ruv in elements:
                if 'replica 4' in ruv:
                    # Not cleaned
                    log.error('test_cleanallruv_clean_force: Master 1 not cleaned!')
                    clean = False
            if clean:
                log.info('test_cleanallruv_clean_force: Master 1 is cleaned.')
        except ldap.LDAPError, e:
            log.fatal('test_cleanallruv_clean_force: Unable to search master 1 for db tombstone: ' +
                      e.message['desc'])

        # Check master 2
        try:
            entry = topology.master2.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, REPLICA_RUV_FILTER)
            if not entry:
                log.error('test_cleanallruv_clean_force: Failed to find db tombstone entry from master')
                repl_fail(replica_inst)
            elements = entry[0].getValues('nsds50ruv')
            for ruv in elements:
                if 'replica 4' in ruv:
                    # Not cleaned
                    log.error('test_cleanallruv_clean_force: Master 1 not cleaned!')
                    clean = False
            if clean:
                log.info('Master 2 is cleaned.')
        except ldap.LDAPError, e:
            log.fatal('test_cleanallruv_clean_force: Unable to search master 2 for db tombstone: ' +
                      e.message['desc'])

        # Check master 3
        try:
            entry = topology.master3.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, REPLICA_RUV_FILTER)
            if not entry:
                log.error('test_cleanallruv_clean_force: Failed to find db tombstone entry from master')
                repl_fail(replica_inst)
            elements = entry[0].getValues('nsds50ruv')
            for ruv in elements:
                if 'replica 4' in ruv:
                    # Not cleaned
                    log.error('test_cleanallruv_clean_force: Master 3 not cleaned!')
                    clean = False
            if clean:
                log.info('test_cleanallruv_clean_force: Master 3 is cleaned.')
        except ldap.LDAPError, e:
            log.fatal('test_cleanallruv_clean_force: Unable to search master 3 for db tombstone: ' +
                      e.message['desc'])

        # Sleep a bit and give it chance to clean up...
        time.sleep(5)
        count += 1

    if not clean:
        log.fatal('test_cleanallruv_clean_force: Failed to clean replicas')
        assert False

    log.info('test_cleanallruv_clean_force: Allow cleanallruv threads to finish')
    time.sleep(30)

    log.info('test_cleanallruv_clean_force PASSED, restoring master 4...')

    #
    # Cleanup - restore master 4
    #
    restore_master4(topology)


def test_cleanallruv_abort(topology):
    '''
    Test the abort task.

    DIsable master 4
    Stop master 2 so that it can not be cleaned
    Run the clean task
    Wait a bit
    Abort the task
    Verify task is aborted
    '''

    log.info('Running test_cleanallruv_abort...')

    # Disable master 4
    log.info('test_cleanallruv_abort: disable replication on master 4...')
    try:
        topology.master4.replica.disableReplication(DEFAULT_SUFFIX)
    except:
        log.fatal('test_cleanallruv_abort: failed to disable replication')
        assert False

    # Remove the agreements from the other masters that point to master 4
    log.info('test_cleanallruv_abort: remove all the agreements to master 4...)')
    try:
        topology.master1.agreement.delete(DEFAULT_SUFFIX, topology.master4)
    except ldap.LDAPError, e:
        log.fatal('test_cleanallruv_abort: Failed to delete agmt(m1 -> m4), error: ' +
                  e.message['desc'])
        assert False
    try:
        topology.master2.agreement.delete(DEFAULT_SUFFIX, topology.master4)
    except ldap.LDAPError, e:
        log.fatal('test_cleanallruv_abort: Failed to delete agmt(m2 -> m4), error: ' +
                  e.message['desc'])
        assert False
    try:
        topology.master3.agreement.delete(DEFAULT_SUFFIX, topology.master4)
    except ldap.LDAPError, e:
        log.fatal('test_cleanallruv_abort: Failed to delete agmt(m3 -> m4), error: ' +
                  e.message['desc'])
        assert False

    # Stop master 2
    log.info('test_cleanallruv_abort: stop master 2 to freeze the cleanAllRUV task...')
    topology.master2.stop(timeout=10)

    # Run the task
    log.info('test_cleanallruv_abort: add the cleanAllRUV task...')
    try:
        (clean_task_dn, rc) = topology.master1.tasks.cleanAllRUV(suffix=DEFAULT_SUFFIX,
                                  replicaid='4', args={TASK_WAIT: False})
    except ValueError, e:
        log.fatal('test_cleanallruv_abort: Problem running cleanAllRuv task: ' +
                  e.message('desc'))
        assert False

    # Wait a bit
    time.sleep(10)

    # Abort the task
    log.info('test_cleanallruv_abort: abort the cleanAllRUV task...')
    try:
        topology.master1.tasks.abortCleanAllRUV(suffix=DEFAULT_SUFFIX, replicaid='4',
                                                args={TASK_WAIT: True})
    except ValueError, e:
        log.fatal('test_cleanallruv_abort: Problem running abortCleanAllRuv task: ' +
                  e.message('desc'))
        assert False

    # Check master 1 does not have the clean task running
    log.info('test_cleanallruv_abort: check master 1 no longer has a cleanAllRUV task...')
    attrlist = ['nsTaskLog', 'nsTaskStatus', 'nsTaskExitCode',
                'nsTaskCurrentItem', 'nsTaskTotalItems']
    done = False
    count = 0
    while not done and count < 5:
        entry = topology.master1.getEntry(clean_task_dn, attrlist=attrlist)
        if not entry or entry.nsTaskExitCode:
            done = True
            break
        time.sleep(1)
        count += 1
    if not done:
        log.fatal('test_cleanallruv_abort: CleanAllRUV task was not aborted')
        assert False

    # Start master 2
    log.info('test_cleanallruv_abort: start master 2 to begin the restore process...')
    topology.master2.start(timeout=10)

    #
    # Now run the clean task task again to we can properly restore master 4
    #
    log.info('test_cleanallruv_abort: run cleanAllRUV task so we can properly restore master 4...')
    try:
        topology.master1.tasks.cleanAllRUV(suffix=DEFAULT_SUFFIX,
                                  replicaid='4', args={TASK_WAIT: True})
    except ValueError, e:
        log.fatal('test_cleanallruv_abort: Problem running cleanAllRuv task: ' + e.message('desc'))
        assert False

    log.info('test_cleanallruv_abort PASSED, restoring master 4...')

    #
    # Cleanup - Restore master 4
    #
    restore_master4(topology)


def test_cleanallruv_abort_restart(topology):
    '''
    Test the abort task can handle a restart, and then resume
    '''

    log.info('Running test_cleanallruv_abort_restart...')

    # Disable master 4
    log.info('test_cleanallruv_abort_restart: disable replication on master 4...')
    try:
        topology.master4.replica.disableReplication(DEFAULT_SUFFIX)
    except:
        log.fatal('error!')
        assert False

    # Remove the agreements from the other masters that point to master 4
    log.info('test_cleanallruv_abort_restart: remove all the agreements to master 4...)')
    try:
        topology.master1.agreement.delete(DEFAULT_SUFFIX, topology.master4)
    except ldap.LDAPError, e:
        log.fatal('test_cleanallruv_abort_restart: Failed to delete agmt(m1 -> m4), error: ' +
                  e.message['desc'])
        assert False
    try:
        topology.master2.agreement.delete(DEFAULT_SUFFIX, topology.master4)
    except ldap.LDAPError, e:
        log.fatal('test_cleanallruv_abort_restart: Failed to delete agmt(m2 -> m4), error: ' +
                  e.message['desc'])
        assert False
    try:
        topology.master3.agreement.delete(DEFAULT_SUFFIX, topology.master4)
    except ldap.LDAPError, e:
        log.fatal('test_cleanallruv_abort_restart: Failed to delete agmt(m3 -> m4), error: ' +
                  e.message['desc'])
        assert False

    # Stop master 3
    log.info('test_cleanallruv_abort_restart: stop master 3 to freeze the cleanAllRUV task...')
    topology.master3.stop(timeout=10)

    # Run the task
    log.info('test_cleanallruv_abort_restart: add the cleanAllRUV task...')
    try:
        (clean_task_dn, rc) = topology.master1.tasks.cleanAllRUV(suffix=DEFAULT_SUFFIX,
                                  replicaid='4', args={TASK_WAIT: False})
    except ValueError, e:
        log.fatal('test_cleanallruv_abort_restart: Problem running cleanAllRuv task: ' +
                  e.message('desc'))
        assert False

    # Wait a bit
    time.sleep(5)

    # Abort the task
    log.info('test_cleanallruv_abort_restart: abort the cleanAllRUV task...')
    try:
        topology.master1.tasks.abortCleanAllRUV(suffix=DEFAULT_SUFFIX, replicaid='4',
                                                certify=True, args={TASK_WAIT: False})
    except ValueError, e:
        log.fatal('test_cleanallruv_abort_restart: Problem running test_cleanallruv_abort_restart task: ' +
                  e.message('desc'))
        assert False

    # Allow task to run for a bit:
    time.sleep(5)

    # Check master 1 does not have the clean task running
    log.info('test_cleanallruv_abort: check master 1 no longer has a cleanAllRUV task...')
    attrlist = ['nsTaskLog', 'nsTaskStatus', 'nsTaskExitCode',
                'nsTaskCurrentItem', 'nsTaskTotalItems']
    done = False
    count = 0
    while not done and count < 10:
        entry = topology.master1.getEntry(clean_task_dn, attrlist=attrlist)
        if not entry or entry.nsTaskExitCode:
            done = True
            break
        time.sleep(1)
        count += 1
    if not done:
        log.fatal('test_cleanallruv_abort_restart: CleanAllRUV task was not aborted')
        assert False

    # Now restart master 1, and make sure the abort process completes
    topology.master1.restart(timeout=30)
    if topology.master1.detectDisorderlyShutdown():
        log.fatal('test_cleanallruv_abort_restart: Master 1 previously crashed!')
        assert False

    # Start master 3
    topology.master3.start(timeout=10)

    # Check master 1 tried to run abort task.  We expect the abort task to be aborted.
    if not topology.master1.searchErrorsLog('Aborting abort task'):
        log.fatal('test_cleanallruv_abort_restart: Abort task did not restart')
        assert False

    #
    # Now run the clean task task again to we can properly restore master 4
    #
    log.info('test_cleanallruv_abort_restart: run cleanAllRUV task so we can properly restore master 4...')
    try:
        topology.master1.tasks.cleanAllRUV(suffix=DEFAULT_SUFFIX,
                                  replicaid='4', args={TASK_WAIT: True})
    except ValueError, e:
        log.fatal('test_cleanallruv_abort_restart: Problem running cleanAllRuv task: ' +
                  e.message('desc'))
        assert False

    log.info('test_cleanallruv_abort_restart PASSED, restoring master 4...')

    #
    # Cleanup - Restore master 4
    #
    restore_master4(topology)


def test_cleanallruv_abort_certify(topology):
    '''
    Test the abort task.

    Disable master 4
    Stop master 2 so that it can not be cleaned
    Run the clean task
    Wait a bit
    Abort the task
    Verify task is aborted
    '''

    log.info('Running test_cleanallruv_abort_certify...')

    # Disable master 4
    log.info('test_cleanallruv_abort_certify: disable replication on master 4...')
    try:
        topology.master4.replica.disableReplication(DEFAULT_SUFFIX)
    except:
        log.fatal('error!')
        assert False

    # Remove the agreements from the other masters that point to master 4
    log.info('test_cleanallruv_abort_certify: remove all the agreements to master 4...)')
    try:
        topology.master1.agreement.delete(DEFAULT_SUFFIX, topology.master4)
    except ldap.LDAPError, e:
        log.fatal('test_cleanallruv_abort_certify: Failed to delete agmt(m1 -> m4), error: ' +
                  e.message['desc'])
        assert False
    try:
        topology.master2.agreement.delete(DEFAULT_SUFFIX, topology.master4)
    except ldap.LDAPError, e:
        log.fatal('test_cleanallruv_abort_certify: Failed to delete agmt(m2 -> m4), error: ' +
                  e.message['desc'])
        assert False
    try:
        topology.master3.agreement.delete(DEFAULT_SUFFIX, topology.master4)
    except ldap.LDAPError, e:
        log.fatal('test_cleanallruv_abort_certify: Failed to delete agmt(m3 -> m4), error: ' +
                  e.message['desc'])
        assert False

    # Stop master 2
    log.info('test_cleanallruv_abort_certify: stop master 2 to freeze the cleanAllRUV task...')
    topology.master2.stop(timeout=10)

    # Run the task
    log.info('test_cleanallruv_abort_certify: add the cleanAllRUV task...')
    try:
        (clean_task_dn, rc) = topology.master1.tasks.cleanAllRUV(suffix=DEFAULT_SUFFIX,
                                  replicaid='4', args={TASK_WAIT: False})
    except ValueError, e:
        log.fatal('test_cleanallruv_abort_certify: Problem running cleanAllRuv task: ' +
                  e.message('desc'))
        assert False

    # Abort the task
    log.info('test_cleanallruv_abort_certify: abort the cleanAllRUV task...')
    try:
        (abort_task_dn, rc) = topology.master1.tasks.abortCleanAllRUV(suffix=DEFAULT_SUFFIX,
                                  replicaid='4', certify=True, args={TASK_WAIT: False})
    except ValueError, e:
        log.fatal('test_cleanallruv_abort_certify: Problem running abortCleanAllRuv task: ' +
                  e.message('desc'))
        assert False

    # Wait a while and make sure the abort task is still running
    log.info('test_cleanallruv_abort_certify: sleep for 10 seconds')
    time.sleep(10)

    attrlist = ['nsTaskLog', 'nsTaskStatus', 'nsTaskExitCode',
                'nsTaskCurrentItem', 'nsTaskTotalItems']
    entry = topology.master1.getEntry(abort_task_dn, attrlist=attrlist)
    if not entry or entry.nsTaskExitCode:
        log.fatal('test_cleanallruv_abort_certify: abort task incorrectly finished')
        assert False

    # Now start master 2 so it can be aborted
    log.info('test_cleanallruv_abort_certify: start master 2 to allow the abort task to finish...')
    topology.master2.start(timeout=10)

    # Wait for the abort task to stop
    done = False
    count = 0
    while not done and count < 60:
        entry = topology.master1.getEntry(abort_task_dn, attrlist=attrlist)
        if not entry or entry.nsTaskExitCode:
            done = True
            break
        time.sleep(1)
        count += 1
    if not done:
        log.fatal('test_cleanallruv_abort_certify: The abort CleanAllRUV task was not aborted')
        assert False

    # Check master 1 does not have the clean task running
    log.info('test_cleanallruv_abort_certify: check master 1 no longer has a cleanAllRUV task...')
    attrlist = ['nsTaskLog', 'nsTaskStatus', 'nsTaskExitCode',
                'nsTaskCurrentItem', 'nsTaskTotalItems']
    done = False
    count = 0
    while not done and count < 5:
        entry = topology.master1.getEntry(clean_task_dn, attrlist=attrlist)
        if not entry or entry.nsTaskExitCode:
            done = True
            break
        time.sleep(1)
        count += 1
    if not done:
        log.fatal('test_cleanallruv_abort_certify: CleanAllRUV task was not aborted')
        assert False

    # Start master 2
    log.info('test_cleanallruv_abort_certify: start master 2 to begin the restore process...')
    topology.master2.start(timeout=10)

    #
    # Now run the clean task task again to we can properly restore master 4
    #
    log.info('test_cleanallruv_abort_certify: run cleanAllRUV task so we can properly restore master 4...')
    try:
        topology.master1.tasks.cleanAllRUV(suffix=DEFAULT_SUFFIX,
                                  replicaid='4', args={TASK_WAIT: True})
    except ValueError, e:
        log.fatal('test_cleanallruv_abort_certify: Problem running cleanAllRuv task: ' +
                  e.message('desc'))
        assert False

    log.info('test_cleanallruv_abort_certify PASSED, restoring master 4...')

    #
    # Cleanup - Restore master 4
    #
    restore_master4(topology)


def test_cleanallruv_stress_clean(topology):
    '''
    Put each server(m1 - m4) under stress, and perform the entire clean process
    '''
    log.info('Running test_cleanallruv_stress_clean...')
    log.info('test_cleanallruv_stress_clean: put all the masters under load...')

    # Put all the masters under load
    m1_add_users = AddUsers(topology.master1, 4000)
    m1_add_users.start()
    m2_add_users = AddUsers(topology.master2, 4000)
    m2_add_users.start()
    m3_add_users = AddUsers(topology.master3, 4000)
    m3_add_users.start()
    m4_add_users = AddUsers(topology.master4, 4000)
    m4_add_users.start()

    # Allow sometime to get replication flowing in all directions
    log.info('test_cleanallruv_stress_clean: allow some time for replication to get flowing...')
    time.sleep(5)

    # Put master 4 into read only mode
    log.info('test_cleanallruv_stress_clean: put master 4 into read-only mode...')
    try:
        topology.master4.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE, 'nsslapd-readonly', 'on')])
    except ldap.LDAPError, e:
        log.fatal('test_cleanallruv_stress_clean: Failed to put master 4 into read-only mode: error ' +
                  e.message['desc'])
        assert False

    # We need to wait for master 4 to push its changes out
    log.info('test_cleanallruv_stress_clean: allow some time for master 4 to push changes out (30 seconds)...')
    time.sleep(30)

    # Disable master 4
    log.info('test_cleanallruv_stress_clean: disable replication on master 4...')
    try:
        topology.master4.replica.disableReplication(DEFAULT_SUFFIX)
    except:
        log.fatal('test_cleanallruv_stress_clean: failed to diable replication')
        assert False

    # Remove the agreements from the other masters that point to master 4
    log.info('test_cleanallruv_stress_clean: remove all the agreements to master 4...')
    try:
        topology.master1.agreement.delete(DEFAULT_SUFFIX, topology.master4)
    except ldap.LDAPError, e:
        log.fatal('test_cleanallruv_stress_clean: Failed to delete agmt(m1 -> m4), error: ' +
                  e.message['desc'])
        assert False
    try:
        topology.master2.agreement.delete(DEFAULT_SUFFIX, topology.master4)
    except ldap.LDAPError, e:
        log.fatal('test_cleanallruv_stress_clean: Failed to delete agmt(m2 -> m4), error: ' +
                  e.message['desc'])
        assert False
    try:
        topology.master3.agreement.delete(DEFAULT_SUFFIX, topology.master4)
    except ldap.LDAPError, e:
        log.fatal('test_cleanallruv_stress_clean: Failed to delete agmt(m3 -> m4), error: ' +
                  e.message['desc'])
        assert False

    # Run the task
    log.info('test_cleanallruv_stress_clean: Run the cleanAllRUV task...')
    try:
        topology.master1.tasks.cleanAllRUV(suffix=DEFAULT_SUFFIX, replicaid='4',
                                           args={TASK_WAIT: True})
    except ValueError, e:
        log.fatal('test_cleanallruv_stress_clean: Problem running cleanAllRuv task: ' +
                  e.message('desc'))
        assert False

    # Wait for the update to finish
    log.info('test_cleanallruv_stress_clean: wait for all the updates to finish...')
    m1_add_users.join()
    m2_add_users.join()
    m3_add_users.join()
    m4_add_users.join()

    # Check the other master's RUV for 'replica 4'
    log.info('test_cleanallruv_stress_clean: check if all the replicas have been cleaned...')
    clean = False
    count = 0
    while not clean and count < 10:
        clean = True

        # Check master 1
        try:
            entry = topology.master1.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, REPLICA_RUV_FILTER)
            if not entry:
                log.error('test_cleanallruv_stress_clean: Failed to find db tombstone entry from master')
                repl_fail(replica_inst)
            elements = entry[0].getValues('nsds50ruv')
            for ruv in elements:
                if 'replica 4' in ruv:
                    # Not cleaned
                    log.error('test_cleanallruv_stress_clean: Master 1 not cleaned!')
                    clean = False
            if clean:
                log.info('test_cleanallruv_stress_clean: Master 1 is cleaned.')
        except ldap.LDAPError, e:
            log.fatal('test_cleanallruv_stress_clean: Unable to search master 1 for db tombstone: ' +
                      e.message['desc'])

        # Check master 2
        try:
            entry = topology.master2.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, REPLICA_RUV_FILTER)
            if not entry:
                log.error('test_cleanallruv_stress_clean: Failed to find db tombstone entry from master')
                repl_fail(replica_inst)
            elements = entry[0].getValues('nsds50ruv')
            for ruv in elements:
                if 'replica 4' in ruv:
                    # Not cleaned
                    log.error('test_cleanallruv_stress_clean: Master 2 not cleaned!')
                    clean = False
            if clean:
                log.info('test_cleanallruv_stress_clean: Master 2 is cleaned.')
        except ldap.LDAPError, e:
            log.fatal('test_cleanallruv_stress_clean: Unable to search master 2 for db tombstone: ' +
                      e.message['desc'])

        # Check master 3
        try:
            entry = topology.master3.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, REPLICA_RUV_FILTER)
            if not entry:
                log.error('test_cleanallruv_stress_clean: Failed to find db tombstone entry from master')
                repl_fail(replica_inst)
            elements = entry[0].getValues('nsds50ruv')
            for ruv in elements:
                if 'replica 4' in ruv:
                    # Not cleaned
                    log.error('test_cleanallruv_stress_clean: Master 3 not cleaned!')
                    clean = False
            if clean:
                log.info('test_cleanallruv_stress_clean: Master 3 is cleaned.')
        except ldap.LDAPError, e:
            log.fatal('test_cleanallruv_stress_clean: Unable to search master 3 for db tombstone: ' +
                      e.message['desc'])

        # Sleep a bit and give it chance to clean up...
        time.sleep(5)
        count += 1

    if not clean:
        log.fatal('test_cleanallruv_stress_clean: Failed to clean replicas')
        assert False

    log.info('test_cleanallruv_stress_clean:  PASSED, restoring master 4...')

    #
    # Cleanup - restore master 4
    #

    # Turn off readonly mode
    try:
        topology.master4.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE, 'nsslapd-readonly', 'off')])
    except ldap.LDAPError, e:
        log.fatal('test_cleanallruv_stress_clean: Failed to put master 4 into read-only mode: error ' +
                  e.message['desc'])
        assert False

    restore_master4(topology)


def test_cleanallruv_final(topology):
    topology.master1.delete()
    topology.master2.delete()
    topology.master3.delete()
    topology.master4.delete()
    log.info('cleanAllRUV test suite PASSED')


def run_isolated():
    global installation1_prefix
    installation1_prefix = None
    topo = topology(True)

    test_cleanallruv_init(topo)
    test_cleanallruv_clean(topo)
    test_cleanallruv_clean_restart(topo)
    test_cleanallruv_clean_force(topo)
    test_cleanallruv_abort(topo)
    test_cleanallruv_abort_restart(topo)
    test_cleanallruv_abort_certify(topo)
    test_cleanallruv_stress_clean(topo)
    test_cleanallruv_final(topo)


if __name__ == '__main__':
    run_isolated()

