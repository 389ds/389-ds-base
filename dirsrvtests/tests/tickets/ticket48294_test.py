# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import sys
import time
import ldap
import logging
import pytest
import shutil
from lib389 import DirSrv, Entry, tools
from lib389 import DirSrvTools
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *

log = logging.getLogger(__name__)

LINKEDATTR_PLUGIN = 'cn=Linked Attributes,cn=plugins,cn=config'
MANAGER_LINK = 'cn=Manager Link,' + LINKEDATTR_PLUGIN
OU_PEOPLE = 'ou=People,' + DEFAULT_SUFFIX
LINKTYPE = 'directReport'
MANAGEDTYPE = 'manager'


class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):
    '''
    This fixture is used to standalone topology for the 'module'.
    '''
    standalone = DirSrv(verbose=False)

    # Args for the standalone instance
    args_instance[SER_HOST] = HOST_STANDALONE
    args_instance[SER_PORT] = PORT_STANDALONE
    args_instance[SER_SERVERID_PROP] = SERVERID_STANDALONE
    args_standalone = args_instance.copy()
    standalone.allocate(args_standalone)

    # Get the status of the instance and restart it if it exists
    instance_standalone = standalone.exists()

    # Remove the instance
    if instance_standalone:
        standalone.delete()

    # Create the instance
    standalone.create()

    # Used to retrieve configuration information (dbdir, confdir...)
    standalone.open()

    def fin():
        standalone.delete()
    request.addfinalizer(fin)

    # Here we have standalone instance up and running
    return TopologyStandalone(standalone)


def _header(topology, label):
    topology.standalone.log.info("###############################################")
    topology.standalone.log.info("####### %s" % label)
    topology.standalone.log.info("###############################################")


def check_attr_val(topology, dn, attr, expected):
    try:
        centry = topology.standalone.search_s(dn, ldap.SCOPE_BASE, 'uid=*')
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
        log.fatal('Failed to search ' + dn + ': ' + e.message['desc'])
        assert False


def _modrdn_entry(topology=None, entry_dn=None, new_rdn=None, del_old=0, new_superior=None):
    assert topology is not None
    assert entry_dn is not None
    assert new_rdn is not None

    topology.standalone.log.info("\n\n######################### MODRDN %s ######################\n" % new_rdn)
    try:
        if new_superior:
            topology.standalone.rename_s(entry_dn, new_rdn, newsuperior=new_superior, delold=del_old)
        else:
            topology.standalone.rename_s(entry_dn, new_rdn, delold=del_old)
    except ldap.NO_SUCH_ATTRIBUTE:
        topology.standalone.log.info("accepted failure due to 47833: modrdn reports error.. but succeeds")
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
                ent = topology.standalone.getEntry(dn, ldap.SCOPE_BASE, myfilter)
                break
            except ldap.NO_SUCH_OBJECT:
                topology.standalone.log.info("Accept failure due to 47833: unable to find (base) a modrdn entry")
                attempt += 1
                time.sleep(1)
        if attempt == 10:
            ent = topology.standalone.getEntry(base, ldap.SCOPE_SUBTREE, myfilter)
            ent = topology.standalone.getEntry(dn, ldap.SCOPE_BASE, myfilter)


def test_48294_init(topology):
    """
    Set up Linked Attribute
    """
    _header(topology, 'Testing Ticket 48294 - Linked Attributes plug-in - won\'t update links after MODRDN operation')

    log.info('Enable Dynamic plugins, and the linked Attrs plugin')
    try:
        topology.standalone.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE, 'nsslapd-dynamic-plugins', 'on')])
    except ldap.LDAPError as e:
        ldap.fatal('Failed to enable dynamic plugin!' + e.message['desc'])
        assert False

    try:
        topology.standalone.plugins.enable(name=PLUGIN_LINKED_ATTRS)
    except ValueError as e:
        ldap.fatal('Failed to enable linked attributes plugin!' + e.message['desc'])
        assert False

    log.info('Add the plugin config entry')
    try:
        topology.standalone.add_s(Entry((MANAGER_LINK, {
                          'objectclass': 'top extensibleObject'.split(),
                          'cn': 'Manager Link',
                          'linkType': LINKTYPE,
                          'managedType': MANAGEDTYPE
                          })))
    except ldap.LDAPError as e:
        log.fatal('Failed to add linked attr config entry: error ' + e.message['desc'])
        assert False

    log.info('Add 2 entries: manager1 and employee1')
    try:
        topology.standalone.add_s(Entry(('uid=manager1,%s' % OU_PEOPLE, {
                          'objectclass': 'top extensibleObject'.split(),
                          'uid': 'manager1'})))
    except ldap.LDAPError as e:
        log.fatal('Add manager1 failed: error ' + e.message['desc'])
        assert False

    try:
        topology.standalone.add_s(Entry(('uid=employee1,%s' % OU_PEOPLE, {
                          'objectclass': 'top extensibleObject'.split(),
                          'uid': 'employee1'})))
    except ldap.LDAPError as e:
        log.fatal('Add employee1 failed: error ' + e.message['desc'])
        assert False

    log.info('Add linktype to manager1')
    topology.standalone.modify_s('uid=manager1,%s' % OU_PEOPLE,
                                 [(ldap.MOD_ADD, LINKTYPE, 'uid=employee1,%s' % OU_PEOPLE)])

    log.info('Check managed attribute')
    check_attr_val(topology, 'uid=employee1,%s' % OU_PEOPLE, MANAGEDTYPE, 'uid=manager1,%s' % OU_PEOPLE)

    log.info('PASSED')


def test_48294_run_0(topology):
    """
    Rename employee1 to employee2 and adjust the value of directReport by replace
    """
    _header(topology, 'Case 0 - Rename employee1 and adjust the link type value by replace')

    log.info('Rename employee1 to employee2')
    _modrdn_entry(topology, entry_dn='uid=employee1,%s' % OU_PEOPLE, new_rdn='uid=employee2')

    log.info('Modify the value of directReport to uid=employee2')
    try:
        topology.standalone.modify_s('uid=manager1,%s' % OU_PEOPLE,
                                     [(ldap.MOD_REPLACE, LINKTYPE, 'uid=employee2,%s' % OU_PEOPLE)])
    except ldap.LDAPError as e:
        log.fatal('Failed to replace uid=employee1 with employee2: ' + e.message['desc'])
        assert False

    log.info('Check managed attribute')
    check_attr_val(topology, 'uid=employee2,%s' % OU_PEOPLE, MANAGEDTYPE, 'uid=manager1,%s' % OU_PEOPLE)

    log.info('PASSED')


def test_48294_run_1(topology):
    """
    Rename employee2 to employee3 and adjust the value of directReport by delete and add
    """
    _header(topology, 'Case 1 - Rename employee2 and adjust the link type value by delete and add')

    log.info('Rename employee2 to employee3')
    _modrdn_entry(topology, entry_dn='uid=employee2,%s' % OU_PEOPLE, new_rdn='uid=employee3')

    log.info('Modify the value of directReport to uid=employee3')
    try:
        topology.standalone.modify_s('uid=manager1,%s' % OU_PEOPLE,
                                     [(ldap.MOD_DELETE, LINKTYPE, 'uid=employee2,%s' % OU_PEOPLE)])
    except ldap.LDAPError as e:
        log.fatal('Failed to delete employee2: ' + e.message['desc'])
        assert False

    try:
        topology.standalone.modify_s('uid=manager1,%s' % OU_PEOPLE,
                                     [(ldap.MOD_ADD, LINKTYPE, 'uid=employee3,%s' % OU_PEOPLE)])
    except ldap.LDAPError as e:
        log.fatal('Failed to add employee3: ' + e.message['desc'])
        assert False

    log.info('Check managed attribute')
    check_attr_val(topology, 'uid=employee3,%s' % OU_PEOPLE, MANAGEDTYPE, 'uid=manager1,%s' % OU_PEOPLE)

    log.info('PASSED')


def test_48294_run_2(topology):
    """
    Rename manager1 to manager2 and make sure the managed attribute value is updated
    """
    _header(topology, 'Case 2 - Rename manager1 to manager2 and make sure the managed attribute value is updated')

    log.info('Rename manager1 to manager2')
    _modrdn_entry(topology, entry_dn='uid=manager1,%s' % OU_PEOPLE, new_rdn='uid=manager2')

    log.info('Check managed attribute')
    check_attr_val(topology, 'uid=employee3,%s' % OU_PEOPLE, MANAGEDTYPE, 'uid=manager2,%s' % OU_PEOPLE)

    log.info('PASSED')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
