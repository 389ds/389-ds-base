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

m1_m2_agmt = None


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
    properties = {RA_NAME:      r'meTo_%s:%s' % (master2.host, master2.port),
                  RA_BINDDN:    defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW:    defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD:    defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    global m1_m2_agmt
    m1_m2_agmt = master1.agreement.create(suffix=SUFFIX, host=master2.host, port=master2.port, properties=properties)
    if not m1_m2_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m1_m2_agmt)

    # Creating agreement from master 2 to master 1
    properties = {RA_NAME:      r'meTo_%s:%s' % (master1.host, master1.port),
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

    return TopologyReplication(master1, master2)


@pytest.fixture(scope="module")
def add_ou_entry(server, idx, myparent):
    name = 'OU%d' % idx
    dn = 'ou=%s,%s' % (name, myparent)
    server.add_s(Entry((dn, {'objectclass': ['top', 'organizationalunit'],
                             'ou': name})))
    time.sleep(1)


def add_user_entry(server, idx, myparent):
    name = 'tuser%d' % idx
    dn = 'uid=%s,%s' % (name, myparent)
    server.add_s(Entry((dn, {'objectclass': ['top', 'person', 'organizationalPerson', 'inetorgperson'],
                             'givenname': 'test',
                             'sn': 'user%d' % idx,
                             'cn': 'Test User%d' % idx,
                             'userpassword': 'password'})))
    time.sleep(1)


def del_user_entry(server, idx, myparent):
    name = 'tuser%d' % idx
    dn = 'uid=%s,%s' % (name, myparent)
    server.delete_s(dn)
    time.sleep(1)


def add_ldapsubentry(server, myparent):
    name = 'nsPwPolicyContainer'
    container = 'cn=%s,%s' % (name, myparent)
    server.add_s(Entry((container, {'objectclass': ['top', 'nsContainer'],
                                    'cn': '%s' % name})))

    name = 'nsPwPolicyEntry'
    pwpentry = 'cn=%s,%s' % (name, myparent)
    pwpdn = 'cn="%s",%s' % (pwpentry, container)
    server.add_s(Entry((pwpdn, {'objectclass': ['top', 'ldapsubentry', 'passwordpolicy'],
                                'passwordStorageScheme': 'ssha',
                                'passwordCheckSyntax': 'on',
                                'passwordInHistory': '6',
                                'passwordChange': 'on',
                                'passwordMinAge': '0',
                                'passwordExp': 'off',
                                'passwordMustChange': 'off',
                                'cn': '%s' % pwpentry})))

    name = 'nsPwTemplateEntry'
    tmplentry = 'cn=%s,%s' % (name, myparent)
    tmpldn = 'cn="%s",%s' % (tmplentry, container)
    server.add_s(Entry((tmpldn, {'objectclass': ['top', 'ldapsubentry', 'costemplate', 'extensibleObject'],
                                'cosPriority': '1',
                                'cn': '%s' % tmplentry})))

    name = 'nsPwPolicy_CoS'
    cos = 'cn=%s,%s' % (name, myparent)
    server.add_s(Entry((cos, {'objectclass': ['top', 'ldapsubentry', 'cosPointerDefinition', 'cosSuperDefinition'],
                              'costemplatedn': '%s' % tmpldn,
                              'cosAttribute': 'pwdpolicysubentry default operational-default',
                              'cn': '%s' % name})))
    time.sleep(1)


def test_ticket48755(topology):
    log.info("Ticket 48755 - moving an entry could make the online init fail")

    M1 = topology.master1
    M2 = topology.master2

    log.info("Generating DIT_0")
    idx = 0
    add_ou_entry(M1, idx, DEFAULT_SUFFIX)

    ou0 = 'ou=OU%d' % idx
    parent0 = '%s,%s' % (ou0, DEFAULT_SUFFIX)
    add_ou_entry(M1, idx, parent0)

    add_ldapsubentry(M1, parent0)

    parent00 = 'ou=OU%d,%s' % (idx, parent0)
    for idx in range(0, 9):
        add_user_entry(M1, idx, parent00)
        if idx % 2 == 0:
            log.info("Turning tuser%d into a tombstone entry" % idx)
            del_user_entry(M1, idx, parent00)

    log.info('%s => %s => %s => 10 USERS' % (DEFAULT_SUFFIX, parent0, parent00))

    log.info("Generating DIT_1")
    idx = 1
    add_ou_entry(M1, idx, DEFAULT_SUFFIX)

    parent1 = 'ou=OU%d,%s' % (idx, DEFAULT_SUFFIX)
    add_ou_entry(M1, idx, parent1)

    add_ldapsubentry(M1, parent1)

    log.info("Moving %s to DIT_1" % parent00)
    M1.rename_s(parent00, ou0, newsuperior=parent1, delold=1)
    time.sleep(1)

    log.info("Moving %s to DIT_1" % parent0)
    parent01 = '%s,%s' % (ou0, parent1)
    M1.rename_s(parent0, ou0, newsuperior=parent01, delold=1)
    time.sleep(1)

    parent001 = '%s,%s' % (ou0, parent01)
    log.info("Moving USERS to %s" % parent0)
    for idx in range(0, 9):
        if idx % 2 == 1:
            name = 'tuser%d' % idx
            rdn = 'uid=%s' % name
            dn = 'uid=%s,%s' % (name, parent01)
            M1.rename_s(dn, rdn, newsuperior=parent001, delold=1)
            time.sleep(1)

    log.info('%s => %s => %s => %s => 10 USERS' % (DEFAULT_SUFFIX, parent1, parent01, parent001))

    log.info("Run Consumer Initialization.")
    global m1_m2_agmt
    M1.startReplication_async(m1_m2_agmt)
    M1.waitForReplInit(m1_m2_agmt)
    time.sleep(2)

    m1entries = M1.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE,
                            '(|(objectclass=ldapsubentry)(objectclass=nstombstone)(nsuniqueid=*))')
    m2entries = M2.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE,
                            '(|(objectclass=ldapsubentry)(objectclass=nstombstone)(nsuniqueid=*))')

    log.info("m1entry count - %d", len(m1entries))
    log.info("m2entry count - %d", len(m2entries))

    assert len(m1entries) == len(m2entries)
    log.info('PASSED')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
