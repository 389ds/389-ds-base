import os
import sys
import time
import ldap
import logging
import pytest
from lib389 import DirSrv, Entry, tools, tasks
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from lib389.utils import *
from ldap.controls.simple import GetEffectiveRightsControl

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

#
# important part. We can deploy Master1 and Master2 on different versions
#
installation1_prefix = None
installation2_prefix = None

TEST_REPL_DN = "cn=test_repl, %s" % SUFFIX

STAGING_CN     = "staged user"
PRODUCTION_CN  = "accounts"
EXCEPT_CN      = "excepts"

STAGING_DN    = "cn=%s,%s" % (STAGING_CN, SUFFIX)
PRODUCTION_DN = "cn=%s,%s" % (PRODUCTION_CN, SUFFIX)
PROD_EXCEPT_DN = "cn=%s,%s" % (EXCEPT_CN, PRODUCTION_DN)

STAGING_PATTERN    = "cn=%s*,%s" % (STAGING_CN[:2],    SUFFIX)
PRODUCTION_PATTERN = "cn=%s*,%s" % (PRODUCTION_CN[:2], SUFFIX)
BAD_STAGING_PATTERN    = "cn=bad*,%s" % (SUFFIX)
BAD_PRODUCTION_PATTERN = "cn=bad*,%s" % (SUFFIX)

BIND_CN        = "bind_entry"
BIND_DN        = "cn=%s,%s" % (BIND_CN, SUFFIX)
BIND_PW        = "password"

NEW_ACCOUNT    = "new_account"
MAX_ACCOUNTS   = 20

CONFIG_MODDN_ACI_ATTR = "nsslapd-moddn-aci"

SRC_ENTRY_CN = "tuser"
EXT_RDN = "01"
DST_ENTRY_CN = SRC_ENTRY_CN + EXT_RDN

SRC_ENTRY_DN = "cn=%s,%s" % (SRC_ENTRY_CN, SUFFIX)
DST_ENTRY_DN = "cn=%s,%s" % (DST_ENTRY_CN, SUFFIX)


class TopologyMaster1Master2(object):
    def __init__(self, master1, master2):
        master1.open()
        self.master1 = master1

        master2.open()
        self.master2 = master2


@pytest.fixture(scope="module")
def topology(request):
    """This fixture is used to create a replicated topology for the 'module'.
    The replicated topology is MASTER1 <-> Master2.
    """

    global installation1_prefix
    global installation2_prefix

    # allocate master1 on a given deployement
    master1 = DirSrv(verbose=False)
    if installation1_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation1_prefix

    # Args for the master1 instance
    args_instance[SER_HOST] = HOST_MASTER_1
    args_instance[SER_PORT] = PORT_MASTER_1
    args_instance[SER_SERVERID_PROP] = SERVERID_MASTER_1
    args_master = args_instance.copy()
    master1.allocate(args_master)

    # allocate master1 on a given deployement
    master2 = DirSrv(verbose=False)
    if installation2_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation2_prefix

    # Args for the consumer instance
    args_instance[SER_HOST] = HOST_MASTER_2
    args_instance[SER_PORT] = PORT_MASTER_2
    args_instance[SER_SERVERID_PROP] = SERVERID_MASTER_2
    args_master = args_instance.copy()
    master2.allocate(args_master)

    # Get the status of the instance and restart it if it exists
    instance_master1 = master1.exists()
    instance_master2 = master2.exists()

    # Remove all the instances
    if instance_master1:
        master1.delete()
    if instance_master2:
        master2.delete()

    # Create the instances
    master1.create()
    master1.open()
    master2.create()
    master2.open()

    #
    # Now prepare the Master-Consumer topology
    #
    # First Enable replication
    master1.replica.enableReplication(suffix=SUFFIX,
                                      role=REPLICAROLE_MASTER,
                                      replicaId=REPLICAID_MASTER_1)
    master2.replica.enableReplication(suffix=SUFFIX,
                                      role=REPLICAROLE_MASTER,
                                      replicaId=REPLICAID_MASTER_2)

    # Initialize the supplier->consumer

    properties = {RA_NAME:      r'meTo_$host:$port',
                  RA_BINDDN:    defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW:    defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD:    defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    repl_agreement = master1.agreement.create(suffix=SUFFIX,
                                              host=master2.host,
                                              port=master2.port,
                                              properties=properties)

    if not repl_agreement:
        log.fatal("Fail to create a replica agreement")
        sys.exit(1)

    log.debug("%s created" % repl_agreement)

    properties = {RA_NAME:      r'meTo_$host:$port',
                  RA_BINDDN:    defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW:    defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD:    defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}
    master2.agreement.create(suffix=SUFFIX,
                             host=master1.host,
                             port=master1.port,
                             properties=properties)

    master1.agreement.init(SUFFIX, HOST_MASTER_2, PORT_MASTER_2)
    master1.waitForReplInit(repl_agreement)

    # Check replication is working fine
    if master1.testReplication(DEFAULT_SUFFIX, master2):
        log.info('Replication is working.')
    else:
        log.fatal('Replication is not working.')
        assert False

    def fin():
        master1.delete()
        master2.delete()
    request.addfinalizer(fin)

    # clear the tmp directory
    master1.clearTmpDir(__file__)

    # Here we have two instances master and consumer
    # with replication working.
    return TopologyMaster1Master2(master1, master2)


def add_attr(topology, attr_name):
    """Adds attribute to the schema"""

    ATTR_VALUE = """(NAME '%s' \
                    DESC 'Attribute filteri-Multi-Valued' \
                    SYNTAX 1.3.6.1.4.1.1466.115.121.1.27)""" % attr_name
    mod = [(ldap.MOD_ADD, 'attributeTypes', ATTR_VALUE)]

    try:
        topology.standalone.modify_s(DN_SCHEMA, mod)
    except ldap.LDAPError, e:
        log.fatal('Failed to add attr (%s): error (%s)' % (attr_name,
                                                           e.message['desc']))
        assert False


@pytest.fixture(params=["lang-ja", "binary", "phonetic"])
def aci_with_attr_subtype(request, topology):
    """Adds and deletes an ACI in the DEFAULT_SUFFIX"""

    TARGET_ATTR = 'protectedOperation'
    USER_ATTR = 'allowedToPerform'
    SUBTYPE = request.param

    log.info("========Executing test with '%s' subtype========" % SUBTYPE)
    log.info("        Add a target attribute")
    add_attr(topology, TARGET_ATTR)

    log.info("        Add a user attribute")
    add_attr(topology, USER_ATTR)

    ACI_TARGET = '(targetattr=%s;%s)' % (TARGET_ATTR, SUBTYPE)
    ACI_ALLOW = '(version 3.0; acl "test aci for subtypes"; allow (read) '
    ACI_SUBJECT = 'userattr = "%s;%s#GROUPDN";)' % (USER_ATTR, SUBTYPE)
    ACI_BODY = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT

    log.info("        Add an ACI with attribute subtype")
    mod = [(ldap.MOD_ADD, 'aci', ACI_BODY)]
    try:
        topology.standalone.modify_s(DEFAULT_SUFFIX, mod)
    except ldap.LDAPError, e:
        log.fatal('Failed to add ACI: error (%s)' % (e.message['desc']))
        assert False

    def fin():
        log.info("        Finally, delete an ACI with the '%s' subtype" %
                                                       SUBTYPE)
        mod = [(ldap.MOD_DELETE, 'aci', ACI_BODY)]
        try:
            topology.standalone.modify_s(DEFAULT_SUFFIX, mod)
        except ldap.LDAPError, e:
            log.fatal('Failed to delete ACI: error (%s)' % (e.message['desc']))
            assert False
    request.addfinalizer(fin)

    return ACI_BODY


def test_aci_attr_subtype_targetattr(topology, aci_with_attr_subtype):
    """Checks, that ACIs allow attribute subtypes in the targetattr keyword

    Test description:
    1. Define two attributes in the schema
        - first will be a targetattr
        - second will be a userattr
    2. Add an ACI with an attribute subtype
        - or language subtype
        - or binary subtype
        - or pronunciation subtype
    """

    log.info("        Search for the added attribute")
    try:
        entries = topology.master1.search_s(DEFAULT_SUFFIX,
                                               ldap.SCOPE_BASE,
                                               '(objectclass=*)', ['aci'])
        entry = str(entries[0])
        assert aci_with_attr_subtype in entry
        log.info("        The added attribute was found")

    except ldap.LDAPError, e:
        log.fatal('Search failed, error: ' + e.message['desc'])
        assert False


def _bind_manager(topology):
    topology.master1.log.info("Bind as %s " % DN_DM)
    topology.master1.simple_bind_s(DN_DM, PASSWORD)


def _bind_normal(topology):
    # bind as bind_entry
    topology.master1.log.info("Bind as %s" % BIND_DN)
    topology.master1.simple_bind_s(BIND_DN, BIND_PW)


def _moddn_aci_deny_tree(topology, mod_type=None,
                         target_from=STAGING_DN, target_to=PROD_EXCEPT_DN):
    """It denies the access moddn_to in cn=except,cn=accounts,SUFFIX"""

    assert mod_type is not None

    ACI_TARGET_FROM = ""
    ACI_TARGET_TO   = ""
    if target_from:
        ACI_TARGET_FROM = "(target_from = \"ldap:///%s\")" % (target_from)
    if target_to:
        ACI_TARGET_TO   = "(target_to   = \"ldap:///%s\")" % (target_to)

    ACI_ALLOW        = "(version 3.0; acl \"Deny MODDN to prod_except\"; deny (moddn)"
    ACI_SUBJECT      = " userdn = \"ldap:///%s\";)" % BIND_DN
    ACI_BODY         = ACI_TARGET_TO + ACI_TARGET_FROM + ACI_ALLOW + ACI_SUBJECT
    mod = [(mod_type, 'aci', ACI_BODY)]
    #topology.master1.modify_s(SUFFIX, mod)
    topology.master1.log.info("Add a DENY aci under %s " % PROD_EXCEPT_DN)
    topology.master1.modify_s(PROD_EXCEPT_DN, mod)


def _write_aci_staging(topology, mod_type=None):
    assert mod_type is not None

    ACI_TARGET = "(targetattr= \"cn\")(target=\"ldap:///cn=*,%s\")" % STAGING_DN
    ACI_ALLOW        = "(version 3.0; acl \"write staging entries\"; allow (write)"
    ACI_SUBJECT      = " userdn = \"ldap:///%s\";)" % BIND_DN
    ACI_BODY         = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT
    mod = [(mod_type, 'aci', ACI_BODY)]
    topology.master1.modify_s(SUFFIX, mod)


def _write_aci_production(topology, mod_type=None):
    assert mod_type is not None

    ACI_TARGET = "(targetattr= \"cn\")(target=\"ldap:///cn=*,%s\")" % PRODUCTION_DN
    ACI_ALLOW        = "(version 3.0; acl \"write production entries\"; allow (write)"
    ACI_SUBJECT      = " userdn = \"ldap:///%s\";)" % BIND_DN
    ACI_BODY         = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT
    mod = [(mod_type, 'aci', ACI_BODY)]
    topology.master1.modify_s(SUFFIX, mod)


def _moddn_aci_staging_to_production(topology, mod_type=None,
                                     target_from=STAGING_DN, target_to=PRODUCTION_DN):
    assert mod_type is not None


    ACI_TARGET_FROM = ""
    ACI_TARGET_TO   = ""
    if target_from:
        ACI_TARGET_FROM = "(target_from = \"ldap:///%s\")" % (target_from)
    if target_to:
        ACI_TARGET_TO   = "(target_to   = \"ldap:///%s\")" % (target_to)

    ACI_ALLOW        = "(version 3.0; acl \"MODDN from staging to production\"; allow (moddn)"
    ACI_SUBJECT      = " userdn = \"ldap:///%s\";)" % BIND_DN
    ACI_BODY         = ACI_TARGET_FROM + ACI_TARGET_TO + ACI_ALLOW + ACI_SUBJECT
    mod = [(mod_type, 'aci', ACI_BODY)]
    topology.master1.modify_s(SUFFIX, mod)

    _write_aci_staging(topology, mod_type=mod_type)


def _moddn_aci_from_production_to_staging(topology, mod_type=None):
    assert mod_type is not None

    ACI_TARGET       = "(target_from = \"ldap:///%s\") (target_to = \"ldap:///%s\")" % (
                        PRODUCTION_DN, STAGING_DN)
    ACI_ALLOW        = "(version 3.0; acl \"MODDN from production to staging\"; allow (moddn)"
    ACI_SUBJECT      = " userdn = \"ldap:///%s\";)" % BIND_DN
    ACI_BODY         = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT
    mod = [(mod_type, 'aci', ACI_BODY)]
    topology.master1.modify_s(SUFFIX, mod)

    _write_aci_production(topology, mod_type=mod_type)


@pytest.fixture(scope="module")
def moddn_setup(topology):
    """Creates
       - a staging DIT
       - a production DIT
       - add accounts in staging DIT
       - enable ACL logging (commented for performance reason)
    """

    topology.master1.log.info("\n\n######## INITIALIZATION ########\n")

    # entry used to bind with
    topology.master1.log.info("Add %s" % BIND_DN)
    topology.master1.add_s(Entry((BIND_DN, {
                                            'objectclass': "top person".split(),
                                            'sn':           BIND_CN,
                                            'cn':           BIND_CN,
                                            'userpassword': BIND_PW})))

    # DIT for staging
    topology.master1.log.info("Add %s" % STAGING_DN)
    topology.master1.add_s(Entry((STAGING_DN, {
                                            'objectclass': "top organizationalRole".split(),
                                            'cn':           STAGING_CN,
                                            'description': "staging DIT"})))

    # DIT for production
    topology.master1.log.info("Add %s" % PRODUCTION_DN)
    topology.master1.add_s(Entry((PRODUCTION_DN, {
                                            'objectclass': "top organizationalRole".split(),
                                            'cn':           PRODUCTION_CN,
                                            'description': "production DIT"})))

    # DIT for production/except
    topology.master1.log.info("Add %s" % PROD_EXCEPT_DN)
    topology.master1.add_s(Entry((PROD_EXCEPT_DN, {
                                            'objectclass': "top organizationalRole".split(),
                                            'cn':           EXCEPT_CN,
                                            'description': "production except DIT"})))

    # enable acl error logging
    #mod = [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', '128')]
    #topology.master1.modify_s(DN_CONFIG, mod)
    #topology.master2.modify_s(DN_CONFIG, mod)

    # add dummy entries in the staging DIT
    for cpt in range(MAX_ACCOUNTS):
        name = "%s%d" % (NEW_ACCOUNT, cpt)
        topology.master1.add_s(Entry(("cn=%s,%s" % (name, STAGING_DN), {
                                            'objectclass': "top person".split(),
                                            'sn': name,
                                            'cn': name})))


def test_mode_default_add_deny(topology, moddn_setup):
    """This test case checks
    that the ADD operation fails (no ADD aci on production)
    """

    topology.master1.log.info("\n\n######## mode moddn_aci : ADD (should fail) ########\n")

    _bind_normal(topology)

    #
    # First try to add an entry in production => INSUFFICIENT_ACCESS
    #
    try:
        topology.master1.log.info("Try to add %s" % PRODUCTION_DN)
        name = "%s%d" % (NEW_ACCOUNT, 0)
        topology.master1.add_s(Entry(("cn=%s,%s" % (name, PRODUCTION_DN), {
                                                'objectclass': "top person".split(),
                                                'sn': name,
                                                'cn': name})))
        assert 0  # this is an error, we should not be allowed to add an entry in production
    except Exception as e:
        topology.master1.log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)


def test_mode_default_delete_deny(topology, moddn_setup):
    """This test case checks
    that the DEL operation fails (no 'delete' aci on production)
    """

    topology.master1.log.info("\n\n######## DELETE (should fail) ########\n")

    _bind_normal(topology)
    #
    # Second try to delete an entry in staging => INSUFFICIENT_ACCESS
    #
    try:
        topology.master1.log.info("Try to delete %s" % STAGING_DN)
        name = "%s%d" % (NEW_ACCOUNT, 0)
        topology.master1.delete_s("cn=%s,%s" % (name, STAGING_DN))
        assert 0  # this is an error, we should not be allowed to add an entry in production
    except Exception as e:
        topology.master1.log.info("Exception (expected): %s" % type(e).__name__)
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
def test_moddn_staging_prod(topology, moddn_setup,
                            index, tfrom, tto, failure):
    """This test case MOVE entry NEW_ACCOUNT0 from staging to prod
    target_to/target_from: equality filter
    """

    topology.master1.log.info("\n\n######## MOVE staging -> Prod (%s) ########\n" % index)
    _bind_normal(topology)

    old_rdn = "cn=%s%s" % (NEW_ACCOUNT, index)
    old_dn  = "%s,%s" % (old_rdn, STAGING_DN)
    new_rdn = old_rdn
    new_superior = PRODUCTION_DN

    #
    # Try to rename without the apropriate ACI  => INSUFFICIENT_ACCESS
    #
    try:
        topology.master1.log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
        topology.master1.rename_s(old_dn, new_rdn, newsuperior=new_superior)
        assert 0
    except AssertionError:
        topology.master1.log.info("Exception (not really expected exception but that is fine as it fails to rename)")
    except Exception as e:
        topology.master1.log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)


    # successfull MOD with the ACI
    topology.master1.log.info("\n\n######## MOVE to and from equality filter ########\n")
    _bind_manager(topology)
    _moddn_aci_staging_to_production(topology, mod_type=ldap.MOD_ADD,
                                     target_from=tfrom, target_to=tto)
    _bind_normal(topology)

    try:
        topology.master1.log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
        topology.master1.rename_s(old_dn, new_rdn, newsuperior=new_superior)
    except Exception as e:
        topology.master1.log.info("Exception (expected): %s" % type(e).__name__)
        if failure:
            assert isinstance(e, ldap.INSUFFICIENT_ACCESS)

    # successfull MOD with the both ACI
    _bind_manager(topology)
    _moddn_aci_staging_to_production(topology, mod_type=ldap.MOD_DELETE,
                                     target_from=tfrom, target_to=tto)
    _bind_normal(topology)


def test_moddn_staging_prod_9(topology, moddn_setup):
    """This test case disable the 'moddn' right so a MODDN requires a 'add' right
    to be successfull.
    It fails to MOVE entry NEW_ACCOUNT9 from staging to prod.
    Add a 'add' right to prod.
    Then it succeeds to MOVE NEW_ACCOUNT9 from staging to prod.

    Then enable the 'moddn' right so a MODDN requires a 'moddn' right
    It fails to MOVE entry NEW_ACCOUNT10 from staging to prod.
    Add a 'moddn' right to prod.
    Then it succeeds to MOVE NEW_ACCOUNT10 from staging to prod.
    """

    topology.master1.log.info("\n\n######## MOVE staging -> Prod (9) ########\n")

    _bind_normal(topology)
    old_rdn = "cn=%s9" % NEW_ACCOUNT
    old_dn  = "%s,%s" % (old_rdn, STAGING_DN)
    new_rdn = old_rdn
    new_superior = PRODUCTION_DN

    #
    # Try to rename without the apropriate ACI  => INSUFFICIENT_ACCESS
    #
    try:
        topology.master1.log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
        topology.master1.rename_s(old_dn, new_rdn, newsuperior=new_superior)
        assert 0
    except AssertionError:
        topology.master1.log.info("Exception (not really expected exception but that is fine as it fails to rename)")
    except Exception as e:
        topology.master1.log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)

    #############
    # Now do tests with no support of moddn aci
    #############
    topology.master1.log.info("Disable the moddn right")
    _bind_manager(topology)
    mod = [(ldap.MOD_REPLACE, CONFIG_MODDN_ACI_ATTR, 'off')]
    topology.master1.modify_s(DN_CONFIG, mod)

    # Add the moddn aci that will not be evaluated because of the config flag
    topology.master1.log.info("\n\n######## MOVE to and from equality filter ########\n")
    _bind_manager(topology)
    _moddn_aci_staging_to_production(topology, mod_type=ldap.MOD_ADD,
                                     target_from=STAGING_DN, target_to=PRODUCTION_DN)
    _bind_normal(topology)

    # It will fail because it will test the ADD right
    try:
        topology.master1.log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
        topology.master1.rename_s(old_dn, new_rdn, newsuperior=new_superior)
        assert 0
    except AssertionError:
        topology.master1.log.info("Exception (not really expected exception but that is fine as it fails to rename)")
    except Exception as e:
        topology.master1.log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)

    # remove the moddn aci
    _bind_manager(topology)
    _moddn_aci_staging_to_production(topology, mod_type=ldap.MOD_DELETE,
                                     target_from=STAGING_DN, target_to=PRODUCTION_DN)
    _bind_normal(topology)

    #
    # add the 'add' right to the production DN
    # Then do a successfull moddn
    #
    ACI_ALLOW        = "(version 3.0; acl \"ADD rights to allow moddn\"; allow (add)"
    ACI_SUBJECT      = " userdn = \"ldap:///%s\";)" % BIND_DN
    ACI_BODY         = ACI_ALLOW + ACI_SUBJECT

    _bind_manager(topology)
    mod = [(ldap.MOD_ADD, 'aci', ACI_BODY)]
    topology.master1.modify_s(PRODUCTION_DN, mod)
    _write_aci_staging(topology, mod_type=ldap.MOD_ADD)
    _bind_normal(topology)

    topology.master1.log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
    topology.master1.rename_s(old_dn, new_rdn, newsuperior=new_superior)

    _bind_manager(topology)
    mod = [(ldap.MOD_DELETE, 'aci', ACI_BODY)]
    topology.master1.modify_s(PRODUCTION_DN, mod)
    _write_aci_staging(topology, mod_type=ldap.MOD_DELETE)
    _bind_normal(topology)

    #############
    # Now do tests with support of moddn aci
    #############
    topology.master1.log.info("Enable the moddn right")
    _bind_manager(topology)
    mod = [(ldap.MOD_REPLACE, CONFIG_MODDN_ACI_ATTR, 'on')]
    topology.master1.modify_s(DN_CONFIG, mod)

    topology.master1.log.info("\n\n######## MOVE staging -> Prod (10) ########\n")

    _bind_normal(topology)
    old_rdn = "cn=%s10" % NEW_ACCOUNT
    old_dn  = "%s,%s" % (old_rdn, STAGING_DN)
    new_rdn = old_rdn
    new_superior = PRODUCTION_DN

    #
    # Try to rename without the apropriate ACI  => INSUFFICIENT_ACCESS
    #
    try:
        topology.master1.log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
        topology.master1.rename_s(old_dn, new_rdn, newsuperior=new_superior)
        assert 0
    except AssertionError:
        topology.master1.log.info("Exception (not really expected exception but that is fine as it fails to rename)")
    except Exception as e:
        topology.master1.log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)

    #
    # add the 'add' right to the production DN
    # Then do a failing moddn
    #
    ACI_ALLOW        = "(version 3.0; acl \"ADD rights to allow moddn\"; allow (add)"
    ACI_SUBJECT      = " userdn = \"ldap:///%s\";)" % BIND_DN
    ACI_BODY         = ACI_ALLOW + ACI_SUBJECT

    _bind_manager(topology)
    mod = [(ldap.MOD_ADD, 'aci', ACI_BODY)]
    topology.master1.modify_s(PRODUCTION_DN, mod)
    _write_aci_staging(topology, mod_type=ldap.MOD_ADD)
    _bind_normal(topology)

    try:
        topology.master1.log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
        topology.master1.rename_s(old_dn, new_rdn, newsuperior=new_superior)
        assert 0
    except AssertionError:
        topology.master1.log.info("Exception (not really expected exception but that is fine as it fails to rename)")
    except Exception as e:
        topology.master1.log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)

    _bind_manager(topology)
    mod = [(ldap.MOD_DELETE, 'aci', ACI_BODY)]
    topology.master1.modify_s(PRODUCTION_DN, mod)
    _write_aci_staging(topology, mod_type=ldap.MOD_DELETE)
    _bind_normal(topology)

    # Add the moddn aci that will be evaluated because of the config flag
    topology.master1.log.info("\n\n######## MOVE to and from equality filter ########\n")
    _bind_manager(topology)
    _moddn_aci_staging_to_production(topology, mod_type=ldap.MOD_ADD,
                                     target_from=STAGING_DN, target_to=PRODUCTION_DN)
    _bind_normal(topology)

    topology.master1.log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
    topology.master1.rename_s(old_dn, new_rdn, newsuperior=new_superior)

    # remove the moddn aci
    _bind_manager(topology)
    _moddn_aci_staging_to_production(topology, mod_type=ldap.MOD_DELETE,
                                     target_from=STAGING_DN, target_to=PRODUCTION_DN)
    _bind_normal(topology)


def test_moddn_prod_staging(topology, moddn_setup):
    """This test checks that we can move ACCOUNT11 from staging to prod
    but not move back ACCOUNT11 from prod to staging
    """

    topology.master1.log.info("\n\n######## MOVE staging -> Prod (11) ########\n")

    _bind_normal(topology)

    old_rdn = "cn=%s11" % NEW_ACCOUNT
    old_dn  = "%s,%s" % (old_rdn, STAGING_DN)
    new_rdn = old_rdn
    new_superior = PRODUCTION_DN

    #
    # Try to rename without the apropriate ACI  => INSUFFICIENT_ACCESS
    #
    try:
        topology.master1.log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
        topology.master1.rename_s(old_dn, new_rdn, newsuperior=new_superior)
        assert 0
    except AssertionError:
        topology.master1.log.info("Exception (not really expected exception but that is fine as it fails to rename)")
    except Exception as e:
        topology.master1.log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)

    # successfull MOD with the ACI
    topology.master1.log.info("\n\n######## MOVE to and from equality filter ########\n")
    _bind_manager(topology)
    _moddn_aci_staging_to_production(topology, mod_type=ldap.MOD_ADD,
                                     target_from=STAGING_DN, target_to=PRODUCTION_DN)
    _bind_normal(topology)

    topology.master1.log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
    topology.master1.rename_s(old_dn, new_rdn, newsuperior=new_superior)

    # Now check we can not move back the entry to staging
    old_rdn = "cn=%s11" % NEW_ACCOUNT
    old_dn  = "%s,%s" % (old_rdn, PRODUCTION_DN)
    new_rdn = old_rdn
    new_superior = STAGING_DN

    # add the write right because we want to check the moddn
    _bind_manager(topology)
    _write_aci_production(topology, mod_type=ldap.MOD_ADD)
    _bind_normal(topology)

    try:
        topology.master1.log.info("Try to move back MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
        topology.master1.rename_s(old_dn, new_rdn, newsuperior=new_superior)
        assert 0
    except AssertionError:
        topology.master1.log.info("Exception (not really expected exception but that is fine as it fails to rename)")
    except Exception as e:
        topology.master1.log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)

    _bind_manager(topology)
    _write_aci_production(topology, mod_type=ldap.MOD_DELETE)
    _bind_normal(topology)

    # successfull MOD with the both ACI
    _bind_manager(topology)
    _moddn_aci_staging_to_production(topology, mod_type=ldap.MOD_DELETE,
                                     target_from=STAGING_DN, target_to=PRODUCTION_DN)
    _bind_normal(topology)


def test_check_repl_M2_to_M1(topology, moddn_setup):
    """Checks that replication is still working M2->M1, using ACCOUNT12"""

    topology.master1.log.info("Bind as %s (M2)" % DN_DM)
    topology.master2.simple_bind_s(DN_DM, PASSWORD)

    rdn = "cn=%s12" % NEW_ACCOUNT
    dn  = "%s,%s" % (rdn, STAGING_DN)

    # First wait for the ACCOUNT19 entry being replicated on M2
    loop = 0
    while loop <= 10:
        try:
            ent = topology.master2.getEntry(dn, ldap.SCOPE_BASE, "(objectclass=*)")
            break
        except ldap.NO_SUCH_OBJECT:
            time.sleep(1)
            loop += 1
    assert loop <= 10

    attribute = 'description'
    tested_value = 'Hello world'
    mod = [(ldap.MOD_ADD, attribute, tested_value)]
    topology.master1.log.info("Update (M2) %s (%s)" % (dn, attribute))
    topology.master2.modify_s(dn, mod)

    loop = 0
    while loop <= 10:
        ent = topology.master1.getEntry(dn, ldap.SCOPE_BASE, "(objectclass=*)")
        assert ent is not None
        if ent.hasAttr(attribute) and (ent.getValue(attribute) == tested_value):
            break

        time.sleep(1)
        loop += 1
    assert loop < 10
    topology.master1.log.info("Update %s (%s) replicated on M1" % (dn, attribute))


def test_moddn_staging_prod_except(topology, moddn_setup):
    """This test case MOVE entry NEW_ACCOUNT13 from staging to prod
    but fails to move entry NEW_ACCOUNT14 from staging to prod_except
    """

    topology.master1.log.info("\n\n######## MOVE staging -> Prod (13) ########\n")
    _bind_normal(topology)

    old_rdn = "cn=%s13" % NEW_ACCOUNT
    old_dn  = "%s,%s" % (old_rdn, STAGING_DN)
    new_rdn = old_rdn
    new_superior = PRODUCTION_DN

    #
    # Try to rename without the apropriate ACI  => INSUFFICIENT_ACCESS
    #
    try:
        topology.master1.log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
        topology.master1.rename_s(old_dn, new_rdn, newsuperior=new_superior)
        assert 0
    except AssertionError:
        topology.master1.log.info("Exception (not really expected exception but that is fine as it fails to rename)")
    except Exception as e:
        topology.master1.log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)

    # successfull MOD with the ACI
    topology.master1.log.info("\n\n######## MOVE to and from equality filter ########\n")
    _bind_manager(topology)
    _moddn_aci_staging_to_production(topology, mod_type=ldap.MOD_ADD,
                                     target_from=STAGING_DN, target_to=PRODUCTION_DN)
    _moddn_aci_deny_tree(topology, mod_type=ldap.MOD_ADD)
    _bind_normal(topology)

    topology.master1.log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
    topology.master1.rename_s(old_dn, new_rdn, newsuperior=new_superior)

    #
    # Now try to move an entry  under except
    #
    topology.master1.log.info("\n\n######## MOVE staging -> Prod/Except (14) ########\n")
    old_rdn = "cn=%s14" % NEW_ACCOUNT
    old_dn  = "%s,%s" % (old_rdn, STAGING_DN)
    new_rdn = old_rdn
    new_superior = PROD_EXCEPT_DN
    try:
        topology.master1.log.info("Try to MODDN %s -> %s,%s" % (old_dn, new_rdn, new_superior))
        topology.master1.rename_s(old_dn, new_rdn, newsuperior=new_superior)
        assert 0
    except AssertionError:
        topology.master1.log.info("Exception (not really expected exception but that is fine as it fails to rename)")
    except Exception as e:
        topology.master1.log.info("Exception (expected): %s" % type(e).__name__)
        assert isinstance(e, ldap.INSUFFICIENT_ACCESS)

    # successfull MOD with the both ACI
    _bind_manager(topology)
    _moddn_aci_staging_to_production(topology, mod_type=ldap.MOD_DELETE,
                                     target_from=STAGING_DN, target_to=PRODUCTION_DN)
    _moddn_aci_deny_tree(topology, mod_type=ldap.MOD_DELETE)
    _bind_normal(topology)


def test_mode_default_ger_no_moddn(topology, moddn_setup):
    topology.master1.log.info("\n\n######## mode moddn_aci : GER no moddn  ########\n")
    request_ctrl = GetEffectiveRightsControl(criticality=True, authzId="dn: " + BIND_DN)
    msg_id = topology.master1.search_ext(PRODUCTION_DN,
                                         ldap.SCOPE_SUBTREE,
                                         "objectclass=*",
                                         serverctrls=[request_ctrl])
    rtype, rdata, rmsgid, response_ctrl = topology.master1.result3(msg_id)
    #ger={}
    value = ''
    for dn, attrs in rdata:
        topology.master1.log.info("dn: %s" % dn)
        value = attrs['entryLevelRights'][0]

    topology.master1.log.info("########  entryLevelRights: %r" % value)
    assert 'n' not in value


def test_mode_default_ger_with_moddn(topology, moddn_setup):
    """This test case adds the moddn aci and check ger contains 'n'"""

    topology.master1.log.info("\n\n######## mode moddn_aci: GER with moddn ########\n")

    # successfull MOD with the ACI
    _bind_manager(topology)
    _moddn_aci_staging_to_production(topology, mod_type=ldap.MOD_ADD,
                                     target_from=STAGING_DN, target_to=PRODUCTION_DN)
    _bind_normal(topology)

    request_ctrl = GetEffectiveRightsControl(criticality=True, authzId="dn: " + BIND_DN)
    msg_id = topology.master1.search_ext(PRODUCTION_DN,
                                         ldap.SCOPE_SUBTREE,
                                         "objectclass=*",
                                         serverctrls=[request_ctrl])
    rtype, rdata, rmsgid, response_ctrl = topology.master1.result3(msg_id)
    #ger={}
    value = ''
    for dn, attrs in rdata:
        topology.master1.log.info("dn: %s" % dn)
        value = attrs['entryLevelRights'][0]

    topology.master1.log.info("########  entryLevelRights: %r" % value)
    assert 'n' in value

    # successfull MOD with the both ACI
    _bind_manager(topology)
    _moddn_aci_staging_to_production(topology, mod_type=ldap.MOD_DELETE,
                                     target_from=STAGING_DN, target_to=PRODUCTION_DN)
    _bind_normal(topology)


def test_mode_switch_default_to_legacy(topology, moddn_setup):
    """This test switch the server from default mode to legacy"""

    topology.master1.log.info("\n\n######## Disable the moddn aci mod ########\n")
    _bind_manager(topology)
    mod = [(ldap.MOD_REPLACE, CONFIG_MODDN_ACI_ATTR, 'off')]
    topology.master1.modify_s(DN_CONFIG, mod)


def test_mode_legacy_ger_no_moddn1(topology, moddn_setup):
    topology.master1.log.info("\n\n######## mode legacy 1: GER no moddn  ########\n")
    request_ctrl = GetEffectiveRightsControl(criticality=True, authzId="dn: " + BIND_DN)
    msg_id = topology.master1.search_ext(PRODUCTION_DN,
                                         ldap.SCOPE_SUBTREE,
                                         "objectclass=*",
                                         serverctrls=[request_ctrl])
    rtype, rdata, rmsgid, response_ctrl = topology.master1.result3(msg_id)
    #ger={}
    value = ''
    for dn, attrs in rdata:
        topology.master1.log.info("dn: %s" % dn)
        value = attrs['entryLevelRights'][0]

    topology.master1.log.info("########  entryLevelRights: %r" % value)
    assert 'n' not in value


def test_mode_legacy_ger_no_moddn2(topology, moddn_setup):
    topology.master1.log.info("\n\n######## mode legacy 2: GER no moddn  ########\n")
    # successfull MOD with the ACI
    _bind_manager(topology)
    _moddn_aci_staging_to_production(topology, mod_type=ldap.MOD_ADD,
                                     target_from=STAGING_DN, target_to=PRODUCTION_DN)
    _bind_normal(topology)

    request_ctrl = GetEffectiveRightsControl(criticality=True, authzId="dn: " + BIND_DN)
    msg_id = topology.master1.search_ext(PRODUCTION_DN,
                                         ldap.SCOPE_SUBTREE,
                                         "objectclass=*",
                                         serverctrls=[request_ctrl])
    rtype, rdata, rmsgid, response_ctrl = topology.master1.result3(msg_id)
    #ger={}
    value = ''
    for dn, attrs in rdata:
        topology.master1.log.info("dn: %s" % dn)
        value = attrs['entryLevelRights'][0]

    topology.master1.log.info("########  entryLevelRights: %r" % value)
    assert 'n' not in value

    # successfull MOD with the both ACI
    _bind_manager(topology)
    _moddn_aci_staging_to_production(topology, mod_type=ldap.MOD_DELETE,
                                     target_from=STAGING_DN, target_to=PRODUCTION_DN)
    _bind_normal(topology)


def test_mode_legacy_ger_with_moddn(topology, moddn_setup):
    topology.master1.log.info("\n\n######## mode legacy : GER with moddn  ########\n")

    # being allowed to read/write the RDN attribute use to allow the RDN
    ACI_TARGET = "(target = \"ldap:///%s\")(targetattr=\"cn\")" % (PRODUCTION_DN)
    ACI_ALLOW    = "(version 3.0; acl \"MODDN production changing the RDN attribute\"; allow (read,search,write)"
    ACI_SUBJECT  = " userdn = \"ldap:///%s\";)" % BIND_DN
    ACI_BODY     = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT

    # successfull MOD with the ACI
    _bind_manager(topology)
    mod = [(ldap.MOD_ADD, 'aci', ACI_BODY)]
    topology.master1.modify_s(SUFFIX, mod)
    _bind_normal(topology)

    request_ctrl = GetEffectiveRightsControl(criticality=True, authzId="dn: " + BIND_DN)
    msg_id = topology.master1.search_ext(PRODUCTION_DN,
                                         ldap.SCOPE_SUBTREE,
                                         "objectclass=*",
                                         serverctrls=[request_ctrl])
    rtype, rdata, rmsgid, response_ctrl = topology.master1.result3(msg_id)
    #ger={}
    value = ''
    for dn, attrs in rdata:
        topology.master1.log.info("dn: %s" % dn)
        value = attrs['entryLevelRights'][0]

    topology.master1.log.info("########  entryLevelRights: %r" % value)
    assert 'n' in value

    # successfull MOD with the both ACI
    _bind_manager(topology)
    mod = [(ldap.MOD_DELETE, 'aci', ACI_BODY)]
    topology.master1.modify_s(SUFFIX, mod)
    #_bind_normal(topology)


@pytest.fixture(scope="module")
def rdn_write_setup(topology):
    topology.master1.log.info("\n\n######## Add entry tuser ########\n")
    topology.master1.add_s(Entry((SRC_ENTRY_DN, {
                                                'objectclass': "top person".split(),
                                                'sn': SRC_ENTRY_CN,
                                                'cn': SRC_ENTRY_CN})))


def test_rdn_write_get_ger(topology, rdn_write_setup):
    ANONYMOUS_DN = ""
    topology.master1.log.info("\n\n######## GER rights for anonymous ########\n")
    request_ctrl = GetEffectiveRightsControl(criticality=True,
                                             authzId="dn:" + ANONYMOUS_DN)
    msg_id = topology.master1.search_ext(SUFFIX,
                                         ldap.SCOPE_SUBTREE,
                                         "objectclass=*",
                                         serverctrls=[request_ctrl])
    rtype, rdata, rmsgid, response_ctrl = topology.master1.result3(msg_id)
    value = ''
    for dn, attrs in rdata:
        topology.master1.log.info("dn: %s" % dn)
        for value in attrs['entryLevelRights']:
            topology.master1.log.info("########  entryLevelRights: %r" % value)
            assert 'n' not in value


def test_rdn_write_modrdn_anonymous(topology, rdn_write_setup):
    ANONYMOUS_DN = ""
    topology.master1.close()
    topology.master1.binddn = ANONYMOUS_DN
    topology.master1.open()
    msg_id = topology.master1.search_ext("", ldap.SCOPE_BASE, "objectclass=*")
    rtype, rdata, rmsgid, response_ctrl = topology.master1.result3(msg_id)
    for dn, attrs in rdata:
        topology.master1.log.info("dn: %s" % dn)
        for attr in attrs:
            topology.master1.log.info("########  %r: %r" % (attr, attrs[attr]))

    try:
        topology.master1.rename_s(SRC_ENTRY_DN, "cn=%s" % DST_ENTRY_CN, delold=True)
    except Exception as e:
        topology.master1.log.info("Exception (expected): %s" % type(e).__name__)
        isinstance(e, ldap.INSUFFICIENT_ACCESS)

    try:
        topology.master1.getEntry(DST_ENTRY_DN, ldap.SCOPE_BASE, "objectclass=*")
        assert False
    except Exception as e:
        topology.master1.log.info("The entry was not renamed (expected)")
        isinstance(e, ldap.NO_SUCH_OBJECT)

    _bind_manager(topology)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
