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

PLUGIN_BITWISE = 'Bitwise Plugin'
TESTBASEDN="dc=bitwise,dc=com"
TESTBACKEND_NAME="TestBitw"

F1 = 'objectclass=testperson'
BITWISE_F2 = '(&(%s)(testUserAccountControl:1.2.840.113556.1.4.803:=514))' % F1
BITWISE_F3 = '(&(%s)(testUserAccountControl:1.2.840.113556.1.4.803:=513))' % F1
BITWISE_F6 = '(&(%s)(testUserAccountControl:1.2.840.113556.1.4.803:=16777216))' % F1

class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):
    global installation1_prefix
    if installation1_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation1_prefix

    # Creating standalone instance ...
    standalone = DirSrv(verbose=False)
    if installation1_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation1_prefix
    args_instance[SER_HOST] = HOST_STANDALONE
    args_instance[SER_PORT] = PORT_STANDALONE
    args_instance[SER_SERVERID_PROP] = SERVERID_STANDALONE
    args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
    args_standalone = args_instance.copy()
    standalone.allocate(args_standalone)
    instance_standalone = standalone.exists()
    if instance_standalone:
        standalone.delete()
    standalone.create()
    standalone.open()

    # Delete each instance in the end
    def fin():
        standalone.delete()
    #request.addfinalizer(fin)

    # Clear out the tmp dir
    standalone.clearTmpDir(__file__)

    return TopologyStandalone(standalone)

def _addBitwiseEntries(topology):

    users = [
        ('testuser2', '65536' ,'PasswordNeverExpired' ),
        ('testuser3', '8388608' ,'PasswordExpired'),
        ('testuser4', '256' ,'TempDuplicateAccount'),
        ('testuser5', '16777216' ,'TrustedAuthDelegation'),
        ('testuser6', '528' ,'AccountLocked'),
        ('testuser7', '513' ,'AccountActive'),
        ('testuser8', '98536 99512 99528'.split() ,'AccountActive PasswordExxpired AccountLocked'.split()),
        ('testuser9', '87536 912'.split() ,'AccountActive PasswordNeverExpired'.split()),
        ('testuser10', '89536 97546 96579'.split() ,'TestVerify1 TestVerify2 TestVerify3'.split() ),
        ('testuser11', '655236' ,'TestStatus1'),
        ('testuser12', '665522' ,'TestStatus2'),
        ('testuser13', '266552' ,'TestStatus3')]
    try:
        topology.standalone.add_s(Entry((TESTBASEDN,
                                         {'objectclass': "top dcobject".split(),
                                          'dc': 'bitwise',
                                          'aci': '(target =\"ldap:///dc=bitwise,dc=com\")' +\
                                               '(targetattr != \"userPassword\")' +\
                                               '(version 3.0;acl \"Anonymous read-search access\";' +\
                                               'allow (read, search, compare)(userdn = \"ldap:///anyone\");)'})))

        topology.standalone.add_s(Entry(('uid=btestuser1,%s' % TESTBASEDN,
                                         {'objectclass': 'top testperson organizationalPerson inetorgperson'.split(),
                                          'mail': 'btestuser1@redhat.com',
                                          'uid': 'btestuser1',
                                          'givenName': 'bit',
                                          'sn': 'testuser1',
                                          'userPassword': 'testuser1',
                                          'testUserAccountControl': '514',
                                          'testUserStatus': 'Disabled',
                                          'cn': 'bit tetsuser1'})))
        for (userid, accCtl,accStatus) in users:
            topology.standalone.add_s(Entry(('uid=b%s,%s' % (userid, TESTBASEDN),
                                         {'objectclass': 'top testperson organizationalPerson inetorgperson'.split(),
                                          'mail': '%s@redhat.com' % userid,
                                          'uid': 'b%s' % userid,
                                          'givenName': 'bit',
                                          'sn': userid,
                                          'userPassword': userid,
                                          'testUserAccountControl': accCtl,
                                          'testUserStatus': accStatus,
                                          'cn': 'bit %s' % userid})))
    except ValueError:
        topology.standalone.log.fatal("add_s failed: %s", ValueError)

def test_ticket48844_init(topology):
    # create a suffix where test entries will be stored
    BITW_SCHEMA_AT_1 = '( NAME \'testUserAccountControl\' DESC \'Attribute Bitwise filteri-Multi-Valued\' SYNTAX 1.3.6.1.4.1.1466.115.121.1.27 )'
    BITW_SCHEMA_AT_2 = '( NAME \'testUserStatus\' DESC \'State of User account active/disabled\' SYNTAX 1.3.6.1.4.1.1466.115.121.1.15 )'
    BITW_SCHEMA_OC_1 = '( NAME \'testperson\' SUP top STRUCTURAL MUST ( sn $ cn $ testUserAccountControl $ testUserStatus )' +\
                   ' MAY ( userPassword $ telephoneNumber $ seeAlso $ description ) X-ORIGIN \'BitWise\' )'
    topology.standalone.schema.add_schema('attributetypes', [BITW_SCHEMA_AT_1, BITW_SCHEMA_AT_2])
    topology.standalone.schema.add_schema('objectClasses', BITW_SCHEMA_OC_1)

    topology.standalone.backend.create(TESTBASEDN, {BACKEND_NAME: TESTBACKEND_NAME})
    topology.standalone.mappingtree.create(TESTBASEDN, bename=TESTBACKEND_NAME, parent=None)
    _addBitwiseEntries(topology)


def test_ticket48844_bitwise_on(topology):
    """
    Check that bitwise plugin (old style MR plugin) that defines
    Its own indexer create function, is selected to evaluate the filter
    """

    topology.standalone.plugins.enable(name=PLUGIN_BITWISE)
    topology.standalone.restart(timeout=10)
    ents = topology.standalone.search_s('cn=%s,cn=plugins,cn=config' % PLUGIN_BITWISE, ldap.SCOPE_BASE, 'objectclass=*')
    assert(ents[0].hasValue('nsslapd-pluginEnabled', 'on'))

    expect = 2
    ents = topology.standalone.search_s(TESTBASEDN, ldap.SCOPE_SUBTREE, BITWISE_F2)
    assert (len(ents) == expect)

    expect=1
    ents = topology.standalone.search_s(TESTBASEDN, ldap.SCOPE_SUBTREE, BITWISE_F3)
    assert (len(ents) == expect)
    assert (ents[0].hasAttr('testUserAccountControl'))

    expect=1
    ents = topology.standalone.search_s(TESTBASEDN, ldap.SCOPE_SUBTREE, BITWISE_F6)
    assert (len(ents) == expect)
    assert (ents[0].hasAttr('testUserAccountControl'))

def test_ticket48844_bitwise_off(topology):
    """
    Check that when bitwise plugin is not enabled, no plugin
    is identified to evaluate the filter -> ldap.UNAVAILABLE_CRITICAL_EXTENSION:
    """
    topology.standalone.plugins.disable(name=PLUGIN_BITWISE)
    topology.standalone.restart(timeout=10)
    ents = topology.standalone.search_s('cn=%s,cn=plugins,cn=config' % PLUGIN_BITWISE, ldap.SCOPE_BASE, 'objectclass=*')
    assert(ents[0].hasValue('nsslapd-pluginEnabled', 'off'))

    res = 0
    try:
        ents = topology.standalone.search_s(TESTBASEDN, ldap.SCOPE_SUBTREE, BITWISE_F2)
    except ldap.UNAVAILABLE_CRITICAL_EXTENSION:
        res = 12
    assert (res == 12)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
