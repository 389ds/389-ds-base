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

if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)


log = logging.getLogger(__name__)


class TopologyReplication(object):
    """The Replication Topology Class"""
    def __init__(self, master1, master2):
        """Init"""
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

    #
    # Create all the agreements
    #
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

    def fin():
        """If we are debugging just stop the instances, otherwise remove
        them
        """
        if DEBUGGING:
            master1.stop()
            master2.stop()
        else:
            master1.delete()
            master2.delete()

    request.addfinalizer(fin)

    # Clear out the tmp dir
    master1.clearTmpDir(__file__)

    return TopologyReplication(master1, master2)


def _create_user(inst, idnum):
    inst.add_s(Entry(
        ('uid=user%s,ou=People,%s' % (idnum, DEFAULT_SUFFIX), {
            'objectClass' : 'top account posixAccount'.split(' '),
            'cn' : 'user',
            'uid' : 'user%s' % idnum,
            'homeDirectory' : '/home/user%s' % idnum,
            'loginShell' : '/bin/nologin',
            'gidNumber' : '-1',
            'uidNumber' : '-1',
        })
    ))


def test_ticket48916(topology):
    """
    https://bugzilla.redhat.com/show_bug.cgi?id=1353629

    This is an issue with ID exhaustion in DNA causing a crash.

    To access each DirSrv instance use:  topology.master1, topology.master2,
        ..., topology.hub1, ..., topology.consumer1,...


    """

    if DEBUGGING:
        # Add debugging steps(if any)...
        pass

    # Enable the plugin on both servers

    dna_m1 = topology.master1.plugins.get('Distributed Numeric Assignment Plugin')
    dna_m2 = topology.master2.plugins.get('Distributed Numeric Assignment Plugin')

    # Configure it
    # Create the container for the ranges to go into.

    topology.master1.add_s(Entry(
        ('ou=Ranges,%s' % DEFAULT_SUFFIX, {
            'objectClass' : 'top organizationalUnit'.split(' '),
            'ou' : 'Ranges',
        })
    ))

    # Create the dnaAdmin?

    # For now we just pinch the dn from the dna_m* types, and add the relevant child config
    # but in the future, this could be a better plugin template type from lib389

    config_dn = dna_m1.dn

    topology.master1.add_s(Entry(
        ('cn=uids,%s' % config_dn, {
            'objectClass' : 'top dnaPluginConfig'.split(' '),
            'cn': 'uids',
            'dnatype': 'uidNumber gidNumber'.split(' '),
            'dnafilter': '(objectclass=posixAccount)',
            'dnascope': '%s' % DEFAULT_SUFFIX,
            'dnaNextValue': '1',
            'dnaMaxValue': '50',
            'dnasharedcfgdn': 'ou=Ranges,%s' % DEFAULT_SUFFIX,
            'dnaThreshold': '0',
            'dnaRangeRequestTimeout': '60',
            'dnaMagicRegen': '-1',
            'dnaRemoteBindDN': 'uid=dnaAdmin,ou=People,%s' % DEFAULT_SUFFIX,
            'dnaRemoteBindCred': 'secret123',
            'dnaNextRange': '80-90'
        })
    ))

    topology.master2.add_s(Entry(
        ('cn=uids,%s' % config_dn, {
            'objectClass' : 'top dnaPluginConfig'.split(' '),
            'cn': 'uids',
            'dnatype': 'uidNumber gidNumber'.split(' '),
            'dnafilter': '(objectclass=posixAccount)',
            'dnascope': '%s' % DEFAULT_SUFFIX,
            'dnaNextValue': '61',
            'dnaMaxValue': '70',
            'dnasharedcfgdn': 'ou=Ranges,%s' % DEFAULT_SUFFIX,
            'dnaThreshold': '2',
            'dnaRangeRequestTimeout': '60',
            'dnaMagicRegen': '-1',
            'dnaRemoteBindDN': 'uid=dnaAdmin,ou=People,%s' % DEFAULT_SUFFIX,
            'dnaRemoteBindCred': 'secret123',
        })
    ))

    # Enable the plugins
    dna_m1.enable()
    dna_m2.enable()

    # Restart the instances
    topology.master1.restart(60)
    topology.master2.restart(60)

    # Wait for a replication .....
    time.sleep(40)

    # Allocate the 10 members to exhaust

    for i in range(1, 11):
        _create_user(topology.master2, i)

    # Allocate the 11th
    _create_user(topology.master2, 11)

    log.info('Test PASSED')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

