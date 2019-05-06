# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import time

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


def check_attr_val(topology_st, dn, attr, expected):
    try:
        centry = topology_st.standalone.search_s(dn, ldap.SCOPE_BASE, 'uid=*')
        if centry:
            val = centry[0].getValue(attr)
            if val.lower() == expected.lower():
                log.info('Value of %s is %s' % (attr, expected))
            else:
                log.info('Value of %s is not %s, but %s' % (attr, expected, val))
                assert False
        else:
            log.fatal('Failed to get %s' % dn)
            assert False
    except ldap.LDAPError as e:
        log.fatal('Failed to search ' + dn + ': ' + e.args[0]['desc'])
        assert False


def _modrdn_entry(topology_st=None, entry_dn=None, new_rdn=None, del_old=0, new_superior=None):
    assert topology_st is not None
    assert entry_dn is not None
    assert new_rdn is not None

    topology_st.standalone.log.info("\n\n######################### MODRDN %s ######################\n" % new_rdn)
    try:
        if new_superior:
            topology_st.standalone.rename_s(entry_dn, new_rdn, newsuperior=new_superior, delold=del_old)
        else:
            topology_st.standalone.rename_s(entry_dn, new_rdn, delold=del_old)
    except ldap.NO_SUCH_ATTRIBUTE:
        topology_st.standalone.log.info("accepted failure due to 47833: modrdn reports error.. but succeeds")
        attempt = 0
        if new_superior:
            dn = "%s,%s" % (new_rdn, new_superior)
            base = new_superior
        else:
            base = ','.join(entry_dn.split(",")[1:])
            dn = "%s, %s" % (new_rdn, base)
        myfilter = entry_dn.split(',')[0]

        while attempt < 10:
            try:
                ent = topology_st.standalone.getEntry(dn, ldap.SCOPE_BASE, myfilter)
                break
            except ldap.NO_SUCH_OBJECT:
                topology_st.standalone.log.info("Accept failure due to 47833: unable to find (base) a modrdn entry")
                attempt += 1
                time.sleep(1)
        if attempt == 10:
            ent = topology_st.standalone.getEntry(base, ldap.SCOPE_SUBTREE, myfilter)
            ent = topology_st.standalone.getEntry(dn, ldap.SCOPE_BASE, myfilter)


def test_48294_init(topology_st):
    """
    Set up Linked Attribute
    """
    _header(topology_st,
            'Testing Ticket 48294 - Linked Attributes plug-in - won\'t update links after MODRDN operation')

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

    log.info('Add linktype to manager1')
    topology_st.standalone.modify_s('uid=manager1,%s' % OU_PEOPLE,
                                    [(ldap.MOD_ADD, LINKTYPE, ensure_bytes('uid=employee1,%s' % OU_PEOPLE))])

    log.info('Check managed attribute')
    check_attr_val(topology_st, 'uid=employee1,%s' % OU_PEOPLE, MANAGEDTYPE, ensure_bytes('uid=manager1,%s' % OU_PEOPLE))

    log.info('PASSED')


def test_48294_run_0(topology_st):
    """
    Rename employee1 to employee2 and adjust the value of directReport by replace
    """
    _header(topology_st, 'Case 0 - Rename employee1 and adjust the link type value by replace')

    log.info('Rename employee1 to employee2')
    _modrdn_entry(topology_st, entry_dn='uid=employee1,%s' % OU_PEOPLE, new_rdn='uid=employee2')

    log.info('Modify the value of directReport to uid=employee2')
    try:
        topology_st.standalone.modify_s('uid=manager1,%s' % OU_PEOPLE,
                                        [(ldap.MOD_REPLACE, LINKTYPE, ensure_bytes('uid=employee2,%s' % OU_PEOPLE))])
    except ldap.LDAPError as e:
        log.fatal('Failed to replace uid=employee1 with employee2: ' + e.args[0]['desc'])
        assert False

    log.info('Check managed attribute')
    check_attr_val(topology_st, 'uid=employee2,%s' % OU_PEOPLE, MANAGEDTYPE, ensure_bytes('uid=manager1,%s' % OU_PEOPLE))

    log.info('PASSED')


def test_48294_run_1(topology_st):
    """
    Rename employee2 to employee3 and adjust the value of directReport by delete and add
    """
    _header(topology_st, 'Case 1 - Rename employee2 and adjust the link type value by delete and add')

    log.info('Rename employee2 to employee3')
    _modrdn_entry(topology_st, entry_dn='uid=employee2,%s' % OU_PEOPLE, new_rdn='uid=employee3')

    log.info('Modify the value of directReport to uid=employee3')
    try:
        topology_st.standalone.modify_s('uid=manager1,%s' % OU_PEOPLE,
                                        [(ldap.MOD_DELETE, LINKTYPE, ensure_bytes('uid=employee2,%s' % OU_PEOPLE))])
    except ldap.LDAPError as e:
        log.fatal('Failed to delete employee2: ' + e.args[0]['desc'])
        assert False

    try:
        topology_st.standalone.modify_s('uid=manager1,%s' % OU_PEOPLE,
                                        [(ldap.MOD_ADD, LINKTYPE, ensure_bytes('uid=employee3,%s' % OU_PEOPLE))])
    except ldap.LDAPError as e:
        log.fatal('Failed to add employee3: ' + e.args[0]['desc'])
        assert False

    log.info('Check managed attribute')
    check_attr_val(topology_st, 'uid=employee3,%s' % OU_PEOPLE, MANAGEDTYPE, ensure_bytes('uid=manager1,%s' % OU_PEOPLE))

    log.info('PASSED')


def test_48294_run_2(topology_st):
    """
    Rename manager1 to manager2 and make sure the managed attribute value is updated
    """
    _header(topology_st, 'Case 2 - Rename manager1 to manager2 and make sure the managed attribute value is updated')

    log.info('Rename manager1 to manager2')
    _modrdn_entry(topology_st, entry_dn='uid=manager1,%s' % OU_PEOPLE, new_rdn='uid=manager2')

    log.info('Check managed attribute')
    check_attr_val(topology_st, 'uid=employee3,%s' % OU_PEOPLE, MANAGEDTYPE, ensure_bytes('uid=manager2,%s' % OU_PEOPLE))

    log.info('PASSED')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
