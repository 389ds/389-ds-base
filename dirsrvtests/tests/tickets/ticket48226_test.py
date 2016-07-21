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
from lib389 import DirSrv, Entry, tools, tasks
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from lib389.utils import *

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

installation1_prefix = None


class TopologyReplication(object):
    def __init__(self, master1, master2):
        master1.open()
        self.master1 = master1
        master2.open()
        self.master2 = master2


@pytest.fixture(scope="module")
def topology(request):
    global installation1_prefix
    os.environ['USE_VALGRIND'] = '1'
    if installation1_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation1_prefix

    # Creating master 1...
    master1 = DirSrv(verbose=False)
    if installation1_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation1_prefix
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
    master2 = DirSrv(verbose=False)
    if installation1_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation1_prefix
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

    # Allow the replicas to get situated with the new agreements...
    time.sleep(5)

    #
    # Initialize all the agreements
    #
    master1.agreement.init(SUFFIX, HOST_MASTER_2, PORT_MASTER_2)
    master1.waitForReplInit(m1_m2_agmt)

    def fin():
        master1.delete()
        master2.delete()
        sbin_dir = get_sbin_dir(prefix=master2.prefix)
        valgrind_disable(sbin_dir)
    request.addfinalizer(fin)

    # Check replication is working...
    if master1.testReplication(DEFAULT_SUFFIX, master2):
        log.info('Replication is working.')
    else:
        log.fatal('Replication is not working.')
        assert False

    return TopologyReplication(master1, master2)


def test_ticket48226_set_purgedelay(topology):
    args = {REPLICA_PURGE_DELAY: '5',
            REPLICA_PURGE_INTERVAL: '5'}
    try:
        topology.master1.replica.setProperties(DEFAULT_SUFFIX, None, None, args)
    except:
        log.fatal('Failed to configure replica')
        assert False
    try:
        topology.master2.replica.setProperties(DEFAULT_SUFFIX, None, None, args)
    except:
        log.fatal('Failed to configure replica')
        assert False
    topology.master1.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE, 'nsslapd-auditlog-logging-enabled', 'on')])
    topology.master2.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE, 'nsslapd-auditlog-logging-enabled', 'on')])
    topology.master1.restart(30)
    topology.master2.restart(30)


def test_ticket48226_1(topology):
    name = 'test_entry'
    dn = "cn=%s,%s" % (name, SUFFIX)

    topology.master1.add_s(Entry((dn, {'objectclass': "top person".split(),
                                        'sn': name,
                                        'cn': name})))

    # First do an update that is replicated
    mods = [(ldap.MOD_ADD, 'description', '5')]
    topology.master1.modify_s(dn, mods)

    nbtry = 0
    while (nbtry <= 10):
        try:
            ent = topology.master2.getEntry(dn, ldap.SCOPE_BASE, "(objectclass=*)", ['description'])
            if ent.hasAttr('description') and ent.getValue('description') == '5':
                break
        except ldap.NO_SUCH_OBJECT:
            pass
        nbtry = nbtry + 1
        time.sleep(1)
    assert nbtry <= 10

    # Stop M2 so that it will not receive the next update
    topology.master2.stop(10)

    # ADD a new value that is not replicated
    mods = [(ldap.MOD_DELETE, 'description', '5')]
    topology.master1.modify_s(dn, mods)

    # Stop M1 so that it will keep del '5' that is unknown from master2
    topology.master1.stop(10)

    # Get the sbin directory so we know where to replace 'ns-slapd'
    sbin_dir = get_sbin_dir(prefix=topology.master2.prefix)

    # Enable valgrind
    valgrind_enable(sbin_dir)

    # start M2 to do the next updates
    topology.master2.start(60)

    # ADD 'description' by '5'
    mods = [(ldap.MOD_DELETE, 'description', '5')]
    topology.master2.modify_s(dn, mods)

    # DEL 'description' by '5'
    mods = [(ldap.MOD_ADD, 'description', '5')]
    topology.master2.modify_s(dn, mods)

    # sleep of purge delay so that the next update will purge the CSN_7
    time.sleep(6)

    # ADD 'description' by '6' that purge the state info
    mods = [(ldap.MOD_ADD, 'description', '6')]
    topology.master2.modify_s(dn, mods)

    # Restart master1
    #topology.master1.start(30)

    results_file = valgrind_get_results_file(topology.master2)

    # Stop master2
    topology.master2.stop(30)

    # Check for leak
    if valgrind_check_file(results_file, VALGRIND_LEAK_STR, 'csnset_dup'):
        log.info('Valgrind reported leak in csnset_dup!')
        assert False
    else:
        log.info('Valgrind is happy!')

    # Check for invalid read/write
    if valgrind_check_file(results_file, VALGRIND_INVALID_STR, 'csnset_dup'):
        log.info('Valgrind reported invalid!')
        assert False
    else:
        log.info('Valgrind is happy!')

    # Check for invalid read/write
    if valgrind_check_file(results_file, VALGRIND_INVALID_STR, 'csnset_free'):
        log.info('Valgrind reported invalid!')
        assert False
    else:
        log.info('Valgrind is happy!')

    log.info('Testcase PASSED')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

