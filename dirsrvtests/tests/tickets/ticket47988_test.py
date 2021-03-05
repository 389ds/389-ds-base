# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
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
import logging
import shutil
import stat
import tarfile
import time
from random import randint

import ldap
import pytest
from lib389 import Entry
from lib389._constants import *
from lib389.topologies import topology_m2
from lib389.utils import *

pytestmark = pytest.mark.tier2

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

TEST_REPL_DN = "cn=test_repl, %s" % SUFFIX
OC_NAME = 'OCticket47988'
MUST = "(postalAddress $ postalCode)"
MAY = "(member $ street)"

OTHER_NAME = 'other_entry'
MAX_OTHERS = 10

BIND_NAME = 'bind_entry'
BIND_DN = 'cn=%s, %s' % (BIND_NAME, SUFFIX)
BIND_PW = 'password'

ENTRY_NAME = 'test_entry'
ENTRY_DN = 'cn=%s, %s' % (ENTRY_NAME, SUFFIX)
ENTRY_OC = "top person %s" % OC_NAME


def _oc_definition(oid_ext, name, must=None, may=None):
    oid = "1.2.3.4.5.6.7.8.9.10.%d" % oid_ext
    desc = 'To test ticket 47490'
    sup = 'person'
    if not must:
        must = MUST
    if not may:
        may = MAY

    new_oc = "( %s  NAME '%s' DESC '%s' SUP %s AUXILIARY MUST %s MAY %s )" % (oid, name, desc, sup, must, may)
    return new_oc


def _header(topology_m2, label):
    topology_m2.ms["supplier1"].log.info("\n\n###############################################")
    topology_m2.ms["supplier1"].log.info("#######")
    topology_m2.ms["supplier1"].log.info("####### %s" % label)
    topology_m2.ms["supplier1"].log.info("#######")
    topology_m2.ms["supplier1"].log.info("###################################################")


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


def test_ticket47988_init(topology_m2):
    """
        It adds
           - Objectclass with MAY 'member'
           - an entry ('bind_entry') with which we bind to test the 'SELFDN' operation
        It deletes the anonymous aci

    """

    _header(topology_m2, 'test_ticket47988_init')

    # enable acl error logging
    mod = [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', ensure_bytes(str(8192)))]  # REPL
    topology_m2.ms["supplier1"].modify_s(DN_CONFIG, mod)
    topology_m2.ms["supplier2"].modify_s(DN_CONFIG, mod)

    mod = [(ldap.MOD_REPLACE, 'nsslapd-accesslog-level', ensure_bytes(str(260)))]  # Internal op
    topology_m2.ms["supplier1"].modify_s(DN_CONFIG, mod)
    topology_m2.ms["supplier2"].modify_s(DN_CONFIG, mod)

    # add dummy entries
    for cpt in range(MAX_OTHERS):
        name = "%s%d" % (OTHER_NAME, cpt)
        topology_m2.ms["supplier1"].add_s(Entry(("cn=%s,%s" % (name, SUFFIX), {
            'objectclass': "top person".split(),
            'sn': name,
            'cn': name})))

    # check that entry 0 is replicated before
    loop = 0
    entryDN = "cn=%s0,%s" % (OTHER_NAME, SUFFIX)
    while loop <= 10:
        try:
            ent = topology_m2.ms["supplier2"].getEntry(entryDN, ldap.SCOPE_BASE, "(objectclass=*)", ['telephonenumber'])
            break
        except ldap.NO_SUCH_OBJECT:
            time.sleep(1)
        loop += 1
    assert (loop <= 10)

    topology_m2.ms["supplier1"].stop(timeout=10)
    topology_m2.ms["supplier2"].stop(timeout=10)

    # install the specific schema M1: ipa3.3, M2: ipa4.1
    schema_file = os.path.join(topology_m2.ms["supplier1"].getDir(__file__, DATA_DIR), "ticket47988/schema_ipa3.3.tar.gz")
    _install_schema(topology_m2.ms["supplier1"], schema_file)
    schema_file = os.path.join(topology_m2.ms["supplier1"].getDir(__file__, DATA_DIR), "ticket47988/schema_ipa4.1.tar.gz")
    _install_schema(topology_m2.ms["supplier2"], schema_file)

    topology_m2.ms["supplier1"].start(timeout=10)
    topology_m2.ms["supplier2"].start(timeout=10)


def _do_update_schema(server, range=3999):
    '''
    Update the schema of the M2 (IPA4.1). to generate a nsSchemaCSN
    '''
    postfix = str(randint(range, range + 1000))
    OID = '2.16.840.1.113730.3.8.12.%s' % postfix
    NAME = 'thierry%s' % postfix
    value = '( %s NAME \'%s\' DESC \'Override for Group Attributes\' STRUCTURAL MUST ( cn ) MAY sn X-ORIGIN ( \'IPA v4.1.2\' \'user defined\' ) )' % (
    OID, NAME)
    mod = [(ldap.MOD_ADD, 'objectclasses', ensure_bytes(value))]
    server.modify_s('cn=schema', mod)


def _do_update_entry(supplier=None, consumer=None, attempts=10):
    '''
    This is doing an update on M2 (IPA4.1) and checks the update has been
    propagated to M1 (IPA3.3)
    '''
    assert (supplier)
    assert (consumer)
    entryDN = "cn=%s0,%s" % (OTHER_NAME, SUFFIX)
    value = str(randint(100, 200))
    mod = [(ldap.MOD_REPLACE, 'telephonenumber', ensure_bytes(value))]
    supplier.modify_s(entryDN, mod)

    loop = 0
    while loop <= attempts:
        ent = consumer.getEntry(entryDN, ldap.SCOPE_BASE, "(objectclass=*)", ['telephonenumber'])
        read_val = ensure_str(ent.telephonenumber) or "0"
        if read_val == value:
            break
        # the expected value is not yet replicated. try again
        time.sleep(5)
        loop += 1
        supplier.log.debug("test_do_update: receive %s (expected %s)" % (read_val, value))
    assert (loop <= attempts)


def _pause_M2_to_M1(topology_m2):
    topology_m2.ms["supplier1"].log.info("\n\n######################### Pause RA M2->M1 ######################\n")
    ents = topology_m2.ms["supplier2"].agreement.list(suffix=SUFFIX)
    assert len(ents) == 1
    topology_m2.ms["supplier2"].agreement.pause(ents[0].dn)


def _resume_M1_to_M2(topology_m2):
    topology_m2.ms["supplier1"].log.info("\n\n######################### resume RA M1->M2 ######################\n")
    ents = topology_m2.ms["supplier1"].agreement.list(suffix=SUFFIX)
    assert len(ents) == 1
    topology_m2.ms["supplier1"].agreement.resume(ents[0].dn)


def _pause_M1_to_M2(topology_m2):
    topology_m2.ms["supplier1"].log.info("\n\n######################### Pause RA M1->M2 ######################\n")
    ents = topology_m2.ms["supplier1"].agreement.list(suffix=SUFFIX)
    assert len(ents) == 1
    topology_m2.ms["supplier1"].agreement.pause(ents[0].dn)


def _resume_M2_to_M1(topology_m2):
    topology_m2.ms["supplier1"].log.info("\n\n######################### resume RA M2->M1 ######################\n")
    ents = topology_m2.ms["supplier2"].agreement.list(suffix=SUFFIX)
    assert len(ents) == 1
    topology_m2.ms["supplier2"].agreement.resume(ents[0].dn)


def test_ticket47988_1(topology_m2):
    '''
    Check that replication is working and pause replication M2->M1
    '''
    _header(topology_m2, 'test_ticket47988_1')

    topology_m2.ms["supplier1"].log.debug("\n\nCheck that replication is working and pause replication M2->M1\n")
    _do_update_entry(supplier=topology_m2.ms["supplier2"], consumer=topology_m2.ms["supplier1"], attempts=5)
    _pause_M2_to_M1(topology_m2)


def test_ticket47988_2(topology_m2):
    '''
    Update M1 schema and trigger update M1->M2
    So M1 should learn new/extended definitions that are in M2 schema
    '''
    _header(topology_m2, 'test_ticket47988_2')

    topology_m2.ms["supplier1"].log.debug("\n\nUpdate M1 schema and an entry on M1\n")
    supplier1_schema_csn = topology_m2.ms["supplier1"].schema.get_schema_csn()
    supplier2_schema_csn = topology_m2.ms["supplier2"].schema.get_schema_csn()
    topology_m2.ms["supplier1"].log.debug("\nBefore updating the schema on M1\n")
    topology_m2.ms["supplier1"].log.debug("Supplier1 nsschemaCSN: %s" % supplier1_schema_csn)
    topology_m2.ms["supplier1"].log.debug("Supplier2 nsschemaCSN: %s" % supplier2_schema_csn)

    # Here M1 should no, should check M2 schema and learn
    _do_update_schema(topology_m2.ms["supplier1"])
    supplier1_schema_csn = topology_m2.ms["supplier1"].schema.get_schema_csn()
    supplier2_schema_csn = topology_m2.ms["supplier2"].schema.get_schema_csn()
    topology_m2.ms["supplier1"].log.debug("\nAfter updating the schema on M1\n")
    topology_m2.ms["supplier1"].log.debug("Supplier1 nsschemaCSN: %s" % supplier1_schema_csn)
    topology_m2.ms["supplier1"].log.debug("Supplier2 nsschemaCSN: %s" % supplier2_schema_csn)
    assert (supplier1_schema_csn)

    # to avoid linger effect where a replication session is reused without checking the schema
    _pause_M1_to_M2(topology_m2)
    _resume_M1_to_M2(topology_m2)

    # topo.supplier1.log.debug("\n\nSleep.... attach the debugger dse_modify")
    # time.sleep(60)
    _do_update_entry(supplier=topology_m2.ms["supplier1"], consumer=topology_m2.ms["supplier2"], attempts=15)
    supplier1_schema_csn = topology_m2.ms["supplier1"].schema.get_schema_csn()
    supplier2_schema_csn = topology_m2.ms["supplier2"].schema.get_schema_csn()
    topology_m2.ms["supplier1"].log.debug("\nAfter a full replication session\n")
    topology_m2.ms["supplier1"].log.debug("Supplier1 nsschemaCSN: %s" % supplier1_schema_csn)
    topology_m2.ms["supplier1"].log.debug("Supplier2 nsschemaCSN: %s" % supplier2_schema_csn)
    assert (supplier1_schema_csn)
    assert (supplier2_schema_csn)


def test_ticket47988_3(topology_m2):
    '''
    Resume replication M2->M1 and check replication is still working
    '''
    _header(topology_m2, 'test_ticket47988_3')

    _resume_M2_to_M1(topology_m2)
    _do_update_entry(supplier=topology_m2.ms["supplier1"], consumer=topology_m2.ms["supplier2"], attempts=5)
    _do_update_entry(supplier=topology_m2.ms["supplier2"], consumer=topology_m2.ms["supplier1"], attempts=5)


def test_ticket47988_4(topology_m2):
    '''
    Check schemaCSN is identical on both server
    And save the nsschemaCSN to later check they do not change unexpectedly
    '''
    _header(topology_m2, 'test_ticket47988_4')

    supplier1_schema_csn = topology_m2.ms["supplier1"].schema.get_schema_csn()
    supplier2_schema_csn = topology_m2.ms["supplier2"].schema.get_schema_csn()
    topology_m2.ms["supplier1"].log.debug("\n\nSupplier1 nsschemaCSN: %s" % supplier1_schema_csn)
    topology_m2.ms["supplier1"].log.debug("\n\nSupplier2 nsschemaCSN: %s" % supplier2_schema_csn)
    assert (supplier1_schema_csn)
    assert (supplier2_schema_csn)
    assert (supplier1_schema_csn == supplier2_schema_csn)

    topology_m2.ms["supplier1"].saved_schema_csn = supplier1_schema_csn
    topology_m2.ms["supplier2"].saved_schema_csn = supplier2_schema_csn


def test_ticket47988_5(topology_m2):
    '''
    Check schemaCSN  do not change unexpectedly
    '''
    _header(topology_m2, 'test_ticket47988_5')

    _do_update_entry(supplier=topology_m2.ms["supplier1"], consumer=topology_m2.ms["supplier2"], attempts=5)
    _do_update_entry(supplier=topology_m2.ms["supplier2"], consumer=topology_m2.ms["supplier1"], attempts=5)
    supplier1_schema_csn = topology_m2.ms["supplier1"].schema.get_schema_csn()
    supplier2_schema_csn = topology_m2.ms["supplier2"].schema.get_schema_csn()
    topology_m2.ms["supplier1"].log.debug("\n\nSupplier1 nsschemaCSN: %s" % supplier1_schema_csn)
    topology_m2.ms["supplier1"].log.debug("\n\nSupplier2 nsschemaCSN: %s" % supplier2_schema_csn)
    assert (supplier1_schema_csn)
    assert (supplier2_schema_csn)
    assert (supplier1_schema_csn == supplier2_schema_csn)

    assert (topology_m2.ms["supplier1"].saved_schema_csn == supplier1_schema_csn)
    assert (topology_m2.ms["supplier2"].saved_schema_csn == supplier2_schema_csn)


def test_ticket47988_6(topology_m2):
    '''
    Update M1 schema and trigger update M2->M1
    So M2 should learn new/extended definitions that are in M1 schema
    '''

    _header(topology_m2, 'test_ticket47988_6')

    topology_m2.ms["supplier1"].log.debug("\n\nUpdate M1 schema and an entry on M1\n")
    supplier1_schema_csn = topology_m2.ms["supplier1"].schema.get_schema_csn()
    supplier2_schema_csn = topology_m2.ms["supplier2"].schema.get_schema_csn()
    topology_m2.ms["supplier1"].log.debug("\nBefore updating the schema on M1\n")
    topology_m2.ms["supplier1"].log.debug("Supplier1 nsschemaCSN: %s" % supplier1_schema_csn)
    topology_m2.ms["supplier1"].log.debug("Supplier2 nsschemaCSN: %s" % supplier2_schema_csn)

    # Here M1 should no, should check M2 schema and learn
    _do_update_schema(topology_m2.ms["supplier1"], range=5999)
    supplier1_schema_csn = topology_m2.ms["supplier1"].schema.get_schema_csn()
    supplier2_schema_csn = topology_m2.ms["supplier2"].schema.get_schema_csn()
    topology_m2.ms["supplier1"].log.debug("\nAfter updating the schema on M1\n")
    topology_m2.ms["supplier1"].log.debug("Supplier1 nsschemaCSN: %s" % supplier1_schema_csn)
    topology_m2.ms["supplier1"].log.debug("Supplier2 nsschemaCSN: %s" % supplier2_schema_csn)
    assert (supplier1_schema_csn)

    # to avoid linger effect where a replication session is reused without checking the schema
    _pause_M1_to_M2(topology_m2)
    _resume_M1_to_M2(topology_m2)

    # topo.supplier1.log.debug("\n\nSleep.... attach the debugger dse_modify")
    # time.sleep(60)
    _do_update_entry(supplier=topology_m2.ms["supplier2"], consumer=topology_m2.ms["supplier1"], attempts=15)
    supplier1_schema_csn = topology_m2.ms["supplier1"].schema.get_schema_csn()
    supplier2_schema_csn = topology_m2.ms["supplier2"].schema.get_schema_csn()
    topology_m2.ms["supplier1"].log.debug("\nAfter a full replication session\n")
    topology_m2.ms["supplier1"].log.debug("Supplier1 nsschemaCSN: %s" % supplier1_schema_csn)
    topology_m2.ms["supplier1"].log.debug("Supplier2 nsschemaCSN: %s" % supplier2_schema_csn)
    assert (supplier1_schema_csn)
    assert (supplier2_schema_csn)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
