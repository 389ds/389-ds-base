# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
from ldap.controls.simple import GetEffectiveRightsControl
from lib389.tasks import *
from lib389.utils import *
from lib389.schema import Schema
from lib389.idm.domain import Domain
from lib389.idm.user import UserAccount, UserAccounts, TEST_USER_PROPERTIES
from lib389.idm.organizationalrole import OrganizationalRole, OrganizationalRoles

from lib389.topologies import topology_m2
from lib389._constants import SUFFIX, DN_SCHEMA, DN_DM, DEFAULT_SUFFIX, PASSWORD

pytestmark = pytest.mark.tier1

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

TEST_REPL_DN = "cn=test_repl, %s" % SUFFIX

STAGING_CN = "staged user"
PRODUCTION_CN = "accounts"
EXCEPT_CN = "excepts"

STAGING_DN = "cn=%s,%s" % (STAGING_CN, SUFFIX)
PRODUCTION_DN = "cn=%s,%s" % (PRODUCTION_CN, SUFFIX)
PROD_EXCEPT_DN = "cn=%s,%s" % (EXCEPT_CN, PRODUCTION_DN)

STAGING_PATTERN = "cn=%s*,%s" % (STAGING_CN[:2], SUFFIX)
PRODUCTION_PATTERN = "cn=%s*,%s" % (PRODUCTION_CN[:2], SUFFIX)
BAD_STAGING_PATTERN = "cn=bad*,%s" % (SUFFIX)
BAD_PRODUCTION_PATTERN = "cn=bad*,%s" % (SUFFIX)

BIND_RDN = "bind_entry"
BIND_DN = "uid=%s,%s" % (BIND_RDN, SUFFIX)
BIND_PW = "password"

NEW_ACCOUNT = "new_account"
MAX_ACCOUNTS = 20

CONFIG_MODDN_ACI_ATTR = "nsslapd-moddn-aci"

SRC_ENTRY_CN = "tuser"
EXT_RDN = "01"
DST_ENTRY_CN = SRC_ENTRY_CN + EXT_RDN

SRC_ENTRY_DN = "cn=%s,%s" % (SRC_ENTRY_CN, SUFFIX)
DST_ENTRY_DN = "cn=%s,%s" % (DST_ENTRY_CN, SUFFIX)


def add_attr(topology_m2, attr_name):
    """Adds attribute to the schema"""

    ATTR_VALUE = """(NAME '%s' \
                    DESC 'Attribute filteri-Multi-Valued' \
                    SYNTAX 1.3.6.1.4.1.1466.115.121.1.27)""" % attr_name
    schema = Schema(topology_m2.ms["master1"])
    schema.add('attributeTypes', ATTR_VALUE)


@pytest.fixture(params=["lang-ja", "binary", "phonetic"])
def aci_with_attr_subtype(request, topology_m2):
    """Adds and deletes an ACI in the DEFAULT_SUFFIX"""

    TARGET_ATTR = 'protectedOperation'
    USER_ATTR = 'allowedToPerform'
    SUBTYPE = request.param
    suffix = Domain(topology_m2.ms["master1"], DEFAULT_SUFFIX)

    log.info("========Executing test with '%s' subtype========" % SUBTYPE)
    log.info("        Add a target attribute")
    add_attr(topology_m2, TARGET_ATTR)

    log.info("        Add a user attribute")
    add_attr(topology_m2, USER_ATTR)

    ACI_TARGET = '(targetattr=%s;%s)' % (TARGET_ATTR, SUBTYPE)
    ACI_ALLOW = '(version 3.0; acl "test aci for subtypes"; allow (read) '
    ACI_SUBJECT = 'userattr = "%s;%s#GROUPDN";)' % (USER_ATTR, SUBTYPE)
    ACI_BODY = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT

    log.info("Add an ACI with attribute subtype")
    suffix.add('aci', ACI_BODY)

    def fin():
        log.info("Finally, delete an ACI with the '%s' subtype" %
                 SUBTYPE)
        suffix.remove('aci', ACI_BODY)

    request.addfinalizer(fin)

    return ACI_BODY


def test_aci_attr_subtype_targetattr(topology_m2, aci_with_attr_subtype):
    """Checks, that ACIs allow attribute subtypes in the targetattr keyword

    :id: a99ccda0-5d0b-4d41-99cc-c5e207b3b687
    :parametrized: yes
    :setup: MMR with two masters,
            Define two attributes in the schema - targetattr and userattr,
            Add an ACI with attribute subtypes - "lang-ja", "binary", "phonetic"
            one by one
    :steps:
        1. Search for the added attribute during setup
           one by one for each subtypes "lang-ja", "binary", "phonetic"
    :expectedresults:
        1. Attributes should be found successfully
           one by one for each subtypes "lang-ja", "binary", "phonetic"
    """

    log.info("Search for the added attribute")
    try:
        entries = topology_m2.ms["master1"].search_s(DEFAULT_SUFFIX,
                                                     ldap.SCOPE_BASE,
                                                     '(objectclass=*)', ['aci'])
        entry = str(entries[0])
        assert aci_with_attr_subtype in entry
        log.info("The added attribute was found")

    except ldap.LDAPError as e:
        log.fatal('Search failed, error: ' + e.message['desc'])
        assert False


def _bind_manager(topology_m2):
    topology_m2.ms["master1"].log.info("Bind as %s " % DN_DM)
    topology_m2.ms["master1"].simple_bind_s(DN_DM, PASSWORD)


def _bind_normal(topology_m2):
    # bind as bind_entry
    topology_m2.ms["master1"].log.info("Bind as %s" % BIND_DN)
    topology_m2.ms["master1"].simple_bind_s(BIND_DN, BIND_PW)


def _moddn_aci_deny_tree(topology_m2, mod_type=None,
                         target_from=STAGING_DN, target_to=PROD_EXCEPT_DN):
    """It denies the access moddn_to in cn=except,cn=accounts,SUFFIX"""

    assert mod_type is not None

    ACI_TARGET_FROM = ""
    ACI_TARGET_TO = ""
    if target_from:
        ACI_TARGET_FROM = "(target_from = \"ldap:///%s\")" % (target_from)
    if target_to:
        ACI_TARGET_TO = "(target_to   = \"ldap:///%s\")" % (target_to)

    ACI_ALLOW = "(version 3.0; acl \"Deny MODDN to prod_except\"; deny (moddn)"
    ACI_SUBJECT = " userdn = \"ldap:///%s\";)" % BIND_DN
    ACI_BODY = ACI_TARGET_TO + ACI_TARGET_FROM + ACI_ALLOW + ACI_SUBJECT
    # topology_m2.ms["master1"].modify_s(SUFFIX, mod)
    topology_m2.ms["master1"].log.info("Add a DENY aci under %s " % PROD_EXCEPT_DN)
    prod_except = OrganizationalRole(topology_m2.ms["master1"], PROD_EXCEPT_DN)
    prod_except.set('aci', ACI_BODY, mod_type)


def _write_aci_staging(topology_m2, mod_type=None):
    assert mod_type is not None

    ACI_TARGET = "(targetattr= \"uid\")(target=\"ldap:///uid=*,%s\")" % STAGING_DN
    ACI_ALLOW = "(version 3.0; acl \"write staging entries\"; allow (write)"
    ACI_SUBJECT = " userdn = \"ldap:///%s\";)" % BIND_DN
    ACI_BODY = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT
    suffix = Domain(topology_m2.ms["master1"], SUFFIX)
    suffix.set('aci', ACI_BODY, mod_type)


def _write_aci_production(topology_m2, mod_type=None):
    assert mod_type is not None

    ACI_TARGET = "(targetattr= \"uid\")(target=\"ldap:///uid=*,%s\")" % PRODUCTION_DN
    ACI_ALLOW = "(version 3.0; acl \"write production entries\"; allow (write)"
    ACI_SUBJECT = " userdn = \"ldap:///%s\";)" % BIND_DN
    ACI_BODY = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT
    suffix = Domain(topology_m2.ms["master1"], SUFFIX)
    suffix.set('aci', ACI_BODY, mod_type)


def _moddn_aci_staging_to_production(topology_m2, mod_type=None,
                                     target_from=STAGING_DN, target_to=PRODUCTION_DN):
    assert mod_type is not None

    ACI_TARGET_FROM = ""
    ACI_TARGET_TO = ""
    if target_from:
        ACI_TARGET_FROM = "(target_from = \"ldap:///%s\")" % (target_from)
    if target_to:
        ACI_TARGET_TO = "(target_to   = \"ldap:///%s\")" % (target_to)

    ACI_ALLOW = "(version 3.0; acl \"MODDN from staging to production\"; allow (moddn)"
    ACI_SUBJECT = " userdn = \"ldap:///%s\";)" % BIND_DN
    ACI_BODY = ACI_TARGET_FROM + ACI_TARGET_TO + ACI_ALLOW + ACI_SUBJECT
    suffix = Domain(topology_m2.ms["master1"], SUFFIX)
    suffix.set('aci', ACI_BODY, mod_type)

    _write_aci_staging(topology_m2, mod_type=mod_type)


def _moddn_aci_from_production_to_staging(topology_m2, mod_type=None):
    assert mod_type is not None

    ACI_TARGET = "(target_from = \"ldap:///%s\") (target_to = \"ldap:///%s\")" % (
        PRODUCTION_DN, STAGING_DN)
    ACI_ALLOW = "(version 3.0; acl \"MODDN from production to staging\"; allow (moddn)"
    ACI_SUBJECT = " userdn = \"ldap:///%s\";)" % BIND_DN
    ACI_BODY = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT
    suffix = Domain(topology_m2.ms["master1"], SUFFIX)
    suffix.set('aci', ACI_BODY, mod_type)

    _write_aci_production(topology_m2, mod_type=mod_type)


@pytest.fixture(scope="module")
def moddn_setup(topology_m2):
    """Creates
       - a staging DIT
       - a production DIT
       - add accounts in staging DIT
       - enable ACL logging (commented for performance reason)
    """

    m1 = topology_m2.ms["master1"]
    o_roles = OrganizationalRoles(m1, SUFFIX)

    m1.log.info("\n\n######## INITIALIZATION ########\n")

    # entry used to bind with
    m1.log.info("Add {}".format(BIND_DN))
    user = UserAccount(m1, BIND_DN)
    user_props = TEST_USER_PROPERTIES.copy()
    user_props.update({'sn': BIND_RDN,
                       'cn': BIND_RDN,
                       'uid': BIND_RDN,
                       'userpassword': BIND_PW})
    user.create(properties=user_props, basedn=SUFFIX)

    # DIT for staging
    m1.log.info("Add {}".format(STAGING_DN))
    o_roles.create(properties={'cn': STAGING_CN, 'description': "staging DIT"})

    # DIT for production
    m1.log.info("Add {}".format(PRODUCTION_DN))
    o_roles.create(properties={'cn': PRODUCTION_CN, 'description': "production DIT"})

    # DIT for production/except
    m1.log.info("Add {}".format(PROD_EXCEPT_DN))
    o_roles_prod = OrganizationalRoles(m1, PRODUCTION_DN)
    o_roles_prod.create(properties={'cn': EXCEPT_CN, 'description': "production except DIT"})

    # enable acl error logging
    # mod = [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', '128')]
    # m1.modify_s(DN_CONFIG, mod)
    # topology_m2.ms["master2"].modify_s(DN_CONFIG, mod)

    # add dummy entries in the staging DIT
    staging_users = UserAccounts(m1, SUFFIX, rdn="cn={}".format(STAGING_CN))
    user_props = TEST_USER_PROPERTIES.copy()
    for cpt in range(MAX_ACCOUNTS):
        name = "{}{}".format(NEW_ACCOUNT, cpt)
        user_props.update({'sn': name, 'cn': name, 'uid': name})
        staging_users.create(properties=user_props)


def test_mode_default_add_deny(topology_m2, moddn_setup):
    """Tests that the ADD operation fails (no ADD aci on production)

    :id: 301d41d3-b8d8-44c5-8eb9-c2d2816b5a4f
    :setup: MMR with two masters,
            M1 - staging DIT
            M2 - production DIT
            add test accounts in staging DIT
    :steps:
        1. Add an entry in production
    :expectedresults:
        1. It should fail due to INSUFFICIENT_ACCESS
    """

    topology_m2.ms["master1"].log.info("\n\n######## mode moddn_aci : ADD (should fail) ########\n")

    _bind_normal(topology_m2)

    #
    # First try to add an entry in production => INSUFFICIENT_ACCESS
    #
    try:
        topology_m2.ms["master1"].log.info("Try to add %s" % PRODUCTION_DN)
        name = "%s%d" % (NEW_ACCOUNT, 0)
        topology_m2.ms["master1"].add_s(Entry(("uid=%s,%s" % (name, PRODUCTION_DN), {
            'objectclass': "top person".split(),
            'sn': name,
            'cn': name,
            'uid': name})))
        assert 0  # this is an error, we should not be allowed to add an entry in production
    except Exception as e:
        topology_m2.ms["master1"].log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)


def test_mode_default_delete_deny(topology_m2, moddn_setup):
    """Tests that the DEL operation fails (no 'delete' aci on production)

    :id: 5dcb2213-3875-489a-8cb5-ace057120ad6
    :setup: MMR with two masters,
            M1 - staging DIT
            M2 - production DIT
            add test accounts in staging DIT
    :steps:
        1. Delete an entry in staging
    :expectedresults:
        1. It should fail due to INSUFFICIENT_ACCESS
    """

    topology_m2.ms["master1"].log.info("\n\n######## DELETE (should fail) ########\n")

    _bind_normal(topology_m2)
    #
    # Second try to delete an entry in staging => INSUFFICIENT_ACCESS
    #
    try:
        topology_m2.ms["master1"].log.info("Try to delete %s" % STAGING_DN)
        name = "%s%d" % (NEW_ACCOUNT, 0)
        topology_m2.ms["master1"].delete_s("uid=%s,%s" % (name, STAGING_DN))
        assert 0  # this is an error, we should not be allowed to add an entry in production
    except Exception as e:
        topology_m2.ms["master1"].log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)


@pytest.mark.parametrize("index,tfrom,tto,failure",
                         [(0, STAGING_DN, PRODUCTION_DN, False),
                          (1, STAGING_DN, PRODUCTION_DN, False),
                          (2, STAGING_DN, BAD_PRODUCTION_PATTERN, True),
                          (3, STAGING_PATTERN, PRODUCTION_DN, False),
                          (4, BAD_STAGING_PATTERN, PRODUCTION_DN, True),
                          (5, STAGING_PATTERN, PRODUCTION_PATTERN, False),
                          (6, None, PRODUCTION_PATTERN, False),
                          (7, STAGING_PATTERN, None, False),
                          (8, None, None, False)])
def test_moddn_staging_prod(topology_m2, moddn_setup,
                            index, tfrom, tto, failure):
    """This test case MOVE entry NEW_ACCOUNT0 from staging to prod
    target_to/target_from: equality filter

    :id: cbafdd68-64d6-431f-9f22-6fbf9ed23ca0
    :parametrized: yes
    :setup: MMR with two masters,
            M1 - staging DIT
            M2 - production DIT
            add test accounts in staging DIT
    :steps:
        1. Try to modify DN with moddn for each value of
           STAGING_DN -> PRODUCTION_DN
        2. Try to modify DN with moddn for each value of
           STAGING_DN -> PRODUCTION_DN with appropriate ACI
    :expectedresults:
        1. It should fail due to INSUFFICIENT_ACCESS
        2. It should pass due to appropriate ACI
    """

    topology_m2.ms["master1"].log.info("\n\n######## MOVE staging -> Prod (%s) ########\n" % index)
    _bind_normal(topology_m2)

    old_rdn = "uid=%s%s" % (NEW_ACCOUNT, index)
    old_dn = "%s,%s" % (old_rdn, STAGING_DN)
    new_rdn = old_rdn
    new_superior = PRODUCTION_DN

    #
    # Try to rename without the appropriate ACI  => INSUFFICIENT_ACCESS
    #
    try:
        topology_m2.ms["master1"].log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
        topology_m2.ms["master1"].rename_s(old_dn, new_rdn, newsuperior=new_superior)
        assert 0
    except AssertionError:
        topology_m2.ms["master1"].log.info(
            "Exception (not really expected exception but that is fine as it fails to rename)")
    except Exception as e:
        topology_m2.ms["master1"].log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)

    # successful MOD with the ACI
    topology_m2.ms["master1"].log.info("\n\n######## MOVE to and from equality filter ########\n")
    _bind_manager(topology_m2)
    _moddn_aci_staging_to_production(topology_m2, mod_type=ldap.MOD_ADD,
                                     target_from=tfrom, target_to=tto)
    _bind_normal(topology_m2)

    try:
        topology_m2.ms["master1"].log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
        topology_m2.ms["master1"].rename_s(old_dn, new_rdn, newsuperior=new_superior)
    except Exception as e:
        topology_m2.ms["master1"].log.info("Exception (expected): %s" % type(e).__name__)
        if failure:
            assert isinstance(e, ldap.INSUFFICIENT_ACCESS)

    # successful MOD with the both ACI
    _bind_manager(topology_m2)
    _moddn_aci_staging_to_production(topology_m2, mod_type=ldap.MOD_DELETE,
                                     target_from=tfrom, target_to=tto)
    _bind_normal(topology_m2)


def test_moddn_staging_prod_9(topology_m2, moddn_setup):
    """Test with nsslapd-moddn-aci set to off so that MODDN requires an 'add' aci.

    :id: 222dd7e8-7ff1-40b8-ad26-6f8e42fbfcd9
    :setup: MMR with two masters,
            M1 - staging DIT
            M2 - production DIT
            add test accounts in staging DIT
    :steps:
        1. Try to modify DN with moddn STAGING_DN -> PRODUCTION_DN
        2. Add the moddn aci that will not be evaluated because of the config flag
        3. Try to do modDN
        4. Remove the moddn aci
        5. Add the 'add' right to the production DN
        6. Try to modify DN with moddn with 'add' right
        7. Enable the moddn right
        8. Try to rename without the appropriate ACI
        9. Add the 'add' right to the production DN
        10. Try to rename without the appropriate ACI
        11. Remove the moddn aci
    :expectedresults:
        1. It should fail due to INSUFFICIENT_ACCESS
        2. It should pass
        3. It should fail due to INSUFFICIENT_ACCESS
        4. It should pass
        5. It should pass
        6. It should pass
        7. It should pass
        8. It should fail due to INSUFFICIENT_ACCESS
        9. It should pass
        10. It should fail due to INSUFFICIENT_ACCESS
        11. It should pass
    """
    topology_m2.ms["master1"].log.info("\n\n######## MOVE staging -> Prod (9) ########\n")

    _bind_normal(topology_m2)
    old_rdn = "uid=%s9" % NEW_ACCOUNT
    old_dn = "%s,%s" % (old_rdn, STAGING_DN)
    new_rdn = old_rdn
    new_superior = PRODUCTION_DN
    prod = OrganizationalRole(topology_m2.ms["master1"], PRODUCTION_DN)

    #
    # Try to rename without the appropriate ACI  => INSUFFICIENT_ACCESS
    #
    try:
        topology_m2.ms["master1"].log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
        topology_m2.ms["master1"].rename_s(old_dn, new_rdn, newsuperior=new_superior)
        assert 0
    except AssertionError:
        topology_m2.ms["master1"].log.info(
            "Exception (not really expected exception but that is fine as it fails to rename)")
    except Exception as e:
        topology_m2.ms["master1"].log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)

    #############
    # Now do tests with no support of moddn aci
    #############
    topology_m2.ms["master1"].log.info("Disable the moddn right")
    _bind_manager(topology_m2)
    topology_m2.ms["master1"].config.set(CONFIG_MODDN_ACI_ATTR, 'off')

    # Add the moddn aci that will not be evaluated because of the config flag
    topology_m2.ms["master1"].log.info("\n\n######## MOVE to and from equality filter ########\n")
    _bind_manager(topology_m2)
    _moddn_aci_staging_to_production(topology_m2, mod_type=ldap.MOD_ADD,
                                     target_from=STAGING_DN, target_to=PRODUCTION_DN)
    _bind_normal(topology_m2)

    # It will fail because it will test the ADD right
    try:
        topology_m2.ms["master1"].log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
        topology_m2.ms["master1"].rename_s(old_dn, new_rdn, newsuperior=new_superior)
        assert 0
    except AssertionError:
        topology_m2.ms["master1"].log.info(
            "Exception (not really expected exception but that is fine as it fails to rename)")
    except Exception as e:
        topology_m2.ms["master1"].log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)

    # remove the moddn aci
    _bind_manager(topology_m2)
    _moddn_aci_staging_to_production(topology_m2, mod_type=ldap.MOD_DELETE,
                                     target_from=STAGING_DN, target_to=PRODUCTION_DN)
    _bind_normal(topology_m2)

    #
    # add the 'add' right to the production DN
    # Then do a successful moddn
    #
    ACI_ALLOW = "(version 3.0; acl \"ADD rights to allow moddn\"; allow (add)"
    ACI_SUBJECT = " userdn = \"ldap:///%s\";)" % BIND_DN
    ACI_BODY = ACI_ALLOW + ACI_SUBJECT

    _bind_manager(topology_m2)
    prod.add('aci', ACI_BODY)
    _write_aci_staging(topology_m2, mod_type=ldap.MOD_ADD)
    _bind_normal(topology_m2)

    topology_m2.ms["master1"].log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
    topology_m2.ms["master1"].rename_s(old_dn, new_rdn, newsuperior=new_superior)

    _bind_manager(topology_m2)
    prod.remove('aci', ACI_BODY)
    _write_aci_staging(topology_m2, mod_type=ldap.MOD_DELETE)
    _bind_normal(topology_m2)

    #############
    # Now do tests with support of moddn aci
    #############
    topology_m2.ms["master1"].log.info("Enable the moddn right")
    _bind_manager(topology_m2)
    topology_m2.ms["master1"].config.set(CONFIG_MODDN_ACI_ATTR, 'on')

    topology_m2.ms["master1"].log.info("\n\n######## MOVE staging -> Prod (10) ########\n")

    _bind_normal(topology_m2)
    old_rdn = "uid=%s10" % NEW_ACCOUNT
    old_dn = "%s,%s" % (old_rdn, STAGING_DN)
    new_rdn = old_rdn
    new_superior = PRODUCTION_DN

    #
    # Try to rename without the appropriate ACI  => INSUFFICIENT_ACCESS
    #
    try:
        topology_m2.ms["master1"].log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
        topology_m2.ms["master1"].rename_s(old_dn, new_rdn, newsuperior=new_superior)
        assert 0
    except AssertionError:
        topology_m2.ms["master1"].log.info(
            "Exception (not really expected exception but that is fine as it fails to rename)")
    except Exception as e:
        topology_m2.ms["master1"].log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)

    #
    # add the 'add' right to the production DN
    # Then do a failing moddn
    #
    ACI_ALLOW = "(version 3.0; acl \"ADD rights to allow moddn\"; allow (add)"
    ACI_SUBJECT = " userdn = \"ldap:///%s\";)" % BIND_DN
    ACI_BODY = ACI_ALLOW + ACI_SUBJECT

    _bind_manager(topology_m2)
    prod.add('aci', ACI_BODY)
    _write_aci_staging(topology_m2, mod_type=ldap.MOD_ADD)
    _bind_normal(topology_m2)

    try:
        topology_m2.ms["master1"].log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
        topology_m2.ms["master1"].rename_s(old_dn, new_rdn, newsuperior=new_superior)
        assert 0
    except AssertionError:
        topology_m2.ms["master1"].log.info(
            "Exception (not really expected exception but that is fine as it fails to rename)")
    except Exception as e:
        topology_m2.ms["master1"].log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)

    _bind_manager(topology_m2)
    prod.remove('aci', ACI_BODY)
    _write_aci_staging(topology_m2, mod_type=ldap.MOD_DELETE)
    _bind_normal(topology_m2)

    # Add the moddn aci that will be evaluated because of the config flag
    topology_m2.ms["master1"].log.info("\n\n######## MOVE to and from equality filter ########\n")
    _bind_manager(topology_m2)
    _moddn_aci_staging_to_production(topology_m2, mod_type=ldap.MOD_ADD,
                                     target_from=STAGING_DN, target_to=PRODUCTION_DN)
    _bind_normal(topology_m2)

    topology_m2.ms["master1"].log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
    topology_m2.ms["master1"].rename_s(old_dn, new_rdn, newsuperior=new_superior)

    # remove the moddn aci
    _bind_manager(topology_m2)
    _moddn_aci_staging_to_production(topology_m2, mod_type=ldap.MOD_DELETE,
                                     target_from=STAGING_DN, target_to=PRODUCTION_DN)
    _bind_normal(topology_m2)


def test_moddn_prod_staging(topology_m2, moddn_setup):
    """This test checks that we can move ACCOUNT11 from staging to prod
       but not move back ACCOUNT11 from prod to staging

    :id: 2b061e92-483f-4399-9f56-8d1c1898b043
    :setup: MMR with two masters,
            M1 - staging DIT
            M2 - production DIT
            add test accounts in staging DIT
    :steps:
        1. Try to rename without the appropriate ACI
        2. Try to MOD with the ACI from stage to production
        3. Try to move back the entry to staging from production
    :expectedresults:
        1. It should fail due to INSUFFICIENT_ACCESS
        2. It should pass
        3. It should fail due to INSUFFICIENT_ACCESS
    """

    topology_m2.ms["master1"].log.info("\n\n######## MOVE staging -> Prod (11) ########\n")

    _bind_normal(topology_m2)

    old_rdn = "uid=%s11" % NEW_ACCOUNT
    old_dn = "%s,%s" % (old_rdn, STAGING_DN)
    new_rdn = old_rdn
    new_superior = PRODUCTION_DN

    #
    # Try to rename without the appropriate ACI  => INSUFFICIENT_ACCESS
    #
    try:
        topology_m2.ms["master1"].log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
        topology_m2.ms["master1"].rename_s(old_dn, new_rdn, newsuperior=new_superior)
        assert 0
    except AssertionError:
        topology_m2.ms["master1"].log.info(
            "Exception (not really expected exception but that is fine as it fails to rename)")
    except Exception as e:
        topology_m2.ms["master1"].log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)

    # successful MOD with the ACI
    topology_m2.ms["master1"].log.info("\n\n######## MOVE to and from equality filter ########\n")
    _bind_manager(topology_m2)
    _moddn_aci_staging_to_production(topology_m2, mod_type=ldap.MOD_ADD,
                                     target_from=STAGING_DN, target_to=PRODUCTION_DN)
    _bind_normal(topology_m2)

    topology_m2.ms["master1"].log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
    topology_m2.ms["master1"].rename_s(old_dn, new_rdn, newsuperior=new_superior)

    # Now check we can not move back the entry to staging
    old_rdn = "uid=%s11" % NEW_ACCOUNT
    old_dn = "%s,%s" % (old_rdn, PRODUCTION_DN)
    new_rdn = old_rdn
    new_superior = STAGING_DN

    # add the write right because we want to check the moddn
    _bind_manager(topology_m2)
    _write_aci_production(topology_m2, mod_type=ldap.MOD_ADD)
    _bind_normal(topology_m2)

    try:
        topology_m2.ms["master1"].log.info("Try to move back MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
        topology_m2.ms["master1"].rename_s(old_dn, new_rdn, newsuperior=new_superior)
        assert 0
    except AssertionError:
        topology_m2.ms["master1"].log.info(
            "Exception (not really expected exception but that is fine as it fails to rename)")
    except Exception as e:
        topology_m2.ms["master1"].log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)

    _bind_manager(topology_m2)
    _write_aci_production(topology_m2, mod_type=ldap.MOD_DELETE)
    _bind_normal(topology_m2)

    # successful MOD with the both ACI
    _bind_manager(topology_m2)
    _moddn_aci_staging_to_production(topology_m2, mod_type=ldap.MOD_DELETE,
                                     target_from=STAGING_DN, target_to=PRODUCTION_DN)
    _bind_normal(topology_m2)


def test_check_repl_M2_to_M1(topology_m2, moddn_setup):
    """Checks that replication is still working M2->M1, using ACCOUNT12

    :id: 08ac131d-34b7-443f-aacd-23025bbd7de1
    :setup: MMR with two masters,
            M1 - staging DIT
            M2 - production DIT
            add test accounts in staging DIT
    :steps:
        1. Add an entry in M2
        2. Search entry on M1
    :expectedresults:
        1. It should pass
        2. It should pass
    """

    topology_m2.ms["master1"].log.info("Bind as %s (M2)" % DN_DM)
    topology_m2.ms["master2"].simple_bind_s(DN_DM, PASSWORD)

    rdn = "uid=%s12" % NEW_ACCOUNT
    dn = "%s,%s" % (rdn, STAGING_DN)
    new_account = UserAccount(topology_m2.ms["master2"], dn)

    # First wait for the ACCOUNT19 entry being replicated on M2
    loop = 0
    while loop <= 10:
        try:
            ent = topology_m2.ms["master2"].getEntry(dn, ldap.SCOPE_BASE, "(objectclass=*)")
            break
        except ldap.NO_SUCH_OBJECT:
            time.sleep(1)
            loop += 1
    assert loop <= 10

    attribute = 'description'
    tested_value = b'Hello world'
    topology_m2.ms["master1"].log.info("Update (M2) %s (%s)" % (dn, attribute))
    new_account.add(attribute, tested_value)

    loop = 0
    while loop <= 10:
        ent = topology_m2.ms["master1"].getEntry(dn, ldap.SCOPE_BASE, "(objectclass=*)")
        assert ent is not None
        if ent.hasAttr(attribute) and (ent.getValue(attribute) == tested_value):
            break

        time.sleep(1)
        loop += 1
    assert loop < 10
    topology_m2.ms["master1"].log.info("Update %s (%s) replicated on M1" % (dn, attribute))


def test_moddn_staging_prod_except(topology_m2, moddn_setup):
    """This test case MOVE entry NEW_ACCOUNT13 from staging to prod
       but fails to move entry NEW_ACCOUNT14 from staging to prod_except

    :id: 02d34f4c-8574-428d-b43f-31227426392c
    :setup: MMR with two masters,
            M1 - staging DIT
            M2 - production DIT
            add test accounts in staging DIT
    :steps:
        1. Try to move entry staging -> Prod
           without the appropriate ACI
        2. Do MOD with the appropriate ACI
        3. Try to move an entry under Prod/Except from stage
        4. Try to do MOD with appropriate ACI
    :expectedresults:
        1. It should fail due to INSUFFICIENT_ACCESS
        2. It should pass
        3. It should fail due to INSUFFICIENT_ACCESS
        4. It should pass
    """

    topology_m2.ms["master1"].log.info("\n\n######## MOVE staging -> Prod (13) ########\n")
    _bind_normal(topology_m2)

    old_rdn = "uid=%s13" % NEW_ACCOUNT
    old_dn = "%s,%s" % (old_rdn, STAGING_DN)
    new_rdn = old_rdn
    new_superior = PRODUCTION_DN

    #
    # Try to rename without the appropriate ACI  => INSUFFICIENT_ACCESS
    #
    try:
        topology_m2.ms["master1"].log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
        topology_m2.ms["master1"].rename_s(old_dn, new_rdn, newsuperior=new_superior)
        assert 0
    except AssertionError:
        topology_m2.ms["master1"].log.info(
            "Exception (not really expected exception but that is fine as it fails to rename)")
    except Exception as e:
        topology_m2.ms["master1"].log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)

    # successful MOD with the ACI
    topology_m2.ms["master1"].log.info("\n\n######## MOVE to and from equality filter ########\n")
    _bind_manager(topology_m2)
    _moddn_aci_staging_to_production(topology_m2, mod_type=ldap.MOD_ADD,
                                     target_from=STAGING_DN, target_to=PRODUCTION_DN)
    _moddn_aci_deny_tree(topology_m2, mod_type=ldap.MOD_ADD)
    _bind_normal(topology_m2)

    topology_m2.ms["master1"].log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
    topology_m2.ms["master1"].rename_s(old_dn, new_rdn, newsuperior=new_superior)

    #
    # Now try to move an entry under except
    #
    topology_m2.ms["master1"].log.info("\n\n######## MOVE staging -> Prod/Except (14) ########\n")
    old_rdn = "uid=%s14" % NEW_ACCOUNT
    old_dn = "%s,%s" % (old_rdn, STAGING_DN)
    new_rdn = old_rdn
    new_superior = PROD_EXCEPT_DN
    try:
        topology_m2.ms["master1"].log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
        topology_m2.ms["master1"].rename_s(old_dn, new_rdn, newsuperior=new_superior)
        assert 0
    except AssertionError:
        topology_m2.ms["master1"].log.info(
            "Exception (not really expected exception but that is fine as it fails to rename)")
    except Exception as e:
        topology_m2.ms["master1"].log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)

    # successful MOD with the both ACI
    _bind_manager(topology_m2)
    _moddn_aci_staging_to_production(topology_m2, mod_type=ldap.MOD_DELETE,
                                     target_from=STAGING_DN, target_to=PRODUCTION_DN)
    _moddn_aci_deny_tree(topology_m2, mod_type=ldap.MOD_DELETE)
    _bind_normal(topology_m2)


def test_mode_default_ger_no_moddn(topology_m2, moddn_setup):
    """mode moddn_aci : Check Get Effective Rights Controls for entries

    :id: f4785d73-3b14-49c0-b981-d6ff96fa3496
    :setup: MMR with two masters,
            M1 - staging DIT
            M2 - production DIT
            add test accounts in staging DIT
    :steps:
        1. Search for GER controls on M1
        2. Check 'n' is not in the entryLevelRights
    :expectedresults:
        1. It should pass
        2. It should pass
    """

    topology_m2.ms["master1"].log.info("\n\n######## mode moddn_aci : GER no moddn  ########\n")
    request_ctrl = GetEffectiveRightsControl(criticality=True,
                                             authzId=ensure_bytes("dn: " + BIND_DN))
    msg_id = topology_m2.ms["master1"].search_ext(PRODUCTION_DN,
                                                  ldap.SCOPE_SUBTREE,
                                                  "objectclass=*",
                                                  serverctrls=[request_ctrl])
    rtype, rdata, rmsgid, response_ctrl = topology_m2.ms["master1"].result3(msg_id)
    # ger={}
    value = ''
    for dn, attrs in rdata:
        topology_m2.ms["master1"].log.info("dn: %s" % dn)
        value = attrs['entryLevelRights'][0]

    topology_m2.ms["master1"].log.info("########  entryLevelRights: %r" % value)
    assert b'n' not in value


def test_mode_default_ger_with_moddn(topology_m2, moddn_setup):
    """This test case adds the moddn aci and check ger contains 'n'

    :id: a752a461-432d-483a-89c0-dfb34045a969
    :setup: MMR with two masters,
            M1 - staging DIT
            M2 - production DIT
            add test accounts in staging DIT
    :steps:
        1. Add moddn ACI on M2
        2. Search for GER controls on M1
        3. Check entryLevelRights value for entries
        4. Check 'n' is in the entryLevelRights
    :expectedresults:
        1. It should pass
        2. It should pass
        3. It should pass
        4. It should pass
    """

    topology_m2.ms["master1"].log.info("\n\n######## mode moddn_aci: GER with moddn ########\n")

    # successful MOD with the ACI
    _bind_manager(topology_m2)
    _moddn_aci_staging_to_production(topology_m2, mod_type=ldap.MOD_ADD,
                                     target_from=STAGING_DN, target_to=PRODUCTION_DN)
    _bind_normal(topology_m2)

    request_ctrl = GetEffectiveRightsControl(criticality=True,
                                             authzId=ensure_bytes("dn: " + BIND_DN))
    msg_id = topology_m2.ms["master1"].search_ext(PRODUCTION_DN,
                                                  ldap.SCOPE_SUBTREE,
                                                  "objectclass=*",
                                                  serverctrls=[request_ctrl])
    rtype, rdata, rmsgid, response_ctrl = topology_m2.ms["master1"].result3(msg_id)
    # ger={}
    value = ''
    for dn, attrs in rdata:
        topology_m2.ms["master1"].log.info("dn: %s" % dn)
        value = attrs['entryLevelRights'][0]

    topology_m2.ms["master1"].log.info("########  entryLevelRights: %r" % value)
    assert b'n' in value

    # successful MOD with the both ACI
    _bind_manager(topology_m2)
    _moddn_aci_staging_to_production(topology_m2, mod_type=ldap.MOD_DELETE,
                                     target_from=STAGING_DN, target_to=PRODUCTION_DN)
    _bind_normal(topology_m2)


def test_mode_legacy_ger_no_moddn1(topology_m2, moddn_setup):
    """This test checks mode legacy : GER no moddn

    :id: e783e05b-d0d0-4fd4-9572-258a81b7bd24
    :setup: MMR with two masters,
            M1 - staging DIT
            M2 - production DIT
            add test accounts in staging DIT
    :steps:
        1. Disable ACI checks - set nsslapd-moddn-aci: off
        2. Search for GER controls on M1
        3. Check entryLevelRights value for entries
        4. Check 'n' is not in the entryLevelRights
    :expectedresults:
        1. It should pass
        2. It should pass
        3. It should pass
        4. It should pass
    """

    topology_m2.ms["master1"].log.info("\n\n######## Disable the moddn aci mod ########\n")
    _bind_manager(topology_m2)
    topology_m2.ms["master1"].config.set(CONFIG_MODDN_ACI_ATTR, 'off')

    topology_m2.ms["master1"].log.info("\n\n######## mode legacy 1: GER no moddn  ########\n")
    request_ctrl = GetEffectiveRightsControl(criticality=True, authzId=ensure_bytes("dn: " + BIND_DN))
    msg_id = topology_m2.ms["master1"].search_ext(PRODUCTION_DN,
                                                  ldap.SCOPE_SUBTREE,
                                                  "objectclass=*",
                                                  serverctrls=[request_ctrl])
    rtype, rdata, rmsgid, response_ctrl = topology_m2.ms["master1"].result3(msg_id)
    # ger={}
    value = ''
    for dn, attrs in rdata:
        topology_m2.ms["master1"].log.info("dn: %s" % dn)
        value = attrs['entryLevelRights'][0]

    topology_m2.ms["master1"].log.info("########  entryLevelRights: %r" % value)
    assert b'n' not in value


def test_mode_legacy_ger_no_moddn2(topology_m2, moddn_setup):
    """This test checks mode legacy : GER no moddn

    :id: af87e024-1744-4f1d-a2d3-ea2687e2351d
    :setup: MMR with two masters,
            M1 - staging DIT
            M2 - production DIT
            add test accounts in staging DIT
    :steps:
        1. Disable ACI checks - set nsslapd-moddn-aci: off
        2. Add moddn ACI on M1
        3. Search for GER controls on M1
        4. Check entryLevelRights value for entries
        5. Check 'n' is not in the entryLevelRights
    :expectedresults:
        1. It should pass
        2. It should pass
        3. It should pass
        4. It should be pass
        5. It should pass
    """

    topology_m2.ms["master1"].log.info("\n\n######## Disable the moddn aci mod ########\n")
    _bind_manager(topology_m2)
    topology_m2.ms["master1"].config.set(CONFIG_MODDN_ACI_ATTR, 'off')

    topology_m2.ms["master1"].log.info("\n\n######## mode legacy 2: GER no moddn  ########\n")
    # successful MOD with the ACI
    _bind_manager(topology_m2)
    _moddn_aci_staging_to_production(topology_m2, mod_type=ldap.MOD_ADD,
                                     target_from=STAGING_DN, target_to=PRODUCTION_DN)
    _bind_normal(topology_m2)

    request_ctrl = GetEffectiveRightsControl(criticality=True,
                                             authzId=ensure_bytes("dn: " + BIND_DN))
    msg_id = topology_m2.ms["master1"].search_ext(PRODUCTION_DN,
                                                  ldap.SCOPE_SUBTREE,
                                                  "objectclass=*",
                                                  serverctrls=[request_ctrl])
    rtype, rdata, rmsgid, response_ctrl = topology_m2.ms["master1"].result3(msg_id)
    # ger={}
    value = ''
    for dn, attrs in rdata:
        topology_m2.ms["master1"].log.info("dn: %s" % dn)
        value = attrs['entryLevelRights'][0]

    topology_m2.ms["master1"].log.info("########  entryLevelRights: %r" % value)
    assert b'n' not in value

    # successful MOD with the both ACI
    _bind_manager(topology_m2)
    _moddn_aci_staging_to_production(topology_m2, mod_type=ldap.MOD_DELETE,
                                     target_from=STAGING_DN, target_to=PRODUCTION_DN)
    _bind_normal(topology_m2)


def test_mode_legacy_ger_with_moddn(topology_m2, moddn_setup):
    """This test checks mode legacy : GER with moddn

    :id: 37c1e537-1b5d-4fab-b62a-50cd8c5b3493
    :setup: MMR with two masters,
            M1 - staging DIT
            M2 - production DIT
            add test accounts in staging DIT
    :steps:
        1. Disable ACI checks - set nsslapd-moddn-aci: off
        2. Add moddn ACI on M1
        3. Search for GER controls on M1
        4. Check entryLevelRights value for entries
        5. Check 'n' is in the entryLevelRights
        6. Try MOD with the both ACI
    :expectedresults:
        1. It should pass
        2. It should pass
        3. It should pass
        4. It should pass
        5. It should pass
        6. It should pass
    """

    suffix = Domain(topology_m2.ms["master1"], SUFFIX)

    topology_m2.ms["master1"].log.info("\n\n######## Disable the moddn aci mod ########\n")
    _bind_manager(topology_m2)
    topology_m2.ms["master1"].config.set(CONFIG_MODDN_ACI_ATTR, 'off')

    topology_m2.ms["master1"].log.info("\n\n######## mode legacy : GER with moddn  ########\n")

    # being allowed to read/write the RDN attribute use to allow the RDN
    ACI_TARGET = "(target = \"ldap:///%s\")(targetattr=\"uid\")" % (PRODUCTION_DN)
    ACI_ALLOW = "(version 3.0; acl \"MODDN production changing the RDN attribute\"; allow (read,search,write)"
    ACI_SUBJECT = " userdn = \"ldap:///%s\";)" % BIND_DN
    ACI_BODY = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT

    # successful MOD with the ACI
    _bind_manager(topology_m2)
    suffix.add('aci', ACI_BODY)
    _bind_normal(topology_m2)

    request_ctrl = GetEffectiveRightsControl(criticality=True, authzId=ensure_bytes("dn: " + BIND_DN))
    msg_id = topology_m2.ms["master1"].search_ext(PRODUCTION_DN,
                                                  ldap.SCOPE_SUBTREE,
                                                  "objectclass=*",
                                                  serverctrls=[request_ctrl])
    rtype, rdata, rmsgid, response_ctrl = topology_m2.ms["master1"].result3(msg_id)
    # ger={}
    value = ''
    for dn, attrs in rdata:
        topology_m2.ms["master1"].log.info("dn: %s" % dn)
        value = attrs['entryLevelRights'][0]

    topology_m2.ms["master1"].log.info("########  entryLevelRights: %r" % value)
    assert b'n' in value

    # successful MOD with the both ACI
    _bind_manager(topology_m2)
    suffix.remove('aci', ACI_BODY)
    # _bind_normal(topology_m2)


@pytest.fixture(scope="module")
def rdn_write_setup(topology_m2):
    topology_m2.ms["master1"].log.info("\n\n######## Add entry tuser ########\n")
    topology_m2.ms["master1"].add_s(Entry((SRC_ENTRY_DN, {
        'objectclass': "top person".split(),
        'sn': SRC_ENTRY_CN,
        'cn': SRC_ENTRY_CN})))


def test_rdn_write_get_ger(topology_m2, rdn_write_setup):
    """This test checks GER rights for anonymous

    :id: d5d85f87-b53d-4f50-8fa6-a9e55c75419b
    :setup: MMR with two masters,
            Add entry tuser
    :steps:
        1. Search for GER controls on M1
        2. Check entryLevelRights value for entries
        3. Check 'n' is not in the entryLevelRights
    :expectedresults:
        1. It should pass
        2. It should be pass
        3. It should pass
    """

    ANONYMOUS_DN = ""
    topology_m2.ms["master1"].log.info("\n\n######## GER rights for anonymous ########\n")
    request_ctrl = GetEffectiveRightsControl(criticality=True,
                                             authzId=ensure_bytes("dn:" + ANONYMOUS_DN))
    msg_id = topology_m2.ms["master1"].search_ext(SUFFIX,
                                                  ldap.SCOPE_SUBTREE,
                                                  "objectclass=*",
                                                  serverctrls=[request_ctrl])
    rtype, rdata, rmsgid, response_ctrl = topology_m2.ms["master1"].result3(msg_id)
    value = ''
    for dn, attrs in rdata:
        topology_m2.ms["master1"].log.info("dn: %s" % dn)
        for value in attrs['entryLevelRights']:
            topology_m2.ms["master1"].log.info("########  entryLevelRights: %r" % value)
            assert b'n' not in value


def test_rdn_write_modrdn_anonymous(topology_m2, rdn_write_setup):
    """Tests anonymous user for modrdn

    :id: fc07be23-3341-44ab-a53c-c68c5f9569c7
    :setup: MMR with two masters,
            Add entry tuser
    :steps:
        1. Bind as anonymous user
        2. Try to perform MODRDN operation (SRC_ENTRY_DN -> DST_ENTRY_CN)
        3. Try to search DST_ENTRY_CN
    :expectedresults:
        1. It should pass
        2. It should fails with INSUFFICIENT_ACCESS
        3. It should fails with NO_SUCH_OBJECT
    """

    ANONYMOUS_DN = ""
    topology_m2.ms["master1"].close()
    topology_m2.ms["master1"].binddn = ANONYMOUS_DN
    topology_m2.ms["master1"].open()
    msg_id = topology_m2.ms["master1"].search_ext("", ldap.SCOPE_BASE, "objectclass=*")
    rtype, rdata, rmsgid, response_ctrl = topology_m2.ms["master1"].result3(msg_id)
    for dn, attrs in rdata:
        topology_m2.ms["master1"].log.info("dn: %s" % dn)
        for attr in attrs:
            topology_m2.ms["master1"].log.info("########  %r: %r" % (attr, attrs[attr]))

    try:
        topology_m2.ms["master1"].rename_s(SRC_ENTRY_DN, "cn=%s" % DST_ENTRY_CN, delold=True)
    except Exception as e:
        topology_m2.ms["master1"].log.info("Exception (expected): %s" % type(e).__name__)
        isinstance(e, ldap.INSUFFICIENT_ACCESS)

    try:
        topology_m2.ms["master1"].getEntry(DST_ENTRY_DN, ldap.SCOPE_BASE, "objectclass=*")
        assert False
    except Exception as e:
        topology_m2.ms["master1"].log.info("The entry was not renamed (expected)")
        isinstance(e, ldap.NO_SUCH_OBJECT)

    _bind_manager(topology_m2)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
