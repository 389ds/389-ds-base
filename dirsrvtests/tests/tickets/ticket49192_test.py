# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import time
import ldap
import logging
import pytest
from lib389 import Entry
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st as topo

pytestmark = pytest.mark.tier2

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

INDEX_DN = 'cn=index,cn=Second_Backend,cn=ldbm database,cn=plugins,cn=config'
SUFFIX_DN = 'cn=Second_Backend,cn=ldbm database,cn=plugins,cn=config'
MY_SUFFIX = "o=hang.com"
USER_DN = 'uid=user,' + MY_SUFFIX


def test_ticket49192(topo):
    """Trigger deadlock when removing suffix
    """

    #
    # Create a second suffix/backend
    #
    log.info('Creating second backend...')
    topo.standalone.backends.create(None, properties={
        BACKEND_NAME: "Second_Backend",
        'suffix': "o=hang.com",
        })
    try:
        topo.standalone.add_s(Entry(("o=hang.com", {
            'objectclass': 'top organization'.split(),
            'o': 'hang.com'})))
    except ldap.LDAPError as e:
        log.fatal('Failed to create 2nd suffix: error ' + e.args[0]['desc'])
        assert False

    #
    # Add roles
    #
    log.info('Adding roles...')
    try:
        topo.standalone.add_s(Entry(('cn=nsManagedDisabledRole,' + MY_SUFFIX, {
            'objectclass': ['top', 'LdapSubEntry',
                            'nsRoleDefinition',
                            'nsSimpleRoleDefinition',
                            'nsManagedRoleDefinition'],
            'cn': 'nsManagedDisabledRole'})))
    except ldap.LDAPError as e:
        log.fatal('Failed to add managed role: error ' + e.args[0]['desc'])
        assert False

    try:
        topo.standalone.add_s(Entry(('cn=nsDisabledRole,' + MY_SUFFIX, {
            'objectclass': ['top', 'LdapSubEntry',
                            'nsRoleDefinition',
                            'nsComplexRoleDefinition',
                            'nsNestedRoleDefinition'],
            'cn': 'nsDisabledRole',
            'nsRoledn': 'cn=nsManagedDisabledRole,' + MY_SUFFIX})))
    except ldap.LDAPError as e:
        log.fatal('Failed to add nested role: error ' + e.args[0]['desc'])
        assert False

    try:
        topo.standalone.add_s(Entry(('cn=nsAccountInactivationTmp,' + MY_SUFFIX, {
            'objectclass': ['top', 'nsContainer'],
            'cn': 'nsAccountInactivationTmp'})))
    except ldap.LDAPError as e:
        log.fatal('Failed to add container: error ' + e.args[0]['desc'])
        assert False

    try:
        topo.standalone.add_s(Entry(('cn=\"cn=nsDisabledRole,' + MY_SUFFIX + '\",cn=nsAccountInactivationTmp,'  + MY_SUFFIX, {
            'objectclass': ['top', 'extensibleObject', 'costemplate',
                            'ldapsubentry'],
            'nsAccountLock': 'true'})))
    except ldap.LDAPError as e:
        log.fatal('Failed to add cos1: error ' + e.args[0]['desc'])
        assert False

    try:
        topo.standalone.add_s(Entry(('cn=nsAccountInactivation_cos,' + MY_SUFFIX, {
            'objectclass': ['top', 'LdapSubEntry', 'cosSuperDefinition',
                            'cosClassicDefinition'],
            'cn': 'nsAccountInactivation_cos',
            'cosTemplateDn': 'cn=nsAccountInactivationTmp,' + MY_SUFFIX,
            'cosSpecifier': 'nsRole',
            'cosAttribute': 'nsAccountLock operational'})))
    except ldap.LDAPError as e:
        log.fatal('Failed to add cos2 : error ' + e.args[0]['desc'])
        assert False

    #
    # Add test entry
    #
    try:
        topo.standalone.add_s(Entry((USER_DN, {
            'objectclass': 'top extensibleObject'.split(),
            'uid': 'user',
            'userpassword': 'password',
        })))
    except ldap.LDAPError as e:
        log.fatal('Failed to add user: error ' + e.args[0]['desc'])
        assert False

    #
    # Inactivate the user account
    #
    try:
        topo.standalone.modify_s(USER_DN,
                                [(ldap.MOD_ADD,
                                  'nsRoleDN',
                                  ensure_bytes('cn=nsManagedDisabledRole,' + MY_SUFFIX))])
    except ldap.LDAPError as e:
        log.fatal('Failed to disable user: error ' + e.args[0]['desc'])
        assert False

    time.sleep(1)

    # Bind as user (should fail)
    try:
        topo.standalone.simple_bind_s(USER_DN, 'password')
        log.error("Bind incorrectly worked")
        assert False
    except ldap.UNWILLING_TO_PERFORM:
        log.info('Got error 53 as expected')
    except ldap.LDAPError as e:
        log.fatal('Bind has unexpected error ' + e.args[0]['desc'])
        assert False

    # Bind as root DN
    try:
        topo.standalone.simple_bind_s(DN_DM, PASSWORD)
    except ldap.LDAPError as e:
        log.fatal('RootDN Bind has unexpected error ' + e.args[0]['desc'])
        assert False

    #
    # Delete suffix
    #
    log.info('Delete the suffix and children...')
    try:
        index_entries = topo.standalone.search_s(
            SUFFIX_DN, ldap.SCOPE_SUBTREE, 'objectclass=top')
    except ldap.LDAPError as e:
            log.error('Failed to search: %s - error %s' % (SUFFIX_DN, str(e)))

    for entry in reversed(index_entries):
        try:
            log.info("Deleting: " + entry.dn)
            if entry.dn != SUFFIX_DN and entry.dn != INDEX_DN:
                topo.standalone.search_s(entry.dn,
                                         ldap.SCOPE_ONELEVEL,
                                         'objectclass=top')
            topo.standalone.delete_s(entry.dn)
        except ldap.LDAPError as e:
            log.fatal('Failed to delete entry: %s - error %s' %
                      (entry.dn, str(e)))
            assert False

    log.info("Test Passed")


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

