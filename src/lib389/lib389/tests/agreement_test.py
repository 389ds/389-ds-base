# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2021 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import ldap
import time
import os
import pytest

from lib389 import NoSuchEntryError
from lib389.agreement import Agreement
from lib389._constants import *
from lib389.properties import *
from lib389 import DirSrv
from lib389.utils import ensure_bytes, ensure_str
from lib389.idm.user import UserAccounts, TEST_USER_PROPERTIES
from lib389.idm.domain import Domain

# Used for One supplier / One consumer topology
HOST_SUPPLIER = LOCALHOST
PORT_SUPPLIER = 40389
SERVERID_SUPPLIER = 'supplier'
REPLICAID_SUPPLIER = 1

HOST_CONSUMER = LOCALHOST
PORT_CONSUMER = 50389
SERVERID_CONSUMER = 'consumer'

SUFFIX = DEFAULT_SUFFIX
ENTRY_DN = "cn=test_entry, %s" % SUFFIX
SECOND_AGMT_TEST_PORT = 12345


class TopologyReplication(object):
    def __init__(self, supplier, consumer):
        supplier.open()
        consumer.open()
        self.supplier = supplier
        self.consumer = consumer


@pytest.fixture(scope="module")
def topology(request):
    # Supplier
    #
    # Create the supplier instance
    supplier = DirSrv(verbose=False)
    supplier.log.debug("supplier allocated")
    args = {SER_HOST: HOST_SUPPLIER,
            SER_PORT: PORT_SUPPLIER,
            SER_SERVERID_PROP: SERVERID_SUPPLIER}
    supplier.allocate(args)
    if supplier.exists():
        supplier.delete()
    supplier.create()
    supplier.open()

    # Enable replication
    supplier.replica.enableReplication(suffix=SUFFIX,
                                       role=ReplicaRole.SUPPLIER,
                                       replicaId=REPLICAID_SUPPLIER)

    # Consumer
    #
    # Create the consumer instance
    consumer = DirSrv(verbose=False)
    consumer.log.debug("Consumer allocated")
    args = {SER_HOST: HOST_CONSUMER,
            SER_PORT: PORT_CONSUMER,
            SER_SERVERID_PROP: SERVERID_CONSUMER}
    consumer.allocate(args)
    if consumer.exists():
        consumer.delete()
    consumer.create()
    consumer.open()

    # Enable replication
    consumer.replica.enableReplication(suffix=SUFFIX,
                                       role=ReplicaRole.CONSUMER)

    # Delete each instance in the end
    def fin():
        supplier.delete()
        consumer.delete()
    request.addfinalizer(fin)

    return TopologyReplication(supplier, consumer)


def test_create(topology):
    """Test to create a replica agreement and initialize the consumer.
    Test on a unknown suffix
    """

    topology.supplier.log.info("\n\n##############\n## CREATE\n##############\n")
    properties = {RA_NAME: ('meTo_%s:%d' % (topology.consumer.host,
                                            topology.consumer.port)),
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    repl_agreement = topology.supplier.agreement.create(
        suffix=SUFFIX, host=topology.consumer.host,
        port=topology.consumer.port, properties=properties)
    topology.supplier.log.debug("%s created" % repl_agreement)
    topology.supplier.agreement.init(SUFFIX, HOST_CONSUMER, PORT_CONSUMER)
    topology.supplier.waitForReplInit(repl_agreement)

    supplier_users = UserAccounts(topology.supplier, SUFFIX)
    consumer_users = UserAccounts(topology.consumer, SUFFIX)

    testuser = supplier_users.create(properties=TEST_USER_PROPERTIES)
    testuser_dn = testuser.dn

    # Add a test entry
    # Check replication is working
    loop = 0
    while loop <= 10:
        try:
            consumer_users.get(dn=testuser_dn)
            break
        except ldap.NO_SUCH_OBJECT:
            time.sleep(1)
            loop += 1
    assert loop <= 10

    # Check that with an invalid suffix it raises NoSuchEntryError
    with pytest.raises(NoSuchEntryError):
        properties = {RA_NAME: r'meAgainTo_%s:%d' %
                      (topology.consumer.host, topology.consumer.port)}
        topology.supplier.agreement.create(suffix="ou=dummy",
                                           host=topology.consumer.host,
                                           port=topology.consumer.port,
                                           properties=properties)


def test_list(topology):
    """List the replica agreement on a suffix => 1
    Add a RA
    List the replica agreements on that suffix again => 2
    List a specific RA

    PREREQUISITE: it exists a replica for SUFFIX and a replica agreement
    """

    topology.supplier.log.info("\n\n###########\n## LIST\n#############\n")
    ents = topology.supplier.agreement.list(suffix=SUFFIX)
    assert len(ents) == 1
    assert ents[0].getValue(RA_PROPNAME_TO_ATTRNAME[RA_CONSUMER_HOST]) == \
        ensure_bytes(topology.consumer.host)
    assert ents[0].getValue(RA_PROPNAME_TO_ATTRNAME[RA_CONSUMER_PORT]) == \
        ensure_bytes(str(topology.consumer.port))

    # Create a second RA to check .list returns 2 RA
    properties = {RA_NAME: r'meTo_%s:%d' % (topology.consumer.host,
                                            SECOND_AGMT_TEST_PORT),
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    topology.supplier.agreement.create(suffix=SUFFIX,
                                       host=topology.consumer.host,
                                       port=SECOND_AGMT_TEST_PORT,
                                       properties=properties)
    ents = topology.supplier.agreement.list(suffix=SUFFIX)
    assert len(ents) == 2

    # Check we can .list a specific RA
    ents = topology.supplier.agreement.list(suffix=SUFFIX,
                                            consumer_host=topology.consumer.host,
                                            consumer_port=topology.consumer.port)
    assert len(ents) == 1
    assert ents[0].getValue(RA_PROPNAME_TO_ATTRNAME[RA_CONSUMER_HOST]) == \
        ensure_bytes(topology.consumer.host)
    assert ents[0].getValue(RA_PROPNAME_TO_ATTRNAME[RA_CONSUMER_PORT]) == \
        ensure_bytes(str(topology.consumer.port))


def test_delete(topology):
    """Delete previously created the replica agreement

    PREREQUISITE: it exists a replica for SUFFIX and a replica agreement
    with a SECOND_AGMT_TEST_PORT specified
    """

    # TODO: add a few test cases for different sets of arguments
    #       using fixtures and parameters.
    topology.supplier.log.info("\n\n##############\n### DELETE\n##############\n")
    ents = topology.supplier.agreement.list(suffix=SUFFIX,
                                            consumer_host=topology.consumer.host,
                                            consumer_port=SECOND_AGMT_TEST_PORT)
    assert len(ents) == 1

    # Find DN of the second agreement
    replica_entries = topology.supplier.replica.list(SUFFIX)
    replica = replica_entries[0]
    agreement_cn = r'meTo_%s:%d' % (topology.consumer.host,
                                    SECOND_AGMT_TEST_PORT)
    agreement_dn = ','.join(["cn=%s" % agreement_cn, replica.dn])

    topology.supplier.agreement.delete(suffix=SUFFIX,
                                       consumer_host=topology.consumer.host,
                                       consumer_port=SECOND_AGMT_TEST_PORT,
                                       agmtdn=agreement_dn)

    ents = topology.supplier.agreement.list(suffix=SUFFIX,
                                            consumer_host=topology.consumer.host,
                                            consumer_port=SECOND_AGMT_TEST_PORT)
    assert not ents


def test_status(topology):
    """Test that status is returned from agreement"""

    topology.supplier.log.info("\n\n###########\n## STATUS\n##########")
    ents = topology.supplier.agreement.list(suffix=SUFFIX)
    for ent in ents:
        ra_status = topology.supplier.agreement.status(ensure_str(ent.dn))
        assert ra_status
        topology.supplier.log.info("Status of %s: %s" % (ent.dn, ra_status))


def test_schedule(topology):
    """Test the schedule behaviour with valid and invalid values"""

    topology.supplier.log.info("\n\n###########\n## SCHEDULE\n#########")
    ents = topology.supplier.agreement.list(suffix=SUFFIX,
                                            consumer_host=topology.consumer.host,
                                            consumer_port=topology.consumer.port)
    assert len(ents) == 1

    topology.supplier.agreement.schedule(ents[0].dn, Agreement.ALWAYS)
    ents = topology.supplier.agreement.list(suffix=SUFFIX,
                                            consumer_host=topology.consumer.host,
                                            consumer_port=topology.consumer.port)
    assert len(ents) == 1
    assert ents[0].getValue(RA_PROPNAME_TO_ATTRNAME[RA_SCHEDULE]) == \
        ensure_bytes(Agreement.ALWAYS)

    topology.supplier.agreement.schedule(ents[0].dn, Agreement.NEVER)
    ents = topology.supplier.agreement.list(suffix=SUFFIX,
                                            consumer_host=topology.consumer.host,
                                            consumer_port=topology.consumer.port)
    assert len(ents) == 1
    assert ents[0].getValue(RA_PROPNAME_TO_ATTRNAME[RA_SCHEDULE]) == \
        ensure_bytes(Agreement.NEVER)

    CUSTOM_SCHEDULE = "0000-1234 6420"
    topology.supplier.agreement.schedule(ents[0].dn, CUSTOM_SCHEDULE)
    ents = topology.supplier.agreement.list(suffix=SUFFIX,
                                            consumer_host=topology.consumer.host,
                                            consumer_port=topology.consumer.port)
    assert len(ents) == 1
    assert ents[0].getValue(RA_PROPNAME_TO_ATTRNAME[RA_SCHEDULE]) == \
        ensure_bytes(CUSTOM_SCHEDULE)

    CUSTOM_SCHEDULES = ("2500-1234 6420",  # Invalid HOUR schedule
                        "0000-2534 6420",  # ^^
                        "1300-1234 6420",  # Starting HOUR after ending HOUR
                        "0062-1234 6420",  # Invalid MIN schedule
                        "0000-1362 6420",  # ^^
                        "0000-1234 6-420",  # Invalid DAYS schedule
                        "0000-1362 64209",  # ^^
                        "0000-1362 01234560")  # ^^

    for CUSTOM_SCHEDULE in CUSTOM_SCHEDULES:
        with pytest.raises(ValueError):
            topology.supplier.agreement.schedule(ents[0].dn, CUSTOM_SCHEDULE)


def test_getProperties(topology):
    """Check the correct behaviour of getProperties function"""

    topology.supplier.log.info("\n\n###########\n## GETPROPERTIES\n############")
    ents = topology.supplier.agreement.list(suffix=SUFFIX,
                                            consumer_host=topology.consumer.host,
                                            consumer_port=topology.consumer.port)
    assert len(ents) == 1
    properties = topology.supplier.agreement.getProperties(agmnt_dn=ents[0].dn)
    for prop in properties:
        topology.supplier.log.info("RA %s : %s -> %s" %
                                   (prop, RA_PROPNAME_TO_ATTRNAME[prop],
                                    properties[prop]))

    properties = topology.supplier.agreement.getProperties(
        agmnt_dn=ents[0].dn, properties=[RA_BINDDN])
    assert len(properties) == 1
    for prop in properties:
        topology.supplier.log.info("RA %s : %s -> %s" %
                                   (prop, RA_PROPNAME_TO_ATTRNAME[prop],
                                    properties[prop]))


def test_setProperties(topology):
    """Set properties to the agreement and check, if it was successful"""

    topology.supplier.log.info("\n\n###########\n### SETPROPERTIES\n##########")
    ents = topology.supplier.agreement.list(suffix=SUFFIX,
                                            consumer_host=topology.consumer.host,
                                            consumer_port=topology.consumer.port)
    assert len(ents) == 1
    test_schedule = "1234-2345 12345"
    test_desc = "test_desc"
    topology.supplier.agreement.setProperties(
        agmnt_dn=ents[0].dn, properties={RA_SCHEDULE: test_schedule,
                                         RA_DESCRIPTION: test_desc})
    properties = topology.supplier.agreement.getProperties(
        agmnt_dn=ents[0].dn, properties=[RA_SCHEDULE, RA_DESCRIPTION])
    assert len(properties) == 2
    assert properties[RA_SCHEDULE][0] == ensure_bytes(test_schedule)
    assert properties[RA_DESCRIPTION][0] == ensure_bytes(test_desc)

    # Set RA Schedule back to "always"
    topology.supplier.agreement.schedule(ents[0].dn, Agreement.ALWAYS)


def test_changes(topology):
    """Test the changes counter behaviour after making some changes
    to the replicated suffix
    """

    topology.supplier.log.info("\n\n##########\n### CHANGES\n##########")
    ents = topology.supplier.agreement.list(suffix=SUFFIX,
                                            consumer_host=topology.consumer.host,
                                            consumer_port=topology.consumer.port)
    assert len(ents) == 1
    value = topology.supplier.agreement.changes(agmnt_dn=ents[0].dn)
    topology.supplier.log.info("\ntest_changes: %d changes\n" % value)
    assert value > 0

    # Do an update
    TEST_STRING = 'test_string'
    supplier_domain = Domain(topology.supplier, SUFFIX)
    consumer_domain = Domain(topology.consumer, SUFFIX)

    supplier_domain.set('description', TEST_STRING)

    # The update has been replicated
    loop = 0
    while loop <= 10:
        if consumer_domain.present('description', TEST_STRING):
            break
        time.sleep(1)
        loop += 1
    assert loop <= 10

    # Give a little time to update a change number on supplier
    time.sleep(2)

    # Check change number
    newvalue = topology.supplier.agreement.changes(agmnt_dn=ents[0].dn)
    topology.supplier.log.info("\ntest_changes: %d changes\n" % newvalue)
    assert (value + 1) == newvalue


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
