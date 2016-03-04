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
import shlex
import subprocess
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

MYDN = 'uid=tuser1M,dc=example,dc=com'
MYLDIF = 'ticket48326.ldif'

class TopologyReplication(object):
    def __init__(self, master1, master2):
        master1.open()
        self.master1 = master1
        master2.open()
        self.master2 = master2


@pytest.fixture(scope="module")
def topology(request):
    global installation1_prefix
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

    # Check replication is working...
    if master1.testReplication(DEFAULT_SUFFIX, master2):
        log.info('Replication is working.')
    else:
        log.fatal('Replication is not working.')
        assert False

    # Delete each instance in the end
    def fin():
        master1.delete()
        master2.delete()
    request.addfinalizer(fin)

    # Clear out the tmp dir
    master1.clearTmpDir(__file__)

    return TopologyReplication(master1, master2)


@pytest.fixture(scope="module")


def add_entry(topology, server, serverPort, expectToFail):
    """
    Adding 1Mentry to the given server
    Check the add result based upon the expectToFail info.
    """
    if expectToFail:
        log.info("Adding 1M entry to %s expecting to fail." % server)
    else:
        log.info("Adding 1M entry to %s expecting to succeed." % server)

    data_dir_path = topology.master1.getDir(__file__, DATA_DIR)
    ldif_file = data_dir_path + MYLDIF

    #strcmdline = '/usr/bin/ldapmodify -x -h localhost -p' + str(serverPort) + '-D' + DN_DM + '-w' + PW_DM + '-af' + ldif_file
    #cmdline = shlex.split(strcmdline)
    cmdline = ['/usr/bin/ldapmodify', '-x', '-h', 'localhost', '-p', str(serverPort),
               '-D', DN_DM, '-w', PW_DM, '-af', ldif_file]
    log.info("Running cmdline (%s): %s" % (server, cmdline))

    try:
        proc = subprocess.Popen(cmdline, stderr=subprocess.PIPE)
    except Exception as e:
        log.info("%s caught in exception: %s" % (cmdline, e))
        assert False

    Found = False
    Expected = "ldap_result: Can't contact LDAP server"
    while True:
        l = proc.stderr.readline()
        if l == "":
            break
        if Expected in l:
            Found = True
            break

    if expectToFail:
        if Found:
            log.info("Adding 1M entry to %s failed as expected: %s" % (server, l))
        else:
            log.fatal("Expected error message %s was not returned: %s" % Expected)
            assert False
    else:
        if Found:
            log.fatal("%s failed although expecting to succeed: %s" % (cmdline, l))
            assert False
        else:
            log.info("Adding 1M entry to %s succeeded as expected" % server)


def test_ticket48326(topology):
    """
    maxbersize is ignored in the replicated operations.
    [settings]
    master1 has default size maxbersize (2MB).
    master2 has much saller size maxbersize (20KB).
    [test case]
    Adding an entry which size is larger than 20KB to master2 fails.
    But adding an entry which size is larger than 20KB and less than 2MB to master1 succeeds
    and the entry is successfully replicated to master2.
    """
    log.info("Ticket 48326 - it could be nice to have nsslapd-maxbersize default to bigger than 2Mb")
    log.info("Set nsslapd-maxbersize: 20K to master2")
    try:
        topology.master2.modify_s("cn=config", [(ldap.MOD_REPLACE, 'nsslapd-maxbersize', '20480')])
    except ldap.LDAPError as e:
        log.error('Failed to set nsslapd-maxbersize == 20480: error ' + e.message['desc'])
        assert False

    add_entry(topology, "master2", PORT_MASTER_2, True)

    add_entry(topology, "master1", PORT_MASTER_1, False)

    time.sleep(1)
   
    log.info('Searching for %s on master2...', MYDN)
    try:
        entries = topology.master2.search_s(MYDN, ldap.SCOPE_BASE, '(objectclass=*)')
        if not entries:
            log.fatal('Entry %s failed to repliate to master2.' % MYDN)
            assert False
        else:
            log.info('SUCCESS: Entry %s is successfully replicated to master2.' % MYDN)
    except ldap.LDAPError as e:
        log.fatal('Search failed: ' + e.message['desc'])
        assert False


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode

    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
