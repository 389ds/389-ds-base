# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
'''
Created on Nov 7, 2013

@author: tbordaz
'''
import os
import sys
import time
import ldap
import logging
import pytest
import tarfile
import stat
import shutil
from random import randint
from lib389 import DirSrv, Entry, tools
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *


logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

#
# important part. We can deploy Master1 and Master2 on different versions
#
installation1_prefix = None
installation2_prefix = None

TEST_REPL_DN = "cn=test_repl, %s" % SUFFIX
OC_NAME = 'OCticket47988'
MUST = "(postalAddress $ postalCode)"
MAY  = "(member $ street)"

OTHER_NAME = 'other_entry'
MAX_OTHERS = 10

BIND_NAME  = 'bind_entry'
BIND_DN    = 'cn=%s, %s' % (BIND_NAME, SUFFIX)
BIND_PW    = 'password'

ENTRY_NAME = 'test_entry'
ENTRY_DN   = 'cn=%s, %s' % (ENTRY_NAME, SUFFIX)
ENTRY_OC   = "top person %s" % OC_NAME

def _oc_definition(oid_ext, name, must=None, may=None):
    oid  = "1.2.3.4.5.6.7.8.9.10.%d" % oid_ext
    desc = 'To test ticket 47490'
    sup  = 'person'
    if not must:
        must = MUST
    if not may:
        may = MAY

    new_oc = "( %s  NAME '%s' DESC '%s' SUP %s AUXILIARY MUST %s MAY %s )" % (oid, name, desc, sup, must, may)
    return new_oc
class TopologyMaster1Master2(object):
    def __init__(self, master1, master2):
        master1.open()
        self.master1 = master1

        master2.open()
        self.master2 = master2


@pytest.fixture(scope="module")
def topology(request):
    '''
        This fixture is used to create a replicated topology for the 'module'.
        The replicated topology is MASTER1 <-> Master2.
    '''
    global installation1_prefix
    global installation2_prefix

    #os.environ['USE_VALGRIND'] = '1'

    # allocate master1 on a given deployement
    master1 = DirSrv(verbose=False)
    if installation1_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation1_prefix

    # Args for the master1 instance
    args_instance[SER_HOST] = HOST_MASTER_1
    args_instance[SER_PORT] = PORT_MASTER_1
    args_instance[SER_SERVERID_PROP] = SERVERID_MASTER_1
    args_master = args_instance.copy()
    master1.allocate(args_master)

    # allocate master1 on a given deployement
    master2 = DirSrv(verbose=False)
    if installation2_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation2_prefix

    # Args for the consumer instance
    args_instance[SER_HOST] = HOST_MASTER_2
    args_instance[SER_PORT] = PORT_MASTER_2
    args_instance[SER_SERVERID_PROP] = SERVERID_MASTER_2
    args_master = args_instance.copy()
    master2.allocate(args_master)

    # Get the status of the instance and restart it if it exists
    instance_master1 = master1.exists()
    instance_master2 = master2.exists()

    # Remove all the instances
    if instance_master1:
        master1.delete()
    if instance_master2:
        master2.delete()

    # Create the instances
    master1.create()
    master1.open()
    master2.create()
    master2.open()

    def fin():
        master1.delete()
        master2.delete()
    request.addfinalizer(fin)

    #
    # Now prepare the Master-Consumer topology
    #
    # First Enable replication
    master1.replica.enableReplication(suffix=SUFFIX, role=REPLICAROLE_MASTER, replicaId=REPLICAID_MASTER_1)
    master2.replica.enableReplication(suffix=SUFFIX, role=REPLICAROLE_MASTER, replicaId=REPLICAID_MASTER_2)

    # Initialize the supplier->consumer

    properties = {RA_NAME:      r'meTo_$host:$port',
                  RA_BINDDN:    defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW:    defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD:    defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    repl_agreement = master1.agreement.create(suffix=SUFFIX, host=master2.host, port=master2.port, properties=properties)

    if not repl_agreement:
        log.fatal("Fail to create a replica agreement")
        sys.exit(1)

    log.debug("%s created" % repl_agreement)

    properties = {RA_NAME:      r'meTo_$host:$port',
                  RA_BINDDN:    defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW:    defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD:    defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    master2.agreement.create(suffix=SUFFIX, host=master1.host, port=master1.port, properties=properties)

    master1.agreement.init(SUFFIX, HOST_MASTER_2, PORT_MASTER_2)
    master1.waitForReplInit(repl_agreement)

    # Check replication is working fine
    if master1.testReplication(DEFAULT_SUFFIX, master2):
        log.info('Replication is working.')
    else:
        log.fatal('Replication is not working.')
        assert False

    # Here we have two instances master and consumer
    return TopologyMaster1Master2(master1, master2)


def _header(topology, label):
    topology.master1.log.info("\n\n###############################################")
    topology.master1.log.info("#######")
    topology.master1.log.info("####### %s" % label)
    topology.master1.log.info("#######")
    topology.master1.log.info("###################################################")


def _install_schema(server, tarFile):
    server.stop(timeout=10)

    tmpSchema = '/tmp/schema_47988'
    if not os.path.isdir(tmpSchema):
        os.mkdir(tmpSchema)

    for the_file in os.listdir(tmpSchema):
        file_path = os.path.join(tmpSchema, the_file)
        if os.path.isfile(file_path):
            os.unlink(file_path)

    os.chdir(tmpSchema)
    tar = tarfile.open(tarFile, 'r:gz')
    for member in tar.getmembers():
        tar.extract(member.name)

    tar.close()

    st = os.stat(server.schemadir)
    os.chmod(server.schemadir, st.st_mode | stat.S_IWUSR | stat.S_IXUSR | stat.S_IRUSR)
    for the_file in os.listdir(tmpSchema):
        schemaFile = os.path.join(server.schemadir, the_file)
        if os.path.isfile(schemaFile):
            if the_file.startswith('99user.ldif'):
                # only replace 99user.ldif, the other standard definition are kept
                os.chmod(schemaFile, stat.S_IWUSR | stat.S_IRUSR)
                server.log.info("replace %s" % schemaFile)
                shutil.copy(the_file, schemaFile)

        else:
            server.log.info("add %s" % schemaFile)
            shutil.copy(the_file, schemaFile)
            os.chmod(schemaFile, stat.S_IRUSR | stat.S_IRGRP)
    os.chmod(server.schemadir, st.st_mode | stat.S_IRUSR | stat.S_IRGRP)


def test_ticket47988_init(topology):
    """
        It adds
           - Objectclass with MAY 'member'
           - an entry ('bind_entry') with which we bind to test the 'SELFDN' operation
        It deletes the anonymous aci

    """

    _header(topology, 'test_ticket47988_init')

    # enable acl error logging
    mod = [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', str(8192))]  # REPL
    topology.master1.modify_s(DN_CONFIG, mod)
    topology.master2.modify_s(DN_CONFIG, mod)

    mod = [(ldap.MOD_REPLACE, 'nsslapd-accesslog-level', str(260))]  # Internal op
    topology.master1.modify_s(DN_CONFIG, mod)
    topology.master2.modify_s(DN_CONFIG, mod)

    # add dummy entries
    for cpt in range(MAX_OTHERS):
        name = "%s%d" % (OTHER_NAME, cpt)
        topology.master1.add_s(Entry(("cn=%s,%s" % (name, SUFFIX), {
                                            'objectclass': "top person".split(),
                                            'sn': name,
                                            'cn': name})))

    # check that entry 0 is replicated before
    loop = 0
    entryDN = "cn=%s0,%s" % (OTHER_NAME, SUFFIX)
    while loop <= 10:
        try:
            ent = topology.master2.getEntry(entryDN, ldap.SCOPE_BASE, "(objectclass=*)", ['telephonenumber'])
            break
        except ldap.NO_SUCH_OBJECT:
            time.sleep(1)
        loop += 1
    assert (loop <= 10)

    topology.master1.stop(timeout=10)
    topology.master2.stop(timeout=10)

    #install the specific schema M1: ipa3.3, M2: ipa4.1
    schema_file = os.path.join(topology.master1.getDir(__file__, DATA_DIR), "ticket47988/schema_ipa3.3.tar.gz")
    _install_schema(topology.master1, schema_file)
    schema_file = os.path.join(topology.master1.getDir(__file__, DATA_DIR), "ticket47988/schema_ipa4.1.tar.gz")
    _install_schema(topology.master2, schema_file)

    topology.master1.start(timeout=10)
    topology.master2.start(timeout=10)


def _do_update_schema(server, range=3999):
    '''
    Update the schema of the M2 (IPA4.1). to generate a nsSchemaCSN
    '''
    postfix = str(randint(range, range + 1000))
    OID = '2.16.840.1.113730.3.8.12.%s' % postfix
    NAME = 'thierry%s' % postfix
    value = '( %s NAME \'%s\' DESC \'Override for Group Attributes\' STRUCTURAL MUST ( cn ) MAY sn X-ORIGIN ( \'IPA v4.1.2\' \'user defined\' ) )' % (OID, NAME)
    mod = [(ldap.MOD_ADD, 'objectclasses', value)]
    server.modify_s('cn=schema', mod)


def _do_update_entry(supplier=None, consumer=None, attempts=10):
    '''
    This is doing an update on M2 (IPA4.1) and checks the update has been
    propagated to M1 (IPA3.3)
    '''
    assert(supplier)
    assert(consumer)
    entryDN = "cn=%s0,%s" % (OTHER_NAME, SUFFIX)
    value = str(randint(100, 200))
    mod = [(ldap.MOD_REPLACE, 'telephonenumber', value)]
    supplier.modify_s(entryDN, mod)

    loop = 0
    while loop <= attempts:
        ent = consumer.getEntry(entryDN, ldap.SCOPE_BASE, "(objectclass=*)", ['telephonenumber'])
        read_val = ent.telephonenumber or "0"
        if read_val == value:
            break
        # the expected value is not yet replicated. try again
        time.sleep(5)
        loop += 1
        supplier.log.debug("test_do_update: receive %s (expected %s)" % (read_val, value))
    assert (loop <= attempts)


def _pause_M2_to_M1(topology):
    topology.master1.log.info("\n\n######################### Pause RA M2->M1 ######################\n")
    ents = topology.master2.agreement.list(suffix=SUFFIX)
    assert len(ents) == 1
    topology.master2.agreement.pause(ents[0].dn)


def _resume_M1_to_M2(topology):
    topology.master1.log.info("\n\n######################### resume RA M1->M2 ######################\n")
    ents = topology.master1.agreement.list(suffix=SUFFIX)
    assert len(ents) == 1
    topology.master1.agreement.resume(ents[0].dn)


def _pause_M1_to_M2(topology):
    topology.master1.log.info("\n\n######################### Pause RA M1->M2 ######################\n")
    ents = topology.master1.agreement.list(suffix=SUFFIX)
    assert len(ents) == 1
    topology.master1.agreement.pause(ents[0].dn)


def _resume_M2_to_M1(topology):
    topology.master1.log.info("\n\n######################### resume RA M2->M1 ######################\n")
    ents = topology.master2.agreement.list(suffix=SUFFIX)
    assert len(ents) == 1
    topology.master2.agreement.resume(ents[0].dn)


def test_ticket47988_1(topology):
    '''
    Check that replication is working and pause replication M2->M1
    '''
    _header(topology, 'test_ticket47988_1')

    topology.master1.log.debug("\n\nCheck that replication is working and pause replication M2->M1\n")
    _do_update_entry(supplier=topology.master2, consumer=topology.master1, attempts=5)
    _pause_M2_to_M1(topology)


def test_ticket47988_2(topology):
    '''
    Update M1 schema and trigger update M1->M2
    So M1 should learn new/extended definitions that are in M2 schema
    '''
    _header(topology, 'test_ticket47988_2')

    topology.master1.log.debug("\n\nUpdate M1 schema and an entry on M1\n")
    master1_schema_csn = topology.master1.schema.get_schema_csn()
    master2_schema_csn = topology.master2.schema.get_schema_csn()
    topology.master1.log.debug("\nBefore updating the schema on M1\n")
    topology.master1.log.debug("Master1 nsschemaCSN: %s" % master1_schema_csn)
    topology.master1.log.debug("Master2 nsschemaCSN: %s" % master2_schema_csn)

    # Here M1 should no, should check M2 schema and learn
    _do_update_schema(topology.master1)
    master1_schema_csn = topology.master1.schema.get_schema_csn()
    master2_schema_csn = topology.master2.schema.get_schema_csn()
    topology.master1.log.debug("\nAfter updating the schema on M1\n")
    topology.master1.log.debug("Master1 nsschemaCSN: %s" % master1_schema_csn)
    topology.master1.log.debug("Master2 nsschemaCSN: %s" % master2_schema_csn)
    assert (master1_schema_csn)

    # to avoid linger effect where a replication session is reused without checking the schema
    _pause_M1_to_M2(topology)
    _resume_M1_to_M2(topology)

    #topo.master1.log.debug("\n\nSleep.... attach the debugger dse_modify")
    #time.sleep(60)
    _do_update_entry(supplier=topology.master1, consumer=topology.master2, attempts=15)
    master1_schema_csn = topology.master1.schema.get_schema_csn()
    master2_schema_csn = topology.master2.schema.get_schema_csn()
    topology.master1.log.debug("\nAfter a full replication session\n")
    topology.master1.log.debug("Master1 nsschemaCSN: %s" % master1_schema_csn)
    topology.master1.log.debug("Master2 nsschemaCSN: %s" % master2_schema_csn)
    assert (master1_schema_csn)
    assert (master2_schema_csn)


def test_ticket47988_3(topology):
    '''
    Resume replication M2->M1 and check replication is still working
    '''
    _header(topology, 'test_ticket47988_3')

    _resume_M2_to_M1(topology)
    _do_update_entry(supplier=topology.master1, consumer=topology.master2, attempts=5)
    _do_update_entry(supplier=topology.master2, consumer=topology.master1, attempts=5)


def test_ticket47988_4(topology):
    '''
    Check schemaCSN is identical on both server
    And save the nsschemaCSN to later check they do not change unexpectedly
    '''
    _header(topology, 'test_ticket47988_4')

    master1_schema_csn = topology.master1.schema.get_schema_csn()
    master2_schema_csn = topology.master2.schema.get_schema_csn()
    topology.master1.log.debug("\n\nMaster1 nsschemaCSN: %s" % master1_schema_csn)
    topology.master1.log.debug("\n\nMaster2 nsschemaCSN: %s" % master2_schema_csn)
    assert (master1_schema_csn)
    assert (master2_schema_csn)
    assert (master1_schema_csn == master2_schema_csn)

    topology.master1.saved_schema_csn = master1_schema_csn
    topology.master2.saved_schema_csn = master2_schema_csn


def test_ticket47988_5(topology):
    '''
    Check schemaCSN  do not change unexpectedly
    '''
    _header(topology, 'test_ticket47988_5')

    _do_update_entry(supplier=topology.master1, consumer=topology.master2, attempts=5)
    _do_update_entry(supplier=topology.master2, consumer=topology.master1, attempts=5)
    master1_schema_csn = topology.master1.schema.get_schema_csn()
    master2_schema_csn = topology.master2.schema.get_schema_csn()
    topology.master1.log.debug("\n\nMaster1 nsschemaCSN: %s" % master1_schema_csn)
    topology.master1.log.debug("\n\nMaster2 nsschemaCSN: %s" % master2_schema_csn)
    assert (master1_schema_csn)
    assert (master2_schema_csn)
    assert (master1_schema_csn == master2_schema_csn)

    assert (topology.master1.saved_schema_csn == master1_schema_csn)
    assert (topology.master2.saved_schema_csn == master2_schema_csn)


def test_ticket47988_6(topology):
    '''
    Update M1 schema and trigger update M2->M1
    So M2 should learn new/extended definitions that are in M1 schema
    '''

    _header(topology, 'test_ticket47988_6')

    topology.master1.log.debug("\n\nUpdate M1 schema and an entry on M1\n")
    master1_schema_csn = topology.master1.schema.get_schema_csn()
    master2_schema_csn = topology.master2.schema.get_schema_csn()
    topology.master1.log.debug("\nBefore updating the schema on M1\n")
    topology.master1.log.debug("Master1 nsschemaCSN: %s" % master1_schema_csn)
    topology.master1.log.debug("Master2 nsschemaCSN: %s" % master2_schema_csn)

    # Here M1 should no, should check M2 schema and learn
    _do_update_schema(topology.master1, range=5999)
    master1_schema_csn = topology.master1.schema.get_schema_csn()
    master2_schema_csn = topology.master2.schema.get_schema_csn()
    topology.master1.log.debug("\nAfter updating the schema on M1\n")
    topology.master1.log.debug("Master1 nsschemaCSN: %s" % master1_schema_csn)
    topology.master1.log.debug("Master2 nsschemaCSN: %s" % master2_schema_csn)
    assert (master1_schema_csn)

    # to avoid linger effect where a replication session is reused without checking the schema
    _pause_M1_to_M2(topology)
    _resume_M1_to_M2(topology)

    #topo.master1.log.debug("\n\nSleep.... attach the debugger dse_modify")
    #time.sleep(60)
    _do_update_entry(supplier=topology.master2, consumer=topology.master1, attempts=15)
    master1_schema_csn = topology.master1.schema.get_schema_csn()
    master2_schema_csn = topology.master2.schema.get_schema_csn()
    topology.master1.log.debug("\nAfter a full replication session\n")
    topology.master1.log.debug("Master1 nsschemaCSN: %s" % master1_schema_csn)
    topology.master1.log.debug("Master2 nsschemaCSN: %s" % master2_schema_csn)
    assert (master1_schema_csn)
    assert (master2_schema_csn)


def test_ticket47988_final(topology):
    log.info('Testcase PASSED')


def run_isolated():
    '''
        run_isolated is used to run these test cases independently of a test scheduler (xunit, py.test..)
        To run isolated without py.test, you need to
            - edit this file and comment '@pytest.fixture' line before 'topology' function.
            - set the installation prefix
            - run this program
    '''
    global installation1_prefix
    global installation2_prefix
    installation1_prefix = None
    installation2_prefix = None

    topo = topology(True)
    test_ticket47988_init(topo)
    test_ticket47988_1(topo)
    test_ticket47988_2(topo)
    test_ticket47988_3(topo)
    test_ticket47988_4(topo)
    test_ticket47988_5(topo)
    test_ticket47988_6(topo)
    test_ticket47988_final(topo)

if __name__ == '__main__':
    run_isolated()

