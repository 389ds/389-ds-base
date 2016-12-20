import os
import sys
import time
import ldap
import logging
import pytest
from lib389 import DirSrv, Entry, tools, tasks
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from lib389.utils import *

DEBUGGING = False
GROUP_DN = ("cn=group," + DEFAULT_SUFFIX)

if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


class TopologyReplication(object):
    def __init__(self, master1, master2):
        master1.open()
        self.master1 = master1
        master2.open()
        self.master2 = master2


@pytest.fixture(scope="module")
def topology(request):
    """Create Replication Deployment"""

    # Creating master 1...
    if DEBUGGING:
        master1 = DirSrv(verbose=True)
    else:
        master1 = DirSrv(verbose=False)
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
    if DEBUGGING:
        master2 = DirSrv(verbose=True)
    else:
        master2 = DirSrv(verbose=False)
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

    def fin():
        """If we are debugging just stop the instances,
        otherwise remove them
        """

        if DEBUGGING:
            master1.stop()
            master2.stop()
        else:
            #master1.delete()
            #master2.delete()
            pass

    request.addfinalizer(fin)

    # Create all the agreements

    # Creating agreement from master 1 to master 2
    properties = {RA_NAME: 'meTo_' + master2.host + ':' + str(master2.port),
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m1_m2_agmt = master1.agreement.create(suffix=SUFFIX, host=master2.host, port=master2.port, properties=properties)
    if not m1_m2_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m1_m2_agmt)

    # Creating agreement from master 2 to master 1
    properties = {RA_NAME: 'meTo_' + master1.host + ':' + str(master1.port),
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m2_m1_agmt = master2.agreement.create(suffix=SUFFIX, host=master1.host, port=master1.port, properties=properties)
    if not m2_m1_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m2_m1_agmt)

    # Allow the replicas to get situated with the new agreements...
    time.sleep(5)

    # Initialize all the agreements
    master1.agreement.init(SUFFIX, HOST_MASTER_2, PORT_MASTER_2)
    master1.waitForReplInit(m1_m2_agmt)

    # Check replication is working...
    if master1.testReplication(DEFAULT_SUFFIX, master2):
        log.info('Replication is working.')
    else:
        log.fatal('Replication is not working.')
        assert False

    # Clear out the tmp dir
    master1.clearTmpDir(__file__)

    return TopologyReplication(master1, master2)

def _add_group_with_members(topology):
    # Create group
    try:
        topology.master1.add_s(Entry((GROUP_DN,
                                         {'objectclass': 'top groupofnames'.split(),
                                          'cn': 'group'})))
    except ldap.LDAPError as e:
        log.fatal('Failed to add group: error ' + e.message['desc'])
        assert False

    # Add members to the group - set timeout
    log.info('Adding members to the group...')
    for idx in range(1, 5):
        try:
            MEMBER_VAL = ("uid=member%d,%s" % (idx, DEFAULT_SUFFIX))
            topology.master1.modify_s(GROUP_DN,
                                         [(ldap.MOD_ADD,
                                           'member',
                                           MEMBER_VAL)])
        except ldap.LDAPError as e:
            log.fatal('Failed to update group: member (%s) - error: %s' %
                      (MEMBER_VAL, e.message['desc']))
            assert False

def _check_memberof(master, presence_flag):
    # Check that members have memberof attribute on M1
    for idx in range(1, 5):
        try:
            USER_DN = ("uid=member%d,%s" % (idx, DEFAULT_SUFFIX))
            ent = master.getEntry(USER_DN, ldap.SCOPE_BASE, "(objectclass=*)")
            if presence_flag:
                    assert ent.hasAttr('memberof') and ent.getValue('memberof') == GROUP_DN
            else:
                    assert not ent.hasAttr('memberof')
        except ldap.LDAPError as e:
            log.fatal('Failed to retrieve user (%s): error %s' % (USER_DN, e.message['desc']))
            assert False

def _check_entry_exist(master, dn):
    attempt = 0
    while attempt <= 10:
        try:
            dn
            ent = master.getEntry(dn, ldap.SCOPE_BASE, "(objectclass=*)")
            break
        except ldap.NO_SUCH_OBJECT:
            attempt = attempt + 1
            time.sleep(1)
        except ldap.LDAPError as e:
            log.fatal('Failed to retrieve user (%s): error %s' % (dn, e.message['desc']))
            assert False
    assert attempt != 10

def test_ticket49073(topology):
    """Write your replication test here.

    To access each DirSrv instance use:  topology.master1, topology.master2,
        ..., topology.hub1, ..., topology.consumer1,...

    Also, if you need any testcase initialization,
    please, write additional fixture for that(include finalizer).
    """
    topology.master1.plugins.enable(name=PLUGIN_MEMBER_OF)
    topology.master1.restart(timeout=10)
    topology.master2.plugins.enable(name=PLUGIN_MEMBER_OF)
    topology.master2.restart(timeout=10)

    # Configure fractional to prevent total init to send memberof
    ents = topology.master1.agreement.list(suffix=SUFFIX)
    assert len(ents) == 1
    log.info('update %s to add nsDS5ReplicatedAttributeListTotal' % ents[0].dn)
    topology.master1.modify_s(ents[0].dn,
                                 [(ldap.MOD_REPLACE,
                                   'nsDS5ReplicatedAttributeListTotal',
                                   '(objectclass=*) $ EXCLUDE '),
                                  (ldap.MOD_REPLACE,
                                   'nsDS5ReplicatedAttributeList',
                                   '(objectclass=*) $ EXCLUDE memberOf')])
    topology.master1.restart(timeout=10)

    #
    #  create some users and a group
    #
    log.info('create users and group...')
    for idx in range(1, 5):
        try:
            USER_DN = ("uid=member%d,%s" % (idx, DEFAULT_SUFFIX))
            topology.master1.add_s(Entry((USER_DN,
                                             {'objectclass': 'top extensibleObject'.split(),
                                              'uid': 'member%d' % (idx)})))
        except ldap.LDAPError as e:
            log.fatal('Failed to add user (%s): error %s' % (USER_DN, e.message['desc']))
            assert False

    _check_entry_exist(topology.master2, "uid=member4,%s" % (DEFAULT_SUFFIX))
    _add_group_with_members(topology)
    _check_entry_exist(topology.master2, GROUP_DN)

    # Check that for regular update memberof was on both side (because plugin is enabled both)
    time.sleep(5)
    _check_memberof(topology.master1, True)
    _check_memberof(topology.master2, True)


    # reinit with fractional definition
    ents = topology.master1.agreement.list(suffix=SUFFIX)
    assert len(ents) == 1
    topology.master1.agreement.init(SUFFIX, HOST_MASTER_2, PORT_MASTER_2)
    topology.master1.waitForReplInit(ents[0].dn)

    # Check that for total update  memberof was on both side 
    # because memberof is NOT excluded from total init
    time.sleep(5)
    _check_memberof(topology.master1, True)
    _check_memberof(topology.master2, True)

    if DEBUGGING:
        # Add debugging steps(if any)...
        pass


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

