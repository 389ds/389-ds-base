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

    # Delete each instance in the end
    def fin():
        standalone.delete()

    request.addfinalizer(fin)

    # Clear out the tmp dir
    standalone.clearTmpDir(__file__)

    return TopologyStandalone(standalone)


def test_ticket48312(topology):
    """
    Configure managed entries plugins(tempalte/definition), then perform a
    modrdn(deleteoldrdn 1), and make sure the server does not crash.
    """

    GROUP_OU = 'ou=groups,' + DEFAULT_SUFFIX
    PEOPLE_OU = 'ou=people,' + DEFAULT_SUFFIX
    USER_DN = 'uid=user1,ou=people,' + DEFAULT_SUFFIX
    CONFIG_DN = 'cn=config,cn=' + PLUGIN_MANAGED_ENTRY + ',cn=plugins,cn=config'
    TEMPLATE_DN = 'cn=MEP Template,' + DEFAULT_SUFFIX
    USER_NEWRDN = 'uid=\+user1'

    #
    # First enable dynamic plugins
    #
    try:
        topology.standalone.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE, 'nsslapd-dynamic-plugins', 'on')])
    except ldap.LDAPError as e:
        ldap.fatal('Failed to enable dynamic plugin!' + e.message['desc'])
        assert False
    topology.standalone.plugins.enable(name=PLUGIN_MANAGED_ENTRY)

    #
    # Add our org units (they should already exist, but do it just in case)
    #
    try:
        topology.standalone.add_s(Entry((PEOPLE_OU, {
                   'objectclass': 'top extensibleObject'.split(),
                   'ou': 'people'})))
    except ldap.ALREADY_EXISTS:
        pass
    except ldap.LDAPError as e:
        log.fatal('test_mep: Failed to add people org unit: error ' + e.message['desc'])
        assert False

    try:
        topology.standalone.add_s(Entry((GROUP_OU, {
                   'objectclass': 'top extensibleObject'.split(),
                   'ou': 'people'})))
    except ldap.ALREADY_EXISTS:
        pass
    except ldap.LDAPError as e:
        log.fatal('test_mep: Failed to add people org unit: error ' + e.message['desc'])
        assert False

    #
    # Add the template entry
    #
    try:
        topology.standalone.add_s(Entry((TEMPLATE_DN, {
                   'objectclass': 'top mepTemplateEntry extensibleObject'.split(),
                   'cn': 'MEP Template',
                   'mepRDNAttr': 'cn',
                   'mepStaticAttr': ['objectclass: posixGroup', 'objectclass: extensibleObject'],
                   'mepMappedAttr': ['cn: $uid', 'uid: $cn', 'gidNumber: $uidNumber']
                   })))
    except ldap.LDAPError as e:
        log.fatal('test_mep: Failed to add template entry: error ' + e.message['desc'])
        assert False

    #
    # Add the definition entry
    #
    try:
        topology.standalone.add_s(Entry((CONFIG_DN, {
                          'objectclass': 'top extensibleObject'.split(),
                          'cn': 'config',
                          'originScope': PEOPLE_OU,
                          'originFilter': 'objectclass=posixAccount',
                          'managedBase': GROUP_OU,
                          'managedTemplate': TEMPLATE_DN
                          })))
    except ldap.LDAPError as e:
        log.fatal('test_mep: Failed to add config entry: error ' + e.message['desc'])
        assert False

    #
    # Add an entry that meets the MEP scope
    #
    try:
        topology.standalone.add_s(Entry((USER_DN, {
                          'objectclass': 'top posixAccount extensibleObject'.split(),
                          'uid': 'user1',
                          'cn': 'user1',
                          'uidNumber': '1',
                          'gidNumber': '1',
                          'homeDirectory': '/home/user1',
                          'description': 'uiser description'
                          })))
    except ldap.LDAPError as e:
        log.fatal('test_mep: Failed to user1: error ' + e.message['desc'])
        assert False

    #
    # Perform a modrdn on USER_DN
    #
    try:
        topology.standalone.rename_s(USER_DN, USER_NEWRDN, delold=1)
    except ldap.LDAPError as e:
        log.error('Failed to modrdn: error ' + e.message['desc'])
        assert False

    log.info('Test complete')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)