# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging

import ldap
import pytest
from lib389 import Entry
from lib389._constants import *
from lib389.topologies import topology_st
from lib389.utils import *

pytestmark = pytest.mark.tier2

log = logging.getLogger(__name__)

LINKEDATTR_PLUGIN = 'cn=Linked Attributes,cn=plugins,cn=config'
MANAGER_LINK = 'cn=Manager Link,' + LINKEDATTR_PLUGIN
OU_PEOPLE = 'ou=People,' + DEFAULT_SUFFIX
LINKTYPE = 'directReport'
MANAGEDTYPE = 'manager'


def _header(topology_st, label):
    topology_st.standalone.log.info("###############################################")
    topology_st.standalone.log.info("####### %s" % label)
    topology_st.standalone.log.info("###############################################")


def check_attr_val(topology_st, dn, attr, expected, revert):
    try:
        centry = topology_st.standalone.search_s(dn, ldap.SCOPE_BASE, 'uid=*')
        if centry:
            val = centry[0].getValue(attr)
            if val:
                if val.lower() == expected.lower():
                    if revert:
                        log.info('Value of %s %s exists, which should not.' % (attr, expected))
                        assert False
                    else:
                        log.info('Value of %s is %s' % (attr, expected))
                else:
                    if revert:
                        log.info('NEEDINFO: Value of %s is not %s, but %s' % (attr, expected, val))
                    else:
                        log.info('Value of %s is not %s, but %s' % (attr, expected, val))
                        assert False
            else:
                if revert:
                    log.info('Value of %s does not expectedly exist' % attr)
                else:
                    log.info('Value of %s does not exist' % attr)
                    assert False
        else:
            log.fatal('Failed to get %s' % dn)
            assert False
    except ldap.LDAPError as e:
        log.fatal('Failed to search ' + dn + ': ' + e.args[0]['desc'])
        assert False


def test_48295_init(topology_st):
    """
    Set up Linked Attribute
    """
    _header(topology_st,
            'Testing Ticket 48295 - Entry cache is not rolled back -- Linked Attributes plug-in - wrong behaviour when adding valid and broken links')

    log.info('Enable Dynamic plugins, and the linked Attrs plugin')
    try:
        topology_st.standalone.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE, 'nsslapd-dynamic-plugins', b'on')])
    except ldap.LDAPError as e:
        log.fatal('Failed to enable dynamic plugin!' + e.args[0]['desc'])
        assert False

    try:
        topology_st.standalone.plugins.enable(name=PLUGIN_LINKED_ATTRS)
    except ValueError as e:
        log.fatal('Failed to enable linked attributes plugin!' + e.args[0]['desc'])
        assert False

    log.info('Add the plugin config entry')
    try:
        topology_st.standalone.add_s(Entry((MANAGER_LINK, {
            'objectclass': 'top extensibleObject'.split(),
            'cn': 'Manager Link',
            'linkType': LINKTYPE,
            'managedType': MANAGEDTYPE
        })))
    except ldap.LDAPError as e:
        log.fatal('Failed to add linked attr config entry: error ' + e.args[0]['desc'])
        assert False

    log.info('Add 2 entries: manager1 and employee1')
    try:
        topology_st.standalone.add_s(Entry(('uid=manager1,%s' % OU_PEOPLE, {
            'objectclass': 'top extensibleObject'.split(),
            'uid': 'manager1'})))
    except ldap.LDAPError as e:
        log.fatal('Add manager1 failed: error ' + e.args[0]['desc'])
        assert False

    try:
        topology_st.standalone.add_s(Entry(('uid=employee1,%s' % OU_PEOPLE, {
            'objectclass': 'top extensibleObject'.split(),
            'uid': 'employee1'})))
    except ldap.LDAPError as e:
        log.fatal('Add employee1 failed: error ' + e.args[0]['desc'])
        assert False

    log.info('PASSED')


def test_48295_run(topology_st):
    """
    Add 2 linktypes - one exists, another does not
    """

    _header(topology_st,
            'Add 2 linktypes to manager1 - one exists, another does not to make sure the managed entry does not have managed type.')
    try:
        topology_st.standalone.modify_s('uid=manager1,%s' % OU_PEOPLE,
                                        [(ldap.MOD_ADD, LINKTYPE, ensure_bytes('uid=employee1,%s' % OU_PEOPLE)),
                                         (ldap.MOD_ADD, LINKTYPE, ensure_bytes('uid=doNotExist,%s' % OU_PEOPLE))])
    except ldap.UNWILLING_TO_PERFORM:
        log.info('Add uid=employee1 and uid=doNotExist expectedly failed.')
        pass

    log.info('Check managed attribute does not exist.')
    check_attr_val(topology_st, 'uid=employee1,%s' % OU_PEOPLE, MANAGEDTYPE, ensure_bytes('uid=manager1,%s' % OU_PEOPLE), True)

    log.info('PASSED')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
