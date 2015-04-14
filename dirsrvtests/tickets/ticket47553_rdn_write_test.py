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
from ldap.controls.simple import GetEffectiveRightsControl

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

installation1_prefix = None

SRC_ENTRY_CN = "tuser"
EXT_RDN = "01"
DST_ENTRY_CN = SRC_ENTRY_CN + EXT_RDN

SRC_ENTRY_DN = "cn=%s,%s" % (SRC_ENTRY_CN, SUFFIX)
DST_ENTRY_DN = "cn=%s,%s" % (DST_ENTRY_CN, SUFFIX)

class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone


#@pytest.fixture(scope="module")
def topology(request):
    global installation1_prefix

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

    # Clear out the tmp dir
    standalone.clearTmpDir(__file__)

    return TopologyStandalone(standalone)

def test_ticket47553_rdn_write_init(topology):
    topology.standalone.log.info("\n\n######################### Add entry tuser ######################\n")
    topology.standalone.add_s(Entry((SRC_ENTRY_DN, {
                                                'objectclass': "top person".split(),
                                                'sn': SRC_ENTRY_CN,
                                                'cn': SRC_ENTRY_CN})))

def test_ticket47553_rdn_write_get_ger(topology):
    ANONYMOUS_DN = ""
    topology.standalone.log.info("\n\n######################### GER rights for anonymous ######################\n")
    request_ctrl = GetEffectiveRightsControl(criticality=True, authzId="dn:" + ANONYMOUS_DN)
    msg_id = topology.standalone.search_ext(SUFFIX, ldap.SCOPE_SUBTREE, "objectclass=*", serverctrls=[request_ctrl])
    rtype, rdata, rmsgid, response_ctrl = topology.standalone.result3(msg_id)
    value = ''
    for dn, attrs in rdata:
        topology.standalone.log.info("dn: %s" % dn)
        for value in attrs['entryLevelRights']:
            topology.standalone.log.info("###############  entryLevelRights: %r" % value)
            assert 'n' not in value

def test_ticket47553_rdn_write_modrdn_anonymous(topology):
    ANONYMOUS_DN = ""
    topology.standalone.close()
    topology.standalone.binddn = ANONYMOUS_DN
    topology.standalone.open()
    msg_id = topology.standalone.search_ext("", ldap.SCOPE_BASE, "objectclass=*")
    rtype, rdata, rmsgid, response_ctrl = topology.standalone.result3(msg_id)
    value = ''
    for dn, attrs in rdata:
        topology.standalone.log.info("dn: %s" % dn)
        for attr in attrs:
            topology.standalone.log.info("###############  %r: %r" % (attr, attrs[attr]))


    try:
        topology.standalone.rename_s(SRC_ENTRY_DN, "cn=%s" % DST_ENTRY_CN, delold=True)
    except Exception as e:
        topology.standalone.log.info("Exception (expected): %s" % type(e).__name__)
        isinstance(e, ldap.INSUFFICIENT_ACCESS)

    try:
        topology.standalone.getEntry(DST_ENTRY_DN, ldap.SCOPE_BASE, "objectclass=*")
        assert False
    except Exception as e:
        topology.standalone.log.info("The entry was not renamed (expected)")
        isinstance(e, ldap.NO_SUCH_OBJECT)

def test_ticket47553_rdn_write(topology):
    '''
    Write your testcase here...
    '''

    log.info('Test complete')


def test_ticket47553_rdn_write_final(topology):
    topology.standalone.delete()
    log.info('Testcase PASSED')


def run_isolated():
    global installation1_prefix
    installation1_prefix = '/home/tbordaz/install_master'

    topo = topology(True)
    test_ticket47553_rdn_write_init(topo)
    test_ticket47553_rdn_write_get_ger(topo)
    test_ticket47553_rdn_write(topo)
    test_ticket47553_rdn_write_modrdn_anonymous(topo)
    test_ticket47553_rdn_write_final(topo)


if __name__ == '__main__':
    run_isolated()

