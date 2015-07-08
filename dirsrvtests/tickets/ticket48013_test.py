import os
import sys
import time
import ldap
import logging
import pytest
import pyasn1
import pyasn1_modules
import ldap,ldapurl
from ldap.ldapobject import SimpleLDAPObject
from ldap.syncrepl import SyncreplConsumer
from lib389 import DirSrv, Entry, tools, tasks
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from lib389.utils import *

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

installation1_prefix = None


class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone


class SyncObject(SimpleLDAPObject, SyncreplConsumer):
    def __init__(self, uri):
        # Init the ldap connection
        SimpleLDAPObject.__init__(self, uri)

    def sync_search(self, test_cookie):
        self.syncrepl_search('dc=example,dc=com', ldap.SCOPE_SUBTREE,
                             filterstr='(objectclass=*)', mode='refreshOnly',
                             cookie=test_cookie)

    def poll(self):
        self.syncrepl_poll(all=1)


@pytest.fixture(scope="module")
def topology(request):
    global installation1_prefix
    if installation1_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation1_prefix

    # Creating standalone instance ...
    standalone = DirSrv(verbose=False)
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


def test_ticket48013(topology):
    '''
    Content Synchonization: Test that invalid cookies are caught
    '''

    cookies = ('#', '##', 'a#a#a', 'a#a#1')

    # Enable dynamic plugins
    try:
        topology.standalone.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE, 'nsslapd-dynamic-plugins', 'on')])
    except ldap.LDAPError as e:
        ldap.error('Failed to enable dynamic plugin!' + e.message['desc'])
        assert False

    # Enable retro changelog
    topology.standalone.plugins.enable(name=PLUGIN_RETRO_CHANGELOG)

    # Enbale content sync plugin
    topology.standalone.plugins.enable(name=PLUGIN_REPL_SYNC)

    # Set everything up
    ldap_url = ldapurl.LDAPUrl('ldap://localhost:31389')
    ldap_connection = SyncObject(ldap_url.initializeUrl())

    # Authenticate
    try:
        ldap_connection.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        print('Login to LDAP server failed: %s' % e.message['desc'])
        assert False

    # Test invalid cookies
    for invalid_cookie in cookies:
        log.info('Testing cookie: %s' % invalid_cookie)
        try:
            ldap_connection.sync_search(invalid_cookie)
            ldap_connection.poll()
            log.fatal('Invalid cookie accepted!')
            assert False
        except Exception as e:
            log.info('Invalid cookie correctly rejected: %s' % e.message['info'])
            pass

    # Success
    log.info('Test complete')


def test_ticket48013_final(topology):
    topology.standalone.delete()
    log.info('Testcase PASSED')


def run_isolated():
    global installation1_prefix
    installation1_prefix = None

    topo = topology(True)
    test_ticket48013(topo)
    test_ticket48013_final(topo)


if __name__ == '__main__':
    run_isolated()

