# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
'''
Created on Nov 27th, 2018

@author: tbordaz
'''
import logging
import subprocess
import pytest
from lib389 import Entry
from lib389.utils import *
from lib389.plugins import *
from lib389._constants import *
from lib389.topologies import topology_st as topo

pytestmark = pytest.mark.tier1

def add_user(server, uid, testbase, locality=None, tel=None, title=None):
    dn = 'uid=%s,%s' % (uid, testbase)
    log.fatal('Adding user (%s): ' % dn)
    server.add_s(Entry((dn, {'objectclass': ['top', 'person', 'organizationalPerson', 'inetOrgPerson'],
                             'cn': 'user_%s' % uid,
                             'sn': 'user_%s' % uid,
                             'uid': uid,
                             'l': locality,
                             'title': title,
                             'telephoneNumber': tel,
                             'description': 'description real'})))

def test_cos_operational_default(topo):
    """operational-default cosAttribute should not overwrite an existing value

    :id: 12fadff9-e14a-4c64-a3ee-51152cb8fcfb
    :setup: Standalone Instance
    :steps:
        1. Create a user entry with attribute 'l' and 'telephonenumber' (real attribute with real value)
        2. Create cos that defines 'l' as operational-default (virt. attr. with value != real value)
        3. Create cos that defines 'telephone' as default (virt. attr. with value != real value)
        4. Check that telephone is retrieved with real value
        5. Check that 'l' is retrieved with real value
    :expectedresults:
        1. should succeed
        2. should succeed
        3. should succeed
    """

    REAL = 'real'
    VIRTUAL = 'virtual'
    TEL_REAL = '1234 is %s' % REAL
    TEL_VIRT = '4321 is %s' % VIRTUAL

    LOC_REAL = 'here is %s' % REAL
    LOC_VIRT = 'there is %s' % VIRTUAL

    TITLE_REAL = 'title is %s' % REAL

    inst = topo[0]

    PEOPLE = 'ou=people,%s' % SUFFIX
    add_user(inst, 'user_0', PEOPLE, locality=LOC_REAL, tel=TEL_REAL, title=TITLE_REAL)

    # locality cos operational-default
    LOC_COS_TEMPLATE = "cn=locality_template,%s" % PEOPLE
    LOC_COS_DEFINITION = "cn=locality_definition,%s" % PEOPLE
    inst.add_s(Entry((LOC_COS_TEMPLATE, {
            'objectclass': ['top', 'extensibleObject', 'costemplate',
                            'ldapsubentry'],
            'l': LOC_VIRT})))

    inst.add_s(Entry((LOC_COS_DEFINITION, {
            'objectclass': ['top', 'LdapSubEntry', 'cosSuperDefinition',
                            'cosPointerDefinition'],
            'cosTemplateDn': LOC_COS_TEMPLATE,
            'cosAttribute': 'l operational-default'})))

    # telephone cos default
    TEL_COS_TEMPLATE = "cn=telephone_template,%s" % PEOPLE
    TEL_COS_DEFINITION = "cn=telephone_definition,%s" % PEOPLE
    inst.add_s(Entry((TEL_COS_TEMPLATE, {
            'objectclass': ['top', 'extensibleObject', 'costemplate',
                            'ldapsubentry'],
            'telephonenumber': TEL_VIRT})))

    inst.add_s(Entry((TEL_COS_DEFINITION, {
            'objectclass': ['top', 'LdapSubEntry', 'cosSuperDefinition',
                            'cosPointerDefinition'],
            'cosTemplateDn': TEL_COS_TEMPLATE,
            'cosAttribute': 'telephonenumber default'})))

    # seeAlso cos operational
    SEEALSO_VIRT = "dc=%s,dc=example,dc=com" % VIRTUAL
    SEEALSO_COS_TEMPLATE = "cn=seealso_template,%s" % PEOPLE
    SEEALSO_COS_DEFINITION = "cn=seealso_definition,%s" % PEOPLE
    inst.add_s(Entry((SEEALSO_COS_TEMPLATE, {
            'objectclass': ['top', 'extensibleObject', 'costemplate',
                            'ldapsubentry'],
            'seealso': SEEALSO_VIRT})))

    inst.add_s(Entry((SEEALSO_COS_DEFINITION, {
            'objectclass': ['top', 'LdapSubEntry', 'cosSuperDefinition',
                            'cosPointerDefinition'],
            'cosTemplateDn': SEEALSO_COS_TEMPLATE,
            'cosAttribute': 'seealso operational'})))

    # description cos override
    DESC_VIRT = "desc is %s" % VIRTUAL
    DESC_COS_TEMPLATE = "cn=desc_template,%s" % PEOPLE
    DESC_COS_DEFINITION = "cn=desc_definition,%s" % PEOPLE
    inst.add_s(Entry((DESC_COS_TEMPLATE, {
            'objectclass': ['top', 'extensibleObject', 'costemplate',
                            'ldapsubentry'],
            'description': DESC_VIRT})))

    inst.add_s(Entry((DESC_COS_DEFINITION, {
            'objectclass': ['top', 'LdapSubEntry', 'cosSuperDefinition',
                            'cosPointerDefinition'],
            'cosTemplateDn': DESC_COS_TEMPLATE,
            'cosAttribute': 'description override'})))

    # title cos override
    TITLE_VIRT = []
    for i in range(2):
        TITLE_VIRT.append("title is %s %d" % (VIRTUAL, i))
    TITLE_COS_TEMPLATE = "cn=title_template,%s" % PEOPLE
    TITLE_COS_DEFINITION = "cn=title_definition,%s" % PEOPLE
    inst.add_s(Entry((TITLE_COS_TEMPLATE, {
            'objectclass': ['top', 'extensibleObject', 'costemplate',
                            'ldapsubentry'],
            'title': TITLE_VIRT})))

    inst.add_s(Entry((TITLE_COS_DEFINITION, {
            'objectclass': ['top', 'LdapSubEntry', 'cosSuperDefinition',
                            'cosPointerDefinition'],
            'cosTemplateDn': TITLE_COS_TEMPLATE,
            'cosAttribute': 'title merge-schemes'})))

    # note that the search requests both attributes (it is required for operational*)
    ents = inst.search_s(SUFFIX, ldap.SCOPE_SUBTREE, "uid=user_0", ["telephonenumber", "l"])
    assert len(ents) == 1
    ent = ents[0]

    # Check telephonenumber (specifier default) with real value => real
    assert ent.hasAttr('telephonenumber')
    value = ent.getValue('telephonenumber')
    log.info('Returned telephonenumber (exp. real): %s' % value)
    log.info('Returned telephonenumber: %d' % value.find(REAL.encode()))
    assert value.find(REAL.encode()) != -1

    # Check 'locality' (specifier operational-default) with real value => real
    assert ent.hasAttr('l')
    value = ent.getValue('l')
    log.info('Returned l (exp. real): %s ' % value)
    log.info('Returned l: %d' % value.find(REAL.encode()))
    assert value.find(REAL.encode()) != -1
    
    # Check 'seealso' (specifier operational) without real value => virtual
    assert not ent.hasAttr('seealso')
    ents = inst.search_s(SUFFIX, ldap.SCOPE_SUBTREE, "uid=user_0", ["seealso"])
    assert len(ents) == 1
    ent = ents[0]
    value = ent.getValue('seealso')
    log.info('Returned seealso (exp. virtual): %s' % value)
    log.info('Returned seealso: %d' % value.find(VIRTUAL.encode()))
    assert value.find(VIRTUAL.encode()) != -1
    
    # Check 'description' (specifier override) with real value => virtual
    assert not ent.hasAttr('description')
    ents = inst.search_s(SUFFIX, ldap.SCOPE_SUBTREE, "uid=user_0")
    assert len(ents) == 1
    ent = ents[0]
    value = ent.getValue('description')
    log.info('Returned description (exp. virtual): %s' % value)
    log.info('Returned description: %d' % value.find(VIRTUAL.encode()))
    assert value.find(VIRTUAL.encode()) != -1

    # Check 'title' (specifier merge-schemes) with real value => real value returned
    ents = inst.search_s(SUFFIX, ldap.SCOPE_SUBTREE, "uid=user_0")
    assert len(ents) == 1
    ent = ents[0]
    found_real = False
    found_virtual = False
    for value in ent.getValues('title'):
        log.info('Returned title (exp. real): %s' % value)
        if value.find(VIRTUAL.encode()) != -1:
            found_virtual = True
        if value.find(REAL.encode()) != -1:
            found_real = True
    assert not found_virtual
    assert found_real

    # Check 'title ((specifier merge-schemes) without real value => real value returned
    ents = inst.search_s(SUFFIX, ldap.SCOPE_SUBTREE, "uid=user_0")
    assert len(ents) == 1
    inst.modify_s(ents[0].dn,[(ldap.MOD_DELETE, 'title', None)])

    inst.restart()
    ents = inst.search_s(SUFFIX, ldap.SCOPE_SUBTREE, "uid=user_0")
    assert len(ents) == 1
    ent = ents[0]
    found_real = False
    found_virtual = False
    count = 0
    for value in ent.getValues('title'):
        log.info('Returned title(exp. virt): %s' % value)
        count = count + 1
        if value.find(VIRTUAL.encode()) != -1:
            found_virtual = True
        if value.find(REAL.encode()) != -1:
            found_real = True
    assert not found_real
    assert found_virtual
    assert count == 2
