import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_m1c1 as topo_r # Replication
from lib389.topologies import topology_i2 as topo_nr # No replication

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

TEST_USER_NAME = 'smrepl_test'
TEST_USER_DN = 'uid={},{}'.format(TEST_USER_NAME, DEFAULT_SUFFIX)
TEST_USER_PWD = 'smrepl_test'


@pytest.fixture
def test_user(topo_r, request):
    """User for binding operation"""

    log.info('Adding user {}'.format(TEST_USER_DN))
    try:
        topo_r.ms["master1"].add_s(Entry((TEST_USER_DN, {
            'objectclass': 'top person'.split(),
            'objectclass': 'organizationalPerson',
            'objectclass': 'inetorgperson',
            'cn': TEST_USER_NAME,
            'sn': TEST_USER_NAME,
            'userpassword': TEST_USER_PWD,
            'mail': '{}@redhat.com'.format(TEST_USER_NAME),
            'uid': TEST_USER_NAME
        })))
    except ldap.LDAPError as e:
        log.error('Failed to add user (%s): error (%s)' % (TEST_USER_DN,
                                                           e.message['desc']))
        raise e

    def fin():
        log.info('Deleting user {}'.format(TEST_USER_DN))
        topo_r.ms["master1"].delete_s(TEST_USER_DN)

    request.addfinalizer(fin)


@pytest.fixture(scope="module")
def replica_without_init(topo_nr):
    """Enable replica without initialization"""

    master = topo_nr.ins["standalone1"]
    consumer = topo_nr.ins["standalone2"]

    master.replica.enableReplication(suffix=DEFAULT_SUFFIX, role=REPLICAROLE_MASTER,
                                     replicaId=REPLICAID_MASTER_1)
    consumer.replica.enableReplication(suffix=DEFAULT_SUFFIX, role=REPLICAROLE_CONSUMER)
    properties = {RA_NAME: 'meTo_{}:{}'.format(consumer.host, str(consumer.port)),
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    agmt = master.agreement.create(suffix=DEFAULT_SUFFIX, host=consumer.host, port=consumer.port, properties=properties)

    return agmt


def test_mail_attr_repl(topo_r, test_user):
    """Check that no crash happens during mail attribute replication

    :Feature: Single master replication

    :Setup: Replication setup with master and consumer instances,
            test user on master

    :Steps: 1. Check that user was replicated to consumer
            2. Back up mail database file
            3. Remove mail attribute from the user entry
            4. Restore mail database
            5. Search for the entry with a substring 'mail=user*'
            6. Search for the entry once again to make sure that server is alive

    :Assert: No crash happens
    """

    master = topo_r.ms["master1"]
    consumer = topo_r.cs["consumer1"]

    log.info("Wait for a user to be replicated")
    time.sleep(3)

    log.info("Check that replication is working")
    entries = consumer.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, "uid={}".format(TEST_USER_NAME),
                                              ["uid"])
    assert entries, "User {} wasn't replicated successfully".format(TEST_USER_NAME)

    entries = consumer.backend.list(DEFAULT_SUFFIX)
    db_dir = entries[0]["nsslapd-directory"]
    mail_db = filter(lambda fl: fl.startswith("mail"), os.listdir(db_dir))
    assert mail_db, "mail.* wasn't found in {}"
    mail_db_path = os.path.join(db_dir, mail_db[0])
    backup_path = os.path.join(DEFAULT_BACKUPDIR, mail_db[0])

    consumer.stop()
    log.info("Back up {} to {}".format(mail_db_path, backup_path))
    shutil.copyfile(mail_db_path, backup_path)
    consumer.start()

    log.info("Remove 'mail' attr from master")
    try:
        master.modify_s(TEST_USER_DN, [(ldap.MOD_DELETE, 'mail', '{}@redhat.com'.format(TEST_USER_NAME))])
    except ldap.LDAPError as e:
        log.error('Failed to delete att user {}: error {}'.format(TEST_USER_DN, e.message['desc']))
        raise e

    log.info("Wait for the replication to happen")
    time.sleep(5)

    consumer.stop()
    log.info("Restore {} to {}".format(backup_path, mail_db_path))
    shutil.copyfile(backup_path, mail_db_path)
    consumer.start()

    log.info("Make a search for mail attribute in attempt to crash server")
    consumer.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, "mail={}*".format(TEST_USER_NAME), ["mail"])

    log.info("Make sure that server hasn't crashed")
    entries = consumer.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, "uid={}".format(TEST_USER_NAME),
                                              ["uid"])
    assert entries, "User {} wasn't replicated successfully".format(TEST_USER_NAME)


def test_lastupdate_attr_before_init(topo_nr, replica_without_init):
    """Check that LastUpdate replica attributes show right values

    :Feature: Single master replication

    :Setup: Replication setup with master and consumer instances
            without initialization

    :Steps: 1. Check nsds5replicaLastUpdateStart, nsds5replicaLastUpdateEnd,
               nsds5replicaLastUpdateStatus attrs

    :Assert: nsds5replicaLastUpdateStart: 0, nsds5replicaLastUpdateEnd: 0 and
             nsds5replicaLastUpdateStatus is not equal to
             "0 Replica acquired successfully: Incremental update started"
    """

    master = topo_nr.ins["standalone1"]
    consumer = topo_nr.ins["standalone2"]

    assert not master.testReplication(DEFAULT_SUFFIX, consumer)

    agmt = master.search_s(replica_without_init, ldap.SCOPE_BASE, "(objectClass=*)",
                           ["nsds5replicaLastUpdateStart",
                            "nsds5replicaLastUpdateEnd",
                            "nsds5replicaLastUpdateStatus"])[0]

    assert agmt["nsds5replicaLastUpdateStart"] == "19700101000000Z"
    assert agmt["nsds5replicaLastUpdateEnd"] == "19700101000000Z"
    assert "Replica acquired successfully" not in agmt["nsds5replicaLastUpdateStatus"]


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
