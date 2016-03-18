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

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

installation1_prefix = None

PEOPLE_OU='people'
PEOPLE_DN = "ou=%s,%s" % (PEOPLE_OU, SUFFIX)
MAX_ACCOUNTS=5

class TopologyReplication(object):
    def __init__(self, master1, master2, master3):
        master1.open()
        self.master1 = master1
        master2.open()
        self.master2 = master2
        master3.open()
        self.master3 = master3


@pytest.fixture(scope="module")
def topology(request):
    global installation1_prefix
    if installation1_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation1_prefix

    # Creating master 1...
    master1 = DirSrv(verbose=False)
    if installation1_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation1_prefix
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
    master2 = DirSrv(verbose=False)
    if installation1_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation1_prefix
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

    # Creating master 3...
    master3 = DirSrv(verbose=False)
    if installation1_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation1_prefix
    args_instance[SER_HOST] = HOST_MASTER_3
    args_instance[SER_PORT] = PORT_MASTER_3
    args_instance[SER_SERVERID_PROP] = SERVERID_MASTER_3
    args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
    args_master = args_instance.copy()
    master3.allocate(args_master)
    instance_master3 = master3.exists()
    if instance_master3:
        master3.delete()
    master3.create()
    master3.open()
    master3.replica.enableReplication(suffix=SUFFIX, role=REPLICAROLE_MASTER, replicaId=REPLICAID_MASTER_3)

    #
    # Create all the agreements
    #
    # Creating agreement from master 1 to master 2
    properties = {RA_BINDDN:    defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW:    defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD:    defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m1_m2_agmt = master1.agreement.create(suffix=SUFFIX, host=master2.host, port=master2.port, properties=properties)
    if not m1_m2_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m1_m2_agmt)

    # Creating agreement from master 1 to master 3
#     properties = {RA_NAME:      r'meTo_$host:$port',
#                   RA_BINDDN:    defaultProperties[REPLICATION_BIND_DN],
#                   RA_BINDPW:    defaultProperties[REPLICATION_BIND_PW],
#                   RA_METHOD:    defaultProperties[REPLICATION_BIND_METHOD],
#                   RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
#     m1_m3_agmt = master1.agreement.create(suffix=SUFFIX, host=master3.host, port=master3.port, properties=properties)
#     if not m1_m3_agmt:
#         log.fatal("Fail to create a master -> master replica agreement")
#         sys.exit(1)
#     log.debug("%s created" % m1_m3_agmt)

    # Creating agreement from master 2 to master 1
    properties = {RA_BINDDN:    defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW:    defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD:    defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m2_m1_agmt = master2.agreement.create(suffix=SUFFIX, host=master1.host, port=master1.port, properties=properties)
    if not m2_m1_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m2_m1_agmt)

    # Creating agreement from master 2 to master 3
    properties = {RA_BINDDN:    defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW:    defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD:    defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m2_m3_agmt = master2.agreement.create(suffix=SUFFIX, host=master3.host, port=master3.port, properties=properties)
    if not m2_m3_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m2_m3_agmt)

    # Creating agreement from master 3 to master 1
#     properties = {RA_NAME:      r'meTo_$host:$port',
#                   RA_BINDDN:    defaultProperties[REPLICATION_BIND_DN],
#                   RA_BINDPW:    defaultProperties[REPLICATION_BIND_PW],
#                   RA_METHOD:    defaultProperties[REPLICATION_BIND_METHOD],
#                   RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
#     m3_m1_agmt = master3.agreement.create(suffix=SUFFIX, host=master1.host, port=master1.port, properties=properties)
#     if not m3_m1_agmt:
#         log.fatal("Fail to create a master -> master replica agreement")
#         sys.exit(1)
#     log.debug("%s created" % m3_m1_agmt)

    # Creating agreement from master 3 to master 2
    properties = {RA_BINDDN:    defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW:    defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD:    defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m3_m2_agmt = master3.agreement.create(suffix=SUFFIX, host=master2.host, port=master2.port, properties=properties)
    if not m3_m2_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m3_m2_agmt)

    # Allow the replicas to get situated with the new agreements...
    time.sleep(5)

    #
    # Initialize all the agreements
    #
    master1.agreement.init(SUFFIX, HOST_MASTER_2, PORT_MASTER_2)
    master1.waitForReplInit(m1_m2_agmt)
    time.sleep(5)  # just to be safe
    master2.agreement.init(SUFFIX, HOST_MASTER_3, PORT_MASTER_3)
    master2.waitForReplInit(m2_m3_agmt)

    # Check replication is working...
    if master1.testReplication(DEFAULT_SUFFIX, master2):
        log.info('Replication is working.')
    else:
        log.fatal('Replication is not working.')
        assert False

    # Delete each instance in the end
    def fin():
        for master in (master1, master2, master3):
        #    master.db2ldif(bename=DEFAULT_BENAME, suffixes=[DEFAULT_SUFFIX], excludeSuffixes=[], encrypt=False, \
        #        repl_data=True, outputfile='%s/ldif/%s.ldif' % (master.dbdir,SERVERID_STANDALONE ))
        #    master.clearBackupFS()
        #    master.backupFS()
            master.delete()
    request.addfinalizer(fin)

    # Clear out the tmp dir
    master1.clearTmpDir(__file__)

    return TopologyReplication(master1, master2, master3)

def _dna_config(server, nextValue=500, maxValue=510):
    log.info("Add dna plugin config entry...%s" % server)

    try:
        server.add_s(Entry(('cn=dna config,cn=Distributed Numeric Assignment Plugin,cn=plugins,cn=config', {
                                         'objectclass': 'top dnaPluginConfig'.split(),
                                         'dnaType': 'description',
                                         'dnaMagicRegen': '-1',
                                         'dnaFilter': '(objectclass=posixAccount)',
                                         'dnaScope': 'ou=people,%s' % SUFFIX,
                                         'dnaNextValue': str(nextValue),
                                         'dnaMaxValue' : str(nextValue+maxValue),
                                         'dnaSharedCfgDN': 'ou=ranges,%s' % SUFFIX
                                         })))

    except ldap.LDAPError as e:
        log.error('Failed to add DNA config entry: error ' + e.message['desc'])
        assert False

    log.info("Enable the DNA plugin...")
    try:
        server.plugins.enable(name=PLUGIN_DNA)
    except e:
        log.error("Failed to enable DNA Plugin: error " + e.message['desc'])
        assert False

    log.info("Restarting the server...")
    server.stop(timeout=120)
    time.sleep(1)
    server.start(timeout=120)
    time.sleep(3)

def test_ticket4026(topology):
    """Write your replication testcase here.

    To access each DirSrv instance use:  topology.master1, topology.master2,
        ..., topology.hub1, ..., topology.consumer1, ...

    Also, if you need any testcase initialization,
    please, write additional fixture for that(include finalizer).
    """

    try:
        topology.master1.add_s(Entry((PEOPLE_DN, {
                                            'objectclass': "top extensibleObject".split(),
                                            'ou': 'people'})))
    except ldap.ALREADY_EXISTS:
        pass
    
    topology.master1.add_s(Entry(('ou=ranges,' + SUFFIX, {
                                     'objectclass': 'top organizationalunit'.split(),
                                     'ou': 'ranges'
                                     })))
    for cpt in range(MAX_ACCOUNTS):
        name = "user%d" % (cpt)
        topology.master1.add_s(Entry(("uid=%s,%s" %(name, PEOPLE_DN), {
                          'objectclass': 'top posixAccount extensibleObject'.split(),
                          'uid': name,
                          'cn': name,
                          'uidNumber': '1',
                          'gidNumber': '1',
                          'homeDirectory': '/home/%s' % name
                          })))
        
    # make master3 having more free slots that master2
    # so master1 will contact master3
    _dna_config(topology.master1, nextValue=100, maxValue=10)
    _dna_config(topology.master2, nextValue=200, maxValue=10)
    _dna_config(topology.master3, nextValue=300, maxValue=3000)

    # Turn on lots of error logging now.
    
    mod = [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', '16384')]
    #mod = [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', '1')]
    topology.master1.modify_s('cn=config', mod)
    topology.master2.modify_s('cn=config', mod)
    topology.master3.modify_s('cn=config', mod)

    # We need to wait for the event in dna.c to fire to start the servers
    # see dna.c line 899
    time.sleep(60)
    
    # add on master1 users with description DNA
    for cpt in range(10):
        name = "user_with_desc1_%d" % (cpt)
        topology.master1.add_s(Entry(("uid=%s,%s" %(name, PEOPLE_DN), {
                          'objectclass': 'top posixAccount extensibleObject'.split(),
                          'uid': name,
                          'cn': name,
                          'description' : '-1',
                          'uidNumber': '1',
                          'gidNumber': '1',
                          'homeDirectory': '/home/%s' % name
                          })))
    # give time to negociate master1 <--> master3
    time.sleep(10)
        # add on master1 users with description DNA
    for cpt in range(11,20):
        name = "user_with_desc1_%d" % (cpt)
        topology.master1.add_s(Entry(("uid=%s,%s" %(name, PEOPLE_DN), {
                          'objectclass': 'top posixAccount extensibleObject'.split(),
                          'uid': name,
                          'cn': name,
                          'description' : '-1',
                          'uidNumber': '1',
                          'gidNumber': '1',
                          'homeDirectory': '/home/%s' % name
                          })))
    log.info('Test complete')
    # add on master1 users with description DNA
    mod = [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', '16384')]
    #mod = [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', '1')]
    topology.master1.modify_s('cn=config', mod)
    topology.master2.modify_s('cn=config', mod)
    topology.master3.modify_s('cn=config', mod)

    log.info('Test complete')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
#     global installation1_prefix
#     installation1_prefix=None
#     topo = topology(True)
#     test_ticket4026(topo)
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
