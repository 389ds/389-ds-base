# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2021 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import logging
import pytest
from lib389.utils import *
from lib389._constants import *
from lib389.replica import Replicas, ReplicationManager
from lib389.dseldif import *
from lib389.topologies import topology_i2 as topo_i2


pytestmark = pytest.mark.tier1

NEW_SUFFIX_NAME = 'test_repl'
NEW_SUFFIX = 'o={}'.format(NEW_SUFFIX_NAME)
NEW_BACKEND = 'repl_base'
CHANGELOG = 'cn=changelog,{}'.format(DN_USERROOT_LDBM)
MAXAGE_ATTR = 'nsslapd-changelogmaxage'
MAXAGE_STR = '30'
TRIMINTERVAL_STR = '5'
TRIMINTERVAL = 'nsslapd-changelogtrim-interval'

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def test_special_symbol_replica_agreement(topo_i2):
    """ Check if agreement starts with "cn=->..." then
    after upgrade does it get removed.

    :id: 68aa0072-4dd4-4e33-b107-cb383a439125
    :setup: two standalone instance
    :steps:
        1. Create and Enable Replication on standalone2 and role as consumer
        2. Create and Enable Replication on standalone1 and role as master
        3. Create a Replication agreement starts with "cn=->..."
        4. Perform an upgrade operation over the master
        5. Check if the agreement is still present or not.
    :expectedresults:
        1. It should be successful
        2. It should be successful
        3. It should be successful
        4. It should be successful
        5. It should be successful
    """

    master = topo_i2.ins["standalone1"]
    consumer = topo_i2.ins["standalone2"]
    consumer.replica.enableReplication(suffix=DEFAULT_SUFFIX, role=ReplicaRole.CONSUMER, replicaId=CONSUMER_REPLICAID)
    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.create_first_master(master)

    properties = {RA_NAME: '-\\3meTo_{}:{}'.format(consumer.host, str(consumer.port)),
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}

    master.agreement.create(suffix=SUFFIX,
                            host=consumer.host,
                            port=consumer.port,
                            properties=properties)

    master.agreement.init(SUFFIX, consumer.host, consumer.port)

    replica_server = Replicas(master).get(DEFAULT_SUFFIX)

    master.upgrade('online')

    agmt = replica_server.get_agreements().list()[0]

    assert agmt.get_attr_val_utf8('cn') == '-\\3meTo_{}:{}'.format(consumer.host, str(consumer.port))


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
