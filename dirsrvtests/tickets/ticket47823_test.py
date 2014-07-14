import os
import sys
import time
import ldap
import logging
import socket
import pytest
import re
import shutil
from lib389 import DirSrv, Entry, tools
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from constants import *


log = logging.getLogger(__name__)

installation_prefix = None

PROVISIONING_CN = "provisioning" 
PROVISIONING_DN = "cn=%s,%s" % (PROVISIONING_CN, SUFFIX) 

ACTIVE_CN = "accounts" 
STAGE_CN  = "staged users" 
DELETE_CN = "deleted users" 
ACTIVE_DN = "cn=%s,%s" % (ACTIVE_CN, SUFFIX) 
STAGE_DN  = "cn=%s,%s" % (STAGE_CN, PROVISIONING_DN) 
DELETE_DN  = "cn=%s,%s" % (DELETE_CN, PROVISIONING_DN) 

STAGE_USER_CN = "stage guy" 
STAGE_USER_DN = "cn=%s,%s" % (STAGE_USER_CN, STAGE_DN) 

ACTIVE_USER_CN = "active guy" 
ACTIVE_USER_DN = "cn=%s,%s" % (ACTIVE_USER_CN, ACTIVE_DN)

ACTIVE_USER_1_CN = "test_1"
ACTIVE_USER_1_DN = "cn=%s,%s" % (ACTIVE_USER_1_CN, ACTIVE_DN)
ACTIVE_USER_2_CN = "test_2"
ACTIVE_USER_2_DN = "cn=%s,%s" % (ACTIVE_USER_2_CN, ACTIVE_DN)

STAGE_USER_1_CN = ACTIVE_USER_1_CN
STAGE_USER_1_DN = "cn=%s,%s" % (STAGE_USER_1_CN, STAGE_DN)
STAGE_USER_2_CN = ACTIVE_USER_2_CN
STAGE_USER_2_DN = "cn=%s,%s" % (STAGE_USER_2_CN, STAGE_DN)

ALL_CONFIG_ATTRS = ['nsslapd-pluginarg0', 'nsslapd-pluginarg1', 'nsslapd-pluginarg2', 
                    'uniqueness-attribute-name', 'uniqueness-subtrees', 'uniqueness-across-all-subtrees']

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

    # clear the tmp directory
    standalone.clearTmpDir(__file__)

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

def _uniqueness_config_entry(topology, name=None):
    if not name:
        return None
    
    ent = topology.standalone.getEntry("cn=%s,%s" % (PLUGIN_ATTR_UNIQUENESS, DN_PLUGIN), ldap.SCOPE_BASE, 
                                    "(objectclass=nsSlapdPlugin)",
                                    ['objectClass', 'cn', 'nsslapd-pluginPath', 'nsslapd-pluginInitfunc',
                                     'nsslapd-pluginType', 'nsslapd-pluginEnabled', 'nsslapd-plugin-depends-on-type',
                                     'nsslapd-pluginId', 'nsslapd-pluginVersion', 'nsslapd-pluginVendor',
                                     'nsslapd-pluginDescription'])
    ent.dn = "cn=%s uniqueness,%s" % (name, DN_PLUGIN)
    return ent

def _build_config(topology, attr_name='cn', subtree_1=None, subtree_2=None, type_config='old', across_subtrees=False):
    assert topology
    assert attr_name
    assert subtree_1
    
    if type_config == 'old':
        # enable the 'cn' uniqueness on Active
        config = _uniqueness_config_entry(topology, attr_name)
        config.setValue('nsslapd-pluginarg0', attr_name)
        config.setValue('nsslapd-pluginarg1', subtree_1)
        if subtree_2:
            config.setValue('nsslapd-pluginarg2', subtree_2)
    else:
        # prepare the config entry
        config = _uniqueness_config_entry(topology, attr_name)
        config.setValue('uniqueness-attribute-name', attr_name)
        config.setValue('uniqueness-subtrees', subtree_1)
        if subtree_2:
            config.setValue('uniqueness-subtrees', subtree_2)
        if across_subtrees:
            config.setValue('uniqueness-across-all-subtrees', 'on')
    return config

def _active_container_invalid_cfg_add(topology):
    '''
    Check uniqueness is not enforced with ADD (invalid config)
    '''
    topology.standalone.add_s(Entry((ACTIVE_USER_1_DN, {
                                            'objectclass': "top person".split(),
                                            'sn':           ACTIVE_USER_1_CN,
                                            'cn':           ACTIVE_USER_1_CN})))

    topology.standalone.add_s(Entry((ACTIVE_USER_2_DN, {
                                        'objectclass': "top person".split(),
                                        'sn':           ACTIVE_USER_2_CN,
                                        'cn':           [ACTIVE_USER_1_CN, ACTIVE_USER_2_CN]})))
    
    topology.standalone.delete_s(ACTIVE_USER_1_DN)
    topology.standalone.delete_s(ACTIVE_USER_2_DN)

def _active_container_add(topology, type_config='old'):
    '''
    Check uniqueness in a single container (Active)
    Add an entry with a given 'cn', then check we can not add an entry with the same 'cn' value
    
    '''
    config = _build_config(topology, attr_name='cn', subtree_1=ACTIVE_DN, subtree_2=None, type_config=type_config, across_subtrees=False)
        
    # remove the 'cn' uniqueness entry
    try:
        topology.standalone.delete_s(config.dn)
        
    except ldap.NO_SUCH_OBJECT:
        pass
    topology.standalone.restart(timeout=120)
    
    topology.standalone.log.info('Uniqueness not enforced: create the entries')
    
    topology.standalone.add_s(Entry((ACTIVE_USER_1_DN, {
                                            'objectclass': "top person".split(),
                                            'sn':           ACTIVE_USER_1_CN,
                                            'cn':           ACTIVE_USER_1_CN})))

    topology.standalone.add_s(Entry((ACTIVE_USER_2_DN, {
                                        'objectclass': "top person".split(),
                                        'sn':           ACTIVE_USER_2_CN,
                                        'cn':           [ACTIVE_USER_1_CN, ACTIVE_USER_2_CN]})))
    
    topology.standalone.delete_s(ACTIVE_USER_1_DN)
    topology.standalone.delete_s(ACTIVE_USER_2_DN)
    
    
    topology.standalone.log.info('Uniqueness enforced: checks second entry is rejected')
    
    # enable the 'cn' uniqueness on Active  
    topology.standalone.add_s(config)
    topology.standalone.restart(timeout=120)
    topology.standalone.add_s(Entry((ACTIVE_USER_1_DN, {
                                            'objectclass': "top person".split(),
                                            'sn':           ACTIVE_USER_1_CN,
                                            'cn':           ACTIVE_USER_1_CN})))

    try:
        topology.standalone.add_s(Entry((ACTIVE_USER_2_DN, {
                                        'objectclass': "top person".split(),
                                        'sn':           ACTIVE_USER_2_CN,
                                        'cn':           [ACTIVE_USER_1_CN, ACTIVE_USER_2_CN]})))
    except ldap.CONSTRAINT_VIOLATION:
        # yes it is expected
        pass
    
    # cleanup the stuff now
    topology.standalone.delete_s(config.dn)
    topology.standalone.delete_s(ACTIVE_USER_1_DN)


    
def _active_container_mod(topology, type_config='old'):
    '''
    Check uniqueness in a single container (active)
    Add and entry with a given 'cn', then check we can not modify an entry with the same 'cn' value
    
    '''
    
    config = _build_config(topology, attr_name='cn', subtree_1=ACTIVE_DN, subtree_2=None, type_config=type_config, across_subtrees=False)
    
    # enable the 'cn' uniqueness on Active
    topology.standalone.add_s(config)
    topology.standalone.restart(timeout=120)
    
    topology.standalone.log.info('Uniqueness enforced: checks MOD ADD entry is rejected')
    topology.standalone.add_s(Entry((ACTIVE_USER_1_DN, {
                                            'objectclass': "top person".split(),
                                            'sn':           ACTIVE_USER_1_CN,
                                            'cn':           ACTIVE_USER_1_CN})))

    topology.standalone.add_s(Entry((ACTIVE_USER_2_DN, {
                                    'objectclass': "top person".split(),
                                    'sn':           ACTIVE_USER_2_CN,
                                    'cn':           ACTIVE_USER_2_CN})))

    try:
        topology.standalone.modify_s(ACTIVE_USER_2_DN, [(ldap.MOD_ADD, 'cn', ACTIVE_USER_1_CN)])
    except ldap.CONSTRAINT_VIOLATION:
        # yes it is expected
        pass
    
    topology.standalone.log.info('Uniqueness enforced: checks MOD REPLACE entry is rejected')
    try:
        topology.standalone.modify_s(ACTIVE_USER_2_DN, [(ldap.MOD_REPLACE, 'cn', [ACTIVE_USER_1_CN, ACTIVE_USER_2_CN])])
    except ldap.CONSTRAINT_VIOLATION:
        # yes it is expected
        pass
    
    # cleanup the stuff now
    topology.standalone.delete_s(config.dn)
    topology.standalone.delete_s(ACTIVE_USER_1_DN)
    topology.standalone.delete_s(ACTIVE_USER_2_DN)
    
def _active_container_modrdn(topology, type_config='old'):
    '''
    Check uniqueness in a single container
    Add and entry with a given 'cn', then check we can not modrdn an entry with the same 'cn' value
    
    '''
    config = _build_config(topology, attr_name='cn', subtree_1=ACTIVE_DN, subtree_2=None, type_config=type_config, across_subtrees=False)
    
    # enable the 'cn' uniqueness on Active
    topology.standalone.add_s(config)
    topology.standalone.restart(timeout=120)
    
    topology.standalone.log.info('Uniqueness enforced: checks MODRDN entry is rejected')
    
    topology.standalone.add_s(Entry((ACTIVE_USER_1_DN, {
                                            'objectclass': "top person".split(),
                                            'sn':           ACTIVE_USER_1_CN,
                                            'cn':           [ACTIVE_USER_1_CN, 'dummy']})))

    topology.standalone.add_s(Entry((ACTIVE_USER_2_DN, {
                                    'objectclass': "top person".split(),
                                    'sn':           ACTIVE_USER_2_CN,
                                    'cn':           ACTIVE_USER_2_CN})))

    try:
        topology.standalone.rename_s(ACTIVE_USER_2_DN, 'cn=dummy', delold=0)
    except ldap.CONSTRAINT_VIOLATION:
        # yes it is expected
        pass
    
    
    # cleanup the stuff now
    topology.standalone.delete_s(config.dn)
    topology.standalone.delete_s(ACTIVE_USER_1_DN)
    topology.standalone.delete_s(ACTIVE_USER_2_DN)

def _active_stage_containers_add(topology, type_config='old', across_subtrees=False):
    '''
    Check uniqueness in several containers
    Add an entry on a container with a given 'cn'
    with across_subtrees=False check we CAN add an entry with the same 'cn' value on the other container
    with across_subtrees=True check we CAN NOT add an entry with the same 'cn' value on the other container
    
    '''
    config = _build_config(topology, attr_name='cn', subtree_1=ACTIVE_DN, subtree_2=STAGE_DN, type_config=type_config, across_subtrees=False)
    
    topology.standalone.add_s(config)
    topology.standalone.restart(timeout=120)
    topology.standalone.add_s(Entry((ACTIVE_USER_1_DN, {
                                            'objectclass': "top person".split(),
                                            'sn':           ACTIVE_USER_1_CN,
                                            'cn':           ACTIVE_USER_1_CN})))
    try:

        # adding an entry on a separated contains with the same 'cn'
        topology.standalone.add_s(Entry((STAGE_USER_1_DN, {
                                    'objectclass': "top person".split(),
                                    'sn':           STAGE_USER_1_CN,
                                    'cn':           ACTIVE_USER_1_CN})))
    except ldap.CONSTRAINT_VIOLATION:
            assert across_subtrees
    
    # cleanup the stuff now
    topology.standalone.delete_s(config.dn)
    topology.standalone.delete_s(ACTIVE_USER_1_DN)
    topology.standalone.delete_s(STAGE_USER_1_DN)
    
def _active_stage_containers_mod(topology, type_config='old', across_subtrees=False):
    '''
    Check uniqueness in a several containers
    Add an entry on a container with a given 'cn', then check we CAN mod an entry with the same 'cn' value on the other container
    
    '''
    config = _build_config(topology, attr_name='cn', subtree_1=ACTIVE_DN, subtree_2=STAGE_DN, type_config=type_config, across_subtrees=False)
    
    topology.standalone.add_s(config)
    topology.standalone.restart(timeout=120)
    # adding an entry on active with a different 'cn'
    topology.standalone.add_s(Entry((ACTIVE_USER_1_DN, {
                                            'objectclass': "top person".split(),
                                            'sn':           ACTIVE_USER_1_CN,
                                            'cn':           ACTIVE_USER_2_CN})))

    # adding an entry on a stage with a different 'cn'
    topology.standalone.add_s(Entry((STAGE_USER_1_DN, {
                                    'objectclass': "top person".split(),
                                    'sn':           STAGE_USER_1_CN,
                                    'cn':           STAGE_USER_1_CN})))
    
    try:
    
        # modify add same value
        topology.standalone.modify_s(STAGE_USER_1_DN, [(ldap.MOD_ADD, 'cn', [ACTIVE_USER_2_CN])])
    except ldap.CONSTRAINT_VIOLATION:
        assert across_subtrees
    
    topology.standalone.delete_s(STAGE_USER_1_DN)
    topology.standalone.add_s(Entry((STAGE_USER_1_DN, {
                                    'objectclass': "top person".split(),
                                    'sn':           STAGE_USER_1_CN,
                                    'cn':           STAGE_USER_2_CN})))
    try:
        # modify replace same value
        topology.standalone.modify_s(STAGE_USER_1_DN, [(ldap.MOD_REPLACE, 'cn', [STAGE_USER_2_CN, ACTIVE_USER_1_CN])])
    except ldap.CONSTRAINT_VIOLATION:
        assert across_subtrees
    
    # cleanup the stuff now
    topology.standalone.delete_s(config.dn)
    topology.standalone.delete_s(ACTIVE_USER_1_DN)
    topology.standalone.delete_s(STAGE_USER_1_DN)
    
def _active_stage_containers_modrdn(topology, type_config='old', across_subtrees=False):
    '''
    Check uniqueness in a several containers
    Add and entry with a given 'cn', then check we CAN modrdn an entry with the same 'cn' value on the other container
    
    '''
    
    config = _build_config(topology, attr_name='cn', subtree_1=ACTIVE_DN, subtree_2=STAGE_DN, type_config=type_config, across_subtrees=False)
    
    # enable the 'cn' uniqueness on Active and Stage
    topology.standalone.add_s(config)
    topology.standalone.restart(timeout=120)
    topology.standalone.add_s(Entry((ACTIVE_USER_1_DN, {
                                            'objectclass': "top person".split(),
                                            'sn':           ACTIVE_USER_1_CN,
                                            'cn':           [ACTIVE_USER_1_CN, 'dummy']})))

    topology.standalone.add_s(Entry((STAGE_USER_1_DN, {
                                    'objectclass': "top person".split(),
                                    'sn':           STAGE_USER_1_CN,
                                    'cn':           STAGE_USER_1_CN})))


    try:
        
        topology.standalone.rename_s(STAGE_USER_1_DN, 'cn=dummy', delold=0)
        
        # check stage entry has 'cn=dummy'
        stage_ent = topology.standalone.getEntry("cn=dummy,%s" % (STAGE_DN), ldap.SCOPE_BASE, "objectclass=*", ['cn'])
        assert stage_ent.hasAttr('cn')
        found = False
        for value in stage_ent.getValues('cn'):
            if value == 'dummy':
                found = True
        assert found
        
        # check active entry has 'cn=dummy'
        active_ent = topology.standalone.getEntry(ACTIVE_USER_1_DN, ldap.SCOPE_BASE, "objectclass=*", ['cn'])
        assert active_ent.hasAttr('cn')
        found = False
        for value in stage_ent.getValues('cn'):
            if value == 'dummy':
                found = True
        assert found
        
        topology.standalone.delete_s("cn=dummy,%s" % (STAGE_DN))
    except ldap.CONSTRAINT_VIOLATION:
        assert across_subtrees
        topology.standalone.delete_s(STAGE_USER_1_DN)
    
    
    
    # cleanup the stuff now
    topology.standalone.delete_s(config.dn)
    topology.standalone.delete_s(ACTIVE_USER_1_DN)
    
def _config_file(topology, action='save'):
    dse_ldif = topology.standalone.confdir + '/dse.ldif'
    sav_file = topology.standalone.confdir + '/dse.ldif.ticket47823'
    if action == 'save':
        shutil.copy(dse_ldif, sav_file)
    else:
        shutil.copy(sav_file, dse_ldif)
    
def _pattern_errorlog(file, log_pattern):
    try:
        _pattern_errorlog.last_pos += 1
    except AttributeError:
        _pattern_errorlog.last_pos = 0
    
    found = None
    log.debug("_pattern_errorlog: start at offset %d" % _pattern_errorlog.last_pos)
    file.seek(_pattern_errorlog.last_pos)
    
    # Use a while true iteration because 'for line in file: hit a
    # python bug that break file.tell()
    while True:
        line = file.readline()
        log.debug("_pattern_errorlog: [%d] %s" % (file.tell(), line))
        found = log_pattern.search(line)
        if ((line == '') or (found)):
            break
        
    log.debug("_pattern_errorlog: end at offset %d" % file.tell())
    _pattern_errorlog.last_pos = file.tell()
    return found
    
def test_ticket47823_init(topology):
    """
        
    """

    # Enabled the plugins
    topology.standalone.plugins.enable(name=PLUGIN_ATTR_UNIQUENESS)
    topology.standalone.restart(timeout=120)

    topology.standalone.add_s(Entry((PROVISIONING_DN, {'objectclass': "top nscontainer".split(),
                                                       'cn': PROVISIONING_CN})))                                       
    topology.standalone.add_s(Entry((ACTIVE_DN, {'objectclass': "top nscontainer".split(),                       
                                                 'cn': ACTIVE_CN})))                                             
    topology.standalone.add_s(Entry((STAGE_DN, {'objectclass': "top nscontainer".split(),                       
                                                'cn': STAGE_CN})))                                              
    topology.standalone.add_s(Entry((DELETE_DN, {'objectclass': "top nscontainer".split(),                       
                                                 'cn': DELETE_CN})))  
    topology.standalone.errorlog_file = open(topology.standalone.errlog, "r")

    topology.standalone.stop(timeout=120)
    time.sleep(1)
    topology.standalone.start(timeout=120)
    time.sleep(3)

    
def test_ticket47823_one_container_add(topology):
    '''
    Check uniqueness in a single container
    Add and entry with a given 'cn', then check we can not add an entry with the same 'cn' value
    
    '''
    _header(topology, "With former config (args), check attribute uniqueness with 'cn' (ADD) ")

    _active_container_add(topology, type_config='old')
    
    _header(topology, "With new config (args), check attribute uniqueness with 'cn' (ADD) ")
    
    _active_container_add(topology, type_config='new')
    
def test_ticket47823_one_container_mod(topology):
    '''
    Check uniqueness in a single container
    Add and entry with a given 'cn', then check we can not modify an entry with the same 'cn' value
    
    '''
    _header(topology, "With former config (args), check attribute uniqueness with 'cn' (MOD)")
    
    _active_container_mod(topology, type_config='old')
    
    _header(topology, "With new config (args), check attribute uniqueness with 'cn' (MOD)")
    
    _active_container_mod(topology, type_config='new')
    
        
    
def test_ticket47823_one_container_modrdn(topology):
    '''
    Check uniqueness in a single container
    Add and entry with a given 'cn', then check we can not modrdn an entry with the same 'cn' value
    
    '''
    _header(topology, "With former config (args), check attribute uniqueness with 'cn' (MODRDN)")
    
    _active_container_modrdn(topology, type_config='old')
    
    _header(topology, "With former config (args), check attribute uniqueness with 'cn' (MODRDN)")
    
    _active_container_modrdn(topology, type_config='new')
    
def test_ticket47823_multi_containers_add(topology):
    '''
    Check uniqueness in a several containers
    Add and entry with a given 'cn', then check we can not add an entry with the same 'cn' value
    
    '''
    _header(topology, "With former config (args), check attribute uniqueness with 'cn' (ADD) ")

    _active_stage_containers_add(topology, type_config='old', across_subtrees=False)
    
    _header(topology, "With new config (args), check attribute uniqueness with 'cn' (ADD) ")
    
    _active_stage_containers_add(topology, type_config='new', across_subtrees=False)
    
def test_ticket47823_multi_containers_mod(topology):
    '''
    Check uniqueness in a several containers
    Add an entry on a container with a given 'cn', then check we CAN mod an entry with the same 'cn' value on the other container
    
    '''
    _header(topology, "With former config (args), check attribute uniqueness with 'cn' (MOD) on separated container")
    
    
    topology.standalone.log.info('Uniqueness not enforced: if same \'cn\' modified (add/replace) on separated containers')
    _active_stage_containers_mod(topology, type_config='old', across_subtrees=False)
    
    _header(topology, "With new config (args), check attribute uniqueness with 'cn' (MOD) on separated container")
    
    
    topology.standalone.log.info('Uniqueness not enforced: if same \'cn\' modified (add/replace) on separated containers')
    _active_stage_containers_mod(topology, type_config='new', across_subtrees=False)
    
def test_ticket47823_multi_containers_modrdn(topology):
    '''
    Check uniqueness in a several containers
    Add and entry with a given 'cn', then check we CAN modrdn an entry with the same 'cn' value on the other container
    
    '''
    _header(topology, "With former config (args), check attribute uniqueness with 'cn' (MODRDN) on separated containers")
    
    topology.standalone.log.info('Uniqueness not enforced: checks MODRDN entry is accepted on separated containers')
    _active_stage_containers_modrdn(topology, type_config='old', across_subtrees=False)
    
    topology.standalone.log.info('Uniqueness not enforced: checks MODRDN entry is accepted on separated containers')
    _active_stage_containers_modrdn(topology, type_config='old')

def test_ticket47823_across_multi_containers_add(topology):
    '''
    Check uniqueness across several containers, uniquely with the new configuration
    Add and entry with a given 'cn', then check we can not add an entry with the same 'cn' value
    
    '''
    _header(topology, "With new config (args), check attribute uniqueness with 'cn' (ADD) across several containers")

    _active_stage_containers_add(topology, type_config='old', across_subtrees=True)
    
def test_ticket47823_across_multi_containers_mod(topology):
    '''
    Check uniqueness across several containers, uniquely with the new configuration
    Add and entry with a given 'cn', then check we can not modifiy an entry with the same 'cn' value
    
    '''
    _header(topology, "With new config (args), check attribute uniqueness with 'cn' (MOD) across several containers")

    _active_stage_containers_mod(topology, type_config='old', across_subtrees=True)

def test_ticket47823_across_multi_containers_modrdn(topology):
    '''
    Check uniqueness across several containers, uniquely with the new configuration
    Add and entry with a given 'cn', then check we can not modrdn an entry with the same 'cn' value
    
    '''
    _header(topology, "With new config (args), check attribute uniqueness with 'cn' (MODRDN) across several containers")

    _active_stage_containers_modrdn(topology, type_config='old', across_subtrees=True)
    
def test_ticket47823_invalid_config_1(topology):
    '''
    Check that an invalid config is detected. No uniqueness enforced
    Using old config: arg0 is missing
    '''
    _header(topology, "Invalid config (old): arg0 is missing")
    
    _config_file(topology, action='save')
    
    # create an invalid config without arg0
    config = _build_config(topology, attr_name='cn', subtree_1=ACTIVE_DN, subtree_2=None, type_config='old', across_subtrees=False)
    
    del config.data['nsslapd-pluginarg0']
    # replace 'cn' uniqueness entry
    try:
        topology.standalone.delete_s(config.dn)
        
    except ldap.NO_SUCH_OBJECT:
        pass
    topology.standalone.add_s(config)

    topology.standalone.getEntry(config.dn, ldap.SCOPE_BASE, "(objectclass=nsSlapdPlugin)", ALL_CONFIG_ATTRS)  
    
    # Check the server did not restart
    try:
        topology.standalone.restart(timeout=5)
        ent = topology.standalone.getEntry(config.dn, ldap.SCOPE_BASE, "(objectclass=nsSlapdPlugin)", ALL_CONFIG_ATTRS)
        if ent:
            # be sure to restore a valid config before assert
            _config_file(topology, action='restore')
        assert not ent
    except ldap.SERVER_DOWN:
            pass
    
    # Check the expected error message
    regex = re.compile("Config info: attribute name not defined")
    res =_pattern_errorlog(topology.standalone.errorlog_file, regex)
    if not res:
        # be sure to restore a valid config before assert
        _config_file(topology, action='restore')    
    assert res
    
    # Check we can restart the server
    _config_file(topology, action='restore')
    topology.standalone.start(timeout=5)
    try:
        topology.standalone.getEntry(config.dn, ldap.SCOPE_BASE, "(objectclass=nsSlapdPlugin)", ALL_CONFIG_ATTRS)
    except ldap.NO_SUCH_OBJECT:
        pass

def test_ticket47823_invalid_config_2(topology):
    '''
    Check that an invalid config is detected. No uniqueness enforced
    Using old config: arg1 is missing
    '''
    _header(topology, "Invalid config (old): arg1 is missing")
    
    _config_file(topology, action='save')
    
    # create an invalid config without arg0
    config = _build_config(topology, attr_name='cn', subtree_1=ACTIVE_DN, subtree_2=None, type_config='old', across_subtrees=False)
    
    del config.data['nsslapd-pluginarg1']
    # replace 'cn' uniqueness entry
    try:
        topology.standalone.delete_s(config.dn)
        
    except ldap.NO_SUCH_OBJECT:
        pass
    topology.standalone.add_s(config)

    topology.standalone.getEntry(config.dn, ldap.SCOPE_BASE, "(objectclass=nsSlapdPlugin)", ALL_CONFIG_ATTRS)  
    
    # Check the server did not restart
    try:
        topology.standalone.restart(timeout=5)
        ent = topology.standalone.getEntry(config.dn, ldap.SCOPE_BASE, "(objectclass=nsSlapdPlugin)", ALL_CONFIG_ATTRS)
        if ent:
            # be sure to restore a valid config before assert
            _config_file(topology, action='restore')
        assert not ent
    except ldap.SERVER_DOWN:
            pass
    
    # Check the expected error message
    regex = re.compile("Config info: No valid subtree is defined")
    res =_pattern_errorlog(topology.standalone.errorlog_file, regex)
    if not res:
        # be sure to restore a valid config before assert
        _config_file(topology, action='restore')    
    assert res
    
    # Check we can restart the server
    _config_file(topology, action='restore')
    topology.standalone.start(timeout=5)
    try:
        topology.standalone.getEntry(config.dn, ldap.SCOPE_BASE, "(objectclass=nsSlapdPlugin)", ALL_CONFIG_ATTRS)
    except ldap.NO_SUCH_OBJECT:
        pass

def test_ticket47823_invalid_config_3(topology):
    '''
    Check that an invalid config is detected. No uniqueness enforced
    Using old config: arg0 is missing
    '''
    _header(topology, "Invalid config (old): arg0 is missing but new config attrname exists")
    
    _config_file(topology, action='save')
    
    # create an invalid config without arg0
    config = _build_config(topology, attr_name='cn', subtree_1=ACTIVE_DN, subtree_2=None, type_config='old', across_subtrees=False)
    
    del config.data['nsslapd-pluginarg0']
    config.data['uniqueness-attribute-name'] = 'cn'
    # replace 'cn' uniqueness entry
    try:
        topology.standalone.delete_s(config.dn)
        
    except ldap.NO_SUCH_OBJECT:
        pass
    topology.standalone.add_s(config)

    topology.standalone.getEntry(config.dn, ldap.SCOPE_BASE, "(objectclass=nsSlapdPlugin)", ALL_CONFIG_ATTRS)  
    
    # Check the server did not restart
    try:
        topology.standalone.restart(timeout=5)
        ent = topology.standalone.getEntry(config.dn, ldap.SCOPE_BASE, "(objectclass=nsSlapdPlugin)", ALL_CONFIG_ATTRS)
        if ent:
            # be sure to restore a valid config before assert
            _config_file(topology, action='restore')
        assert not ent
    except ldap.SERVER_DOWN:
            pass
    
    # Check the expected error message
    regex = re.compile("Config info: objectclass for subtree entries is not defined")
    res =_pattern_errorlog(topology.standalone.errorlog_file, regex)
    if not res:
        # be sure to restore a valid config before assert
        _config_file(topology, action='restore')    
    assert res
    
    # Check we can restart the server
    _config_file(topology, action='restore')
    topology.standalone.start(timeout=5)
    try:
        topology.standalone.getEntry(config.dn, ldap.SCOPE_BASE, "(objectclass=nsSlapdPlugin)", ALL_CONFIG_ATTRS)
    except ldap.NO_SUCH_OBJECT:
        pass
    
def test_ticket47823_invalid_config_4(topology):
    '''
    Check that an invalid config is detected. No uniqueness enforced
    Using old config: arg1 is missing
    '''
    _header(topology, "Invalid config (old): arg1 is missing but new config exist")
    
    _config_file(topology, action='save')
    
    # create an invalid config without arg0
    config = _build_config(topology, attr_name='cn', subtree_1=ACTIVE_DN, subtree_2=None, type_config='old', across_subtrees=False)
    
    del config.data['nsslapd-pluginarg1']
    config.data['uniqueness-subtrees'] = ACTIVE_DN
    # replace 'cn' uniqueness entry
    try:
        topology.standalone.delete_s(config.dn)
        
    except ldap.NO_SUCH_OBJECT:
        pass
    topology.standalone.add_s(config)

    topology.standalone.getEntry(config.dn, ldap.SCOPE_BASE, "(objectclass=nsSlapdPlugin)", ALL_CONFIG_ATTRS)  
    
    # Check the server did not restart
    try:
        topology.standalone.restart(timeout=5)
        ent = topology.standalone.getEntry(config.dn, ldap.SCOPE_BASE, "(objectclass=nsSlapdPlugin)", ALL_CONFIG_ATTRS)
        if ent:
            # be sure to restore a valid config before assert
            _config_file(topology, action='restore')
        assert not ent
    except ldap.SERVER_DOWN:
            pass
    
    # Check the expected error message
    regex = re.compile("Config info: No valid subtree is defined")
    res =_pattern_errorlog(topology.standalone.errorlog_file, regex)
    if not res:
        # be sure to restore a valid config before assert
        _config_file(topology, action='restore')    
    assert res
    
    # Check we can restart the server
    _config_file(topology, action='restore')
    topology.standalone.start(timeout=5)
    try:
        topology.standalone.getEntry(config.dn, ldap.SCOPE_BASE, "(objectclass=nsSlapdPlugin)", ALL_CONFIG_ATTRS)
    except ldap.NO_SUCH_OBJECT:
        pass
    
def test_ticket47823_invalid_config_5(topology):
    '''
    Check that an invalid config is detected. No uniqueness enforced
    Using new config: uniqueness-attribute-name is missing
    '''
    _header(topology, "Invalid config (new): uniqueness-attribute-name is missing")
    
    _config_file(topology, action='save')
    
    # create an invalid config without arg0
    config = _build_config(topology, attr_name='cn', subtree_1=ACTIVE_DN, subtree_2=None, type_config='new', across_subtrees=False)
    
    del config.data['uniqueness-attribute-name']
    # replace 'cn' uniqueness entry
    try:
        topology.standalone.delete_s(config.dn)
        
    except ldap.NO_SUCH_OBJECT:
        pass
    topology.standalone.add_s(config)

    topology.standalone.getEntry(config.dn, ldap.SCOPE_BASE, "(objectclass=nsSlapdPlugin)", ALL_CONFIG_ATTRS)  
    
    # Check the server did not restart
    try:
        topology.standalone.restart(timeout=5)
        ent = topology.standalone.getEntry(config.dn, ldap.SCOPE_BASE, "(objectclass=nsSlapdPlugin)", ALL_CONFIG_ATTRS)
        if ent:
            # be sure to restore a valid config before assert
            _config_file(topology, action='restore')
        assert not ent
    except ldap.SERVER_DOWN:
            pass
    
    # Check the expected error message
    regex = re.compile("Config info: attribute name not defined")
    res =_pattern_errorlog(topology.standalone.errorlog_file, regex)
    if not res:
        # be sure to restore a valid config before assert
        _config_file(topology, action='restore')    
    assert res
    
    # Check we can restart the server
    _config_file(topology, action='restore')
    topology.standalone.start(timeout=5)
    try:
        topology.standalone.getEntry(config.dn, ldap.SCOPE_BASE, "(objectclass=nsSlapdPlugin)", ALL_CONFIG_ATTRS)
    except ldap.NO_SUCH_OBJECT:
        pass

def test_ticket47823_invalid_config_6(topology):
    '''
    Check that an invalid config is detected. No uniqueness enforced
    Using new config: uniqueness-subtrees is missing
    '''
    _header(topology, "Invalid config (new): uniqueness-subtrees is missing")
    
    _config_file(topology, action='save')
    
    # create an invalid config without arg0
    config = _build_config(topology, attr_name='cn', subtree_1=ACTIVE_DN, subtree_2=None, type_config='new', across_subtrees=False)
    
    del config.data['uniqueness-subtrees']
    # replace 'cn' uniqueness entry
    try:
        topology.standalone.delete_s(config.dn)
        
    except ldap.NO_SUCH_OBJECT:
        pass
    topology.standalone.add_s(config)

    topology.standalone.getEntry(config.dn, ldap.SCOPE_BASE, "(objectclass=nsSlapdPlugin)", ALL_CONFIG_ATTRS)  
    
    # Check the server did not restart
    try:
        topology.standalone.restart(timeout=5)
        ent = topology.standalone.getEntry(config.dn, ldap.SCOPE_BASE, "(objectclass=nsSlapdPlugin)", ALL_CONFIG_ATTRS)
        if ent:
            # be sure to restore a valid config before assert
            _config_file(topology, action='restore')
        assert not ent
    except ldap.SERVER_DOWN:
            pass
    
    # Check the expected error message
    regex = re.compile("Config info: objectclass for subtree entries is not defined")
    res =_pattern_errorlog(topology.standalone.errorlog_file, regex)
    if not res:
        # be sure to restore a valid config before assert
        _config_file(topology, action='restore')    
    assert res
    
    # Check we can restart the server
    _config_file(topology, action='restore')
    topology.standalone.start(timeout=5)
    try:
        topology.standalone.getEntry(config.dn, ldap.SCOPE_BASE, "(objectclass=nsSlapdPlugin)", ALL_CONFIG_ATTRS)
    except ldap.NO_SUCH_OBJECT:
        pass
    
def test_ticket47823_invalid_config_7(topology):
    '''
    Check that an invalid config is detected. No uniqueness enforced
    Using new config: uniqueness-subtrees is missing
    '''
    _header(topology, "Invalid config (new): uniqueness-subtrees are invalid")
    
    _config_file(topology, action='save')
    
    # create an invalid config without arg0
    config = _build_config(topology, attr_name='cn', subtree_1="this_is dummy DN", subtree_2="an other=dummy DN", type_config='new', across_subtrees=False)
    
    # replace 'cn' uniqueness entry
    try:
        topology.standalone.delete_s(config.dn)
        
    except ldap.NO_SUCH_OBJECT:
        pass
    topology.standalone.add_s(config)

    topology.standalone.getEntry(config.dn, ldap.SCOPE_BASE, "(objectclass=nsSlapdPlugin)", ALL_CONFIG_ATTRS)  
    
    # Check the server did not restart
    try:
        topology.standalone.restart(timeout=5)
        ent = topology.standalone.getEntry(config.dn, ldap.SCOPE_BASE, "(objectclass=nsSlapdPlugin)", ALL_CONFIG_ATTRS)
        if ent:
            # be sure to restore a valid config before assert
            _config_file(topology, action='restore')
        assert not ent
    except ldap.SERVER_DOWN:
            pass
    
    # Check the expected error message
    regex = re.compile("Config info: No valid subtree is defined")
    res =_pattern_errorlog(topology.standalone.errorlog_file, regex)
    if not res:
        # be sure to restore a valid config before assert
        _config_file(topology, action='restore')    
    assert res
    
    # Check we can restart the server
    _config_file(topology, action='restore')
    topology.standalone.start(timeout=5)
    try:
        topology.standalone.getEntry(config.dn, ldap.SCOPE_BASE, "(objectclass=nsSlapdPlugin)", ALL_CONFIG_ATTRS)
    except ldap.NO_SUCH_OBJECT:
        pass
def test_ticket47823_final(topology):
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
    test_ticket47823_init(topo)
    
    # run old/new config style that makes uniqueness checking on one subtree
    test_ticket47823_one_container_add(topo)
    test_ticket47823_one_container_mod(topo)
    test_ticket47823_one_container_modrdn(topo)
    
    # run old config style that makes uniqueness checking on each defined subtrees
    test_ticket47823_multi_containers_add(topo)
    test_ticket47823_multi_containers_mod(topo)
    test_ticket47823_multi_containers_modrdn(topo)
    test_ticket47823_across_multi_containers_add(topo)
    test_ticket47823_across_multi_containers_mod(topo)
    test_ticket47823_across_multi_containers_modrdn(topo)
    
    test_ticket47823_invalid_config_1(topo)
    test_ticket47823_invalid_config_2(topo)
    test_ticket47823_invalid_config_3(topo)
    test_ticket47823_invalid_config_4(topo)
    test_ticket47823_invalid_config_5(topo)
    test_ticket47823_invalid_config_6(topo)
    test_ticket47823_invalid_config_7(topo)
    
    test_ticket47823_final(topo)
    

if __name__ == '__main__':
    run_isolated()
