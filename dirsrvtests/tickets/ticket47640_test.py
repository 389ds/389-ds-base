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


def test_ticket47640(topology):
    '''
    Linked Attrs Plugins - verify that if the plugin fails to update the link entry
    that the entire operation is aborted
    '''

    # Enable Dynamic plugins, and the linked Attrs plugin
    try:
        topology.standalone.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE, 'nsslapd-dynamic-plugins', 'on')])
    except ldap.LDAPError, e:
        ldap.fatal('Failed to enable dynamic plugin!' + e.message['desc'])
        assert False

    try:
        topology.standalone.plugins.enable(name=PLUGIN_LINKED_ATTRS)
    except ValueError, e:
        ldap.fatal('Failed to enable linked attributes plugin!' + e.message['desc'])
        assert False

    # Add the plugin config entry
    try:
        topology.standalone.add_s(Entry(('cn=manager link,cn=Linked Attributes,cn=plugins,cn=config', {
                          'objectclass': 'top extensibleObject'.split(),
                          'cn': 'Manager Link',
                          'linkType': 'seeAlso',
                          'managedType': 'seeAlso'
                          })))
    except ldap.LDAPError, e:
        log.fatal('Failed to add linked attr config entry: error ' + e.message['desc'])
        assert False

    # Add an entry who has a link to an entry that does not exist
    OP_REJECTED = False
    try:
        topology.standalone.add_s(Entry(('uid=manager,' + DEFAULT_SUFFIX, {
                          'objectclass': 'top extensibleObject'.split(),
                          'uid': 'manager',
                          'seeAlso': 'uid=user,dc=example,dc=com'
                          })))
    except ldap.UNWILLING_TO_PERFORM:
        # Success
        log.info('Add operation correctly rejected.')
        OP_REJECTED = True
    except ldap.LDAPError, e:
        log.fatal('Add operation incorrectly rejected: error %s - ' +
                  'expected "unwilling to perform"' % e.message['desc'])
        assert False
    if not OP_REJECTED:
        log.fatal('Add operation incorrectly allowed')
        assert False

    log.info('Test complete')


def test_ticket47640_final(topology):
    topology.standalone.delete()
    log.info('Testcase PASSED')


def run_isolated():
    global installation1_prefix
    installation1_prefix = None

    topo = topology(True)
    test_ticket47640(topo)
    test_ticket47640_final(topo)


if __name__ == '__main__':
    run_isolated()

