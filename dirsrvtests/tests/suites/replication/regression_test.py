# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import ldif
import pytest
import subprocess
from lib389.idm.user import TEST_USER_PROPERTIES, UserAccounts
from lib389.pwpolicy import PwPolicyManager
from lib389.utils import *
from lib389.topologies import topology_m2 as topo_m2, TopologyMain, topology_m3 as topo_m3, create_topology, _remove_ssca_db, topology_i2 as topo_i2
from lib389._constants import *
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.idm.user import UserAccount
from lib389.idm.group import Groups, Group
from lib389.idm.domain import Domain
from lib389.idm.directorymanager import DirectoryManager
from lib389.replica import Replicas, ReplicationManager, Changelog5, BootstrapReplicationManager
from lib389.agreement import Agreements
from lib389 import pid_from_file


pytestmark = pytest.mark.tier1

NEW_SUFFIX_NAME = 'test_repl'
NEW_SUFFIX = 'o={}'.format(NEW_SUFFIX_NAME)
NEW_BACKEND = 'repl_base'
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


def find_start_location(file, no):
    log_pattern = re.compile("slapd_daemon - slapd started.")
    count = 0
    while True:
        line = file.readline()
        log.debug("_pattern_errorlog: [%d] %s" % (file.tell(), line))
        found = log_pattern.search(line)
        if (found):
            count = count + 1
            if (count == no):
                return file.tell()
        if (line == ''):
            break
    return -1


def pattern_errorlog(file, log_pattern, start_location=0):

    count = 0
    log.debug("_pattern_errorlog: start from the beginning")
    file.seek(start_location)

    # Use a while true iteration because 'for line in file: hit a
    # python bug that break file.tell()
    while True:
        line = file.readline()
        log.debug("_pattern_errorlog: [%d] %s" % (file.tell(), line))
        found = log_pattern.search(line)
        if (found):
            count = count + 1
        if (line == ''):
            break

    log.debug("_pattern_errorlog: complete (count=%d)" % count)
    return count


def _move_ruv(ldif_file):
    """ Move RUV entry in an ldif file to the top"""

    with open(ldif_file) as f:
        parser = ldif.LDIFRecordList(f)
        parser.parse()

        ldif_list = parser.all_records
        for dn in ldif_list:
            if dn[0].startswith('nsuniqueid=ffffffff-ffffffff-ffffffff-ffffffff'):
                ruv_index = ldif_list.index(dn)
                ldif_list.insert(0, ldif_list.pop(ruv_index))
                break

    with open(ldif_file, 'w') as f:
        ldif_writer = ldif.LDIFWriter(f)
        for dn, entry in ldif_list:
            ldif_writer.unparse(dn, entry)


@pytest.fixture(scope="module")
def topo_with_sigkill(request):
    """Create Replication Deployment with two masters"""

    topology = create_topology({ReplicaRole.MASTER: 2})

    def _kill_ns_slapd(inst):
        pid = str(pid_from_file(inst.ds_paths.pid_file))
        cmd = ['kill', '-9', pid]
        subprocess.Popen(cmd, stdout=subprocess.PIPE)

    def fin():
        # Kill the hanging process at the end of test to prevent failures in the following tests
        if DEBUGGING:
            [_kill_ns_slapd(inst) for inst in topology]
        else:
            [_kill_ns_slapd(inst) for inst in topology]
            assert _remove_ssca_db(topology)
            [inst.delete() for inst in topology if inst.exists()]
    request.addfinalizer(fin)

    return topology


@pytest.fixture()
def create_entry(topo_m2, request):
    """Add test entry using UserAccounts"""

    log.info('Adding a test entry user')
    users = UserAccounts(topo_m2.ms["master1"], DEFAULT_SUFFIX)
    tuser = users.ensure_state(properties=TEST_USER_PROPERTIES)
    return tuser


def add_ou_entry(server, idx, parent):
    ous = OrganizationalUnits(server, parent)
    name = 'OU%d' % idx
    ous.create(properties={'ou': '%s' % name})


def add_user_entry(server, idx, parent):
    users = UserAccounts(server, DEFAULT_SUFFIX, rdn=parent)
    user_properties = {
        'uid': 'tuser%d' % idx,
        'givenname': 'test',
        'cn': 'Test User%d' % idx,
        'sn': 'user%d' % idx,
        'userpassword': PW_DM,
        'uidNumber' : '1000%d' % idx,
        'gidNumber': '2000%d' % idx,
        'homeDirectory': '/home/{}'.format('tuser%d' % idx)
    }
    users.create(properties=user_properties)


def del_user_entry(server, idx, parent):
    users = UserAccounts(server, DEFAULT_SUFFIX, rdn=parent)
    test_user = users.get('tuser%d' % idx)
    test_user.delete()


def rename_entry(server, idx, ou_name, new_parent):
    users = UserAccounts(server, DEFAULT_SUFFIX, rdn=ou_name)
    name = 'tuser%d' % idx
    rdn = 'uid=%s' % name
    test_user = users.get(name)
    test_user.rename(new_rdn=rdn, newsuperior=new_parent)


def add_ldapsubentry(server, parent):
    pwp = PwPolicyManager(server)
    policy_props = {'passwordStorageScheme': 'ssha',
                                'passwordCheckSyntax': 'on',
                                'passwordInHistory': '6',
                                'passwordChange': 'on',
                                'passwordMinAge': '0',
                                'passwordExp': 'off',
                                'passwordMustChange': 'off',}
    log.info('Create password policy for subtree {}'.format(parent))
    pwp.create_subtree_policy(parent, policy_props)


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

    properties = {RA_NAME: '-\\3meTo_{}:{}'.format(consumer.host,
                                                   str(consumer.port)),
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

    assert agmt.get_attr_val_utf8('cn') == '-\\3meTo_{}:{}'.format(consumer.host,
                                                                   str(consumer.port))



def test_double_delete(topo_m2, create_entry):
    """Check that double delete of the entry doesn't crash server

    :id: 3496c82d-636a-48c9-973c-2455b12164cc
    :setup: Two masters replication setup, a test entry
    :steps:
        1. Delete the entry on the first master
        2. Delete the entry on the second master
        3. Check that server is alive
    :expectedresults:
        1. Entry should be successfully deleted from first master
        2. Entry should be successfully deleted from second aster
        3. Server should me alive
    """

    m1 = topo_m2.ms["master1"]
    m2 = topo_m2.ms["master2"]

    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.disable_to_master(m1, [m2])
    repl.disable_to_master(m2, [m1])

    log.info('Deleting entry {} from master1'.format(create_entry.dn))
    topo_m2.ms["master1"].delete_s(create_entry.dn)

    log.info('Deleting entry {} from master2'.format(create_entry.dn))
    topo_m2.ms["master2"].delete_s(create_entry.dn)

    repl.enable_to_master(m2, [m1])
    repl.enable_to_master(m1, [m2])

    repl.test_replication(m1, m2)
    repl.test_replication(m2, m1)


@pytest.mark.bz1506831
def test_repl_modrdn(topo_m2):
    """Test that replicated MODRDN does not break replication

    :id: a3e17698-9eb4-41e0-b537-8724b9915fa6
    :setup: Two masters replication setup
    :steps:
        1. Add 3 test OrganizationalUnits A, B and C
        2. Add 1 test user under OU=A
        3. Add same test user under OU=B
        4. Stop Replication
        5. Apply modrdn to M1 - move test user from OU A -> C
        6. Apply modrdn on M2 - move test user from OU B -> C
        7. Start Replication
        8. Check that there should be only one test entry under ou=C on both masters
        9. Check that the replication is working fine both ways M1 <-> M2
    :expectedresults:
        1. This should pass
        2. This should pass
        3. This should pass
        4. This should pass
        5. This should pass
        6. This should pass
        7. This should pass
        8. This should pass
        9. This should pass
    """

    master1 = topo_m2.ms["master1"]
    master2 = topo_m2.ms["master2"]

    repl = ReplicationManager(DEFAULT_SUFFIX)

    log.info("Add test entries - Add 3 OUs and 2 same users under 2 different OUs")
    OUs = OrganizationalUnits(master1, DEFAULT_SUFFIX)
    OU_A = OUs.create(properties={
        'ou': 'A',
        'description': 'A',
    })
    OU_B = OUs.create(properties={
        'ou': 'B',
        'description': 'B',
    })
    OU_C = OUs.create(properties={
        'ou': 'C',
        'description': 'C',
    })

    users = UserAccounts(master1, DEFAULT_SUFFIX, rdn='ou={}'.format(OU_A.rdn))
    tuser_A = users.create(properties=TEST_USER_PROPERTIES)

    users = UserAccounts(master1, DEFAULT_SUFFIX, rdn='ou={}'.format(OU_B.rdn))
    tuser_B = users.create(properties=TEST_USER_PROPERTIES)

    repl.test_replication(master1, master2)
    repl.test_replication(master2, master1)

    log.info("Stop Replication")
    topo_m2.pause_all_replicas()

    log.info("Apply modrdn to M1 - move test user from OU A -> C")
    master1.rename_s(tuser_A.dn, 'uid=testuser1', newsuperior=OU_C.dn, delold=1)

    log.info("Apply modrdn on M2 - move test user from OU B -> C")
    master2.rename_s(tuser_B.dn, 'uid=testuser1', newsuperior=OU_C.dn, delold=1)

    log.info("Start Replication")
    topo_m2.resume_all_replicas()

    log.info("Wait for sometime for repl to resume")
    repl.test_replication(master1, master2)
    repl.test_replication(master2, master1)

    log.info("Check that there should be only one test entry under ou=C on both masters")
    users = UserAccounts(master1, DEFAULT_SUFFIX, rdn='ou={}'.format(OU_C.rdn))
    assert len(users.list()) == 1

    users = UserAccounts(master2, DEFAULT_SUFFIX, rdn='ou={}'.format(OU_C.rdn))
    assert len(users.list()) == 1

    log.info("Check that the replication is working fine both ways, M1 <-> M2")
    repl.test_replication(master1, master2)
    repl.test_replication(master2, master1)


def test_password_repl_error(topo_m2, create_entry):
    """Check that error about userpassword replication is properly logged

    :id: 714130ff-e4f0-4633-9def-c1f4b24abfef
    :setup: Four masters replication setup, a test entry
    :steps:
        1. Change userpassword on the first master
        2. Restart the servers to flush the logs
        3. Check the error log for an replication error
    :expectedresults:
        1. Password should be successfully changed
        2. Server should be successfully restarted
        3. There should be no replication errors in the error log
    """

    m1 = topo_m2.ms["master1"]
    m2 = topo_m2.ms["master2"]
    TEST_ENTRY_NEW_PASS = 'new_pass'

    log.info('Clean the error log')
    m2.deleteErrorLogs()

    log.info('Set replication loglevel')
    m2.config.loglevel((ErrorLog.REPLICA,))

    log.info('Modifying entry {} - change userpassword on master 1'.format(create_entry.dn))

    create_entry.set('userpassword', TEST_ENTRY_NEW_PASS)

    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.wait_for_replication(m1, m2)

    log.info('Restart the servers to flush the logs')
    for num in range(1, 3):
        topo_m2.ms["master{}".format(num)].restart()

    try:
        log.info('Check that password works on master 2')
        create_entry_m2 = UserAccount(m2, create_entry.dn)
        create_entry_m2.bind(TEST_ENTRY_NEW_PASS)

        log.info('Check the error log for the error with {}'.format(create_entry.dn))
        assert not m2.ds_error_log.match('.*can.t add a change for {}.*'.format(create_entry.dn))
    finally:
        log.info('Set the default loglevel')
        m2.config.loglevel((ErrorLog.DEFAULT,))


def test_invalid_agmt(topo_m2):
    """Test adding that an invalid agreement is properly rejected and does not crash the server

    :id: 6c3b2a7e-edcd-4327-a003-6bd878ff722b
    :setup: Four masters replication setup
    :steps:
        1. Add invalid agreement (nsds5ReplicaEnabled set to invalid value)
        2. Verify the server is still running
    :expectedresults:
        1. Invalid repl agreement should be rejected
        2. Server should be still running
    """

    m1 = topo_m2.ms["master1"]
    m2 = topo_m2.ms["master2"]

    repl = ReplicationManager(DEFAULT_SUFFIX)

    replicas = Replicas(m1)
    replica = replicas.get(DEFAULT_SUFFIX)
    agmts = replica.get_agreements()

    # Add invalid agreement (nsds5ReplicaEnabled set to invalid value)
    with pytest.raises(ldap.UNWILLING_TO_PERFORM):
        agmts.create(properties={
            'cn': 'whatever',
            'nsDS5ReplicaRoot': DEFAULT_SUFFIX,
            'nsDS5ReplicaBindDN': 'cn=replication manager,cn=config',
            'nsDS5ReplicaBindMethod': 'simple',
            'nsDS5ReplicaTransportInfo': 'LDAP',
            'nsds5replicaTimeout': '5',
            'description': "test agreement",
            'nsDS5ReplicaHost': m2.host,
            'nsDS5ReplicaPort': str(m2.port),
            'nsDS5ReplicaCredentials': 'whatever',
            'nsds5ReplicaEnabled': 'YEAH MATE, LETS REPLICATE'
        })

    # Verify the server is still running
    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.test_replication(m1, m2)
    repl.test_replication(m2, m1)


def test_fetch_bindDnGroup(topo_m2):
    """Check the bindDNGroup is fetched on first replication session

    :id: 5f1b1f59-6744-4260-b091-c82d22130025
    :setup: 2 Master Instances
    :steps:
        1. Create a replication bound user and group, but the user *not* member of the group
        2. Check that replication is working
        3. Some preparation is required because of lib389 magic that already define a replication via group
           - define the group as groupDN for replication and 60sec as fetch interval
           - pause RA in both direction
           - Define the user as bindDn of the RAs
        4. restart servers.
            It sets the fetch time to 0, so next session will refetch the group
        5. Before resuming RA, add user to groupDN (on both side as replication is not working at that time)
        6. trigger an update and check replication is working and
           there is no failure logged on supplier side 'does not have permission to supply replication updates to the replica'
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
    """

    # If you need any test suite initialization,
    # please, write additional fixture for that (including finalizer).
    # Topology for suites are predefined in lib389/topologies.py.

    # If you need host, port or any other data about instance,
    # Please, use the instance object attributes for that (for example, topo.ms["master1"].serverid)
    M1 = topo_m2.ms['master1']
    M2 = topo_m2.ms['master2']

    # Enable replication log level. Not really necessary
    M1.modify_s('cn=config', [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', b'8192')])
    M2.modify_s('cn=config', [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', b'8192')])

    # Create a group and a user
    PEOPLE = "ou=People,%s" % SUFFIX
    PASSWD = 'password'
    REPL_MGR_BOUND_DN = 'repl_mgr_bound_dn'

    uid = REPL_MGR_BOUND_DN.encode()
    users = UserAccounts(M1, PEOPLE, rdn=None)
    user_props = TEST_USER_PROPERTIES.copy()
    user_props.update({'uid': uid, 'cn': uid, 'sn': '_%s' % uid, 'userpassword': PASSWD.encode(), 'description': b'value creation'})
    create_user = users.create(properties=user_props)

    groups_M1 = Groups(M1, DEFAULT_SUFFIX)
    group_properties = {
        'cn': 'group1',
        'description': 'testgroup'}
    group_M1 = groups_M1.create(properties=group_properties)
    group_M2 = Group(M2, group_M1.dn)
    assert(not group_M1.is_member(create_user.dn))

    # Check that M1 and M2 are in sync
    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.wait_for_replication(M1, M2, timeout=20)

    # Define the group as the replication manager and fetch interval as 60sec
    replicas = Replicas(M1)
    replica = replicas.list()[0]
    replica.apply_mods([(ldap.MOD_REPLACE, 'nsDS5ReplicaBindDnGroupCheckInterval', '60'),
                        (ldap.MOD_REPLACE, 'nsDS5ReplicaBindDnGroup', group_M1.dn)])

    replicas = Replicas(M2)
    replica = replicas.list()[0]
    replica.apply_mods([(ldap.MOD_REPLACE, 'nsDS5ReplicaBindDnGroupCheckInterval', '60'),
                        (ldap.MOD_REPLACE, 'nsDS5ReplicaBindDnGroup', group_M1.dn)])

    # Then pause the replication agreement to prevent them trying to acquire
    # while the user is not member of the group
    topo_m2.pause_all_replicas()

    # Define the user as the bindDN of the RAs
    for inst in (M1, M2):
        agmts = Agreements(inst)
        agmt = agmts.list()[0]
        agmt.replace('nsDS5ReplicaBindDN', create_user.dn.encode())
        agmt.replace('nsds5ReplicaCredentials', PASSWD.encode())

    # Key step
    # The restart will fetch the group/members define in the replica
    #
    # The user NOT member of the group replication will not work until bindDNcheckInterval
    #
    # With the fix, the first fetch is not taken into account (fetch time=0)
    # so on the first session, the group will be fetched
    M1.restart()
    M2.restart()

    # Replication being broken here we need to directly do the same update.
    # Sorry not found another solution except total update
    group_M1.add_member(create_user.dn)
    group_M2.add_member(create_user.dn)

    topo_m2.resume_all_replicas()

    # trigger updates to be sure to have a replication session, giving some time
    M1.modify_s(create_user.dn, [(ldap.MOD_ADD, 'description', b'value_1_1')])
    M2.modify_s(create_user.dn, [(ldap.MOD_ADD, 'description', b'value_2_2')])
    time.sleep(10)

    # Check replication is working
    ents = M1.search_s(create_user.dn, ldap.SCOPE_BASE, '(objectclass=*)')
    for ent in ents:
        assert (ent.hasAttr('description'))
        found = 0
        for val in ent.getValues('description'):
            if (val == b'value_1_1'):
                found = found + 1
            elif (val == b'value_2_2'):
                found = found + 1
        assert (found == 2)

    ents = M2.search_s(create_user.dn, ldap.SCOPE_BASE, '(objectclass=*)')
    for ent in ents:
        assert (ent.hasAttr('description'))
        found = 0
        for val in ent.getValues('description'):
            if (val == b'value_1_1'):
                found = found + 1
            elif (val == b'value_2_2'):
                found = found + 1
        assert (found == 2)

    # Check in the logs that the member was detected in the group although
    # at startup it was not member of the group
    regex = re.compile("does not have permission to supply replication updates to the replica.")
    errorlog_M1 = open(M1.errlog, "r")
    errorlog_M2 = open(M1.errlog, "r")

    # Find the last restart position
    restart_location_M1 = find_start_location(errorlog_M1, 2)
    assert (restart_location_M1 != -1)
    restart_location_M2 = find_start_location(errorlog_M2, 2)
    assert (restart_location_M2 != -1)

    # Then check there is no failure to authenticate
    count = pattern_errorlog(errorlog_M1, regex, start_location=restart_location_M1)
    assert(count <= 1)
    count = pattern_errorlog(errorlog_M2, regex, start_location=restart_location_M2)
    assert(count <= 1)


def test_plugin_bind_dn_tracking_and_replication(topo_m2):
    """Testing nsslapd-plugin-binddn-tracking does not cause issues around
        access control and reconfiguring replication/repl agmt.

    :id: dd689d03-69b8-4bf9-a06e-2acd19d5e2c9
    :setup: 2 master topology
    :steps:
        1. Turn on plugin binddn tracking
        2. Add some users
        3. Make an update as a user
        4. Make an update to the replica config
        5. Make an update to the repliocation agreement
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
    """

    m1 = topo_m2.ms["master1"]

    # Turn on bind dn tracking
    m1.config.set('nsslapd-plugin-binddn-tracking', 'on')

    # Add two users
    users = UserAccounts(m1, DEFAULT_SUFFIX)
    user1 = users.create_test_user(uid=1011)
    user1.set('userpassword', PASSWORD)
    user2 = users.create_test_user(uid=1012)

    # Add an aci
    acival = '(targetattr ="cn")(version 3.0;acl "Test bind dn tracking"' + \
             ';allow (all) (userdn = "ldap:///{}");)'.format(user1.dn)
    Domain(m1, DEFAULT_SUFFIX).add('aci', acival)

    # Bind as user and make an update
    user1.rebind(PASSWORD)
    user2.set('cn', 'new value')
    dm = DirectoryManager(m1)
    dm.rebind()

    # modify replica
    replica = Replicas(m1).get(DEFAULT_SUFFIX)
    replica.set(REPL_PROTOCOL_TIMEOUT, "30")

    # modify repl agmt
    agmt = replica.get_agreements().list()[0]
    agmt.set(REPL_PROTOCOL_TIMEOUT, "20")


def test_cleanallruv_repl(topo_m3):
    """Test that cleanallruv could not break replication if anchor csn in ruv originated in deleted replica

    :id: 46faba9a-897e-45b8-98dc-aec7fa8cec9a
    :setup: 3 Masters
    :steps:
        1. Configure error log level to 8192 in all masters
        2. Modify nsslapd-changelogmaxage=30 and nsslapd-changelogtrim-interval=5 for M1 and M2
        3. Add test users to 3 masters
        4. Launch ClearRuv but withForce
        5. Check the users after CleanRUV, because of changelog trimming, it will effect the CLs
    :expectedresults:
        1. Error logs should be configured successfully
        2. Modify should be successful
        3. Test users should be added successfully
        4. ClearRuv should be launched successfully
        5. Users should be present according to the changelog trimming effect
    """

    M1 = topo_m3.ms["master1"]
    M2 = topo_m3.ms["master2"]
    M3 = topo_m3.ms["master3"]

    log.info("Change the error log levels for all masters")
    for s in (M1, M2, M3):
        s.config.replace('nsslapd-errorlog-level', "8192")

    log.info("Get the replication agreements for all 3 masters")
    m1_m2 = M1.agreement.list(suffix=SUFFIX, consumer_host=M2.host, consumer_port=M2.port)
    m1_m3 = M1.agreement.list(suffix=SUFFIX, consumer_host=M3.host, consumer_port=M3.port)
    m3_m1 = M3.agreement.list(suffix=SUFFIX, consumer_host=M1.host, consumer_port=M1.port)

    log.info("Get the changelog enteries for M1 and M2")
    changelog_m1 = Changelog5(M1)
    changelog_m2 = Changelog5(M2)

    log.info("Modify nsslapd-changelogmaxage=30 and nsslapd-changelogtrim-interval=5 for M1 and M2")
    changelog_m1.set_max_age(MAXAGE_STR)
    changelog_m1.set_trim_interval(TRIMINTERVAL_STR)

    log.info("Add test users to 3 masters")
    users_m1 = UserAccounts(M1, DEFAULT_SUFFIX)
    users_m2 = UserAccounts(M2, DEFAULT_SUFFIX)
    users_m3 = UserAccounts(M3, DEFAULT_SUFFIX)
    user_props = TEST_USER_PROPERTIES.copy()

    user_props.update({'uid': "testuser10"})
    user10 = users_m1.create(properties=user_props)

    user_props.update({'uid': "testuser20"})
    user20 = users_m2.create(properties=user_props)

    user_props.update({'uid': "testuser30"})
    user30 = users_m3.create(properties=user_props)

    # ::important:: the testuser31 is the oldest csn in M2,
    # because it will be cleared by changelog trimming
    user_props.update({'uid': "testuser31"})
    user31 = users_m3.create(properties=user_props)

    user_props.update({'uid': "testuser11"})
    user11 = users_m1.create(properties=user_props)

    user_props.update({'uid': "testuser21"})
    user21 = users_m2.create(properties=user_props)
    # this is to trigger changelog trim and interval values
    time.sleep(40)

    # Here M1, M2, M3 should have 11,21,31 and 10,20,30 are CL cleared
    M2.stop()
    M1.agreement.pause(m1_m2[0].dn)
    user_props.update({'uid': "testuser32"})
    user32 = users_m3.create(properties=user_props)

    user_props.update({'uid': "testuser33"})
    user33 = users_m3.create(properties=user_props)

    user_props.update({'uid': "testuser12"})
    user12 = users_m1.create(properties=user_props)

    M3.agreement.pause(m3_m1[0].dn)
    M3.agreement.resume(m3_m1[0].dn)
    time.sleep(40)

    # Here because of changelog trimming testusers 31 and 32 are CL cleared
    # ClearRuv is launched but with Force
    M3.stop()
    M1.tasks.cleanAllRUV(suffix=SUFFIX, replicaid='3',
                         force=True, args={TASK_WAIT: False})

    # here M1 should clear 31
    M2.start()
    M1.agreement.pause(m1_m2[0].dn)
    M1.agreement.resume(m1_m2[0].dn)
    time.sleep(10)

    # Check the users after CleanRUV
    expected_m1_users = [user31.dn, user11.dn, user21.dn, user32.dn, user33.dn, user12.dn]
    expected_m1_users = [x.lower() for x in expected_m1_users]
    expected_m2_users = [user31.dn, user11.dn, user21.dn, user12.dn]
    expected_m2_users = [x.lower() for x in expected_m2_users]

    current_m1_users = [user.dn for user in users_m1.list()]
    current_m1_users = [x.lower() for x in current_m1_users]
    current_m2_users = [user.dn for user in users_m2.list()]
    current_m2_users = [x.lower() for x in current_m2_users]

    assert set(expected_m1_users).issubset(current_m1_users)
    assert set(expected_m2_users).issubset(current_m2_users)


@pytest.mark.ds49915
@pytest.mark.bz1626375
def test_online_reinit_may_hang(topo_with_sigkill):
    """Online reinitialization may hang when the first
       entry of the DB is RUV entry instead of the suffix

    :id: cded6afa-66c0-4c65-9651-993ba3f7a49c
    :setup: 2 Master Instances
    :steps:
        1. Export the database
        2. Move RUV entry to the top in the ldif file
        3. Import the ldif file
        4. Online replica initializaton
    :expectedresults:
        1. Ldif file should be created successfully
        2. RUV entry should be on top in the ldif file
        3. Import should be successful
        4. Server should not hang and consume 100% CPU
    """
    M1 = topo_with_sigkill.ms["master1"]
    M2 = topo_with_sigkill.ms["master2"]
    M1.stop()
    ldif_file = '%s/master1.ldif' % M1.get_ldif_dir()
    M1.db2ldif(bename=DEFAULT_BENAME, suffixes=[DEFAULT_SUFFIX],
               excludeSuffixes=None, repl_data=True,
               outputfile=ldif_file, encrypt=False)
    _move_ruv(ldif_file)
    M1.ldif2db(DEFAULT_BENAME, None, None, None, ldif_file)
    M1.start()
    # After this server may hang
    agmt = Agreements(M1).list()[0]
    agmt.begin_reinit()
    (done, error) = agmt.wait_reinit()
    assert done is True
    assert error is False
    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.test_replication_topology(topo_with_sigkill)

    if DEBUGGING:
        # Add debugging steps(if any)...
        pass


@pytest.mark.bz1314956
@pytest.mark.ds48755
def test_moving_entry_make_online_init_fail(topology_m2):
    """
    Moving an entry could make the online init fail

    :id: e3895be7-884a-4e9f-80e3-24e9a5167c9e
    :setup: Two masters replication setup
    :steps:
         1. Generate DIT_0
         2. Generate password policy for DIT_0
         3. Create users for DIT_0
         4. Turn idx % 2 == 0 users into tombstones
         5. Generate DIT_1
         6. Move 'ou=OU0,ou=OU0,dc=example,dc=com' to DIT_1
         7. Move 'ou=OU0,dc=example,dc=com' to DIT_1
         8. Move idx % 2 == 1 users to 'ou=OU0,ou=OU0,ou=OU1,dc=example,dc=com'
         9. Init replicas
         10. Number of entries should match on both masters

    :expectedresults:
         1. Success
         2. Success
         3. Success
         4. Success
         5. Success
         6. Success
         7. Success
         8. Success
         9. Success
         10. Success
    """

    M1 = topology_m2.ms["master1"]
    M2 = topology_m2.ms["master2"]

    log.info("Generating DIT_0")
    idx = 0
    add_ou_entry(M1, idx, DEFAULT_SUFFIX)
    log.info("Created entry: ou=OU0, dc=example, dc=com")

    ou0 = 'ou=OU%d' % idx
    first_parent = '%s,%s' % (ou0, DEFAULT_SUFFIX)
    add_ou_entry(M1, idx, first_parent)
    log.info("Created entry: ou=OU0, ou=OU0, dc=example, dc=com")

    add_ldapsubentry(M1, first_parent)

    ou_name = 'ou=OU%d,ou=OU%d' % (idx, idx)
    second_parent = 'ou=OU%d,%s' % (idx, first_parent)
    for idx in range(0, 9):
        add_user_entry(M1, idx, ou_name)
        if idx % 2 == 0:
            log.info("Turning tuser%d into a tombstone entry" % idx)
            del_user_entry(M1, idx, ou_name)

    log.info('%s => %s => %s => 10 USERS' % (DEFAULT_SUFFIX, first_parent, second_parent))

    log.info("Generating DIT_1")
    idx = 1
    add_ou_entry(M1, idx, DEFAULT_SUFFIX)
    log.info("Created entry: ou=OU1,dc=example,dc=com")

    third_parent = 'ou=OU%d,%s' % (idx, DEFAULT_SUFFIX)
    add_ou_entry(M1, idx, third_parent)
    log.info("Created entry: ou=OU1, ou=OU1, dc=example, dc=com")

    add_ldapsubentry(M1, third_parent)

    log.info("Moving %s to DIT_1" % second_parent)
    OrganizationalUnits(M1, second_parent).get('OU0').rename(ou0, newsuperior=third_parent)

    log.info("Moving %s to DIT_1" % first_parent)
    fourth_parent = '%s,%s' % (ou0, third_parent)
    OrganizationalUnits(M1, first_parent).get('OU0').rename(ou0, newsuperior=fourth_parent)

    fifth_parent = '%s,%s' % (ou0, fourth_parent)

    ou_name = 'ou=OU0,ou=OU1'
    log.info("Moving USERS to %s" % fifth_parent)
    for idx in range(0, 9):
        if idx % 2 == 1:
            rename_entry(M1, idx, ou_name, fifth_parent)

    log.info('%s => %s => %s => %s => 10 USERS' % (DEFAULT_SUFFIX, third_parent, fourth_parent, fifth_parent))

    log.info("Run Initialization.")
    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.wait_for_replication(M1, M2, timeout=5)

    m1entries = M1.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE,
                            '(|(objectclass=ldapsubentry)(objectclass=nstombstone)(nsuniqueid=*))')
    m2entries = M2.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE,
                            '(|(objectclass=ldapsubentry)(objectclass=nstombstone)(nsuniqueid=*))')

    log.info("m1entry count - %d", len(m1entries))
    log.info("m2entry count - %d", len(m2entries))

    assert len(m1entries) == len(m2entries)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
