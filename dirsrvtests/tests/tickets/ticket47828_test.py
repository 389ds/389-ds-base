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

log = logging.getLogger(__name__)

ACCT_POLICY_CONFIG_DN = 'cn=config,cn=%s,cn=plugins,cn=config' % PLUGIN_ACCT_POLICY
ACCT_POLICY_DN = 'cn=Account Inactivation Policy,%s' % SUFFIX
from lib389.utils import *

# Skip on older versions
pytestmark = [pytest.mark.tier2,
              pytest.mark.skipif(ds_is_older('1.3.3'), reason="Not implemented")]
INACTIVITY_LIMIT = '9'
SEARCHFILTER = '(objectclass=*)'

DUMMY_CONTAINER = 'cn=dummy container,%s' % SUFFIX
PROVISIONING = 'cn=provisioning,%s' % SUFFIX
ACTIVE_USER1_CN = 'active user1'
ACTIVE_USER1_DN = 'cn=%s,%s' % (ACTIVE_USER1_CN, SUFFIX)
STAGED_USER1_CN = 'staged user1'
STAGED_USER1_DN = 'cn=%s,%s' % (STAGED_USER1_CN, PROVISIONING)
DUMMY_USER1_CN = 'dummy user1'
DUMMY_USER1_DN = 'cn=%s,%s' % (DUMMY_USER1_CN, DUMMY_CONTAINER)

ALLOCATED_ATTR = 'employeeNumber'


def _header(topology_st, label):
    topology_st.standalone.log.info("\n\n###############################################")
    topology_st.standalone.log.info("#######")
    topology_st.standalone.log.info("####### %s" % label)
    topology_st.standalone.log.info("#######")
    topology_st.standalone.log.info("###############################################")


def test_ticket47828_init(topology_st):
    """
    Enable DNA
    """
    topology_st.standalone.plugins.enable(name=PLUGIN_DNA)

    topology_st.standalone.add_s(Entry((PROVISIONING, {'objectclass': "top nscontainer".split(),
                                                       'cn': 'provisioning'})))
    topology_st.standalone.add_s(Entry((DUMMY_CONTAINER, {'objectclass': "top nscontainer".split(),
                                                          'cn': 'dummy container'})))

    dn_config = "cn=excluded scope, cn=%s, %s" % (PLUGIN_DNA, DN_PLUGIN)
    topology_st.standalone.add_s(Entry((dn_config, {'objectclass': "top extensibleObject".split(),
                                                    'cn': 'excluded scope',
                                                    'dnaType': ALLOCATED_ATTR,
                                                    'dnaNextValue': str(1000),
                                                    'dnaMaxValue': str(2000),
                                                    'dnaMagicRegen': str(-1),
                                                    'dnaFilter': '(&(objectClass=person)(objectClass=organizationalPerson)(objectClass=inetOrgPerson))',
                                                    'dnaScope': SUFFIX})))
    topology_st.standalone.restart(timeout=10)


def test_ticket47828_run_0(topology_st):
    """
    NO exclude scope: Add an active entry and check its ALLOCATED_ATTR is set
    """
    _header(topology_st, 'NO exclude scope: Add an active entry and check its ALLOCATED_ATTR is set')

    topology_st.standalone.add_s(
        Entry((ACTIVE_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                 'cn': ACTIVE_USER1_CN,
                                 'sn': ACTIVE_USER1_CN,
                                 ALLOCATED_ATTR: str(-1)})))
    ent = topology_st.standalone.getEntry(ACTIVE_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ensure_str(ent.getValue(ALLOCATED_ATTR)) != str(-1)
    topology_st.standalone.log.debug('%s.%s=%s' % (ACTIVE_USER1_CN, ALLOCATED_ATTR, ensure_str(ent.getValue(ALLOCATED_ATTR))))
    topology_st.standalone.delete_s(ACTIVE_USER1_DN)


def test_ticket47828_run_1(topology_st):
    """
    NO exclude scope: Add an active entry and check its ALLOCATED_ATTR is unchanged (!= magic)
    """
    _header(topology_st, 'NO exclude scope: Add an active entry and check its ALLOCATED_ATTR is unchanged (!= magic)')

    topology_st.standalone.add_s(
        Entry((ACTIVE_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                 'cn': ACTIVE_USER1_CN,
                                 'sn': ACTIVE_USER1_CN,
                                 ALLOCATED_ATTR: str(20)})))
    ent = topology_st.standalone.getEntry(ACTIVE_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ensure_str(ent.getValue(ALLOCATED_ATTR)) == str(20)
    topology_st.standalone.log.debug('%s.%s=%s' % (ACTIVE_USER1_CN, ALLOCATED_ATTR, ensure_str(ent.getValue(ALLOCATED_ATTR))))
    topology_st.standalone.delete_s(ACTIVE_USER1_DN)


def test_ticket47828_run_2(topology_st):
    """
    NO exclude scope: Add a staged entry and check its ALLOCATED_ATTR is  set
    """
    _header(topology_st, 'NO exclude scope: Add a staged entry and check its ALLOCATED_ATTR is  set')

    topology_st.standalone.add_s(
        Entry((STAGED_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                 'cn': STAGED_USER1_CN,
                                 'sn': STAGED_USER1_CN,
                                 ALLOCATED_ATTR: str(-1)})))
    ent = topology_st.standalone.getEntry(STAGED_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ensure_str(ent.getValue(ALLOCATED_ATTR)) != str(-1)
    topology_st.standalone.log.debug('%s.%s=%s' % (STAGED_USER1_CN, ALLOCATED_ATTR, ensure_str(ent.getValue(ALLOCATED_ATTR))))
    topology_st.standalone.delete_s(STAGED_USER1_DN)


def test_ticket47828_run_3(topology_st):
    """
    NO exclude scope: Add a staged entry and check its ALLOCATED_ATTR is unchanged (!= magic)
    """
    _header(topology_st, 'NO exclude scope: Add a staged entry and check its ALLOCATED_ATTR is unchanged (!= magic)')

    topology_st.standalone.add_s(
        Entry((STAGED_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                 'cn': STAGED_USER1_CN,
                                 'sn': STAGED_USER1_CN,
                                 ALLOCATED_ATTR: str(20)})))
    ent = topology_st.standalone.getEntry(STAGED_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ensure_str(ent.getValue(ALLOCATED_ATTR)) == str(20)
    topology_st.standalone.log.debug('%s.%s=%s' % (STAGED_USER1_CN, ALLOCATED_ATTR, ensure_str(ent.getValue(ALLOCATED_ATTR))))
    topology_st.standalone.delete_s(STAGED_USER1_DN)


def test_ticket47828_run_4(topology_st):
    '''
    Exclude the provisioning container
    '''
    _header(topology_st, 'Exclude the provisioning container')

    dn_config = "cn=excluded scope, cn=%s, %s" % (PLUGIN_DNA, DN_PLUGIN)
    mod = [(ldap.MOD_REPLACE, 'dnaExcludeScope', ensure_bytes(PROVISIONING))]
    topology_st.standalone.modify_s(dn_config, mod)


def test_ticket47828_run_5(topology_st):
    """
    Provisioning excluded scope: Add an active entry and check its ALLOCATED_ATTR is set
    """
    _header(topology_st, 'Provisioning excluded scope: Add an active entry and check its ALLOCATED_ATTR is set')

    topology_st.standalone.add_s(
        Entry((ACTIVE_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                 'cn': ACTIVE_USER1_CN,
                                 'sn': ACTIVE_USER1_CN,
                                 ALLOCATED_ATTR: str(-1)})))
    ent = topology_st.standalone.getEntry(ACTIVE_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ensure_str(ensure_str(ent.getValue(ALLOCATED_ATTR))) != str(-1)
    topology_st.standalone.log.debug('%s.%s=%s' % (ACTIVE_USER1_CN, ALLOCATED_ATTR, ensure_str(ensure_str(ent.getValue(ALLOCATED_ATTR)))))
    topology_st.standalone.delete_s(ACTIVE_USER1_DN)


def test_ticket47828_run_6(topology_st):
    """
    Provisioning excluded scope: Add an active entry and check its ALLOCATED_ATTR is unchanged (!= magic)
    """
    _header(topology_st,
            'Provisioning excluded scope: Add an active entry and check its ALLOCATED_ATTR is unchanged (!= magic)')

    topology_st.standalone.add_s(
        Entry((ACTIVE_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                 'cn': ACTIVE_USER1_CN,
                                 'sn': ACTIVE_USER1_CN,
                                 ALLOCATED_ATTR: str(20)})))
    ent = topology_st.standalone.getEntry(ACTIVE_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ensure_str(ensure_str(ent.getValue(ALLOCATED_ATTR))) == str(20)
    topology_st.standalone.log.debug('%s.%s=%s' % (ACTIVE_USER1_CN, ALLOCATED_ATTR, ensure_str(ensure_str(ent.getValue(ALLOCATED_ATTR)))))
    topology_st.standalone.delete_s(ACTIVE_USER1_DN)


def test_ticket47828_run_7(topology_st):
    """
    Provisioning excluded scope: Add a staged entry and check its ALLOCATED_ATTR is  not set
    """
    _header(topology_st, 'Provisioning excluded scope: Add a staged entry and check its ALLOCATED_ATTR is  not set')

    topology_st.standalone.add_s(
        Entry((STAGED_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                 'cn': STAGED_USER1_CN,
                                 'sn': STAGED_USER1_CN,
                                 ALLOCATED_ATTR: str(-1)})))
    ent = topology_st.standalone.getEntry(STAGED_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ensure_str(ent.getValue(ALLOCATED_ATTR)) == str(-1)
    topology_st.standalone.log.debug('%s.%s=%s' % (STAGED_USER1_CN, ALLOCATED_ATTR, ensure_str(ent.getValue(ALLOCATED_ATTR))))
    topology_st.standalone.delete_s(STAGED_USER1_DN)


def test_ticket47828_run_8(topology_st):
    """
    Provisioning excluded scope: Add a staged entry and check its ALLOCATED_ATTR is unchanged (!= magic)
    """
    _header(topology_st,
            'Provisioning excluded scope: Add a staged entry and check its ALLOCATED_ATTR is unchanged (!= magic)')

    topology_st.standalone.add_s(
        Entry((STAGED_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                 'cn': STAGED_USER1_CN,
                                 'sn': STAGED_USER1_CN,
                                 ALLOCATED_ATTR: str(20)})))
    ent = topology_st.standalone.getEntry(STAGED_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ensure_str(ent.getValue(ALLOCATED_ATTR)) == str(20)
    topology_st.standalone.log.debug('%s.%s=%s' % (STAGED_USER1_CN, ALLOCATED_ATTR, ensure_str(ent.getValue(ALLOCATED_ATTR))))
    topology_st.standalone.delete_s(STAGED_USER1_DN)


def test_ticket47828_run_9(topology_st):
    """
    Provisioning excluded scope: Add an dummy entry and check its ALLOCATED_ATTR is set
    """
    _header(topology_st, 'Provisioning excluded scope: Add an dummy entry and check its ALLOCATED_ATTR is set')

    topology_st.standalone.add_s(
        Entry((DUMMY_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                'cn': DUMMY_USER1_CN,
                                'sn': DUMMY_USER1_CN,
                                ALLOCATED_ATTR: str(-1)})))
    ent = topology_st.standalone.getEntry(DUMMY_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ensure_str(ent.getValue(ALLOCATED_ATTR)) != str(-1)
    topology_st.standalone.log.debug('%s.%s=%s' % (DUMMY_USER1_CN, ALLOCATED_ATTR, ensure_str(ent.getValue(ALLOCATED_ATTR))))
    topology_st.standalone.delete_s(DUMMY_USER1_DN)


def test_ticket47828_run_10(topology_st):
    """
    Provisioning excluded scope: Add an dummy entry and check its ALLOCATED_ATTR is unchanged (!= magic)
    """
    _header(topology_st,
            'Provisioning excluded scope: Add an dummy entry and check its ALLOCATED_ATTR is unchanged (!= magic)')

    topology_st.standalone.add_s(
        Entry((DUMMY_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                'cn': DUMMY_USER1_CN,
                                'sn': DUMMY_USER1_CN,
                                ALLOCATED_ATTR: str(20)})))
    ent = topology_st.standalone.getEntry(DUMMY_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ensure_str(ent.getValue(ALLOCATED_ATTR)) == str(20)
    topology_st.standalone.log.debug('%s.%s=%s' % (DUMMY_USER1_CN, ALLOCATED_ATTR, ensure_str(ent.getValue(ALLOCATED_ATTR))))
    topology_st.standalone.delete_s(DUMMY_USER1_DN)


def test_ticket47828_run_11(topology_st):
    '''
    Exclude (in addition) the dummy container
    '''
    _header(topology_st, 'Exclude (in addition) the dummy container')

    dn_config = "cn=excluded scope, cn=%s, %s" % (PLUGIN_DNA, DN_PLUGIN)
    mod = [(ldap.MOD_ADD, 'dnaExcludeScope', ensure_bytes(DUMMY_CONTAINER))]
    topology_st.standalone.modify_s(dn_config, mod)


def test_ticket47828_run_12(topology_st):
    """
    Provisioning/Dummy excluded scope: Add an active entry and check its ALLOCATED_ATTR is set
    """
    _header(topology_st, 'Provisioning/Dummy excluded scope: Add an active entry and check its ALLOCATED_ATTR is set')

    topology_st.standalone.add_s(
        Entry((ACTIVE_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                 'cn': ACTIVE_USER1_CN,
                                 'sn': ACTIVE_USER1_CN,
                                 ALLOCATED_ATTR: str(-1)})))
    ent = topology_st.standalone.getEntry(ACTIVE_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ensure_str(ent.getValue(ALLOCATED_ATTR)) != str(-1)
    topology_st.standalone.log.debug('%s.%s=%s' % (ACTIVE_USER1_CN, ALLOCATED_ATTR, ensure_str(ent.getValue(ALLOCATED_ATTR))))
    topology_st.standalone.delete_s(ACTIVE_USER1_DN)


def test_ticket47828_run_13(topology_st):
    """
    Provisioning/Dummy excluded scope: Add an active entry and check its ALLOCATED_ATTR is unchanged (!= magic)
    """
    _header(topology_st,
            'Provisioning/Dummy excluded scope: Add an active entry and check its ALLOCATED_ATTR is unchanged (!= magic)')

    topology_st.standalone.add_s(
        Entry((ACTIVE_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                 'cn': ACTIVE_USER1_CN,
                                 'sn': ACTIVE_USER1_CN,
                                 ALLOCATED_ATTR: str(20)})))
    ent = topology_st.standalone.getEntry(ACTIVE_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ensure_str(ent.getValue(ALLOCATED_ATTR)) == str(20)
    topology_st.standalone.log.debug('%s.%s=%s' % (ACTIVE_USER1_CN, ALLOCATED_ATTR, ensure_str(ent.getValue(ALLOCATED_ATTR))))
    topology_st.standalone.delete_s(ACTIVE_USER1_DN)


def test_ticket47828_run_14(topology_st):
    """
    Provisioning/Dummy excluded scope: Add a staged entry and check its ALLOCATED_ATTR is not set
    """
    _header(topology_st,
            'Provisioning/Dummy excluded scope: Add a staged entry and check its ALLOCATED_ATTR is not set')

    topology_st.standalone.add_s(
        Entry((STAGED_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                 'cn': STAGED_USER1_CN,
                                 'sn': STAGED_USER1_CN,
                                 ALLOCATED_ATTR: str(-1)})))
    ent = topology_st.standalone.getEntry(STAGED_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ensure_str(ent.getValue(ALLOCATED_ATTR)) == str(-1)
    topology_st.standalone.log.debug('%s.%s=%s' % (STAGED_USER1_CN, ALLOCATED_ATTR, ensure_str(ent.getValue(ALLOCATED_ATTR))))
    topology_st.standalone.delete_s(STAGED_USER1_DN)


def test_ticket47828_run_15(topology_st):
    """
    Provisioning/Dummy excluded scope: Add a staged entry and check its ALLOCATED_ATTR is unchanged (!= magic)
    """
    _header(topology_st,
            'Provisioning/Dummy excluded scope: Add a staged entry and check its ALLOCATED_ATTR is unchanged (!= magic)')

    topology_st.standalone.add_s(
        Entry((STAGED_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                 'cn': STAGED_USER1_CN,
                                 'sn': STAGED_USER1_CN,
                                 ALLOCATED_ATTR: str(20)})))
    ent = topology_st.standalone.getEntry(STAGED_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ensure_str(ent.getValue(ALLOCATED_ATTR)) == str(20)
    topology_st.standalone.log.debug('%s.%s=%s' % (STAGED_USER1_CN, ALLOCATED_ATTR, ensure_str(ent.getValue(ALLOCATED_ATTR))))
    topology_st.standalone.delete_s(STAGED_USER1_DN)


def test_ticket47828_run_16(topology_st):
    """
    Provisioning/Dummy excluded scope: Add an dummy entry and check its ALLOCATED_ATTR is not set
    """
    _header(topology_st,
            'Provisioning/Dummy excluded scope: Add an dummy entry and check its ALLOCATED_ATTR not is set')

    topology_st.standalone.add_s(
        Entry((DUMMY_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                'cn': DUMMY_USER1_CN,
                                'sn': DUMMY_USER1_CN,
                                ALLOCATED_ATTR: str(-1)})))
    ent = topology_st.standalone.getEntry(DUMMY_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ensure_str(ent.getValue(ALLOCATED_ATTR)) == str(-1)
    topology_st.standalone.log.debug('%s.%s=%s' % (DUMMY_USER1_CN, ALLOCATED_ATTR, ensure_str(ent.getValue(ALLOCATED_ATTR))))
    topology_st.standalone.delete_s(DUMMY_USER1_DN)


def test_ticket47828_run_17(topology_st):
    """
    Provisioning/Dummy excluded scope: Add an dummy entry and check its ALLOCATED_ATTR is unchanged (!= magic)
    """
    _header(topology_st,
            'Provisioning/Dummy excluded scope: Add an dummy entry and check its ALLOCATED_ATTR is unchanged (!= magic)')

    topology_st.standalone.add_s(
        Entry((DUMMY_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                'cn': DUMMY_USER1_CN,
                                'sn': DUMMY_USER1_CN,
                                ALLOCATED_ATTR: str(20)})))
    ent = topology_st.standalone.getEntry(DUMMY_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ensure_str(ent.getValue(ALLOCATED_ATTR)) == str(20)
    topology_st.standalone.log.debug('%s.%s=%s' % (DUMMY_USER1_CN, ALLOCATED_ATTR, ensure_str(ent.getValue(ALLOCATED_ATTR))))
    topology_st.standalone.delete_s(DUMMY_USER1_DN)


def test_ticket47828_run_18(topology_st):
    '''
    Exclude PROVISIONING and a wrong container
    '''
    _header(topology_st, 'Exclude PROVISIONING and a wrong container')

    dn_config = "cn=excluded scope, cn=%s, %s" % (PLUGIN_DNA, DN_PLUGIN)
    mod = [(ldap.MOD_REPLACE, 'dnaExcludeScope', ensure_bytes(PROVISIONING))]
    topology_st.standalone.modify_s(dn_config, mod)
    try:
        mod = [(ldap.MOD_ADD, 'dnaExcludeScope', ensure_bytes("invalidDN,%s" % SUFFIX))]
        topology_st.standalone.modify_s(dn_config, mod)
        raise ValueError("invalid dnaExcludeScope value (not a DN)")
    except ldap.INVALID_SYNTAX:
        pass


def test_ticket47828_run_19(topology_st):
    """
    Provisioning+wrong container excluded scope: Add an active entry and check its ALLOCATED_ATTR is set
    """
    _header(topology_st,
            'Provisioning+wrong container excluded scope: Add an active entry and check its ALLOCATED_ATTR is set')

    topology_st.standalone.add_s(
        Entry((ACTIVE_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                 'cn': ACTIVE_USER1_CN,
                                 'sn': ACTIVE_USER1_CN,
                                 ALLOCATED_ATTR: str(-1)})))
    ent = topology_st.standalone.getEntry(ACTIVE_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ensure_str(ent.getValue(ALLOCATED_ATTR)) != str(-1)
    topology_st.standalone.log.debug('%s.%s=%s' % (ACTIVE_USER1_CN, ALLOCATED_ATTR, ensure_str(ent.getValue(ALLOCATED_ATTR))))
    topology_st.standalone.delete_s(ACTIVE_USER1_DN)


def test_ticket47828_run_20(topology_st):
    """
    Provisioning+wrong container excluded scope: Add an active entry and check its ALLOCATED_ATTR is unchanged (!= magic)
    """
    _header(topology_st,
            'Provisioning+wrong container excluded scope: Add an active entry and check its ALLOCATED_ATTR is unchanged (!= magic)')

    topology_st.standalone.add_s(
        Entry((ACTIVE_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                 'cn': ACTIVE_USER1_CN,
                                 'sn': ACTIVE_USER1_CN,
                                 ALLOCATED_ATTR: str(20)})))
    ent = topology_st.standalone.getEntry(ACTIVE_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ensure_str(ent.getValue(ALLOCATED_ATTR)) == str(20)
    topology_st.standalone.log.debug('%s.%s=%s' % (ACTIVE_USER1_CN, ALLOCATED_ATTR, ensure_str(ent.getValue(ALLOCATED_ATTR))))
    topology_st.standalone.delete_s(ACTIVE_USER1_DN)


def test_ticket47828_run_21(topology_st):
    """
    Provisioning+wrong container excluded scope: Add a staged entry and check its ALLOCATED_ATTR is  not set
    """
    _header(topology_st,
            'Provisioning+wrong container excluded scope: Add a staged entry and check its ALLOCATED_ATTR is  not set')

    topology_st.standalone.add_s(
        Entry((STAGED_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                 'cn': STAGED_USER1_CN,
                                 'sn': STAGED_USER1_CN,
                                 ALLOCATED_ATTR: str(-1)})))
    ent = topology_st.standalone.getEntry(STAGED_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ensure_str(ent.getValue(ALLOCATED_ATTR)) == str(-1)
    topology_st.standalone.log.debug('%s.%s=%s' % (STAGED_USER1_CN, ALLOCATED_ATTR, ensure_str(ent.getValue(ALLOCATED_ATTR))))
    topology_st.standalone.delete_s(STAGED_USER1_DN)


def test_ticket47828_run_22(topology_st):
    """
    Provisioning+wrong container excluded scope: Add a staged entry and check its ALLOCATED_ATTR is unchanged (!= magic)
    """
    _header(topology_st,
            'Provisioning+wrong container excluded scope: Add a staged entry and check its ALLOCATED_ATTR is unchanged (!= magic)')

    topology_st.standalone.add_s(
        Entry((STAGED_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                 'cn': STAGED_USER1_CN,
                                 'sn': STAGED_USER1_CN,
                                 ALLOCATED_ATTR: str(20)})))
    ent = topology_st.standalone.getEntry(STAGED_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ensure_str(ent.getValue(ALLOCATED_ATTR)) == str(20)
    topology_st.standalone.log.debug('%s.%s=%s' % (STAGED_USER1_CN, ALLOCATED_ATTR, ensure_str(ent.getValue(ALLOCATED_ATTR))))
    topology_st.standalone.delete_s(STAGED_USER1_DN)


def test_ticket47828_run_23(topology_st):
    """
    Provisioning+wrong container excluded scope: Add an dummy entry and check its ALLOCATED_ATTR is set
    """
    _header(topology_st,
            'Provisioning+wrong container excluded scope: Add an dummy entry and check its ALLOCATED_ATTR is set')

    topology_st.standalone.add_s(
        Entry((DUMMY_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                'cn': DUMMY_USER1_CN,
                                'sn': DUMMY_USER1_CN,
                                ALLOCATED_ATTR: str(-1)})))
    ent = topology_st.standalone.getEntry(DUMMY_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ensure_str(ent.getValue(ALLOCATED_ATTR)) != str(-1)
    topology_st.standalone.log.debug('%s.%s=%s' % (DUMMY_USER1_CN, ALLOCATED_ATTR, ensure_str(ent.getValue(ALLOCATED_ATTR))))
    topology_st.standalone.delete_s(DUMMY_USER1_DN)


def test_ticket47828_run_24(topology_st):
    """
    Provisioning+wrong container excluded scope: Add an dummy entry and check its ALLOCATED_ATTR is unchanged (!= magic)
    """
    _header(topology_st,
            'Provisioning+wrong container excluded scope: Add an dummy entry and check its ALLOCATED_ATTR is unchanged (!= magic)')

    topology_st.standalone.add_s(
        Entry((DUMMY_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                'cn': DUMMY_USER1_CN,
                                'sn': DUMMY_USER1_CN,
                                ALLOCATED_ATTR: str(20)})))
    ent = topology_st.standalone.getEntry(DUMMY_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ensure_str(ent.getValue(ALLOCATED_ATTR)) == str(20)
    topology_st.standalone.log.debug('%s.%s=%s' % (DUMMY_USER1_CN, ALLOCATED_ATTR, ensure_str(ent.getValue(ALLOCATED_ATTR))))
    topology_st.standalone.delete_s(DUMMY_USER1_DN)


def test_ticket47828_run_25(topology_st):
    '''
    Exclude  a wrong container
    '''
    _header(topology_st, 'Exclude a wrong container')

    dn_config = "cn=excluded scope, cn=%s, %s" % (PLUGIN_DNA, DN_PLUGIN)

    try:
        mod = [(ldap.MOD_REPLACE, 'dnaExcludeScope', ensure_bytes("invalidDN,%s" % SUFFIX))]
        topology_st.standalone.modify_s(dn_config, mod)
        raise ValueError("invalid dnaExcludeScope value (not a DN)")
    except ldap.INVALID_SYNTAX:
        pass


def test_ticket47828_run_26(topology_st):
    """
    Wrong container excluded scope: Add an active entry and check its ALLOCATED_ATTR is set
    """
    _header(topology_st, 'Wrong container excluded scope: Add an active entry and check its ALLOCATED_ATTR is set')

    topology_st.standalone.add_s(
        Entry((ACTIVE_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                 'cn': ACTIVE_USER1_CN,
                                 'sn': ACTIVE_USER1_CN,
                                 ALLOCATED_ATTR: str(-1)})))
    ent = topology_st.standalone.getEntry(ACTIVE_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ensure_str(ent.getValue(ALLOCATED_ATTR)) != str(-1)
    topology_st.standalone.log.debug('%s.%s=%s' % (ACTIVE_USER1_CN, ALLOCATED_ATTR, ensure_str(ent.getValue(ALLOCATED_ATTR))))
    topology_st.standalone.delete_s(ACTIVE_USER1_DN)


def test_ticket47828_run_27(topology_st):
    """
    Wrong container excluded scope: Add an active entry and check its ALLOCATED_ATTR is unchanged (!= magic)
    """
    _header(topology_st,
            'Wrong container excluded scope: Add an active entry and check its ALLOCATED_ATTR is unchanged (!= magic)')

    topology_st.standalone.add_s(
        Entry((ACTIVE_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                 'cn': ACTIVE_USER1_CN,
                                 'sn': ACTIVE_USER1_CN,
                                 ALLOCATED_ATTR: str(20)})))
    ent = topology_st.standalone.getEntry(ACTIVE_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ensure_str(ent.getValue(ALLOCATED_ATTR)) == str(20)
    topology_st.standalone.log.debug('%s.%s=%s' % (ACTIVE_USER1_CN, ALLOCATED_ATTR, ensure_str(ent.getValue(ALLOCATED_ATTR))))
    topology_st.standalone.delete_s(ACTIVE_USER1_DN)


def test_ticket47828_run_28(topology_st):
    """
    Wrong container excluded scope: Add a staged entry and check its ALLOCATED_ATTR is  not set
    """
    _header(topology_st, 'Wrong container excluded scope: Add a staged entry and check its ALLOCATED_ATTR is  not set')

    topology_st.standalone.add_s(
        Entry((STAGED_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                 'cn': STAGED_USER1_CN,
                                 'sn': STAGED_USER1_CN,
                                 ALLOCATED_ATTR: str(-1)})))
    ent = topology_st.standalone.getEntry(STAGED_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ensure_str(ent.getValue(ALLOCATED_ATTR)) == str(-1)
    topology_st.standalone.log.debug('%s.%s=%s' % (STAGED_USER1_CN, ALLOCATED_ATTR, ensure_str(ent.getValue(ALLOCATED_ATTR))))
    topology_st.standalone.delete_s(STAGED_USER1_DN)


def test_ticket47828_run_29(topology_st):
    """
    Wrong container excluded scope: Add a staged entry and check its ALLOCATED_ATTR is unchanged (!= magic)
    """
    _header(topology_st,
            'Wrong container excluded scope: Add a staged entry and check its ALLOCATED_ATTR is unchanged (!= magic)')

    topology_st.standalone.add_s(
        Entry((STAGED_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                 'cn': STAGED_USER1_CN,
                                 'sn': STAGED_USER1_CN,
                                 ALLOCATED_ATTR: str(20)})))
    ent = topology_st.standalone.getEntry(STAGED_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ensure_str(ent.getValue(ALLOCATED_ATTR)) == str(20)
    topology_st.standalone.log.debug('%s.%s=%s' % (STAGED_USER1_CN, ALLOCATED_ATTR, ensure_str(ent.getValue(ALLOCATED_ATTR))))
    topology_st.standalone.delete_s(STAGED_USER1_DN)


def test_ticket47828_run_30(topology_st):
    """
    Wrong container excluded scope: Add an dummy entry and check its ALLOCATED_ATTR is set
    """
    _header(topology_st, 'Wrong container excluded scope: Add an dummy entry and check its ALLOCATED_ATTR is set')

    topology_st.standalone.add_s(
        Entry((DUMMY_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                'cn': DUMMY_USER1_CN,
                                'sn': DUMMY_USER1_CN,
                                ALLOCATED_ATTR: str(-1)})))
    ent = topology_st.standalone.getEntry(DUMMY_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ensure_str(ent.getValue(ALLOCATED_ATTR)) != str(-1)
    topology_st.standalone.log.debug('%s.%s=%s' % (DUMMY_USER1_CN, ALLOCATED_ATTR, ensure_str(ent.getValue(ALLOCATED_ATTR))))
    topology_st.standalone.delete_s(DUMMY_USER1_DN)


def test_ticket47828_run_31(topology_st):
    """
    Wrong container excluded scope: Add an dummy entry and check its ALLOCATED_ATTR is unchanged (!= magic)
    """
    _header(topology_st,
            'Wrong container excluded scope: Add an dummy entry and check its ALLOCATED_ATTR is unchanged (!= magic)')

    topology_st.standalone.add_s(
        Entry((DUMMY_USER1_DN, {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                'cn': DUMMY_USER1_CN,
                                'sn': DUMMY_USER1_CN,
                                ALLOCATED_ATTR: str(20)})))
    ent = topology_st.standalone.getEntry(DUMMY_USER1_DN, ldap.SCOPE_BASE, "(objectclass=*)")
    assert ent.hasAttr(ALLOCATED_ATTR)
    assert ensure_str(ent.getValue(ALLOCATED_ATTR)) == str(20)
    topology_st.standalone.log.debug('%s.%s=%s' % (DUMMY_USER1_CN, ALLOCATED_ATTR, ensure_str(ent.getValue(ALLOCATED_ATTR))))
    topology_st.standalone.delete_s(DUMMY_USER1_DN)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
