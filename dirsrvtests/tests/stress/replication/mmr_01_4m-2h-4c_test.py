import os
import sys
import time
import datetime
import ldap
import logging
import pytest
import threading
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from lib389.utils import *
from lib389.repltools import ReplTools

pytestmark = pytest.mark.tier3

logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

DEBUGGING = False
ADD_DEL_COUNT = 5000
MAX_LOOPS = 5
TEST_CONVERGE_LATENCY = True
CONVERGENCE_TIMEOUT = '60'
master_list = []
hub_list = []
con_list = []
TEST_START = time.time()

LAST_DN_IDX = ADD_DEL_COUNT - 1
LAST_DN_M1 = 'DEL dn="uid=master_1-%d,%s' % (LAST_DN_IDX, DEFAULT_SUFFIX)
LAST_DN_M2 = 'DEL dn="uid=master_2-%d,%s' % (LAST_DN_IDX, DEFAULT_SUFFIX)
LAST_DN_M3 = 'DEL dn="uid=master_3-%d,%s' % (LAST_DN_IDX, DEFAULT_SUFFIX)
LAST_DN_M4 = 'DEL dn="uid=master_4-%d,%s' % (LAST_DN_IDX, DEFAULT_SUFFIX)


class TopologyReplication(object):
    """The Replication Topology Class"""
    def __init__(self, master1, master2, master3, master4, hub1, hub2,
                 consumer1, consumer2, consumer3, consumer4):
        """Init"""
        master1.open()
        self.master1 = master1
        master2.open()
        self.master2 = master2
        master3.open()
        self.master3 = master3
        master4.open()
        self.master4 = master4
        hub1.open()
        self.hub1 = hub1
        hub2.open()
        self.hub2 = hub2
        consumer1.open()
        self.consumer1 = consumer1
        consumer2.open()
        self.consumer2 = consumer2
        consumer3.open()
        self.consumer3 = consumer3
        consumer4.open()
        self.consumer4 = consumer4
        master_list.append(master1.serverid)
        master_list.append(master2.serverid)
        master_list.append(master3.serverid)
        master_list.append(master4.serverid)
        hub_list.append(hub1.serverid)
        hub_list.append(hub2.serverid)
        con_list.append(consumer1.serverid)
        con_list.append(consumer2.serverid)
        con_list.append(consumer3.serverid)
        con_list.append(consumer4.serverid)


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
    master1.replica.enableReplication(suffix=SUFFIX, role=ReplicaRole.MASTER,
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
    master2.replica.enableReplication(suffix=SUFFIX, role=ReplicaRole.MASTER,
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
    master3.replica.enableReplication(suffix=SUFFIX, role=ReplicaRole.MASTER,
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
    master4.replica.enableReplication(suffix=SUFFIX, role=ReplicaRole.MASTER,
                                      replicaId=REPLICAID_MASTER_4)

    # Creating hub 1...
    if DEBUGGING:
        hub1 = DirSrv(verbose=True)
    else:
        hub1 = DirSrv(verbose=False)
    args_instance[SER_HOST] = HOST_HUB_1
    args_instance[SER_PORT] = PORT_HUB_1
    args_instance[SER_SERVERID_PROP] = SERVERID_HUB_1
    args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
    args_hub = args_instance.copy()
    hub1.allocate(args_hub)
    instance_hub1 = hub1.exists()
    if instance_hub1:
        hub1.delete()
    hub1.create()
    hub1.open()
    hub1.replica.enableReplication(suffix=SUFFIX, role=ReplicaRole.HUB,
                                   replicaId=REPLICAID_HUB_1)

    # Creating hub 2...
    if DEBUGGING:
        hub2 = DirSrv(verbose=True)
    else:
        hub2 = DirSrv(verbose=False)
    args_instance[SER_HOST] = HOST_HUB_2
    args_instance[SER_PORT] = PORT_HUB_2
    args_instance[SER_SERVERID_PROP] = SERVERID_HUB_2
    args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
    args_hub = args_instance.copy()
    hub2.allocate(args_hub)
    instance_hub2 = hub2.exists()
    if instance_hub2:
        hub2.delete()
    hub2.create()
    hub2.open()
    hub2.replica.enableReplication(suffix=SUFFIX, role=ReplicaRole.HUB,
                                   replicaId=REPLICAID_HUB_2)

    # Creating consumer 1...
    if DEBUGGING:
        consumer1 = DirSrv(verbose=True)
    else:
        consumer1 = DirSrv(verbose=False)
    args_instance[SER_HOST] = HOST_CONSUMER_1
    args_instance[SER_PORT] = PORT_CONSUMER_1
    args_instance[SER_SERVERID_PROP] = SERVERID_CONSUMER_1
    args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
    args_consumer = args_instance.copy()
    consumer1.allocate(args_consumer)
    instance_consumer1 = consumer1.exists()
    if instance_consumer1:
        consumer1.delete()
    consumer1.create()
    consumer1.open()
    consumer1.replica.enableReplication(suffix=SUFFIX,
                                        role=ReplicaRole.CONSUMER,
                                        replicaId=CONSUMER_REPLICAID)

    # Creating consumer 2...
    if DEBUGGING:
        consumer2 = DirSrv(verbose=True)
    else:
        consumer2 = DirSrv(verbose=False)
    args_instance[SER_HOST] = HOST_CONSUMER_2
    args_instance[SER_PORT] = PORT_CONSUMER_2
    args_instance[SER_SERVERID_PROP] = SERVERID_CONSUMER_2
    args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
    args_consumer = args_instance.copy()
    consumer2.allocate(args_consumer)
    instance_consumer2 = consumer2.exists()
    if instance_consumer2:
        consumer2.delete()
    consumer2.create()
    consumer2.open()
    consumer2.replica.enableReplication(suffix=SUFFIX,
                                        role=ReplicaRole.CONSUMER,
                                        replicaId=CONSUMER_REPLICAID)

    # Creating consumer 3...
    if DEBUGGING:
        consumer3 = DirSrv(verbose=True)
    else:
        consumer3 = DirSrv(verbose=False)
    args_instance[SER_HOST] = HOST_CONSUMER_3
    args_instance[SER_PORT] = PORT_CONSUMER_3
    args_instance[SER_SERVERID_PROP] = SERVERID_CONSUMER_3
    args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
    args_consumer = args_instance.copy()
    consumer3.allocate(args_consumer)
    instance_consumer3 = consumer3.exists()
    if instance_consumer3:
        consumer3.delete()
    consumer3.create()
    consumer3.open()
    consumer3.replica.enableReplication(suffix=SUFFIX,
                                        role=ReplicaRole.CONSUMER,
                                        replicaId=CONSUMER_REPLICAID)

    # Creating consumer 4...
    if DEBUGGING:
        consumer4 = DirSrv(verbose=True)
    else:
        consumer4 = DirSrv(verbose=False)
    args_instance[SER_HOST] = HOST_CONSUMER_4
    args_instance[SER_PORT] = PORT_CONSUMER_4
    args_instance[SER_SERVERID_PROP] = SERVERID_CONSUMER_4
    args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
    args_consumer = args_instance.copy()
    consumer4.allocate(args_consumer)
    instance_consumer4 = consumer4.exists()
    if instance_consumer4:
        consumer4.delete()
    consumer4.create()
    consumer4.open()
    consumer4.replica.enableReplication(suffix=SUFFIX,
                                        role=ReplicaRole.CONSUMER,
                                        replicaId=CONSUMER_REPLICAID)

    #
    # Create all the agreements
    #

    # Creating agreement from master 1 to master 2
    properties = {RA_NAME: 'meTo_' + master2.host + ':' + str(master2.port),
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m1_m2_agmt = master1.agreement.create(suffix=SUFFIX, host=master2.host,
                                          port=master2.port,
                                          properties=properties)
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
                                          port=master3.port,
                                          properties=properties)
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
                                          port=master4.port,
                                          properties=properties)
    if not m1_m4_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m1_m4_agmt)

    # Creating agreement from master 1 to hub 1
    properties = {RA_NAME: 'meTo_' + hub1.host + ':' + str(hub1.port),
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m1_h1_agmt = master1.agreement.create(suffix=SUFFIX, host=hub1.host,
                                          port=hub1.port,
                                          properties=properties)
    if not m1_h1_agmt:
        log.fatal("Fail to create a master -> hub replica agreement")
        sys.exit(1)
    log.debug("%s created" % m1_h1_agmt)

    # Creating agreement from master 1 to hub 2
    properties = {RA_NAME: 'meTo_' + hub2.host + ':' + str(hub2.port),
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m1_h2_agmt = master1.agreement.create(suffix=SUFFIX, host=hub2.host,
                                          port=hub2.port,
                                          properties=properties)
    if not m1_h2_agmt:
        log.fatal("Fail to create a master -> hub replica agreement")
        sys.exit(1)
    log.debug("%s created" % m1_h2_agmt)

    # Creating agreement from master 2 to master 1
    properties = {RA_NAME: 'meTo_' + master1.host + ':' + str(master1.port),
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m2_m1_agmt = master2.agreement.create(suffix=SUFFIX, host=master1.host,
                                          port=master1.port,
                                          properties=properties)
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
                                          port=master3.port,
                                          properties=properties)
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
                                          port=master4.port,
                                          properties=properties)
    if not m2_m4_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m2_m4_agmt)

    # Creating agreement from master 2 to hub 1
    properties = {RA_NAME: 'meTo_' + hub1.host + ':' + str(hub1.port),
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m2_h1_agmt = master2.agreement.create(suffix=SUFFIX, host=hub1.host,
                                          port=hub1.port,
                                          properties=properties)
    if not m2_h1_agmt:
        log.fatal("Fail to create a master -> hub replica agreement")
        sys.exit(1)
    log.debug("%s created" % m2_h1_agmt)

    # Creating agreement from master 2 to hub 2
    properties = {RA_NAME: 'meTo_' + hub2.host + ':' + str(hub2.port),
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m2_h2_agmt = master2.agreement.create(suffix=SUFFIX, host=hub2.host,
                                          port=hub2.port,
                                          properties=properties)
    if not m2_h2_agmt:
        log.fatal("Fail to create a master -> hub replica agreement")
        sys.exit(1)
    log.debug("%s created" % m2_h2_agmt)

    # Creating agreement from master 3 to master 1
    properties = {RA_NAME: 'meTo_' + master1.host + ':' + str(master1.port),
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m3_m1_agmt = master3.agreement.create(suffix=SUFFIX, host=master1.host,
                                          port=master1.port,
                                          properties=properties)
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
                                          port=master2.port,
                                          properties=properties)
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
                                          port=master4.port,
                                          properties=properties)
    if not m3_m4_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m3_m4_agmt)

    # Creating agreement from master 3 to hub 1
    properties = {RA_NAME: 'meTo_' + hub1.host + ':' + str(hub1.port),
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m3_h1_agmt = master3.agreement.create(suffix=SUFFIX, host=hub1.host,
                                          port=hub1.port,
                                          properties=properties)
    if not m3_h1_agmt:
        log.fatal("Fail to create a master -> hub replica agreement")
        sys.exit(1)
    log.debug("%s created" % m3_h1_agmt)

    # Creating agreement from master 3 to hub 2
    properties = {RA_NAME: 'meTo_' + hub2.host + ':' + str(hub2.port),
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m3_h2_agmt = master3.agreement.create(suffix=SUFFIX, host=hub2.host,
                                          port=hub2.port,
                                          properties=properties)
    if not m3_h2_agmt:
        log.fatal("Fail to create a master -> hub replica agreement")
        sys.exit(1)
    log.debug("%s created" % m3_h2_agmt)

    # Creating agreement from master 4 to master 1
    properties = {RA_NAME: 'meTo_' + master1.host + ':' + str(master1.port),
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m4_m1_agmt = master4.agreement.create(suffix=SUFFIX, host=master1.host,
                                          port=master1.port,
                                          properties=properties)
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
                                          port=master2.port,
                                          properties=properties)
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
                                          port=master3.port,
                                          properties=properties)
    if not m4_m3_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m4_m3_agmt)

    # Creating agreement from master 4 to hub 1
    properties = {RA_NAME: 'meTo_' + hub1.host + ':' + str(hub1.port),
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m4_h1_agmt = master4.agreement.create(suffix=SUFFIX, host=hub1.host,
                                          port=hub1.port,
                                          properties=properties)
    if not m4_h1_agmt:
        log.fatal("Fail to create a master -> hub replica agreement")
        sys.exit(1)
    log.debug("%s created" % m4_h1_agmt)

    # Creating agreement from master 4 to hub 2
    properties = {RA_NAME: 'meTo_' + hub2.host + ':' + str(hub2.port),
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m4_h2_agmt = master4.agreement.create(suffix=SUFFIX, host=hub2.host,
                                          port=hub2.port,
                                          properties=properties)
    if not m4_h2_agmt:
        log.fatal("Fail to create a master -> hub replica agreement")
        sys.exit(1)
    log.debug("%s created" % m4_h2_agmt)

    # Creating agreement from hub 1 to consumer 1
    properties = {RA_NAME: 'me2_' + consumer1.host + ':' + str(consumer1.port),
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    h1_c1_agmt = hub1.agreement.create(suffix=SUFFIX, host=consumer1.host,
                                       port=consumer1.port,
                                       properties=properties)
    if not h1_c1_agmt:
        log.fatal("Fail to create a hub -> consumer replica agreement")
        sys.exit(1)
    log.debug("%s created" % h1_c1_agmt)

    # Creating agreement from hub 1 to consumer 2
    properties = {RA_NAME: 'me2_' + consumer2.host + ':' + str(consumer2.port),
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    h1_c2_agmt = hub1.agreement.create(suffix=SUFFIX, host=consumer2.host,
                                       port=consumer2.port,
                                       properties=properties)
    if not h1_c2_agmt:
        log.fatal("Fail to create a hub -> consumer replica agreement")
        sys.exit(1)
    log.debug("%s created" % h1_c2_agmt)

    # Creating agreement from hub 1 to consumer 3
    properties = {RA_NAME: 'me2_' + consumer3.host + ':' + str(consumer3.port),
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    h1_c3_agmt = hub1.agreement.create(suffix=SUFFIX, host=consumer3.host,
                                       port=consumer3.port,
                                       properties=properties)
    if not h1_c3_agmt:
        log.fatal("Fail to create a hub -> consumer replica agreement")
        sys.exit(1)
    log.debug("%s created" % h1_c3_agmt)

    # Creating agreement from hub 1 to consumer 4
    properties = {RA_NAME: 'me2_' + consumer4.host + ':' + str(consumer4.port),
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    h1_c4_agmt = hub1.agreement.create(suffix=SUFFIX, host=consumer4.host,
                                       port=consumer4.port,
                                       properties=properties)
    if not h1_c4_agmt:
        log.fatal("Fail to create a hub -> consumer replica agreement")
        sys.exit(1)
    log.debug("%s created" % h1_c4_agmt)

    # Creating agreement from hub 2 to consumer 1
    properties = {RA_NAME: 'me2_' + consumer1.host + ':' + str(consumer1.port),
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    h2_c1_agmt = hub2.agreement.create(suffix=SUFFIX, host=consumer1.host,
                                       port=consumer1.port,
                                       properties=properties)
    if not h2_c1_agmt:
        log.fatal("Fail to create a hub -> consumer replica agreement")
        sys.exit(1)
    log.debug("%s created" % h2_c1_agmt)

    # Creating agreement from hub 2 to consumer 2
    properties = {RA_NAME: 'me2_' + consumer2.host + ':' + str(consumer2.port),
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    h2_c2_agmt = hub2.agreement.create(suffix=SUFFIX, host=consumer2.host,
                                       port=consumer2.port,
                                       properties=properties)
    if not h2_c2_agmt:
        log.fatal("Fail to create a hub -> consumer replica agreement")
        sys.exit(1)
    log.debug("%s created" % h2_c2_agmt)

    # Creating agreement from hub 2 to consumer 3
    properties = {RA_NAME: 'me2_' + consumer3.host + ':' + str(consumer3.port),
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    h2_c3_agmt = hub2.agreement.create(suffix=SUFFIX, host=consumer3.host,
                                       port=consumer3.port,
                                       properties=properties)
    if not h2_c3_agmt:
        log.fatal("Fail to create a hub -> consumer replica agreement")
        sys.exit(1)
    log.debug("%s created" % h2_c3_agmt)

    # Creating agreement from hub 2 to consumer 4
    properties = {RA_NAME: 'me2_' + consumer4.host + ':' + str(consumer4.port),
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    h2_c4_agmt = hub2.agreement.create(suffix=SUFFIX, host=consumer4.host,
                                       port=consumer4.port,
                                       properties=properties)
    if not h2_c4_agmt:
        log.fatal("Fail to create a hub -> consumer replica agreement")
        sys.exit(1)
    log.debug("%s created" % h2_c4_agmt)

    # Allow the replicas to get situated with the new agreements...
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
    master1.agreement.init(SUFFIX, HOST_HUB_1, PORT_HUB_1)
    master1.waitForReplInit(m1_h1_agmt)
    hub1.agreement.init(SUFFIX, HOST_CONSUMER_1, PORT_CONSUMER_1)
    hub1.waitForReplInit(h1_c1_agmt)
    hub1.agreement.init(SUFFIX, HOST_CONSUMER_2, PORT_CONSUMER_2)
    hub1.waitForReplInit(h1_c2_agmt)
    hub1.agreement.init(SUFFIX, HOST_CONSUMER_3, PORT_CONSUMER_3)
    hub1.waitForReplInit(h1_c3_agmt)
    hub1.agreement.init(SUFFIX, HOST_CONSUMER_4, PORT_CONSUMER_4)
    hub1.waitForReplInit(h1_c4_agmt)
    master1.agreement.init(SUFFIX, HOST_HUB_2, PORT_HUB_2)
    master1.waitForReplInit(m1_h2_agmt)

    # Check replication is working...
    if master1.testReplication(DEFAULT_SUFFIX, consumer1):
        log.info('Replication is working.')
    else:
        log.fatal('Replication is not working.')
        assert False

    def fin():
        """If we are debugging just stop the instances, otherwise remove
        them
        """
        if DEBUGGING:
            master1.stop()
            master2.stop()
            master3.stop()
            master4.stop()
            hub1.stop()
            hub2.stop()
            consumer1.stop()
            consumer2.stop()
            consumer3.stop()
            consumer4.stop()
        else:
            master1.delete()
            master2.delete()
            master3.delete()
            master4.delete()
            hub1.delete()
            hub2.delete()
            consumer1.delete()
            consumer2.delete()
            consumer3.delete()
            consumer4.delete()
    request.addfinalizer(fin)

    return TopologyReplication(master1, master2, master3, master4, hub1, hub2,
                               consumer1, consumer2, consumer3, consumer4)


class AddDelUsers(threading.Thread):
    """Add's and delets 50000 entries"""
    def __init__(self, inst):
        """
        Initialize the thread
        """
        threading.Thread.__init__(self)
        self.daemon = True
        self.inst = inst
        self.name = inst.serverid

    def run(self):
        """
        Start adding users
        """
        idx = 0

        log.info('AddDelUsers (%s) Adding and deleting %d entries...' %
                 (self.name, ADD_DEL_COUNT))

        while idx < ADD_DEL_COUNT:
            RDN_VAL = ('uid=%s-%d' % (self.name, idx))
            USER_DN = ('%s,%s' % (RDN_VAL, DEFAULT_SUFFIX))

            try:
                self.inst.add_s(Entry((USER_DN, {'objectclass':
                                            'top extensibleObject'.split(),
                                            'uid': RDN_VAL})))
            except ldap.LDAPError as e:
                log.fatal('AddDelUsers (%s): failed to add (%s) error: %s' %
                          (self.name, USER_DN, str(e)))
                assert False

            try:
                self.inst.delete_s(USER_DN)
            except ldap.LDAPError as e:
                log.fatal('AddDelUsers (%s): failed to delete (%s) error: %s' %
                          (self.name, USER_DN, str(e)))
                assert False

            idx += 1

        log.info('AddDelUsers (%s) - Finished at: %s' %
                 (self.name, getDateTime()))


def measureConvergence(topology):
    """Find and measure the convergence of entries from each master
    """

    replicas = [topology.master1, topology.master2, topology.master3,
                topology.master4, topology.hub1, topology.hub2,
                topology.consumer1, topology.consumer2, topology.consumer3,
                topology.consumer4]

    if ADD_DEL_COUNT > 10:
        interval = int(ADD_DEL_COUNT / 10)
    else:
        interval = 1

    for master in [('1', topology.master1),
                   ('2', topology.master2),
                   ('3', topology.master3),
                   ('4', topology.master4)]:
        # Start with the first entry
        entries = ['ADD dn="uid=master_%s-0,%s' %
                   (master[0], DEFAULT_SUFFIX)]

        # Add incremental entries to the list
        idx = interval
        while idx < ADD_DEL_COUNT:
            entries.append('ADD dn="uid=master_%s-%d,%s' %
                         (master[0], idx, DEFAULT_SUFFIX))
            idx += interval

        # Add the last entry to the list (if it was not already added)
        if idx != (ADD_DEL_COUNT - 1):
            entries.append('ADD dn="uid=master_%s-%d,%s' %
                           (master[0], (ADD_DEL_COUNT - 1),
                           DEFAULT_SUFFIX))

        ReplTools.replConvReport(DEFAULT_SUFFIX, entries, master[1], replicas)


def test_MMR_Integrity(topology):
    """Apply load to 4 masters at the same time.  Perform adds and deletes.
    If any updates are missed we will see an error 32 in the access logs or
    we will have entries left over once the test completes.
    """
    loop = 0

    ALL_REPLICAS = [topology.master1, topology.master2, topology.master3,
                    topology.master4,
                    topology.hub1, topology.hub2,
                    topology.consumer1, topology.consumer2,
                    topology.consumer3, topology.consumer4]

    if TEST_CONVERGE_LATENCY:
        try:
            for inst in ALL_REPLICAS:
                replica = inst.replicas.get(DEFAULT_SUFFIX)
                replica.set('nsds5ReplicaReleaseTimeout', CONVERGENCE_TIMEOUT)
        except ldap.LDAPError as e:
            log.fatal('Failed to set replicas release timeout - error: %s' %
                      (str(e)))
            assert False

    if DEBUGGING:
        # Enable Repl logging, and increase the max logs
        try:
            for inst in ALL_REPLICAS:
                inst.enableReplLogging()
                inst.modify_s("cn=config", [(ldap.MOD_REPLACE,
                                             'nsslapd-errorlog-maxlogsperdir',
                                             '5')])
        except ldap.LDAPError as e:
            log.fatal('Failed to set max logs - error: %s' % (str(e)))
            assert False

    while loop < MAX_LOOPS:
        # Remove the current logs so we have a clean set of logs to check.
        log.info('Pass %d...' % (loop + 1))
        log.info("Removing logs...")
        for inst in ALL_REPLICAS:
            inst.deleteAllLogs()

        # Fire off 4 threads to apply the load
        log.info("Start adding/deleting: " + getDateTime())
        startTime = time.time()
        add_del_m1 = AddDelUsers(topology.master1)
        add_del_m1.start()
        add_del_m2 = AddDelUsers(topology.master2)
        add_del_m2.start()
        add_del_m3 = AddDelUsers(topology.master3)
        add_del_m3.start()
        add_del_m4 = AddDelUsers(topology.master4)
        add_del_m4.start()

        # Wait for threads to finish sending their updates
        add_del_m1.join()
        add_del_m2.join()
        add_del_m3.join()
        add_del_m4.join()
        log.info("Finished adding/deleting entries: " + getDateTime())

        #
        # Loop checking for error 32's, and for convergence to complete
        #
        log.info("Waiting for replication to converge...")
        while True:
            # First check for error 32's
            for inst in ALL_REPLICAS:
                if inst.searchAccessLog(" err=32 "):
                    log.fatal('An add was missed on: ' + inst.serverid)
                    assert False

            # Next check to see if the last update is in the access log
            converged = True
            for inst in ALL_REPLICAS:
                if not inst.searchAccessLog(LAST_DN_M1) or \
                   not inst.searchAccessLog(LAST_DN_M2) or \
                   not inst.searchAccessLog(LAST_DN_M3) or \
                   not inst.searchAccessLog(LAST_DN_M4):
                    converged = False
                    break

            if converged:
                elapsed_tm = int(time.time() - startTime)
                convtime = str(datetime.timedelta(seconds=elapsed_tm))
                log.info('Replication converged at: ' + getDateTime() +
                         ' - Elapsed Time:  ' + convtime)
                break
            else:
                # Check if replication is idle
                replicas = [topology.master1, topology.master2,
                            topology.master3, topology.master4,
                            topology.hub1, topology.hub2]
                if ReplTools.replIdle(replicas, DEFAULT_SUFFIX):
                    # Replication is idle - wait 30 secs for access log buffer
                    time.sleep(30)

                    # Now check the access log again...
                    converged = True
                    for inst in ALL_REPLICAS:
                        if not inst.searchAccessLog(LAST_DN_M1) or \
                           not inst.searchAccessLog(LAST_DN_M2) or \
                           not inst.searchAccessLog(LAST_DN_M3) or \
                           not inst.searchAccessLog(LAST_DN_M4):
                            converged = False
                            break

                    if converged:
                        elapsed_tm = int(time.time() - startTime)
                        convtime = str(datetime.timedelta(seconds=elapsed_tm))
                        log.info('Replication converged at: ' + getDateTime() +
                                 ' - Elapsed Time:  ' + convtime)
                        break
                    else:
                        log.fatal('Stopping replication check: ' +
                                  getDateTime())
                        log.fatal('Failure: Replication is complete, but we ' +
                                  'never converged.')
                        assert False

            # Sleep a bit before the next pass
            time.sleep(3)

        #
        # Finally check the CSN's
        #
        log.info("Check the CSN's...")
        if not ReplTools.checkCSNs(ALL_REPLICAS):
            assert False
        log.info("All CSN's present and accounted for.")

        #
        # Print the convergence report
        #
        log.info('Measuring convergence...')
        measureConvergence(topology)

        #
        # Test complete
        #
        log.info('No lingering entries.')
        log.info('Pass %d complete.' % (loop + 1))
        elapsed_tm = int(time.time() - TEST_START)
        convtime = str(datetime.timedelta(seconds=elapsed_tm))
        log.info('Entire test ran for: ' + convtime)

        loop += 1

    log.info('Test PASSED')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
