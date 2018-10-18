# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import ldap
import os
import pytest
import logging

from lib389 import InvalidArgumentError
from lib389._constants import *
from lib389.properties import *
from lib389 import DirSrv, Entry

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

# Used for One master / One consumer topology
HOST_MASTER = LOCALHOST
PORT_MASTER = 40389
SERVERID_MASTER = 'master'
REPLICAID_MASTER = 1

HOST_CONSUMER = LOCALHOST
PORT_CONSUMER = 50389
SERVERID_CONSUMER = 'consumer'

TEST_REPL_DN = "uid=test,%s" % DEFAULT_SUFFIX
INSTANCE_PORT = 54321
INSTANCE_SERVERID = 'dirsrv'
INSTANCE_BACKUP = os.environ.get('BACKUPDIR', DEFAULT_BACKUPDIR)
NEW_SUFFIX_1 = 'ou=test_master'
NEW_BACKEND_1 = 'test_masterdb'
NEW_RM_1 = "cn=replication manager,%s" % NEW_SUFFIX_1

NEW_SUFFIX_2 = 'ou=test_consumer'
NEW_BACKEND_2 = 'test_consumerdb'

NEW_SUFFIX_3 = 'ou=test_enablereplication_1'
NEW_BACKEND_3 = 'test_enablereplicationdb_1'

NEW_SUFFIX_4 = 'ou=test_enablereplication_2'
NEW_BACKEND_4 = 'test_enablereplicationdb_2'

NEW_SUFFIX_5 = 'ou=test_enablereplication_3'
NEW_BACKEND_5 = 'test_enablereplicationdb_3'


class TopologyReplication(object):
    def __init__(self, master, consumer):
        master.open()
        consumer.open()
        self.master = master
        self.consumer = consumer


@pytest.fixture(scope="module")
def topology(request):
    # Create the master instance
    master = DirSrv(verbose=False)
    master.log.debug("Master allocated")
    args = {SER_HOST: HOST_MASTER,
            SER_PORT: PORT_MASTER,
            SER_SERVERID_PROP: SERVERID_MASTER}
    master.allocate(args)
    if master.exists():
        master.delete()
    master.create()
    master.open()

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

    # Delete each instance in the end
    def fin():
        master.delete()
        consumer.delete()
    request.addfinalizer(fin)

    return TopologyReplication(master, consumer)


def test_create(topology):
    """This test creates
         - suffix/backend (NEW_SUFFIX_[12], NEW_BACKEND_[12]) : Master
         - suffix/backend (NEW_SUFFIX_[12], NEW_BACKEND_[12]) : Consumer
         - replica NEW_SUFFIX_1 as MASTER : Master
         - replica NEW_SUFFIX_2 as CONSUMER : Master
    """

    log.info("\n\n##########\n### CREATE\n############")
    #
    # MASTER (suffix/backend)
    #
    backendEntry = topology.master.backend.create(
        suffix=NEW_SUFFIX_1, properties={BACKEND_NAME: NEW_BACKEND_1})
    backendEntry = topology.master.backend.create(
        suffix=NEW_SUFFIX_2, properties={BACKEND_NAME: NEW_BACKEND_2})

    ents = topology.master.mappingtree.list()
    master_nb_mappingtree = len(ents)

    # create a first additional mapping tree
    topology.master.mappingtree.create(NEW_SUFFIX_1, bename=NEW_BACKEND_1)
    ents = topology.master.mappingtree.list()
    assert len(ents) == (master_nb_mappingtree + 1)
    topology.master.add_s(Entry((NEW_SUFFIX_1,
                          {'objectclass': "top organizationalunit".split(),
                           'ou': NEW_SUFFIX_1.split('=', 1)[1]})))

    # create a second additional mapping tree
    topology.master.mappingtree.create(NEW_SUFFIX_2, bename=NEW_BACKEND_2)
    ents = topology.master.mappingtree.list()
    assert len(ents) == (master_nb_mappingtree + 2)
    topology.master.add_s(Entry((NEW_SUFFIX_2,
                          {'objectclass': "top organizationalunit".split(),
                           'ou': NEW_SUFFIX_2.split('=', 1)[1]})))
    log.info('Master it exists now %d suffix(es)' % len(ents))

    #
    # CONSUMER (suffix/backend)
    #
    backendEntry = topology.consumer.backend.create(
        suffix=NEW_SUFFIX_1, properties={BACKEND_NAME: NEW_BACKEND_1})
    backendEntry = topology.consumer.backend.create(
        suffix=NEW_SUFFIX_2, properties={BACKEND_NAME: NEW_BACKEND_2})

    ents = topology.consumer.mappingtree.list()
    consumer_nb_mappingtree = len(ents)

    # create a first additional mapping tree
    topology.consumer.mappingtree.create(NEW_SUFFIX_1, bename=NEW_BACKEND_1)
    ents = topology.consumer.mappingtree.list()
    assert len(ents) == (consumer_nb_mappingtree + 1)
    topology.consumer.add_s(Entry((NEW_SUFFIX_1,
                            {'objectclass': "top organizationalunit".split(),
                             'ou': NEW_SUFFIX_1.split('=', 1)[1]})))

    # create a second additional mapping tree
    topology.consumer.mappingtree.create(NEW_SUFFIX_2, bename=NEW_BACKEND_2)
    ents = topology.consumer.mappingtree.list()
    assert len(ents) == (consumer_nb_mappingtree + 2)
    topology.consumer.add_s(Entry((NEW_SUFFIX_2,
                            {'objectclass': "top organizationalunit".split(),
                             'ou': NEW_SUFFIX_2.split('=', 1)[1]})))
    log.info('Consumer it exists now %d suffix(es)' % len(ents))

    #
    # Now create REPLICAS on master
    #
    # check it exists this entry to stores the changelogs
    topology.master.changelog.create()

    # create a master
    topology.master.replica.create(suffix=NEW_SUFFIX_1,
                                   role=ReplicaRole.MASTER,
                                   rid=1)
    ents = topology.master.replica.list()
    assert len(ents) == 1
    log.info('Master replica %s' % ents[0].dn)

    # create a consumer
    topology.master.replica.create(suffix=NEW_SUFFIX_2,
                                   role=ReplicaRole.CONSUMER)
    ents = topology.master.replica.list()
    assert len(ents) == 2
    ents = topology.master.replica.list(suffix=NEW_SUFFIX_2)
    log.info('Consumer replica %s' % ents[0].dn)

    #
    # Now create REPLICAS on consumer
    #
    # create a master
    topology.consumer.replica.create(suffix=NEW_SUFFIX_1,
                                     role=ReplicaRole.CONSUMER)
    ents = topology.consumer.replica.list()
    assert len(ents) == 1
    log.info('Consumer replica %s' % ents[0].dn)

    # create a consumer
    topology.consumer.replica.create(suffix=NEW_SUFFIX_2,
                                     role=ReplicaRole.CONSUMER)
    ents = topology.consumer.replica.list()
    assert len(ents) == 2
    ents = topology.consumer.replica.list(suffix=NEW_SUFFIX_2)
    log.info('Consumer replica %s' % ents[0].dn)


def test_list(topology):
    """This test checks:
         - existing replicas can be retrieved
         - access to unknown replica does not fail

    PRE-CONDITION:
         It exists on MASTER two replicas NEW_SUFFIX_1 and NEW_SUFFIX_2
         created by test_create()
    """

    log.info("\n\n############\n### LIST\n############")
    ents = topology.master.replica.list()
    assert len(ents) == 2

    # Check we can retrieve a replica with its suffix
    ents = topology.master.replica.list(suffix=NEW_SUFFIX_1)
    assert len(ents) == 1
    replica_dn_1 = ents[0].dn

    # Check we can retrieve a replica with its suffix
    ents = topology.master.replica.list(suffix=NEW_SUFFIX_2)
    assert len(ents) == 1
    replica_dn_2 = ents[0].dn

    # Check we can retrieve a replica with its DN
    ents = topology.master.replica.list(replica_dn=replica_dn_1)
    assert len(ents) == 1
    assert replica_dn_1 == ents[0].dn

    # Check we can retrieve a replica if we provide DN and suffix
    ents = topology.master.replica.list(suffix=NEW_SUFFIX_2,
                                        replica_dn=replica_dn_2)
    assert len(ents) == 1
    assert replica_dn_2 == ents[0].dn

    # Check DN is used before suffix name
    ents = topology.master.replica.list(suffix=NEW_SUFFIX_2,
                                        replica_dn=replica_dn_1)
    assert len(ents) == 1
    assert replica_dn_1 == ents[0].dn

    # Check that invalid value does not break
    ents = topology.master.replica.list(suffix="X")
    for ent in ents:
        log.critical("Unexpected replica: %s" % ent.dn)
    assert len(ents) == 0


def test_create_repl_manager(topology):
    """The tests are
         - create the default Replication manager/Password
         - create a specific Replication manager/ default Password
         - Check we can bind successfully
         - create a specific Replication manager / specific Password
         - Check we can bind successfully
    """

    log.info("\n\n###########\n### CREATE_REPL_MANAGER\n###########")
    # First create the default replication manager
    topology.consumer.replica.create_repl_manager()
    ents = topology.consumer.search_s(defaultProperties[REPLICATION_BIND_DN],
                                      ldap.SCOPE_BASE, "objectclass=*")
    assert len(ents) == 1
    assert ents[0].dn == defaultProperties[REPLICATION_BIND_DN]

    # Second create a custom replication manager under NEW_SUFFIX_2
    rm_dn = "cn=replication manager,%s" % NEW_SUFFIX_2
    topology.consumer.replica.create_repl_manager(repl_manager_dn=rm_dn)
    ents = topology.consumer.search_s(rm_dn, ldap.SCOPE_BASE, "objectclass=*")
    assert len(ents) == 1
    assert ents[0].dn == rm_dn

    # Check we can bind
    topology.consumer.simple_bind_s(rm_dn,
                                    defaultProperties[REPLICATION_BIND_PW])

    # Check we fail to bind
    with pytest.raises(ldap.INVALID_CREDENTIALS) as excinfo:
        topology.consumer.simple_bind_s(rm_dn, "dummy")
    log.info("Exception: %s" % str(excinfo.value))

    # now rebind
    topology.consumer.simple_bind_s(topology.consumer.binddn,
                                    topology.consumer.bindpw)

    # Create a custom replication manager under NEW_SUFFIX_1
    # with a specified password
    rm_dn = NEW_RM_1
    topology.consumer.replica.create_repl_manager(repl_manager_dn=rm_dn,
                                                  repl_manager_pw="Secret123")
    ents = topology.consumer.search_s(rm_dn, ldap.SCOPE_BASE, "objectclass=*")
    assert len(ents) == 1
    assert ents[0].dn == rm_dn

    # Check we can bind
    topology.consumer.simple_bind_s(rm_dn, "Secret123")

    # Check we fail to bind
    with pytest.raises(ldap.INVALID_CREDENTIALS) as excinfo:
        topology.consumer.simple_bind_s(rm_dn, "dummy")
    log.info("Exception: %s" % str(excinfo.value))
    topology.consumer.simple_bind_s(topology.consumer.binddn,
                                    topology.consumer.bindpw)


def test_enableReplication(topology):
    """It checks
         - Ability to enable replication on a supplier
         - Ability to enable replication on a consumer
         - Failure to enable replication with wrong replicaID on supplier
         - Failure to enable replication with wrong replicaID on consumer
    """

    log.info("\n\n############\n### ENABLEREPLICATION\n##########")
    #
    # MASTER (suffix/backend)
    #
    backendEntry = topology.master.backend.create(suffix=NEW_SUFFIX_3,
                                                  properties={BACKEND_NAME:
                                                              NEW_BACKEND_3})

    ents = topology.master.mappingtree.list()
    master_nb_mappingtree = len(ents)

    # create a first additional mapping tree
    topology.master.mappingtree.create(NEW_SUFFIX_3, bename=NEW_BACKEND_3)
    ents = topology.master.mappingtree.list()
    assert len(ents) == (master_nb_mappingtree + 1)
    topology.master.add_s(Entry((NEW_SUFFIX_3,
                          {'objectclass': "top organizationalunit".split(),
                           'ou': NEW_SUFFIX_3.split('=', 1)[1]})))

    # a supplier should have replicaId in [1..CONSUMER_REPLICAID[
    with pytest.raises(ValueError) as excinfo:
        topology.master.replica.enableReplication(suffix=NEW_SUFFIX_3,
                                                  role=ReplicaRole.MASTER,
                                                  replicaId=CONSUMER_REPLICAID)
    log.info("Exception (expected): %s" % str(excinfo.value))
    topology.master.replica.enableReplication(suffix=NEW_SUFFIX_3,
                                              role=ReplicaRole.MASTER,
                                              replicaId=1)

    #
    # MASTER (suffix/backend)
    #
    backendEntry = topology.master.backend.create(suffix=NEW_SUFFIX_4,
                                                  properties={BACKEND_NAME:
                                                              NEW_BACKEND_4})

    ents = topology.master.mappingtree.list()
    master_nb_mappingtree = len(ents)

    # create a first additional mapping tree
    topology.master.mappingtree.create(NEW_SUFFIX_4, bename=NEW_BACKEND_4)
    ents = topology.master.mappingtree.list()
    assert len(ents) == (master_nb_mappingtree + 1)
    topology.master.add_s(Entry((NEW_SUFFIX_4,
                          {'objectclass': "top organizationalunit".split(),
                           'ou': NEW_SUFFIX_4.split('=', 1)[1]})))

    # A consumer should have CONSUMER_REPLICAID not '1'
    with pytest.raises(ValueError) as excinfo:
        topology.master.replica.enableReplication(suffix=NEW_SUFFIX_4,
                                                  role=ReplicaRole.CONSUMER,
                                                  replicaId=1)
    log.info("Exception (expected): %s" % str(excinfo.value))
    topology.master.replica.enableReplication(suffix=NEW_SUFFIX_4,
                                              role=ReplicaRole.CONSUMER)


def test_disableReplication(topology):
    """It checks
         - Ability to disable replication on a supplier
         - Ability to disable replication on a consumer
         - Failure to disable replication with wrong suffix on supplier
         - Failure to disable replication with wrong suffix on consumer
    """

    log.info("\n\n############\n### DISABLEREPLICATION\n##########")
    topology.master.replica.disableReplication(suffix=NEW_SUFFIX_3)
    with pytest.raises(ldap.LDAPError) as excinfo:
        topology.master.replica.disableReplication(suffix=NEW_SUFFIX_3)
    log.info("Exception (expected): %s" % str(excinfo.value))

    topology.master.replica.disableReplication(suffix=NEW_SUFFIX_4)
    with pytest.raises(ldap.LDAPError) as excinfo:
        topology.master.replica.disableReplication(suffix=NEW_SUFFIX_4)
    log.info("Exception (expected): %s" % str(excinfo.value))


def test_setProperties(topology):
    """Set some properties
    Verified that valid properties are set
    Verified that invalid properties raise an Exception

    PRE-REQUISITE: it exists a replica for NEW_SUFFIX_1
    """

    log.info("\n\n##########\n### SETPROPERTIES\n############")
    # set valid values to SUFFIX_1
    properties = {REPLICA_BINDDN: NEW_RM_1,
                  REPLICA_PURGE_INTERVAL: str(3600),
                  REPLICA_PURGE_DELAY: str(5 * 24 * 3600),
                  REPLICA_REFERRAL: "ldap://%s:1234/" % LOCALHOST}
    topology.master.replica.setProperties(suffix=NEW_SUFFIX_1,
                                          properties=properties)

    # Check the values have been written
    replicas = topology.master.replica.list(suffix=NEW_SUFFIX_1)
    assert len(replicas) == 1
    for prop in properties:
        attr = REPLICA_PROPNAME_TO_ATTRNAME[prop]
        val = replicas[0].getValue(attr)
        log.info("Replica[%s] -> %s: %s" % (prop, attr, val))
        assert val == properties[prop]

    # Check invalid properties raise exception
    with pytest.raises(ValueError) as excinfo:
        properties = {"dummy": 'dummy'}
        topology.master.replica.setProperties(suffix=NEW_SUFFIX_1,
                                              properties=properties)
    log.info("Exception (expected): %s" % str(excinfo.value))

    # check call without suffix/dn/entry raise InvalidArgumentError
    with pytest.raises(InvalidArgumentError) as excinfo:
        properties = {REPLICA_BINDDN: NEW_RM_1}
        topology.master.replica.setProperties(properties=properties)
    log.info("Exception (expected): %s" % str(excinfo.value))

    # check that if we do not provide a valid entry it raises ValueError
    with pytest.raises(ValueError) as excinfo:
        properties = {REPLICA_BINDDN: NEW_RM_1}
        topology.master.replica.setProperties(replica_entry="dummy",
                                              properties=properties)
    log.info("Exception (expected): %s" % str(excinfo.value))

    # check that with an invalid suffix or replica_dn it raise ValueError
    with pytest.raises(ValueError) as excinfo:
        properties = {REPLICA_BINDDN: NEW_RM_1}
        topology.master.replica.setProperties(suffix="dummy",
                                              properties=properties)
    log.info("Exception (expected): %s" % str(excinfo.value))


def test_getProperties(topology):
    """Currently not implemented"""

    log.info("\n\n############\n### GETPROPERTIES\n###########")
    with pytest.raises(NotImplementedError) as excinfo:
        topology.master.replica.getProperties(suffix=NEW_SUFFIX_1)
    log.info("Exception (expected): %s" % str(excinfo.value))


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
