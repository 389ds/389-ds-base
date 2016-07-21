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

PEOPLE_OU='people'
PEOPLE_DN = "ou=%s,%s" % (PEOPLE_OU, SUFFIX)
MAX_ACCOUNTS=5

BINDMETHOD_ATTR  = 'dnaRemoteBindMethod'
BINDMETHOD_VALUE = "SASL/GSSAPI"
PROTOCOLE_ATTR   = 'dnaRemoteConnProtocol'
PROTOCOLE_VALUE  = 'LDAP'

class TopologyReplication(object):
    def __init__(self, master1, master2):
        master1.open()
        self.master1 = master1
        master2.open()
        self.master2 = master2


@pytest.fixture(scope="module")
def topology(request):

    # Creating master 1...
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
    properties = {RA_NAME:      r'meTo_$host:$port',
                  RA_BINDDN:    defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW:    defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD:    defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    m1_m2_agmt = master1.agreement.create(suffix=SUFFIX, host=master2.host, port=master2.port, properties=properties)
    if not m1_m2_agmt:
        log.fatal("Fail to create a master -> master replica agreement")
        sys.exit(1)
    log.debug("%s created" % m1_m2_agmt)

    # Creating agreement from master 2 to master 1
    properties = {RA_NAME:      r'meTo_$host:$port',
                  RA_BINDDN:    defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW:    defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD:    defaultProperties[REPLICATION_BIND_METHOD],
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

    # Delete each instance in the end
    def fin():
        master1.delete()
        master2.delete()
    request.addfinalizer(fin)

    return TopologyReplication(master1, master2)


def _dna_config(server, nextValue=500, maxValue=510):
    log.info("Add dna plugin config entry...%s" % server)

    cfg_base_dn = 'cn=dna config,cn=Distributed Numeric Assignment Plugin,cn=plugins,cn=config'

    try:
        server.add_s(Entry((cfg_base_dn, {
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


SHARE_CFG_BASE = 'ou=ranges,' + SUFFIX

def _wait_shared_cfg_servers(server, expected):
    attempts = 0
    ents = []
    try:
        ents = server.search_s(SHARE_CFG_BASE, ldap.SCOPE_ONELEVEL, "(objectclass=*)")
    except ldap.NO_SUCH_OBJECT:
        pass
    except lib389.NoSuchEntryError:
        pass
    while (len(ents) != expected):
        assert attempts < 10
        time.sleep(5)
        try:
            ents = server.search_s(SHARE_CFG_BASE, ldap.SCOPE_ONELEVEL, "(objectclass=*)")
        except ldap.NO_SUCH_OBJECT:
            pass
        except lib389.NoSuchEntryError:
            pass

def _shared_cfg_server_update(server, method=BINDMETHOD_VALUE, transport=PROTOCOLE_VALUE):
    log.info('\n======================== Update dnaPortNum=%d ============================\n'% server.port)
    try:
        ent = server.getEntry(SHARE_CFG_BASE, ldap.SCOPE_ONELEVEL, "(dnaPortNum=%d)" % server.port)
        mod = [(ldap.MOD_REPLACE, BINDMETHOD_ATTR, method),
               (ldap.MOD_REPLACE, PROTOCOLE_ATTR, transport)]
        server.modify_s(ent.dn, mod)

        log.info('\n======================== Update done\n')
        ent = server.getEntry(SHARE_CFG_BASE, ldap.SCOPE_ONELEVEL, "(dnaPortNum=%d)" % server.port)
    except ldap.NO_SUCH_OBJECT:
        log.fatal("Unknown host")
        assert False


def test_ticket48362(topology):
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

    topology.master1.add_s(Entry((SHARE_CFG_BASE, {
                                     'objectclass': 'top organizationalunit'.split(),
                                     'ou': 'ranges'
                                     })))
    # master 1 will have a valid remaining range (i.e. 101)
    # master 2 will not have a valid remaining range (i.e. 0) so dna servers list on master2
    # will not contain master 2. So at restart, master 2 is recreated without the method/protocol attribute
    _dna_config(topology.master1, nextValue=1000, maxValue=100)
    _dna_config(topology.master2, nextValue=2000, maxValue=-1)

    # check we have all the servers available
    _wait_shared_cfg_servers(topology.master1, 2)
    _wait_shared_cfg_servers(topology.master2, 2)

    # now force the method/transport on the servers entry
    _shared_cfg_server_update(topology.master1)
    _shared_cfg_server_update(topology.master2)



    log.info('\n======================== BEFORE RESTART ============================\n')
    ent = topology.master1.getEntry(SHARE_CFG_BASE, ldap.SCOPE_ONELEVEL, "(dnaPortNum=%d)" % topology.master1.port)
    log.info('\n======================== BEFORE RESTART ============================\n')
    assert(ent.hasAttr(BINDMETHOD_ATTR) and ent.getValue(BINDMETHOD_ATTR) == BINDMETHOD_VALUE)
    assert(ent.hasAttr(PROTOCOLE_ATTR) and ent.getValue(PROTOCOLE_ATTR) == PROTOCOLE_VALUE)


    ent = topology.master2.getEntry(SHARE_CFG_BASE, ldap.SCOPE_ONELEVEL, "(dnaPortNum=%d)" % topology.master2.port)
    log.info('\n======================== BEFORE RESTART ============================\n')
    assert(ent.hasAttr(BINDMETHOD_ATTR) and ent.getValue(BINDMETHOD_ATTR) == BINDMETHOD_VALUE)
    assert(ent.hasAttr(PROTOCOLE_ATTR) and ent.getValue(PROTOCOLE_ATTR) == PROTOCOLE_VALUE)
    topology.master1.restart(10)
    topology.master2.restart(10)

    # to allow DNA plugin to recreate the local host entry
    time.sleep(40)

    log.info('\n=================== AFTER RESTART =================================\n')
    ent = topology.master1.getEntry(SHARE_CFG_BASE, ldap.SCOPE_ONELEVEL, "(dnaPortNum=%d)" % topology.master1.port)
    log.info('\n=================== AFTER RESTART =================================\n')
    assert(ent.hasAttr(BINDMETHOD_ATTR) and ent.getValue(BINDMETHOD_ATTR) == BINDMETHOD_VALUE)
    assert(ent.hasAttr(PROTOCOLE_ATTR) and ent.getValue(PROTOCOLE_ATTR) == PROTOCOLE_VALUE)

    ent = topology.master2.getEntry(SHARE_CFG_BASE, ldap.SCOPE_ONELEVEL, "(dnaPortNum=%d)" % topology.master2.port)
    log.info('\n=================== AFTER RESTART =================================\n')
    assert(ent.hasAttr(BINDMETHOD_ATTR) and ent.getValue(BINDMETHOD_ATTR) == BINDMETHOD_VALUE)
    assert(ent.hasAttr(PROTOCOLE_ATTR) and ent.getValue(PROTOCOLE_ATTR) == PROTOCOLE_VALUE)
    log.info('Test complete')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
