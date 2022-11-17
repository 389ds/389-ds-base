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

from lib389._constants import SUFFIX, DEFAULT_SUFFIX, PLUGIN_MANAGED_ENTRY, DN_LDBM

pytestmark = pytest.mark.tier2

PEOPLE_OU = 'people'
PEOPLE_DN = "ou=%s,%s" % (PEOPLE_OU, SUFFIX)
GROUPS_OU = 'groups'
GROUPS_DN = "ou=%s,%s" % (GROUPS_OU, SUFFIX)
DEFINITIONS_CN = 'definitions'
DEFINITIONS_DN = "cn=%s,%s" % (DEFINITIONS_CN, SUFFIX)
TEMPLATES_CN = 'templates'
TEMPLATES_DN = "cn=%s,%s" % (TEMPLATES_CN, SUFFIX)
MANAGED_GROUP_TEMPLATES_CN = 'managed group templates'
MANAGED_GROUP_TEMPLATES_DN = 'cn=%s,%s' % (MANAGED_GROUP_TEMPLATES_CN, TEMPLATES_DN)
MANAGED_GROUP_MEP_TMPL_CN = 'UPG'
MANAGED_GROUP_MEP_TMPL_DN = 'cn=%s,%s' % (MANAGED_GROUP_MEP_TMPL_CN, MANAGED_GROUP_TEMPLATES_DN)
MANAGED_GROUP_DEF_CN = 'managed group definition'
MANAGED_GROUP_DEF_DN = 'cn=%s,%s' % (MANAGED_GROUP_DEF_CN, DEFINITIONS_DN)

MAX_ACCOUNTS = 2

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


def test_ticket47976_init(topology_st):
    """Create mep definitions and templates"""

    try:
        topology_st.standalone.add_s(Entry((PEOPLE_DN, {
            'objectclass': "top extensibleObject".split(),
            'ou': 'people'})))
    except ldap.ALREADY_EXISTS:
        pass
    try:
        topology_st.standalone.add_s(Entry((GROUPS_DN, {
            'objectclass': "top extensibleObject".split(),
            'ou': GROUPS_OU})))
    except ldap.ALREADY_EXISTS:
        pass
    topology_st.standalone.add_s(Entry((DEFINITIONS_DN, {
        'objectclass': "top nsContainer".split(),
        'cn': DEFINITIONS_CN})))
    topology_st.standalone.add_s(Entry((TEMPLATES_DN, {
        'objectclass': "top nsContainer".split(),
        'cn': TEMPLATES_CN})))
    topology_st.standalone.add_s(Entry((MANAGED_GROUP_DEF_DN, {
        'objectclass': "top extensibleObject".split(),
        'cn': MANAGED_GROUP_DEF_CN,
        'originScope': PEOPLE_DN,
        'originFilter': '(objectclass=posixAccount)',
        'managedBase': GROUPS_DN,
        'managedTemplate': MANAGED_GROUP_MEP_TMPL_DN})))

    topology_st.standalone.add_s(Entry((MANAGED_GROUP_TEMPLATES_DN, {
        'objectclass': "top nsContainer".split(),
        'cn': MANAGED_GROUP_TEMPLATES_CN})))

    topology_st.standalone.add_s(Entry((MANAGED_GROUP_MEP_TMPL_DN, {
        'objectclass': "top mepTemplateEntry".split(),
        'cn': MANAGED_GROUP_MEP_TMPL_CN,
        'mepRDNAttr': 'cn',
        'mepStaticAttr': ['objectclass: posixGroup',
                          'objectclass: extensibleObject'],
        'mepMappedAttr': ['cn: $cn|uid: $cn',
                          'gidNumber: $uidNumber']})))

    topology_st.standalone.plugins.enable(name=PLUGIN_MANAGED_ENTRY)
    topology_st.standalone.restart(timeout=10)


def test_ticket47976_1(topology_st):
    mod = [(ldap.MOD_REPLACE, 'nsslapd-pluginConfigArea', ensure_bytes(DEFINITIONS_DN))]
    topology_st.standalone.modify_s('cn=%s,cn=plugins,cn=config' % PLUGIN_MANAGED_ENTRY, mod)
    topology_st.standalone.stop(timeout=10)
    topology_st.standalone.start(timeout=10)
    for cpt in range(MAX_ACCOUNTS):
        name = "user%d" % (cpt)
        topology_st.standalone.add_s(Entry(("uid=%s,%s" % (name, PEOPLE_DN), {
            'objectclass': 'top posixAccount extensibleObject'.split(),
            'uid': name,
            'cn': name,
            'uidNumber': '1',
            'gidNumber': '1',
            'homeDirectory': '/home/%s' % name
        })))


def test_ticket47976_2(topology_st):
    """It reimports the database with a very large page size
    so all the entries (user and its private group).
    """

    log.info('Test complete')
    mod = [(ldap.MOD_REPLACE, 'nsslapd-db-page-size', ensure_bytes(str(128 * 1024)))]
    topology_st.standalone.modify_s(DN_LDBM, mod)

    # Get the the full path and name for our LDIF we will be exporting
    log.info('Export LDIF file...')
    ldif_dir = topology_st.standalone.get_ldif_dir()
    ldif_file = ldif_dir + "/export.ldif"
    args = {EXPORT_REPL_INFO: False,
            TASK_WAIT: True}
    exportTask = Tasks(topology_st.standalone)
    try:
        exportTask.exportLDIF(DEFAULT_SUFFIX, None, ldif_file, args)
    except ValueError:
        assert False
    # import the new ldif file
    log.info('Import LDIF file...')
    importTask = Tasks(topology_st.standalone)
    args = {TASK_WAIT: True}
    try:
        importTask.importLDIF(DEFAULT_SUFFIX, None, ldif_file, args)
        os.remove(ldif_file)
    except ValueError:
        os.remove(ldif_file)
        assert False


def test_ticket47976_3(topology_st):
    """A single delete of a user should hit 47976, because mep post op will
    delete its related group.
    """

    log.info('Testing if the delete will hang or not')
    # log.info("\n\nAttach\n\n debugger")
    # time.sleep(60)
    topology_st.standalone.set_option(ldap.OPT_TIMEOUT, 5)
    try:
        for cpt in range(MAX_ACCOUNTS):
            name = "user%d" % (cpt)
            topology_st.standalone.delete_s("uid=%s,%s" % (name, PEOPLE_DN))
    except ldap.TIMEOUT as e:
        log.fatal('Timeout... likely it hangs (47976)')
        assert False

    # check the entry has been deleted
    for cpt in range(MAX_ACCOUNTS):
        try:
            name = "user%d" % (cpt)
            topology_st.standalone.getEntry("uid=%s,%s" % (name, PEOPLE_DN), ldap.SCOPE_BASE, 'objectclass=*')
            assert False
        except ldap.NO_SUCH_OBJECT:
            log.info('%s was correctly deleted' % name)
            pass

    assert cpt == (MAX_ACCOUNTS - 1)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
