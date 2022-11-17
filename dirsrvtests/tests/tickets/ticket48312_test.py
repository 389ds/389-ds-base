# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st

from lib389._constants import DEFAULT_SUFFIX, PLUGIN_MANAGED_ENTRY, DN_CONFIG

pytestmark = pytest.mark.tier2

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


def test_ticket48312(topology_st):
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
        topology_st.standalone.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE, 'nsslapd-dynamic-plugins', b'on')])
    except ldap.LDAPError as e:
        log.fatal('Failed to enable dynamic plugin!' + e.args[0]['desc'])
        assert False
    topology_st.standalone.plugins.enable(name=PLUGIN_MANAGED_ENTRY)

    #
    # Add our org units (they should already exist, but do it just in case)
    #
    try:
        topology_st.standalone.add_s(Entry((PEOPLE_OU, {
            'objectclass': 'top extensibleObject'.split(),
            'ou': 'people'})))
    except ldap.ALREADY_EXISTS:
        pass
    except ldap.LDAPError as e:
        log.fatal('test_mep: Failed to add people org unit: error ' + e.args[0]['desc'])
        assert False

    try:
        topology_st.standalone.add_s(Entry((GROUP_OU, {
            'objectclass': 'top extensibleObject'.split(),
            'ou': 'people'})))
    except ldap.ALREADY_EXISTS:
        pass
    except ldap.LDAPError as e:
        log.fatal('test_mep: Failed to add people org unit: error ' + e.args[0]['desc'])
        assert False

    #
    # Add the template entry
    #
    try:
        topology_st.standalone.add_s(Entry((TEMPLATE_DN, {
            'objectclass': 'top mepTemplateEntry extensibleObject'.split(),
            'cn': 'MEP Template',
            'mepRDNAttr': 'cn',
            'mepStaticAttr': ['objectclass: posixGroup', 'objectclass: extensibleObject'],
            'mepMappedAttr': ['cn: $uid', 'uid: $cn', 'gidNumber: $uidNumber']
        })))
    except ldap.LDAPError as e:
        log.fatal('test_mep: Failed to add template entry: error ' + e.args[0]['desc'])
        assert False

    #
    # Add the definition entry
    #
    try:
        topology_st.standalone.add_s(Entry((CONFIG_DN, {
            'objectclass': 'top extensibleObject'.split(),
            'cn': 'config',
            'originScope': PEOPLE_OU,
            'originFilter': 'objectclass=posixAccount',
            'managedBase': GROUP_OU,
            'managedTemplate': TEMPLATE_DN
        })))
    except ldap.LDAPError as e:
        log.fatal('test_mep: Failed to add config entry: error ' + e.args[0]['desc'])
        assert False

    #
    # Add an entry that meets the MEP scope
    #
    try:
        topology_st.standalone.add_s(Entry((USER_DN, {
            'objectclass': 'top posixAccount extensibleObject'.split(),
            'uid': 'user1',
            'cn': 'user1',
            'uidNumber': '1',
            'gidNumber': '1',
            'homeDirectory': '/home/user1',
            'description': 'uiser description'
        })))
    except ldap.LDAPError as e:
        log.fatal('test_mep: Failed to user1: error ' + e.args[0]['desc'])
        assert False

    #
    # Perform a modrdn on USER_DN
    #
    try:
        topology_st.standalone.rename_s(USER_DN, USER_NEWRDN, delold=1)
    except ldap.LDAPError as e:
        log.error('Failed to modrdn: error ' + e.args[0]['desc'])
        assert False

    log.info('Test complete')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
