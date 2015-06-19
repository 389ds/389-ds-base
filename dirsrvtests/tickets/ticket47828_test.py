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
import socket
import pytest
import shutil
from lib389 import DirSrv, Entry, tools
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *

log = logging.getLogger(__name__)

installation_prefix = None

ACCT_POLICY_CONFIG_DN = 'cn=config,cn=%s,cn=plugins,cn=config' % PLUGIN_ACCT_POLICY 
ACCT_POLICY_DN = 'cn=Account Inactivation Pplicy,%s' % SUFFIX
INACTIVITY_LIMIT = '9'
SEARCHFILTER = '(objectclass=*)'

DUMMY_CONTAINER = 'cn=dummy container,%s' % SUFFIX
PROVISIONING = 'cn=provisioning,%s' % SUFFIX
ACTIVE_USER1_CN = 'active user1'
ACTIVE_USER1_DN = 'cn=%s,%s' % (ACTIVE_USER1_CN, SUFFIX)
STAGED_USER1_CN = 'staged user1'
STAGED_USER1_DN = 'cn=%s,%s' % (STAGED_USER1_CN, PROVISIONING)
DUMMY_USER1_CN  = 'dummy user1'
DUMMY_USER1_DN  = 'cn=%s,%s' % (DUMMY_USER1_CN, DUMMY_CONTAINER)

ALLOCATED_ATTR = 'employeeNumber'

class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):
    '''
        This fixture is used to standalone topology for the 'module'.
        At the beginning, It may exists a standalone instance.
        It may also exists a backup for the standalone instance.

        Principle:
            If standalone instance exists:
                restart it
            If backup of standalone exists:
                create/rebind to standalone

                restore standalone instance from backup
            else:
                Cleanup everything
                    remove instance
                    remove backup
                Create instance
                Create backup
    '''
    global installation_prefix

    if installation_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation_prefix

    standalone = DirSrv(verbose=False)

    # Args for the standalone instance
    args_instance[SER_HOST] = HOST_STANDALONE
    args_instance[SER_PORT] = PORT_STANDALONE
    args_instance[SER_SERVERID_PROP] = SERVERID_STANDALONE
    args_standalone = args_instance.copy()
    standalone.allocate(args_standalone)

    # Get the status of the backups
    backup_standalone = standalone.checkBackupFS()

    # Get the status of the instance and restart it if it exists
    instance_standalone = standalone.exists()
    if instance_standalone:
        # assuming the instance is already stopped, just wait 5 sec max
        standalone.stop(timeout=5)
        try:
            standalone.start(timeout=10)
        except ldap.SERVER_DOWN:
            pass

    if backup_standalone:
        # The backup exist, assuming it is correct
        # we just re-init the instance with it
        if not instance_standalone:
            standalone.create()
            # Used to retrieve configuration information (dbdir, confdir...)
            standalone.open()

        # restore standalone instance from backup
        standalone.stop(timeout=10)
        standalone.restoreFS(backup_standalone)
        standalone.start(timeout=10)

    else:
        # We should be here only in two conditions
        #      - This is the first time a test involve standalone instance
        #      - Something weird happened (instance/backup destroyed)
        #        so we discard everything and recreate all

        # Remove the backup. So even if we have a specific backup file
        # (e.g backup_standalone) we clear backup that an instance may have created
        if backup_standalone:
            standalone.clearBackupFS()

        # Remove the instance
        if instance_standalone:
            standalone.delete()

        # Create the instance
        standalone.create()

        # Used to retrieve configuration information (dbdir, confdir...)
        standalone.open()

        # Time to create the backups
        standalone.stop(timeout=10)
        standalone.backupfile = standalone.backupFS()
        standalone.start(timeout=10)

    #
    # Here we have standalone instance up and running
    # Either coming from a backup recovery
    # or from a fresh (re)init
    # Time to return the topology
    return TopologyStandalone(standalone)

def _header(topology, label):
    topology.standalone.log.info("\n\n###############################################")
    topology.standalone.log.info("#######")
    topology.standalone.log.info("####### %s" % label)
    topology.standalone.log.info("#######")
    topology.standalone.log.info("###############################################")

def test_ticket47828_init(topology):
    """
    Enable DNA
    """
    topology.standalone.plugins.enable(name=PLUGIN_DNA)

    topology.standalone.add_s(Entry((PROVISIONING,{'objectclass': "top nscontainer".split(),
                                                  'cn': 'provisioning'})))
    topology.standalone.add_s(Entry((DUMMY_CONTAINER,{'objectclass': "top nscontainer".split(),
                                                  'cn': 'dummy container'})))
    
    dn_config = "cn=excluded scope, cn=%s, %s" % (PLUGIN_DNA, DN_PLUGIN)
    topology.standalone.add_s(Entry((dn_config, {'objectclass': "top extensibleObject".split(),
                                                    'cn': 'excluded scope',
                                                    'dnaType': ALLOCATED_ATTR,
                                                    'dnaNextValue': str(1000),
                                                    'dnaMaxValue': str(2000),
                                                    'dnaMagicRegen': str(-1),
                                                    'dnaFilter': '(&(objectClass=person)(objectClass=organizationalPerson)(objectClass=inetOrgPerson))',
                                                    'dnaScope': SUFFIX})))
    topology.standalone.restart(timeout=10)

                                                    

def test_ticket47828_run_0(topology):
    """
    NO exclude scope: Add an active entry and check its ALLOCATED_ATTR is set
    """
    _header(topology, 'NO exclude scope: Add an active entry and check its ALLOCATED_ATTR is set')

    topology.standalone.add_s(Entry((ACTIVE_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                                'cn': ACTIVE_USER1_CN,
                                                'sn': ACTIVE_USER1_CN,
                                                ALLOCATED_ATTR: str(-1)})))
    ent = topology.standalone.getEntry(ACTIVE_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ent.getValue(ALLOCATED_ATTR) != str(-1)
    topology.standalone.log.debug('%s.%s=%s' % (ACTIVE_USER1_CN, ALLOCATED_ATTR, ent.getValue(ALLOCATED_ATTR)))
    topology.standalone.delete_s(ACTIVE_USER1_DN)
    
def test_ticket47828_run_1(topology):
    """
    NO exclude scope: Add an active entry and check its ALLOCATED_ATTR is unchanged (!= magic)
    """
    _header(topology, 'NO exclude scope: Add an active entry and check its ALLOCATED_ATTR is unchanged (!= magic)')

    topology.standalone.add_s(Entry((ACTIVE_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                                'cn': ACTIVE_USER1_CN,
                                                'sn': ACTIVE_USER1_CN,
                                                ALLOCATED_ATTR: str(20)})))
    ent = topology.standalone.getEntry(ACTIVE_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ent.getValue(ALLOCATED_ATTR) == str(20)
    topology.standalone.log.debug('%s.%s=%s' % (ACTIVE_USER1_CN, ALLOCATED_ATTR, ent.getValue(ALLOCATED_ATTR)))
    topology.standalone.delete_s(ACTIVE_USER1_DN)
    
def test_ticket47828_run_2(topology):
    """
    NO exclude scope: Add a staged entry and check its ALLOCATED_ATTR is  set
    """
    _header(topology, 'NO exclude scope: Add a staged entry and check its ALLOCATED_ATTR is  set')

    topology.standalone.add_s(Entry((STAGED_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                                'cn': STAGED_USER1_CN,
                                                'sn': STAGED_USER1_CN,
                                                ALLOCATED_ATTR: str(-1)})))
    ent = topology.standalone.getEntry(STAGED_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ent.getValue(ALLOCATED_ATTR) != str(-1)
    topology.standalone.log.debug('%s.%s=%s' % (STAGED_USER1_CN, ALLOCATED_ATTR, ent.getValue(ALLOCATED_ATTR)))
    topology.standalone.delete_s(STAGED_USER1_DN)
    
def test_ticket47828_run_3(topology):
    """
    NO exclude scope: Add a staged entry and check its ALLOCATED_ATTR is unchanged (!= magic)
    """
    _header(topology, 'NO exclude scope: Add a staged entry and check its ALLOCATED_ATTR is unchanged (!= magic)')

    topology.standalone.add_s(Entry((STAGED_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                                'cn': STAGED_USER1_CN,
                                                'sn': STAGED_USER1_CN,
                                                ALLOCATED_ATTR: str(20)})))
    ent = topology.standalone.getEntry(STAGED_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ent.getValue(ALLOCATED_ATTR) == str(20)
    topology.standalone.log.debug('%s.%s=%s' % (STAGED_USER1_CN, ALLOCATED_ATTR, ent.getValue(ALLOCATED_ATTR)))
    topology.standalone.delete_s(STAGED_USER1_DN)
    
def test_ticket47828_run_4(topology):
    '''
    Exclude the provisioning container
    '''
    _header(topology, 'Exclude the provisioning container')
    
    dn_config = "cn=excluded scope, cn=%s, %s" % (PLUGIN_DNA, DN_PLUGIN)
    mod = [(ldap.MOD_REPLACE, 'dnaExcludeScope', PROVISIONING)]
    topology.standalone.modify_s(dn_config, mod)
    
def test_ticket47828_run_5(topology):
    """
    Provisioning excluded scope: Add an active entry and check its ALLOCATED_ATTR is set
    """
    _header(topology, 'Provisioning excluded scope: Add an active entry and check its ALLOCATED_ATTR is set')

    topology.standalone.add_s(Entry((ACTIVE_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                                'cn': ACTIVE_USER1_CN,
                                                'sn': ACTIVE_USER1_CN,
                                                ALLOCATED_ATTR: str(-1)})))
    ent = topology.standalone.getEntry(ACTIVE_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ent.getValue(ALLOCATED_ATTR) != str(-1)
    topology.standalone.log.debug('%s.%s=%s' % (ACTIVE_USER1_CN, ALLOCATED_ATTR, ent.getValue(ALLOCATED_ATTR)))
    topology.standalone.delete_s(ACTIVE_USER1_DN)
    
def test_ticket47828_run_6(topology):
    """
    Provisioning excluded scope: Add an active entry and check its ALLOCATED_ATTR is unchanged (!= magic)
    """
    _header(topology, 'Provisioning excluded scope: Add an active entry and check its ALLOCATED_ATTR is unchanged (!= magic)')

    topology.standalone.add_s(Entry((ACTIVE_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                                'cn': ACTIVE_USER1_CN,
                                                'sn': ACTIVE_USER1_CN,
                                                ALLOCATED_ATTR: str(20)})))
    ent = topology.standalone.getEntry(ACTIVE_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ent.getValue(ALLOCATED_ATTR) == str(20)
    topology.standalone.log.debug('%s.%s=%s' % (ACTIVE_USER1_CN, ALLOCATED_ATTR, ent.getValue(ALLOCATED_ATTR)))
    topology.standalone.delete_s(ACTIVE_USER1_DN)
    
def test_ticket47828_run_7(topology):
    """
    Provisioning excluded scope: Add a staged entry and check its ALLOCATED_ATTR is  not set
    """
    _header(topology, 'Provisioning excluded scope: Add a staged entry and check its ALLOCATED_ATTR is  not set')

    topology.standalone.add_s(Entry((STAGED_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                                'cn': STAGED_USER1_CN,
                                                'sn': STAGED_USER1_CN,
                                                ALLOCATED_ATTR: str(-1)})))
    ent = topology.standalone.getEntry(STAGED_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ent.getValue(ALLOCATED_ATTR) == str(-1)
    topology.standalone.log.debug('%s.%s=%s' % (STAGED_USER1_CN, ALLOCATED_ATTR, ent.getValue(ALLOCATED_ATTR)))
    topology.standalone.delete_s(STAGED_USER1_DN)
    
def test_ticket47828_run_8(topology):
    """
    Provisioning excluded scope: Add a staged entry and check its ALLOCATED_ATTR is unchanged (!= magic)
    """
    _header(topology, 'Provisioning excluded scope: Add a staged entry and check its ALLOCATED_ATTR is unchanged (!= magic)')

    topology.standalone.add_s(Entry((STAGED_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                                'cn': STAGED_USER1_CN,
                                                'sn': STAGED_USER1_CN,
                                                ALLOCATED_ATTR: str(20)})))
    ent = topology.standalone.getEntry(STAGED_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ent.getValue(ALLOCATED_ATTR) == str(20)
    topology.standalone.log.debug('%s.%s=%s' % (STAGED_USER1_CN, ALLOCATED_ATTR, ent.getValue(ALLOCATED_ATTR)))
    topology.standalone.delete_s(STAGED_USER1_DN)
    
def test_ticket47828_run_9(topology):
    """
    Provisioning excluded scope: Add an dummy entry and check its ALLOCATED_ATTR is set
    """
    _header(topology, 'Provisioning excluded scope: Add an dummy entry and check its ALLOCATED_ATTR is set')

    topology.standalone.add_s(Entry((DUMMY_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                                'cn': DUMMY_USER1_CN,
                                                'sn': DUMMY_USER1_CN,
                                                ALLOCATED_ATTR: str(-1)})))
    ent = topology.standalone.getEntry(DUMMY_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ent.getValue(ALLOCATED_ATTR) != str(-1)
    topology.standalone.log.debug('%s.%s=%s' % (DUMMY_USER1_CN, ALLOCATED_ATTR, ent.getValue(ALLOCATED_ATTR)))
    topology.standalone.delete_s(DUMMY_USER1_DN)
    
def test_ticket47828_run_10(topology):
    """
    Provisioning excluded scope: Add an dummy entry and check its ALLOCATED_ATTR is unchanged (!= magic)
    """
    _header(topology, 'Provisioning excluded scope: Add an dummy entry and check its ALLOCATED_ATTR is unchanged (!= magic)')

    topology.standalone.add_s(Entry((DUMMY_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                                'cn': DUMMY_USER1_CN,
                                                'sn': DUMMY_USER1_CN,
                                                ALLOCATED_ATTR: str(20)})))
    ent = topology.standalone.getEntry(DUMMY_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ent.getValue(ALLOCATED_ATTR) == str(20)
    topology.standalone.log.debug('%s.%s=%s' % (DUMMY_USER1_CN, ALLOCATED_ATTR, ent.getValue(ALLOCATED_ATTR)))
    topology.standalone.delete_s(DUMMY_USER1_DN)
    
def test_ticket47828_run_11(topology):
    '''
    Exclude (in addition) the dummy container
    '''
    _header(topology, 'Exclude (in addition) the dummy container')
    
    dn_config = "cn=excluded scope, cn=%s, %s" % (PLUGIN_DNA, DN_PLUGIN)
    mod = [(ldap.MOD_ADD, 'dnaExcludeScope', DUMMY_CONTAINER)]
    topology.standalone.modify_s(dn_config, mod)
    
def test_ticket47828_run_12(topology):
    """
    Provisioning/Dummy excluded scope: Add an active entry and check its ALLOCATED_ATTR is set
    """
    _header(topology, 'Provisioning/Dummy excluded scope: Add an active entry and check its ALLOCATED_ATTR is set')

    topology.standalone.add_s(Entry((ACTIVE_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                                'cn': ACTIVE_USER1_CN,
                                                'sn': ACTIVE_USER1_CN,
                                                ALLOCATED_ATTR: str(-1)})))
    ent = topology.standalone.getEntry(ACTIVE_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ent.getValue(ALLOCATED_ATTR) != str(-1)
    topology.standalone.log.debug('%s.%s=%s' % (ACTIVE_USER1_CN, ALLOCATED_ATTR, ent.getValue(ALLOCATED_ATTR)))
    topology.standalone.delete_s(ACTIVE_USER1_DN)
    
def test_ticket47828_run_13(topology):
    """
    Provisioning/Dummy excluded scope: Add an active entry and check its ALLOCATED_ATTR is unchanged (!= magic)
    """
    _header(topology, 'Provisioning/Dummy excluded scope: Add an active entry and check its ALLOCATED_ATTR is unchanged (!= magic)')

    topology.standalone.add_s(Entry((ACTIVE_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                                'cn': ACTIVE_USER1_CN,
                                                'sn': ACTIVE_USER1_CN,
                                                ALLOCATED_ATTR: str(20)})))
    ent = topology.standalone.getEntry(ACTIVE_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ent.getValue(ALLOCATED_ATTR) == str(20)
    topology.standalone.log.debug('%s.%s=%s' % (ACTIVE_USER1_CN, ALLOCATED_ATTR, ent.getValue(ALLOCATED_ATTR)))
    topology.standalone.delete_s(ACTIVE_USER1_DN)
    
def test_ticket47828_run_14(topology):
    """
    Provisioning/Dummy excluded scope: Add a staged entry and check its ALLOCATED_ATTR is not set
    """
    _header(topology, 'Provisioning/Dummy excluded scope: Add a staged entry and check its ALLOCATED_ATTR is not set')

    topology.standalone.add_s(Entry((STAGED_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                                'cn': STAGED_USER1_CN,
                                                'sn': STAGED_USER1_CN,
                                                ALLOCATED_ATTR: str(-1)})))
    ent = topology.standalone.getEntry(STAGED_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ent.getValue(ALLOCATED_ATTR) == str(-1)
    topology.standalone.log.debug('%s.%s=%s' % (STAGED_USER1_CN, ALLOCATED_ATTR, ent.getValue(ALLOCATED_ATTR)))
    topology.standalone.delete_s(STAGED_USER1_DN)
    
def test_ticket47828_run_15(topology):
    """
    Provisioning/Dummy excluded scope: Add a staged entry and check its ALLOCATED_ATTR is unchanged (!= magic)
    """
    _header(topology, 'Provisioning/Dummy excluded scope: Add a staged entry and check its ALLOCATED_ATTR is unchanged (!= magic)')

    topology.standalone.add_s(Entry((STAGED_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                                'cn': STAGED_USER1_CN,
                                                'sn': STAGED_USER1_CN,
                                                ALLOCATED_ATTR: str(20)})))
    ent = topology.standalone.getEntry(STAGED_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ent.getValue(ALLOCATED_ATTR) == str(20)
    topology.standalone.log.debug('%s.%s=%s' % (STAGED_USER1_CN, ALLOCATED_ATTR, ent.getValue(ALLOCATED_ATTR)))
    topology.standalone.delete_s(STAGED_USER1_DN)
    
def test_ticket47828_run_16(topology):
    """
    Provisioning/Dummy excluded scope: Add an dummy entry and check its ALLOCATED_ATTR is not set
    """
    _header(topology, 'Provisioning/Dummy excluded scope: Add an dummy entry and check its ALLOCATED_ATTR not is set')

    topology.standalone.add_s(Entry((DUMMY_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                                'cn': DUMMY_USER1_CN,
                                                'sn': DUMMY_USER1_CN,
                                                ALLOCATED_ATTR: str(-1)})))
    ent = topology.standalone.getEntry(DUMMY_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ent.getValue(ALLOCATED_ATTR) == str(-1)
    topology.standalone.log.debug('%s.%s=%s' % (DUMMY_USER1_CN, ALLOCATED_ATTR, ent.getValue(ALLOCATED_ATTR)))
    topology.standalone.delete_s(DUMMY_USER1_DN)
    
def test_ticket47828_run_17(topology):
    """
    Provisioning/Dummy excluded scope: Add an dummy entry and check its ALLOCATED_ATTR is unchanged (!= magic)
    """
    _header(topology, 'Provisioning/Dummy excluded scope: Add an dummy entry and check its ALLOCATED_ATTR is unchanged (!= magic)')

    topology.standalone.add_s(Entry((DUMMY_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                                'cn': DUMMY_USER1_CN,
                                                'sn': DUMMY_USER1_CN,
                                                ALLOCATED_ATTR: str(20)})))
    ent = topology.standalone.getEntry(DUMMY_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ent.getValue(ALLOCATED_ATTR) == str(20)
    topology.standalone.log.debug('%s.%s=%s' % (DUMMY_USER1_CN, ALLOCATED_ATTR, ent.getValue(ALLOCATED_ATTR)))
    topology.standalone.delete_s(DUMMY_USER1_DN)
    
    
def test_ticket47828_run_18(topology):
    '''
    Exclude PROVISIONING and a wrong container
    '''
    _header(topology, 'Exclude PROVISIONING and a wrong container')
    
    dn_config = "cn=excluded scope, cn=%s, %s" % (PLUGIN_DNA, DN_PLUGIN)
    mod = [(ldap.MOD_REPLACE, 'dnaExcludeScope', PROVISIONING)]
    topology.standalone.modify_s(dn_config, mod)
    try:
        mod = [(ldap.MOD_ADD, 'dnaExcludeScope', "invalidDN,%s" % SUFFIX)]
        topology.standalone.modify_s(dn_config, mod)
        raise ValueError("invalid dnaExcludeScope value (not a DN)")
    except ldap.INVALID_SYNTAX:
        pass
    
def test_ticket47828_run_19(topology):
    """
    Provisioning+wrong container excluded scope: Add an active entry and check its ALLOCATED_ATTR is set
    """
    _header(topology, 'Provisioning+wrong container excluded scope: Add an active entry and check its ALLOCATED_ATTR is set')

    topology.standalone.add_s(Entry((ACTIVE_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                                'cn': ACTIVE_USER1_CN,
                                                'sn': ACTIVE_USER1_CN,
                                                ALLOCATED_ATTR: str(-1)})))
    ent = topology.standalone.getEntry(ACTIVE_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ent.getValue(ALLOCATED_ATTR) != str(-1)
    topology.standalone.log.debug('%s.%s=%s' % (ACTIVE_USER1_CN, ALLOCATED_ATTR, ent.getValue(ALLOCATED_ATTR)))
    topology.standalone.delete_s(ACTIVE_USER1_DN)
    
def test_ticket47828_run_20(topology):
    """
    Provisioning+wrong container excluded scope: Add an active entry and check its ALLOCATED_ATTR is unchanged (!= magic)
    """
    _header(topology, 'Provisioning+wrong container excluded scope: Add an active entry and check its ALLOCATED_ATTR is unchanged (!= magic)')

    topology.standalone.add_s(Entry((ACTIVE_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                                'cn': ACTIVE_USER1_CN,
                                                'sn': ACTIVE_USER1_CN,
                                                ALLOCATED_ATTR: str(20)})))
    ent = topology.standalone.getEntry(ACTIVE_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ent.getValue(ALLOCATED_ATTR) == str(20)
    topology.standalone.log.debug('%s.%s=%s' % (ACTIVE_USER1_CN, ALLOCATED_ATTR, ent.getValue(ALLOCATED_ATTR)))
    topology.standalone.delete_s(ACTIVE_USER1_DN)
    
def test_ticket47828_run_21(topology):
    """
    Provisioning+wrong container excluded scope: Add a staged entry and check its ALLOCATED_ATTR is  not set
    """
    _header(topology, 'Provisioning+wrong container excluded scope: Add a staged entry and check its ALLOCATED_ATTR is  not set')

    topology.standalone.add_s(Entry((STAGED_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                                'cn': STAGED_USER1_CN,
                                                'sn': STAGED_USER1_CN,
                                                ALLOCATED_ATTR: str(-1)})))
    ent = topology.standalone.getEntry(STAGED_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ent.getValue(ALLOCATED_ATTR) == str(-1)
    topology.standalone.log.debug('%s.%s=%s' % (STAGED_USER1_CN, ALLOCATED_ATTR, ent.getValue(ALLOCATED_ATTR)))
    topology.standalone.delete_s(STAGED_USER1_DN)
    
def test_ticket47828_run_22(topology):
    """
    Provisioning+wrong container excluded scope: Add a staged entry and check its ALLOCATED_ATTR is unchanged (!= magic)
    """
    _header(topology, 'Provisioning+wrong container excluded scope: Add a staged entry and check its ALLOCATED_ATTR is unchanged (!= magic)')

    topology.standalone.add_s(Entry((STAGED_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                                'cn': STAGED_USER1_CN,
                                                'sn': STAGED_USER1_CN,
                                                ALLOCATED_ATTR: str(20)})))
    ent = topology.standalone.getEntry(STAGED_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ent.getValue(ALLOCATED_ATTR) == str(20)
    topology.standalone.log.debug('%s.%s=%s' % (STAGED_USER1_CN, ALLOCATED_ATTR, ent.getValue(ALLOCATED_ATTR)))
    topology.standalone.delete_s(STAGED_USER1_DN)
    
def test_ticket47828_run_23(topology):
    """
    Provisioning+wrong container excluded scope: Add an dummy entry and check its ALLOCATED_ATTR is set
    """
    _header(topology, 'Provisioning+wrong container excluded scope: Add an dummy entry and check its ALLOCATED_ATTR is set')

    topology.standalone.add_s(Entry((DUMMY_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                                'cn': DUMMY_USER1_CN,
                                                'sn': DUMMY_USER1_CN,
                                                ALLOCATED_ATTR: str(-1)})))
    ent = topology.standalone.getEntry(DUMMY_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ent.getValue(ALLOCATED_ATTR) != str(-1)
    topology.standalone.log.debug('%s.%s=%s' % (DUMMY_USER1_CN, ALLOCATED_ATTR, ent.getValue(ALLOCATED_ATTR)))
    topology.standalone.delete_s(DUMMY_USER1_DN)
    
def test_ticket47828_run_24(topology):
    """
    Provisioning+wrong container excluded scope: Add an dummy entry and check its ALLOCATED_ATTR is unchanged (!= magic)
    """
    _header(topology, 'Provisioning+wrong container excluded scope: Add an dummy entry and check its ALLOCATED_ATTR is unchanged (!= magic)')

    topology.standalone.add_s(Entry((DUMMY_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                                'cn': DUMMY_USER1_CN,
                                                'sn': DUMMY_USER1_CN,
                                                ALLOCATED_ATTR: str(20)})))
    ent = topology.standalone.getEntry(DUMMY_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ent.getValue(ALLOCATED_ATTR) == str(20)
    topology.standalone.log.debug('%s.%s=%s' % (DUMMY_USER1_CN, ALLOCATED_ATTR, ent.getValue(ALLOCATED_ATTR)))
    topology.standalone.delete_s(DUMMY_USER1_DN)
    
def test_ticket47828_run_25(topology):
    '''
    Exclude  a wrong container
    '''
    _header(topology, 'Exclude a wrong container')
    
    dn_config = "cn=excluded scope, cn=%s, %s" % (PLUGIN_DNA, DN_PLUGIN)
    
    try:
        mod = [(ldap.MOD_REPLACE, 'dnaExcludeScope', "invalidDN,%s" % SUFFIX)]
        topology.standalone.modify_s(dn_config, mod)
        raise ValueError("invalid dnaExcludeScope value (not a DN)")
    except ldap.INVALID_SYNTAX:
        pass
    
def test_ticket47828_run_26(topology):
    """
    Wrong container excluded scope: Add an active entry and check its ALLOCATED_ATTR is set
    """
    _header(topology, 'Wrong container excluded scope: Add an active entry and check its ALLOCATED_ATTR is set')

    topology.standalone.add_s(Entry((ACTIVE_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                                'cn': ACTIVE_USER1_CN,
                                                'sn': ACTIVE_USER1_CN,
                                                ALLOCATED_ATTR: str(-1)})))
    ent = topology.standalone.getEntry(ACTIVE_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ent.getValue(ALLOCATED_ATTR) != str(-1)
    topology.standalone.log.debug('%s.%s=%s' % (ACTIVE_USER1_CN, ALLOCATED_ATTR, ent.getValue(ALLOCATED_ATTR)))
    topology.standalone.delete_s(ACTIVE_USER1_DN)
    
def test_ticket47828_run_27(topology):
    """
    Wrong container excluded scope: Add an active entry and check its ALLOCATED_ATTR is unchanged (!= magic)
    """
    _header(topology, 'Wrong container excluded scope: Add an active entry and check its ALLOCATED_ATTR is unchanged (!= magic)')

    topology.standalone.add_s(Entry((ACTIVE_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                                'cn': ACTIVE_USER1_CN,
                                                'sn': ACTIVE_USER1_CN,
                                                ALLOCATED_ATTR: str(20)})))
    ent = topology.standalone.getEntry(ACTIVE_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ent.getValue(ALLOCATED_ATTR) == str(20)
    topology.standalone.log.debug('%s.%s=%s' % (ACTIVE_USER1_CN, ALLOCATED_ATTR, ent.getValue(ALLOCATED_ATTR)))
    topology.standalone.delete_s(ACTIVE_USER1_DN)
    
def test_ticket47828_run_28(topology):
    """
    Wrong container excluded scope: Add a staged entry and check its ALLOCATED_ATTR is  not set
    """
    _header(topology, 'Wrong container excluded scope: Add a staged entry and check its ALLOCATED_ATTR is  not set')

    topology.standalone.add_s(Entry((STAGED_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                                'cn': STAGED_USER1_CN,
                                                'sn': STAGED_USER1_CN,
                                                ALLOCATED_ATTR: str(-1)})))
    ent = topology.standalone.getEntry(STAGED_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ent.getValue(ALLOCATED_ATTR) == str(-1)
    topology.standalone.log.debug('%s.%s=%s' % (STAGED_USER1_CN, ALLOCATED_ATTR, ent.getValue(ALLOCATED_ATTR)))
    topology.standalone.delete_s(STAGED_USER1_DN)
    
def test_ticket47828_run_29(topology):
    """
    Wrong container excluded scope: Add a staged entry and check its ALLOCATED_ATTR is unchanged (!= magic)
    """
    _header(topology, 'Wrong container excluded scope: Add a staged entry and check its ALLOCATED_ATTR is unchanged (!= magic)')

    topology.standalone.add_s(Entry((STAGED_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                                'cn': STAGED_USER1_CN,
                                                'sn': STAGED_USER1_CN,
                                                ALLOCATED_ATTR: str(20)})))
    ent = topology.standalone.getEntry(STAGED_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ent.getValue(ALLOCATED_ATTR) == str(20)
    topology.standalone.log.debug('%s.%s=%s' % (STAGED_USER1_CN, ALLOCATED_ATTR, ent.getValue(ALLOCATED_ATTR)))
    topology.standalone.delete_s(STAGED_USER1_DN)
    
def test_ticket47828_run_30(topology):
    """
    Wrong container excluded scope: Add an dummy entry and check its ALLOCATED_ATTR is set
    """
    _header(topology, 'Wrong container excluded scope: Add an dummy entry and check its ALLOCATED_ATTR is set')

    topology.standalone.add_s(Entry((DUMMY_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                                'cn': DUMMY_USER1_CN,
                                                'sn': DUMMY_USER1_CN,
                                                ALLOCATED_ATTR: str(-1)})))
    ent = topology.standalone.getEntry(DUMMY_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ent.getValue(ALLOCATED_ATTR) != str(-1)
    topology.standalone.log.debug('%s.%s=%s' % (DUMMY_USER1_CN, ALLOCATED_ATTR, ent.getValue(ALLOCATED_ATTR)))
    topology.standalone.delete_s(DUMMY_USER1_DN)
    
def test_ticket47828_run_31(topology):
    """
    Wrong container excluded scope: Add an dummy entry and check its ALLOCATED_ATTR is unchanged (!= magic)
    """
    _header(topology, 'Wrong container excluded scope: Add an dummy entry and check its ALLOCATED_ATTR is unchanged (!= magic)')

    topology.standalone.add_s(Entry((DUMMY_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                                'cn': DUMMY_USER1_CN,
                                                'sn': DUMMY_USER1_CN,
                                                ALLOCATED_ATTR: str(20)})))
    ent = topology.standalone.getEntry(DUMMY_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ent.getValue(ALLOCATED_ATTR) == str(20)
    topology.standalone.log.debug('%s.%s=%s' % (DUMMY_USER1_CN, ALLOCATED_ATTR, ent.getValue(ALLOCATED_ATTR)))
    topology.standalone.delete_s(DUMMY_USER1_DN)
    
def test_ticket47828_final(topology):
    topology.standalone.plugins.disable(name=PLUGIN_DNA)
    topology.standalone.stop(timeout=10)

def run_isolated():
    '''
        run_isolated is used to run these test cases independently of a test scheduler (xunit, py.test..)
        To run isolated without py.test, you need to
            - edit this file and comment '@pytest.fixture' line before 'topology' function.
            - set the installation prefix
            - run this program
    '''
    global installation_prefix
    installation_prefix = None

    topo = topology(True)
    test_ticket47828_init(topo)
    
    test_ticket47828_run_0(topo)
    test_ticket47828_run_1(topo)
    test_ticket47828_run_2(topo)
    test_ticket47828_run_3(topo)
    test_ticket47828_run_4(topo)
    test_ticket47828_run_5(topo)
    test_ticket47828_run_6(topo)
    test_ticket47828_run_7(topo)
    test_ticket47828_run_8(topo)
    test_ticket47828_run_9(topo)
    test_ticket47828_run_10(topo)
    test_ticket47828_run_11(topo)
    test_ticket47828_run_12(topo)
    test_ticket47828_run_13(topo)
    test_ticket47828_run_14(topo)
    test_ticket47828_run_15(topo)
    test_ticket47828_run_16(topo)
    test_ticket47828_run_17(topo)
    test_ticket47828_run_18(topo)
    test_ticket47828_run_19(topo)
    test_ticket47828_run_20(topo)
    test_ticket47828_run_21(topo)
    test_ticket47828_run_22(topo)
    test_ticket47828_run_23(topo)
    test_ticket47828_run_24(topo)
    test_ticket47828_run_25(topo)
    test_ticket47828_run_26(topo)
    test_ticket47828_run_27(topo)
    test_ticket47828_run_28(topo)
    test_ticket47828_run_29(topo)
    test_ticket47828_run_30(topo)
    test_ticket47828_run_31(topo)
    
    test_ticket47828_final(topo)
    

if __name__ == '__main__':
    run_isolated()
