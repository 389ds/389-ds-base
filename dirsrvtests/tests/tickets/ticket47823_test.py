# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import re
import shutil
import subprocess
import time

import ldap
import pytest
from lib389 import Entry
from lib389._constants import *
from lib389.topologies import topology_st

log = logging.getLogger(__name__)
from lib389.utils import *

# Skip on older versions
pytestmark = [pytest.mark.tier2,
              pytest.mark.skipif(ds_is_older('1.3.3'), reason="Not implemented")]
PROVISIONING_CN = "provisioning"
PROVISIONING_DN = "cn=%s,%s" % (PROVISIONING_CN, SUFFIX)

ACTIVE_CN = "accounts"
STAGE_CN = "staged users"
DELETE_CN = "deleted users"
ACTIVE_DN = "cn=%s,%s" % (ACTIVE_CN, SUFFIX)
STAGE_DN = "cn=%s,%s" % (STAGE_CN, PROVISIONING_DN)
DELETE_DN = "cn=%s,%s" % (DELETE_CN, PROVISIONING_DN)

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


def _header(topology_st, label):
    topology_st.standalone.log.info("\n\n###############################################")
    topology_st.standalone.log.info("#######")
    topology_st.standalone.log.info("####### %s" % label)
    topology_st.standalone.log.info("#######")
    topology_st.standalone.log.info("###############################################")


def _uniqueness_config_entry(topology_st, name=None):
    if not name:
        return None

    ent = topology_st.standalone.getEntry("cn=%s,%s" % (PLUGIN_ATTR_UNIQUENESS, DN_PLUGIN), ldap.SCOPE_BASE,
                                          "(objectclass=nsSlapdPlugin)",
                                          ['objectClass', 'cn', 'nsslapd-pluginPath', 'nsslapd-pluginInitfunc',
                                           'nsslapd-pluginType', 'nsslapd-pluginEnabled',
                                           'nsslapd-plugin-depends-on-type',
                                           'nsslapd-pluginId', 'nsslapd-pluginVersion', 'nsslapd-pluginVendor',
                                           'nsslapd-pluginDescription'])
    ent.dn = "cn=%s uniqueness,%s" % (name, DN_PLUGIN)
    return ent


def _build_config(topology_st, attr_name='cn', subtree_1=None, subtree_2=None, type_config='old',
                  across_subtrees=False):
    assert topology_st
    assert attr_name
    assert subtree_1

    if type_config == 'old':
        # enable the 'cn' uniqueness on Active
        config = _uniqueness_config_entry(topology_st, attr_name)
        config.setValue('nsslapd-pluginarg0', attr_name)
        config.setValue('nsslapd-pluginarg1', subtree_1)
        if subtree_2:
            config.setValue('nsslapd-pluginarg2', subtree_2)
    else:
        # prepare the config entry
        config = _uniqueness_config_entry(topology_st, attr_name)
        config.setValue('uniqueness-attribute-name', attr_name)
        config.setValue('uniqueness-subtrees', subtree_1)
        if subtree_2:
            config.setValue('uniqueness-subtrees', subtree_2)
        if across_subtrees:
            config.setValue('uniqueness-across-all-subtrees', 'on')
    return config


def _active_container_invalid_cfg_add(topology_st):
    '''
    Check uniqueness is not enforced with ADD (invalid config)
    '''
    topology_st.standalone.add_s(Entry((ACTIVE_USER_1_DN, {
        'objectclass': "top person".split(),
        'sn': ACTIVE_USER_1_CN,
        'cn': ACTIVE_USER_1_CN})))

    topology_st.standalone.add_s(Entry((ACTIVE_USER_2_DN, {
        'objectclass': "top person".split(),
        'sn': ACTIVE_USER_2_CN,
        'cn': [ACTIVE_USER_1_CN, ACTIVE_USER_2_CN]})))

    topology_st.standalone.delete_s(ACTIVE_USER_1_DN)
    topology_st.standalone.delete_s(ACTIVE_USER_2_DN)


def _active_container_add(topology_st, type_config='old'):
    '''
    Check uniqueness in a single container (Active)
    Add an entry with a given 'cn', then check we can not add an entry with the same 'cn' value

    '''
    config = _build_config(topology_st, attr_name='cn', subtree_1=ACTIVE_DN, subtree_2=None, type_config=type_config,
                           across_subtrees=False)

    # remove the 'cn' uniqueness entry
    try:
        topology_st.standalone.delete_s(config.dn)

    except ldap.NO_SUCH_OBJECT:
        pass
    topology_st.standalone.restart(timeout=120)

    topology_st.standalone.log.info('Uniqueness not enforced: create the entries')

    topology_st.standalone.add_s(Entry((ACTIVE_USER_1_DN, {
        'objectclass': "top person".split(),
        'sn': ACTIVE_USER_1_CN,
        'cn': ACTIVE_USER_1_CN})))

    topology_st.standalone.add_s(Entry((ACTIVE_USER_2_DN, {
        'objectclass': "top person".split(),
        'sn': ACTIVE_USER_2_CN,
        'cn': [ACTIVE_USER_1_CN, ACTIVE_USER_2_CN]})))

    topology_st.standalone.delete_s(ACTIVE_USER_1_DN)
    topology_st.standalone.delete_s(ACTIVE_USER_2_DN)

    topology_st.standalone.log.info('Uniqueness enforced: checks second entry is rejected')

    # enable the 'cn' uniqueness on Active
    topology_st.standalone.add_s(config)
    topology_st.standalone.restart(timeout=120)
    topology_st.standalone.add_s(Entry((ACTIVE_USER_1_DN, {
        'objectclass': "top person".split(),
        'sn': ACTIVE_USER_1_CN,
        'cn': ACTIVE_USER_1_CN})))

    try:
        topology_st.standalone.add_s(Entry((ACTIVE_USER_2_DN, {
            'objectclass': "top person".split(),
            'sn': ACTIVE_USER_2_CN,
            'cn': [ACTIVE_USER_1_CN, ACTIVE_USER_2_CN]})))
    except ldap.CONSTRAINT_VIOLATION:
        # yes it is expected
        pass

    # cleanup the stuff now
    topology_st.standalone.delete_s(config.dn)
    topology_st.standalone.delete_s(ACTIVE_USER_1_DN)


def _active_container_mod(topology_st, type_config='old'):
    '''
    Check uniqueness in a single container (active)
    Add and entry with a given 'cn', then check we can not modify an entry with the same 'cn' value

    '''

    config = _build_config(topology_st, attr_name='cn', subtree_1=ACTIVE_DN, subtree_2=None, type_config=type_config,
                           across_subtrees=False)

    # enable the 'cn' uniqueness on Active
    topology_st.standalone.add_s(config)
    topology_st.standalone.restart(timeout=120)

    topology_st.standalone.log.info('Uniqueness enforced: checks MOD ADD entry is rejected')
    topology_st.standalone.add_s(Entry((ACTIVE_USER_1_DN, {
        'objectclass': "top person".split(),
        'sn': ACTIVE_USER_1_CN,
        'cn': ACTIVE_USER_1_CN})))

    topology_st.standalone.add_s(Entry((ACTIVE_USER_2_DN, {
        'objectclass': "top person".split(),
        'sn': ACTIVE_USER_2_CN,
        'cn': ACTIVE_USER_2_CN})))

    try:
        topology_st.standalone.modify_s(ACTIVE_USER_2_DN, [(ldap.MOD_ADD, 'cn', ensure_bytes(ACTIVE_USER_1_CN))])
    except ldap.CONSTRAINT_VIOLATION:
        # yes it is expected
        pass

    topology_st.standalone.log.info('Uniqueness enforced: checks MOD REPLACE entry is rejected')
    try:
        topology_st.standalone.modify_s(ACTIVE_USER_2_DN,
                                        [(ldap.MOD_REPLACE, 'cn', [ensure_bytes(ACTIVE_USER_1_CN), ensure_bytes(ACTIVE_USER_2_CN)])])
    except ldap.CONSTRAINT_VIOLATION:
        # yes it is expected
        pass

    # cleanup the stuff now
    topology_st.standalone.delete_s(config.dn)
    topology_st.standalone.delete_s(ACTIVE_USER_1_DN)
    topology_st.standalone.delete_s(ACTIVE_USER_2_DN)


def _active_container_modrdn(topology_st, type_config='old'):
    '''
    Check uniqueness in a single container
    Add and entry with a given 'cn', then check we can not modrdn an entry with the same 'cn' value

    '''
    config = _build_config(topology_st, attr_name='cn', subtree_1=ACTIVE_DN, subtree_2=None, type_config=type_config,
                           across_subtrees=False)

    # enable the 'cn' uniqueness on Active
    topology_st.standalone.add_s(config)
    topology_st.standalone.restart(timeout=120)

    topology_st.standalone.log.info('Uniqueness enforced: checks MODRDN entry is rejected')

    topology_st.standalone.add_s(Entry((ACTIVE_USER_1_DN, {
        'objectclass': "top person".split(),
        'sn': ACTIVE_USER_1_CN,
        'cn': [ACTIVE_USER_1_CN, 'dummy']})))

    topology_st.standalone.add_s(Entry((ACTIVE_USER_2_DN, {
        'objectclass': "top person".split(),
        'sn': ACTIVE_USER_2_CN,
        'cn': ACTIVE_USER_2_CN})))

    try:
        topology_st.standalone.rename_s(ACTIVE_USER_2_DN, 'cn=dummy', delold=0)
    except ldap.CONSTRAINT_VIOLATION:
        # yes it is expected
        pass

    # cleanup the stuff now
    topology_st.standalone.delete_s(config.dn)
    topology_st.standalone.delete_s(ACTIVE_USER_1_DN)
    topology_st.standalone.delete_s(ACTIVE_USER_2_DN)


def _active_stage_containers_add(topology_st, type_config='old', across_subtrees=False):
    '''
    Check uniqueness in several containers
    Add an entry on a container with a given 'cn'
    with across_subtrees=False check we CAN add an entry with the same 'cn' value on the other container
    with across_subtrees=True check we CAN NOT add an entry with the same 'cn' value on the other container

    '''
    config = _build_config(topology_st, attr_name='cn', subtree_1=ACTIVE_DN, subtree_2=STAGE_DN,
                           type_config=type_config, across_subtrees=False)

    topology_st.standalone.add_s(config)
    topology_st.standalone.restart(timeout=120)
    topology_st.standalone.add_s(Entry((ACTIVE_USER_1_DN, {
        'objectclass': "top person".split(),
        'sn': ACTIVE_USER_1_CN,
        'cn': ACTIVE_USER_1_CN})))
    try:

        # adding an entry on a separated contains with the same 'cn'
        topology_st.standalone.add_s(Entry((STAGE_USER_1_DN, {
            'objectclass': "top person".split(),
            'sn': STAGE_USER_1_CN,
            'cn': ACTIVE_USER_1_CN})))
    except ldap.CONSTRAINT_VIOLATION:
        assert across_subtrees

    # cleanup the stuff now
    topology_st.standalone.delete_s(config.dn)
    topology_st.standalone.delete_s(ACTIVE_USER_1_DN)
    topology_st.standalone.delete_s(STAGE_USER_1_DN)


def _active_stage_containers_mod(topology_st, type_config='old', across_subtrees=False):
    '''
    Check uniqueness in a several containers
    Add an entry on a container with a given 'cn', then check we CAN mod an entry with the same 'cn' value on the other container

    '''
    config = _build_config(topology_st, attr_name='cn', subtree_1=ACTIVE_DN, subtree_2=STAGE_DN,
                           type_config=type_config, across_subtrees=False)

    topology_st.standalone.add_s(config)
    topology_st.standalone.restart(timeout=120)
    # adding an entry on active with a different 'cn'
    topology_st.standalone.add_s(Entry((ACTIVE_USER_1_DN, {
        'objectclass': "top person".split(),
        'sn': ACTIVE_USER_1_CN,
        'cn': ACTIVE_USER_2_CN})))

    # adding an entry on a stage with a different 'cn'
    topology_st.standalone.add_s(Entry((STAGE_USER_1_DN, {
        'objectclass': "top person".split(),
        'sn': STAGE_USER_1_CN,
        'cn': STAGE_USER_1_CN})))

    try:

        # modify add same value
        topology_st.standalone.modify_s(STAGE_USER_1_DN, [(ldap.MOD_ADD, 'cn', [ensure_bytes(ACTIVE_USER_2_CN)])])
    except ldap.CONSTRAINT_VIOLATION:
        assert across_subtrees

    topology_st.standalone.delete_s(STAGE_USER_1_DN)
    topology_st.standalone.add_s(Entry((STAGE_USER_1_DN, {
        'objectclass': "top person".split(),
        'sn': STAGE_USER_1_CN,
        'cn': STAGE_USER_2_CN})))
    try:
        # modify replace same value
        topology_st.standalone.modify_s(STAGE_USER_1_DN,
                                        [(ldap.MOD_REPLACE, 'cn', [ensure_bytes(STAGE_USER_2_CN), ensure_bytes(ACTIVE_USER_1_CN)])])
    except ldap.CONSTRAINT_VIOLATION:
        assert across_subtrees

    # cleanup the stuff now
    topology_st.standalone.delete_s(config.dn)
    topology_st.standalone.delete_s(ACTIVE_USER_1_DN)
    topology_st.standalone.delete_s(STAGE_USER_1_DN)


def _active_stage_containers_modrdn(topology_st, type_config='old', across_subtrees=False):
    '''
    Check uniqueness in a several containers
    Add and entry with a given 'cn', then check we CAN modrdn an entry with the same 'cn' value on the other container

    '''

    config = _build_config(topology_st, attr_name='cn', subtree_1=ACTIVE_DN, subtree_2=STAGE_DN,
                           type_config=type_config, across_subtrees=False)

    # enable the 'cn' uniqueness on Active and Stage
    topology_st.standalone.add_s(config)
    topology_st.standalone.restart(timeout=120)
    topology_st.standalone.add_s(Entry((ACTIVE_USER_1_DN, {
        'objectclass': "top person".split(),
        'sn': ACTIVE_USER_1_CN,
        'cn': [ACTIVE_USER_1_CN, 'dummy']})))

    topology_st.standalone.add_s(Entry((STAGE_USER_1_DN, {
        'objectclass': "top person".split(),
        'sn': STAGE_USER_1_CN,
        'cn': STAGE_USER_1_CN})))

    try:

        topology_st.standalone.rename_s(STAGE_USER_1_DN, 'cn=dummy', delold=0)

        # check stage entry has 'cn=dummy'
        stage_ent = topology_st.standalone.getEntry("cn=dummy,%s" % (STAGE_DN), ldap.SCOPE_BASE, "objectclass=*",
                                                    ['cn'])
        assert stage_ent.hasAttr('cn')
        found = False
        for value in stage_ent.getValues('cn'):
            if ensure_str(value) == 'dummy':
                found = True
        assert found

        # check active entry has 'cn=dummy'
        active_ent = topology_st.standalone.getEntry(ACTIVE_USER_1_DN, ldap.SCOPE_BASE, "objectclass=*", ['cn'])
        assert active_ent.hasAttr('cn')
        found = False
        for value in stage_ent.getValues('cn'):
            if ensure_str(value) == 'dummy':
                found = True
        assert found

        topology_st.standalone.delete_s("cn=dummy,%s" % (STAGE_DN))
    except ldap.CONSTRAINT_VIOLATION:
        assert across_subtrees
        topology_st.standalone.delete_s(STAGE_USER_1_DN)

    # cleanup the stuff now
    topology_st.standalone.delete_s(config.dn)
    topology_st.standalone.delete_s(ACTIVE_USER_1_DN)


def _config_file(topology_st, action='save'):
    dse_ldif = topology_st.standalone.confdir + '/dse.ldif'
    sav_file = topology_st.standalone.confdir + '/dse.ldif.ticket47823'
    if action == 'save':
        shutil.copy(dse_ldif, sav_file)
    else:
        shutil.copy(sav_file, dse_ldif)
    time.sleep(1)


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


def test_ticket47823_init(topology_st):
    """

    """

    # Enabled the plugins
    topology_st.standalone.plugins.enable(name=PLUGIN_ATTR_UNIQUENESS)
    topology_st.standalone.restart(timeout=120)

    topology_st.standalone.add_s(Entry((PROVISIONING_DN, {'objectclass': "top nscontainer".split(),
                                                          'cn': PROVISIONING_CN})))
    topology_st.standalone.add_s(Entry((ACTIVE_DN, {'objectclass': "top nscontainer".split(),
                                                    'cn': ACTIVE_CN})))
    topology_st.standalone.add_s(Entry((STAGE_DN, {'objectclass': "top nscontainer".split(),
                                                   'cn': STAGE_CN})))
    topology_st.standalone.add_s(Entry((DELETE_DN, {'objectclass': "top nscontainer".split(),
                                                    'cn': DELETE_CN})))
    topology_st.standalone.errorlog_file = open(topology_st.standalone.errlog, "r")

    topology_st.standalone.stop(timeout=120)
    time.sleep(1)
    topology_st.standalone.start(timeout=120)
    time.sleep(3)


def test_ticket47823_one_container_add(topology_st):
    '''
    Check uniqueness in a single container
    Add and entry with a given 'cn', then check we can not add an entry with the same 'cn' value

    '''
    _header(topology_st, "With former config (args), check attribute uniqueness with 'cn' (ADD) ")

    _active_container_add(topology_st, type_config='old')

    _header(topology_st, "With new config (args), check attribute uniqueness with 'cn' (ADD) ")

    _active_container_add(topology_st, type_config='new')


def test_ticket47823_one_container_mod(topology_st):
    '''
    Check uniqueness in a single container
    Add and entry with a given 'cn', then check we can not modify an entry with the same 'cn' value

    '''
    _header(topology_st, "With former config (args), check attribute uniqueness with 'cn' (MOD)")

    _active_container_mod(topology_st, type_config='old')

    _header(topology_st, "With new config (args), check attribute uniqueness with 'cn' (MOD)")

    _active_container_mod(topology_st, type_config='new')


def test_ticket47823_one_container_modrdn(topology_st):
    '''
    Check uniqueness in a single container
    Add and entry with a given 'cn', then check we can not modrdn an entry with the same 'cn' value

    '''
    _header(topology_st, "With former config (args), check attribute uniqueness with 'cn' (MODRDN)")

    _active_container_modrdn(topology_st, type_config='old')

    _header(topology_st, "With former config (args), check attribute uniqueness with 'cn' (MODRDN)")

    _active_container_modrdn(topology_st, type_config='new')


def test_ticket47823_multi_containers_add(topology_st):
    '''
    Check uniqueness in a several containers
    Add and entry with a given 'cn', then check we can not add an entry with the same 'cn' value

    '''
    _header(topology_st, "With former config (args), check attribute uniqueness with 'cn' (ADD) ")

    _active_stage_containers_add(topology_st, type_config='old', across_subtrees=False)

    _header(topology_st, "With new config (args), check attribute uniqueness with 'cn' (ADD) ")

    _active_stage_containers_add(topology_st, type_config='new', across_subtrees=False)


def test_ticket47823_multi_containers_mod(topology_st):
    '''
    Check uniqueness in a several containers
    Add an entry on a container with a given 'cn', then check we CAN mod an entry with the same 'cn' value on the other container

    '''
    _header(topology_st, "With former config (args), check attribute uniqueness with 'cn' (MOD) on separated container")

    topology_st.standalone.log.info(
        'Uniqueness not enforced: if same \'cn\' modified (add/replace) on separated containers')
    _active_stage_containers_mod(topology_st, type_config='old', across_subtrees=False)

    _header(topology_st, "With new config (args), check attribute uniqueness with 'cn' (MOD) on separated container")

    topology_st.standalone.log.info(
        'Uniqueness not enforced: if same \'cn\' modified (add/replace) on separated containers')
    _active_stage_containers_mod(topology_st, type_config='new', across_subtrees=False)


def test_ticket47823_multi_containers_modrdn(topology_st):
    '''
    Check uniqueness in a several containers
    Add and entry with a given 'cn', then check we CAN modrdn an entry with the same 'cn' value on the other container

    '''
    _header(topology_st,
            "With former config (args), check attribute uniqueness with 'cn' (MODRDN) on separated containers")

    topology_st.standalone.log.info('Uniqueness not enforced: checks MODRDN entry is accepted on separated containers')
    _active_stage_containers_modrdn(topology_st, type_config='old', across_subtrees=False)

    topology_st.standalone.log.info('Uniqueness not enforced: checks MODRDN entry is accepted on separated containers')
    _active_stage_containers_modrdn(topology_st, type_config='old')


def test_ticket47823_across_multi_containers_add(topology_st):
    '''
    Check uniqueness across several containers, uniquely with the new configuration
    Add and entry with a given 'cn', then check we can not add an entry with the same 'cn' value

    '''
    _header(topology_st, "With new config (args), check attribute uniqueness with 'cn' (ADD) across several containers")

    _active_stage_containers_add(topology_st, type_config='old', across_subtrees=True)


def test_ticket47823_across_multi_containers_mod(topology_st):
    '''
    Check uniqueness across several containers, uniquely with the new configuration
    Add and entry with a given 'cn', then check we can not modifiy an entry with the same 'cn' value

    '''
    _header(topology_st, "With new config (args), check attribute uniqueness with 'cn' (MOD) across several containers")

    _active_stage_containers_mod(topology_st, type_config='old', across_subtrees=True)


def test_ticket47823_across_multi_containers_modrdn(topology_st):
    '''
    Check uniqueness across several containers, uniquely with the new configuration
    Add and entry with a given 'cn', then check we can not modrdn an entry with the same 'cn' value

    '''
    _header(topology_st,
            "With new config (args), check attribute uniqueness with 'cn' (MODRDN) across several containers")

    _active_stage_containers_modrdn(topology_st, type_config='old', across_subtrees=True)


def test_ticket47823_invalid_config_1(topology_st):
    '''
    Check that an invalid config is detected. No uniqueness enforced
    Using old config: arg0 is missing
    '''
    _header(topology_st, "Invalid config (old): arg0 is missing")

    _config_file(topology_st, action='save')

    # create an invalid config without arg0
    config = _build_config(topology_st, attr_name='cn', subtree_1=ACTIVE_DN, subtree_2=None, type_config='old',
                           across_subtrees=False)

    del config.data['nsslapd-pluginarg0']
    # replace 'cn' uniqueness entry
    try:
        topology_st.standalone.delete_s(config.dn)

    except ldap.NO_SUCH_OBJECT:
        pass
    topology_st.standalone.add_s(config)

    topology_st.standalone.getEntry(config.dn, ldap.SCOPE_BASE, "(objectclass=nsSlapdPlugin)", ALL_CONFIG_ATTRS)

    # Check the server did not restart
    topology_st.standalone.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', b'65536')])
    try:
        topology_st.standalone.restart()
        ent = topology_st.standalone.getEntry(config.dn, ldap.SCOPE_BASE, "(objectclass=nsSlapdPlugin)",
                                              ALL_CONFIG_ATTRS)
        if ent:
            # be sure to restore a valid config before assert
            _config_file(topology_st, action='restore')
        assert not ent
    except:
        pass

    # Check the expected error message
    regex = re.compile("[U|u]nable to parse old style")
    res = _pattern_errorlog(topology_st.standalone.errorlog_file, regex)
    if not res:
        # be sure to restore a valid config before assert
        _config_file(topology_st, action='restore')
    assert res

    # Check we can restart the server
    _config_file(topology_st, action='restore')
    topology_st.standalone.start()
    try:
        topology_st.standalone.getEntry(config.dn, ldap.SCOPE_BASE, "(objectclass=nsSlapdPlugin)", ALL_CONFIG_ATTRS)
    except ldap.NO_SUCH_OBJECT:
        pass


def test_ticket47823_invalid_config_2(topology_st):
    '''
    Check that an invalid config is detected. No uniqueness enforced
    Using old config: arg1 is missing
    '''
    _header(topology_st, "Invalid config (old): arg1 is missing")

    _config_file(topology_st, action='save')

    # create an invalid config without arg0
    config = _build_config(topology_st, attr_name='cn', subtree_1=ACTIVE_DN, subtree_2=None, type_config='old',
                           across_subtrees=False)

    del config.data['nsslapd-pluginarg1']
    # replace 'cn' uniqueness entry
    try:
        topology_st.standalone.delete_s(config.dn)

    except ldap.NO_SUCH_OBJECT:
        pass
    topology_st.standalone.add_s(config)

    topology_st.standalone.getEntry(config.dn, ldap.SCOPE_BASE, "(objectclass=nsSlapdPlugin)", ALL_CONFIG_ATTRS)

    # Check the server did not restart
    try:
        topology_st.standalone.restart()
        ent = topology_st.standalone.getEntry(config.dn, ldap.SCOPE_BASE, "(objectclass=nsSlapdPlugin)",
                                              ALL_CONFIG_ATTRS)
        if ent:
            # be sure to restore a valid config before assert
            _config_file(topology_st, action='restore')
        assert not ent
    except:
        pass

    # Check the expected error message
    regex = re.compile("No valid subtree is defined")
    res = _pattern_errorlog(topology_st.standalone.errorlog_file, regex)
    if not res:
        # be sure to restore a valid config before assert
        _config_file(topology_st, action='restore')
    assert res

    # Check we can restart the server
    _config_file(topology_st, action='restore')
    topology_st.standalone.start()
    try:
        topology_st.standalone.getEntry(config.dn, ldap.SCOPE_BASE, "(objectclass=nsSlapdPlugin)", ALL_CONFIG_ATTRS)
    except ldap.NO_SUCH_OBJECT:
        pass


def test_ticket47823_invalid_config_3(topology_st):
    '''
    Check that an invalid config is detected. No uniqueness enforced
    Using old config: arg0 is missing
    '''
    _header(topology_st, "Invalid config (old): arg0 is missing but new config attrname exists")

    _config_file(topology_st, action='save')

    # create an invalid config without arg0
    config = _build_config(topology_st, attr_name='cn', subtree_1=ACTIVE_DN, subtree_2=None, type_config='old',
                           across_subtrees=False)

    del config.data['nsslapd-pluginarg0']
    config.data['uniqueness-attribute-name'] = 'cn'
    # replace 'cn' uniqueness entry
    try:
        topology_st.standalone.delete_s(config.dn)

    except ldap.NO_SUCH_OBJECT:
        pass
    topology_st.standalone.add_s(config)

    topology_st.standalone.getEntry(config.dn, ldap.SCOPE_BASE, "(objectclass=nsSlapdPlugin)", ALL_CONFIG_ATTRS)

    # Check the server did not restart
    topology_st.standalone.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', b'65536')])
    try:
        topology_st.standalone.restart()
        ent = topology_st.standalone.getEntry(config.dn, ldap.SCOPE_BASE, "(objectclass=nsSlapdPlugin)",
                                              ALL_CONFIG_ATTRS)
        if ent:
            # be sure to restore a valid config before assert
            _config_file(topology_st, action='restore')
        assert not ent
    except:
        pass

    # Check the expected error message
    regex = re.compile("[U|u]nable to parse old style")
    res = _pattern_errorlog(topology_st.standalone.errorlog_file, regex)
    if not res:
        # be sure to restore a valid config before assert
        _config_file(topology_st, action='restore')
    assert res

    # Check we can restart the server
    _config_file(topology_st, action='restore')
    topology_st.standalone.start()
    try:
        topology_st.standalone.getEntry(config.dn, ldap.SCOPE_BASE, "(objectclass=nsSlapdPlugin)", ALL_CONFIG_ATTRS)
    except ldap.NO_SUCH_OBJECT:
        pass


def test_ticket47823_invalid_config_4(topology_st):
    '''
    Check that an invalid config is detected. No uniqueness enforced
    Using old config: arg1 is missing
    '''
    _header(topology_st, "Invalid config (old): arg1 is missing but new config exist")

    _config_file(topology_st, action='save')

    # create an invalid config without arg0
    config = _build_config(topology_st, attr_name='cn', subtree_1=ACTIVE_DN, subtree_2=None, type_config='old',
                           across_subtrees=False)

    del config.data['nsslapd-pluginarg1']
    config.data['uniqueness-subtrees'] = ACTIVE_DN
    # replace 'cn' uniqueness entry
    try:
        topology_st.standalone.delete_s(config.dn)

    except ldap.NO_SUCH_OBJECT:
        pass
    topology_st.standalone.add_s(config)

    topology_st.standalone.getEntry(config.dn, ldap.SCOPE_BASE, "(objectclass=nsSlapdPlugin)", ALL_CONFIG_ATTRS)

    # Check the server did not restart
    try:
        topology_st.standalone.restart()
        ent = topology_st.standalone.getEntry(config.dn, ldap.SCOPE_BASE, "(objectclass=nsSlapdPlugin)",
                                              ALL_CONFIG_ATTRS)
        if ent:
            # be sure to restore a valid config before assert
            _config_file(topology_st, action='restore')
        assert not ent
    except:
        pass

    # Check the expected error message
    regex = re.compile("No valid subtree is defined")
    res = _pattern_errorlog(topology_st.standalone.errorlog_file, regex)
    if not res:
        # be sure to restore a valid config before assert
        _config_file(topology_st, action='restore')
    assert res

    # Check we can restart the server
    _config_file(topology_st, action='restore')
    topology_st.standalone.start()
    try:
        topology_st.standalone.getEntry(config.dn, ldap.SCOPE_BASE, "(objectclass=nsSlapdPlugin)", ALL_CONFIG_ATTRS)
    except ldap.NO_SUCH_OBJECT:
        pass


def test_ticket47823_invalid_config_5(topology_st):
    '''
    Check that an invalid config is detected. No uniqueness enforced
    Using new config: uniqueness-attribute-name is missing
    '''
    _header(topology_st, "Invalid config (new): uniqueness-attribute-name is missing")

    _config_file(topology_st, action='save')

    # create an invalid config without arg0
    config = _build_config(topology_st, attr_name='cn', subtree_1=ACTIVE_DN, subtree_2=None, type_config='new',
                           across_subtrees=False)

    del config.data['uniqueness-attribute-name']
    # replace 'cn' uniqueness entry
    try:
        topology_st.standalone.delete_s(config.dn)

    except ldap.NO_SUCH_OBJECT:
        pass
    topology_st.standalone.add_s(config)

    topology_st.standalone.getEntry(config.dn, ldap.SCOPE_BASE, "(objectclass=nsSlapdPlugin)", ALL_CONFIG_ATTRS)

    # Check the server did not restart
    try:
        topology_st.standalone.restart()
        ent = topology_st.standalone.getEntry(config.dn, ldap.SCOPE_BASE, "(objectclass=nsSlapdPlugin)",
                                              ALL_CONFIG_ATTRS)
        if ent:
            # be sure to restore a valid config before assert
            _config_file(topology_st, action='restore')
        assert not ent
    except:
        pass

    # Check the expected error message
    regex = re.compile("[A|a]ttribute name not defined")
    res = _pattern_errorlog(topology_st.standalone.errorlog_file, regex)
    if not res:
        # be sure to restore a valid config before assert
        _config_file(topology_st, action='restore')
    assert res

    # Check we can restart the server
    _config_file(topology_st, action='restore')
    topology_st.standalone.start()
    try:
        topology_st.standalone.getEntry(config.dn, ldap.SCOPE_BASE, "(objectclass=nsSlapdPlugin)", ALL_CONFIG_ATTRS)
    except ldap.NO_SUCH_OBJECT:
        pass


def test_ticket47823_invalid_config_6(topology_st):
    '''
    Check that an invalid config is detected. No uniqueness enforced
    Using new config: uniqueness-subtrees is missing
    '''
    _header(topology_st, "Invalid config (new): uniqueness-subtrees is missing")

    _config_file(topology_st, action='save')

    # create an invalid config without arg0
    config = _build_config(topology_st, attr_name='cn', subtree_1=ACTIVE_DN, subtree_2=None, type_config='new',
                           across_subtrees=False)

    del config.data['uniqueness-subtrees']
    # replace 'cn' uniqueness entry
    try:
        topology_st.standalone.delete_s(config.dn)

    except ldap.NO_SUCH_OBJECT:
        pass
    topology_st.standalone.add_s(config)

    topology_st.standalone.getEntry(config.dn, ldap.SCOPE_BASE, "(objectclass=nsSlapdPlugin)", ALL_CONFIG_ATTRS)

    # Check the server did not restart
    try:
        topology_st.standalone.restart()
        ent = topology_st.standalone.getEntry(config.dn, ldap.SCOPE_BASE, "(objectclass=nsSlapdPlugin)",
                                              ALL_CONFIG_ATTRS)
        if ent:
            # be sure to restore a valid config before assert
            _config_file(topology_st, action='restore')
        assert not ent
    except:
        pass

    # Check the expected error message
    regex = re.compile("[O|o]bjectclass for subtree entries is not defined")
    res = _pattern_errorlog(topology_st.standalone.errorlog_file, regex)
    if not res:
        # be sure to restore a valid config before assert
        _config_file(topology_st, action='restore')
    assert res

    # Check we can restart the server
    _config_file(topology_st, action='restore')
    topology_st.standalone.start()
    try:
        topology_st.standalone.getEntry(config.dn, ldap.SCOPE_BASE, "(objectclass=nsSlapdPlugin)", ALL_CONFIG_ATTRS)
    except ldap.NO_SUCH_OBJECT:
        pass


def test_ticket47823_invalid_config_7(topology_st):
    '''
    Check that an invalid config is detected. No uniqueness enforced
    Using new config: uniqueness-subtrees is missing
    '''
    _header(topology_st, "Invalid config (new): uniqueness-subtrees are invalid")

    _config_file(topology_st, action='save')

    # create an invalid config without arg0
    config = _build_config(topology_st, attr_name='cn', subtree_1="this_is dummy DN", subtree_2="an other=dummy DN",
                           type_config='new', across_subtrees=False)

    topology_st.standalone.modify_s(DN_CONFIG, [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', b'65536')])
    # replace 'cn' uniqueness entry
    try:
        topology_st.standalone.delete_s(config.dn)

    except ldap.NO_SUCH_OBJECT:
        pass
    topology_st.standalone.add_s(config)

    topology_st.standalone.getEntry(config.dn, ldap.SCOPE_BASE, "(objectclass=nsSlapdPlugin)", ALL_CONFIG_ATTRS)

    # Check the server did not restart
    try:
        topology_st.standalone.restart()
        ent = topology_st.standalone.getEntry(config.dn, ldap.SCOPE_BASE, "(objectclass=nsSlapdPlugin)",
                                              ALL_CONFIG_ATTRS)
        if ent:
            # be sure to restore a valid config before assert
            _config_file(topology_st, action='restore')
        assert not ent
    except:
        pass

    # Check the expected error message
    regex = re.compile("No valid subtree is defined")
    res = _pattern_errorlog(topology_st.standalone.errorlog_file, regex)
    if not res:
        # be sure to restore a valid config before assert
        _config_file(topology_st, action='restore')
    assert res

    # Check we can restart the server
    _config_file(topology_st, action='restore')
    topology_st.standalone.start()
    try:
        topology_st.standalone.getEntry(config.dn, ldap.SCOPE_BASE, "(objectclass=nsSlapdPlugin)", ALL_CONFIG_ATTRS)
    except ldap.NO_SUCH_OBJECT:
        pass


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
