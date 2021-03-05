import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_m2

from lib389._constants import SUFFIX, DEFAULT_SUFFIX, PLUGIN_DNA

# Skip on older versions
pytestmark = [pytest.mark.tier2,
              pytest.mark.skipif(ds_is_older('1.3.4'), reason="Not implemented")]

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

PEOPLE_OU = 'people'
PEOPLE_DN = "ou=%s,%s" % (PEOPLE_OU, SUFFIX)
MAX_ACCOUNTS = 5

BINDMETHOD_ATTR = 'dnaRemoteBindMethod'
BINDMETHOD_VALUE = b'SASL/GSSAPI'
PROTOCOLE_ATTR = 'dnaRemoteConnProtocol'
PROTOCOLE_VALUE = b'LDAP'

SHARE_CFG_BASE = 'ou=ranges,' + SUFFIX


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
            'dnaMaxValue': str(nextValue + maxValue),
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
    log.info('\n======================== Update dnaPortNum=%d ============================\n' % server.port)
    try:
        ent = server.getEntry(SHARE_CFG_BASE, ldap.SCOPE_ONELEVEL, "(dnaPortNum=%d)" % server.port)
        mod = [(ldap.MOD_REPLACE, BINDMETHOD_ATTR, ensure_bytes(method)),
               (ldap.MOD_REPLACE, PROTOCOLE_ATTR, ensure_bytes(transport))]
        server.modify_s(ent.dn, mod)

        log.info('\n======================== Update done\n')
        ent = server.getEntry(SHARE_CFG_BASE, ldap.SCOPE_ONELEVEL, "(dnaPortNum=%d)" % server.port)
    except ldap.NO_SUCH_OBJECT:
        log.fatal("Unknown host")
        assert False


def test_ticket48362(topology_m2):
    """Write your replication testcase here.

    To access each DirSrv instance use:  topology_m2.ms["supplier1"], topology_m2.ms["supplier2"],
        ..., topology_m2.hub1, ..., topology_m2.consumer1, ...

    Also, if you need any testcase initialization,
    please, write additional fixture for that(include finalizer).
    """

    try:
        topology_m2.ms["supplier1"].add_s(Entry((PEOPLE_DN, {
            'objectclass': "top extensibleObject".split(),
            'ou': 'people'})))
    except ldap.ALREADY_EXISTS:
        pass

    topology_m2.ms["supplier1"].add_s(Entry((SHARE_CFG_BASE, {
        'objectclass': 'top organizationalunit'.split(),
        'ou': 'ranges'
    })))
    # supplier 1 will have a valid remaining range (i.e. 101)
    # supplier 2 will not have a valid remaining range (i.e. 0) so dna servers list on supplier2
    # will not contain supplier 2. So at restart, supplier 2 is recreated without the method/protocol attribute
    _dna_config(topology_m2.ms["supplier1"], nextValue=1000, maxValue=100)
    _dna_config(topology_m2.ms["supplier2"], nextValue=2000, maxValue=-1)

    # check we have all the servers available
    _wait_shared_cfg_servers(topology_m2.ms["supplier1"], 2)
    _wait_shared_cfg_servers(topology_m2.ms["supplier2"], 2)

    # now force the method/transport on the servers entry
    _shared_cfg_server_update(topology_m2.ms["supplier1"])
    _shared_cfg_server_update(topology_m2.ms["supplier2"])

    log.info('\n======================== BEFORE RESTART ============================\n')
    ent = topology_m2.ms["supplier1"].getEntry(SHARE_CFG_BASE, ldap.SCOPE_ONELEVEL,
                                             "(dnaPortNum=%d)" % topology_m2.ms["supplier1"].port)
    log.info('\n======================== BEFORE RESTART ============================\n')
    assert (ent.hasAttr(BINDMETHOD_ATTR) and ent.getValue(BINDMETHOD_ATTR) == BINDMETHOD_VALUE)
    assert (ent.hasAttr(PROTOCOLE_ATTR) and ent.getValue(PROTOCOLE_ATTR) == PROTOCOLE_VALUE)

    ent = topology_m2.ms["supplier2"].getEntry(SHARE_CFG_BASE, ldap.SCOPE_ONELEVEL,
                                             "(dnaPortNum=%d)" % topology_m2.ms["supplier2"].port)
    log.info('\n======================== BEFORE RESTART ============================\n')
    assert (ent.hasAttr(BINDMETHOD_ATTR) and ent.getValue(BINDMETHOD_ATTR) == BINDMETHOD_VALUE)
    assert (ent.hasAttr(PROTOCOLE_ATTR) and ent.getValue(PROTOCOLE_ATTR) == PROTOCOLE_VALUE)
    topology_m2.ms["supplier1"].restart(10)
    topology_m2.ms["supplier2"].restart(10)

    # to allow DNA plugin to recreate the local host entry
    time.sleep(40)

    log.info('\n=================== AFTER RESTART =================================\n')
    ent = topology_m2.ms["supplier1"].getEntry(SHARE_CFG_BASE, ldap.SCOPE_ONELEVEL,
                                             "(dnaPortNum=%d)" % topology_m2.ms["supplier1"].port)
    log.info('\n=================== AFTER RESTART =================================\n')
    assert (ent.hasAttr(BINDMETHOD_ATTR) and ent.getValue(BINDMETHOD_ATTR) == BINDMETHOD_VALUE)
    assert (ent.hasAttr(PROTOCOLE_ATTR) and ent.getValue(PROTOCOLE_ATTR) == PROTOCOLE_VALUE)

    ent = topology_m2.ms["supplier2"].getEntry(SHARE_CFG_BASE, ldap.SCOPE_ONELEVEL,
                                             "(dnaPortNum=%d)" % topology_m2.ms["supplier2"].port)
    log.info('\n=================== AFTER RESTART =================================\n')
    assert (ent.hasAttr(BINDMETHOD_ATTR) and ent.getValue(BINDMETHOD_ATTR) == BINDMETHOD_VALUE)
    assert (ent.hasAttr(PROTOCOLE_ATTR) and ent.getValue(PROTOCOLE_ATTR) == PROTOCOLE_VALUE)
    log.info('Test complete')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
