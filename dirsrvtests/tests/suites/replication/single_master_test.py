# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
from lib389.tasks import *
from lib389.utils import *

from lib389.idm.user import UserAccounts, TEST_USER_PROPERTIES

from lib389.replica import ReplicationManager, Replicas
from lib389.backend import Backends

from lib389.topologies import topology_m1c1 as topo_r # Replication
from lib389.topologies import topology_i2 as topo_nr # No replication
from lib389.utils import ldap, os, ds_is_older, get_default_db_lib

from lib389._constants import (ReplicaRole, DEFAULT_SUFFIX, REPLICAID_SUPPLIER_1,
                                REPLICATION_BIND_DN, REPLICATION_BIND_PW,
                                REPLICATION_BIND_METHOD, REPLICATION_TRANSPORT, DEFAULT_BACKUPDIR,
                                RA_NAME, RA_BINDDN, RA_BINDPW, RA_METHOD, RA_TRANSPORT_PROT,
                                defaultProperties)
import json

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

@pytest.mark.skipif(get_default_db_lib() != "bdb", reason="Test requires bdb files")
def test_mail_attr_repl(topo_r):
    """Check that no crash happens during mail attribute replication

    :id: 959edc84-05be-4bf9-a541-53afae482052
    :customerscenario: True
    :setup: Replication setup with supplier and consumer instances,
            test user on supplier
    :steps:
        1. Check that user was replicated to consumer
        2. Back up mail database file
        3. Remove mail attribute from the user entry
        4. Restore mail database
        5. Search for the entry with a substring 'mail=user*'
        6. Search for the entry once again to make sure that server is alive
    :expectedresults:
        1. The user should be replicated to consumer
        2. Operation should be successful
        3. The mail attribute should be removed
        4. Operation should be successful
        5. Search should be successful
        6. No crash should happen
    """

    supplier = topo_r.ms["supplier1"]
    consumer = topo_r.cs["consumer1"]
    repl = ReplicationManager(DEFAULT_SUFFIX)

    m_users = UserAccounts(topo_r.ms["supplier1"], DEFAULT_SUFFIX)
    m_user = m_users.ensure_state(properties=TEST_USER_PROPERTIES)
    m_user.ensure_present('mail', 'testuser@redhat.com')

    log.info("Check that replication is working")
    repl.wait_for_replication(supplier, consumer)
    c_users = UserAccounts(topo_r.cs["consumer1"], DEFAULT_SUFFIX)
    c_user = c_users.get('testuser')

    c_bes = Backends(consumer)
    c_be = c_bes.get(DEFAULT_SUFFIX)

    db_dir = c_be.get_attr_val_utf8('nsslapd-directory')

    mail_db = list(filter(lambda fl: fl.startswith("mail"), os.listdir(db_dir)))
    assert mail_db, "mail.* wasn't found in {}"
    mail_db_path = os.path.join(db_dir, mail_db[0])
    backup_path = os.path.join(DEFAULT_BACKUPDIR, mail_db[0])

    consumer.stop()
    log.info("Back up {} to {}".format(mail_db_path, backup_path))
    shutil.copyfile(mail_db_path, backup_path)
    consumer.start()

    log.info("Remove 'mail' attr from supplier")
    m_user.remove_all('mail')

    log.info("Wait for the replication to happen")
    repl.wait_for_replication(supplier, consumer)

    consumer.stop()
    log.info("Restore {} to {}".format(backup_path, mail_db_path))
    shutil.copyfile(backup_path, mail_db_path)
    consumer.start()

    log.info("Make a search for mail attribute in attempt to crash server")
    c_user.get_attr_val("mail")

    log.info("Make sure that server hasn't crashed")
    repl.test_replication(supplier, consumer)


def test_lastupdate_attr_before_init(topo_nr):
    """Check that LastUpdate replica attributes show right values

    :id: bc8ce431-ff65-41f5-9331-605cbcaaa887
    :customerscenario: True
    :setup: Replication setup with supplier and consumer instances
            without initialization
    :steps:
        1. Check nsds5replicaLastUpdateStart value
        2. Check nsds5replicaLastUpdateEnd value
        3. Check nsds5replicaLastUpdateStatus value
        4. Check nsds5replicaLastUpdateStatusJSON is parsable
    :expectedresults:
        1. nsds5replicaLastUpdateStart should be equal to 0
        2. nsds5replicaLastUpdateEnd should be equal to 0
        3. nsds5replicaLastUpdateStatus should not be equal
           to "Replica acquired successfully: Incremental update started"
        4. Success
    """

    supplier = topo_nr.ins["standalone1"]
    consumer = topo_nr.ins["standalone2"]

    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.create_first_supplier(supplier)

    # Manually create an un-synced consumer.

    consumer_replicas = Replicas(consumer)
    consumer_replicas.create(properties={
        'cn': 'replica',
        'nsDS5ReplicaRoot': DEFAULT_SUFFIX,
        'nsDS5ReplicaId': '65535',
        'nsDS5Flags': '0',
        'nsDS5ReplicaType': '2',
    })

    agmt = repl.ensure_agreement(supplier, consumer)
    with pytest.raises(Exception):
        repl.wait_for_replication(supplier, consumer, timeout=5)

    assert agmt.get_attr_val_utf8('nsds5replicaLastUpdateStart') == "19700101000000Z"
    assert agmt.get_attr_val_utf8("nsds5replicaLastUpdateEnd") == "19700101000000Z"
    assert "replica acquired successfully" not in agmt.get_attr_val_utf8_l("nsds5replicaLastUpdateStatus")

    # make sure the JSON attribute is parsable
    json_status = agmt.get_attr_val_utf8("nsds5replicaLastUpdateStatusJSON")
    if json_status is not None:
        json_obj = json.loads(json_status)
        log.debug("JSON status message: {}".format(json_obj))

if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
