# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import sys
import time
import logging
import pytest
from lib389 import DirSrv
from lib389._constants import *
from lib389.properties import *

DEBUGGING = False
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


class TopologyMain(object):
    def __init__(self, standalone=None, masters=None,
                 consumers=None, hubs=None):
        if standalone:
            self.standalone = standalone
        if masters:
            self.ms = masters
        if consumers:
            self.cs = consumers
        if hubs:
            self.hs = hubs


@pytest.fixture(scope="module")
def topology_st(request):
    """Create DS standalone instance"""

    if DEBUGGING:
        standalone = DirSrv(verbose=True)
    else:
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

    def fin():
        if DEBUGGING:
            standalone.stop()
        else:
            standalone.delete()

    request.addfinalizer(fin)

    return TopologyMain(standalone=standalone)


@pytest.fixture(scope="module")
def topology_m2(request):
    """Create Replication Deployment with two masters"""

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
    master1.replica.enableReplication(suffix=SUFFIX, role=REPLICAROLE_MASTER,
                                      replicaId=REPLICAID_MASTER_1)

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
    master2.replica.enableReplication(suffix=SUFFIX, role=REPLICAROLE_MASTER,
                                      replicaId=REPLICAID_MASTER_2)

    def fin():
        if DEBUGGING:
            master1.stop()
            master2.stop()
        else:
            master1.delete()
            master2.delete()

    request.addfinalizer(fin)

    # Create all the agreements
    # Creating agreement from master 1 to master 2
    properties = {RA_NAME: 'meTo_{}:{}'.format(master2.host, str(master2.port)),
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m1_m2_agmt = master1.agreement.create(suffix=SUFFIX, host=master2.host,
                                          port=master2.port, properties=properties)
    if not m1_m2_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("{} created".format(m1_m2_agmt))

    # Creating agreement from master 2 to master 1
    properties = {RA_NAME: 'meTo_' + master1.host + ':' + str(master1.port),
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m2_m1_agmt = master2.agreement.create(suffix=SUFFIX, host=master1.host,
                                          port=master1.port, properties=properties)
    if not m2_m1_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("{} created".format(m2_m1_agmt))

    # Allow the replicas to get situated with the new agreements...
    time.sleep(5)

    # Initialize all the agreements
    master1.agreement.init(SUFFIX, HOST_MASTER_2, PORT_MASTER_2)
    master1.waitForReplInit(m1_m2_agmt)

    # Check replication is working...
    if master1.testReplication(DEFAULT_SUFFIX, master2):
        log.info('Replication is working.')
    else:
        log.fatal('Replication is not working.')
        assert False

    # Clear out the tmp dir
    master1.clearTmpDir(__file__)

    return TopologyMain(masters={"master1": master1, "master1_agmts": {"m1_m2": m1_m2_agmt},
                                 "master2": master2, "master2_agmts": {"m2_m1": m2_m1_agmt}})


@pytest.fixture(scope="module")
def topology_m4(request):
    """Create Replication Deployment with four masters"""

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
    master1.replica.enableReplication(suffix=SUFFIX, role=REPLICAROLE_MASTER,
                                      replicaId=REPLICAID_MASTER_1)

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
    master2.replica.enableReplication(suffix=SUFFIX, role=REPLICAROLE_MASTER,
                                      replicaId=REPLICAID_MASTER_2)

    # Creating master 3...
    if DEBUGGING:
        master3 = DirSrv(verbose=True)
    else:
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
    master3.replica.enableReplication(suffix=SUFFIX, role=REPLICAROLE_MASTER,
                                      replicaId=REPLICAID_MASTER_3)

    # Creating master 4...
    if DEBUGGING:
        master4 = DirSrv(verbose=True)
    else:
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
    master4.replica.enableReplication(suffix=SUFFIX, role=REPLICAROLE_MASTER,
                                      replicaId=REPLICAID_MASTER_4)

    def fin():
        if DEBUGGING:
            master1.stop()
            master2.stop()
            master3.stop()
            master4.stop()
        else:
            master1.delete()
            master2.delete()
            master3.delete()
            master4.delete()

    request.addfinalizer(fin)

    # Create all the agreements
    # Creating agreement from master 1 to master 2
    properties = {RA_NAME: 'meTo_' + master2.host + ':' + str(master2.port),
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m1_m2_agmt = master1.agreement.create(suffix=SUFFIX, host=master2.host,
                                          port=master2.port, properties=properties)
    if not m1_m2_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m1_m2_agmt)

    # Creating agreement from master 1 to master 3
    properties = {RA_NAME: 'meTo_' + master3.host + ':' + str(master3.port),
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m1_m3_agmt = master1.agreement.create(suffix=SUFFIX, host=master3.host,
                                          port=master3.port, properties=properties)
    if not m1_m3_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m1_m3_agmt)

    # Creating agreement from master 1 to master 4
    properties = {RA_NAME: 'meTo_' + master4.host + ':' + str(master4.port),
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m1_m4_agmt = master1.agreement.create(suffix=SUFFIX, host=master4.host,
                                          port=master4.port, properties=properties)
    if not m1_m4_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m1_m4_agmt)

    # Creating agreement from master 2 to master 1
    properties = {RA_NAME: 'meTo_' + master1.host + ':' + str(master1.port),
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m2_m1_agmt = master2.agreement.create(suffix=SUFFIX, host=master1.host,
                                          port=master1.port, properties=properties)
    if not m2_m1_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m2_m1_agmt)

    # Creating agreement from master 2 to master 3
    properties = {RA_NAME: 'meTo_' + master3.host + ':' + str(master3.port),
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m2_m3_agmt = master2.agreement.create(suffix=SUFFIX, host=master3.host,
                                          port=master3.port, properties=properties)
    if not m2_m3_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m2_m3_agmt)

    # Creating agreement from master 2 to master 4
    properties = {RA_NAME: 'meTo_' + master4.host + ':' + str(master4.port),
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m2_m4_agmt = master2.agreement.create(suffix=SUFFIX, host=master4.host,
                                          port=master4.port, properties=properties)
    if not m2_m4_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m2_m4_agmt)

    # Creating agreement from master 3 to master 1
    properties = {RA_NAME: 'meTo_' + master1.host + ':' + str(master1.port),
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m3_m1_agmt = master3.agreement.create(suffix=SUFFIX, host=master1.host,
                                          port=master1.port, properties=properties)
    if not m3_m1_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m3_m1_agmt)

    # Creating agreement from master 3 to master 2
    properties = {RA_NAME: 'meTo_' + master2.host + ':' + str(master2.port),
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m3_m2_agmt = master3.agreement.create(suffix=SUFFIX, host=master2.host,
                                          port=master2.port, properties=properties)
    if not m3_m2_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m3_m2_agmt)

    # Creating agreement from master 3 to master 4
    properties = {RA_NAME: 'meTo_' + master4.host + ':' + str(master4.port),
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m3_m4_agmt = master3.agreement.create(suffix=SUFFIX, host=master4.host,
                                          port=master4.port, properties=properties)
    if not m3_m4_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m3_m4_agmt)

    # Creating agreement from master 4 to master 1
    properties = {RA_NAME: 'meTo_' + master1.host + ':' + str(master1.port),
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m4_m1_agmt = master4.agreement.create(suffix=SUFFIX, host=master1.host,
                                          port=master1.port, properties=properties)
    if not m4_m1_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m4_m1_agmt)

    # Creating agreement from master 4 to master 2
    properties = {RA_NAME: 'meTo_' + master2.host + ':' + str(master2.port),
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m4_m2_agmt = master4.agreement.create(suffix=SUFFIX, host=master2.host,
                                          port=master2.port, properties=properties)
    if not m4_m2_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m4_m2_agmt)

    # Creating agreement from master 4 to master 3
    properties = {RA_NAME: 'meTo_' + master3.host + ':' + str(master3.port),
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m4_m3_agmt = master4.agreement.create(suffix=SUFFIX, host=master3.host,
                                          port=master3.port, properties=properties)
    if not m4_m3_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m4_m3_agmt)

    # Allow the replicas to get situated with the new agreements...
    time.sleep(5)

    # Initialize all the agreements
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

    return TopologyMain(masters={"master1": master1, "master1_agmts": {"m1_m2": m1_m2_agmt,
                                                                       "m1_m3": m1_m3_agmt,
                                                                       "m1_m4": m1_m4_agmt},
                                 "master2": master2, "master2_agmts": {"m2_m1": m2_m1_agmt,
                                                                       "m2_m3": m2_m3_agmt,
                                                                       "m2_m4": m2_m4_agmt},
                                 "master3": master3, "master3_agmts": {"m3_m1": m3_m1_agmt,
                                                                       "m3_m2": m3_m2_agmt,
                                                                       "m3_m4": m3_m4_agmt},
                                 "master4": master4, "master4_agmts": {"m4_m1": m4_m1_agmt,
                                                                       "m4_m2": m4_m2_agmt,
                                                                       "m4_m3": m4_m3_agmt}})

