# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---


"""
Importing necessary Modules.
"""

import logging
import time
import ldap
import os
import pytest

from lib389._constants import ErrorLog, PW_DM, DEFAULT_SUFFIX, DEFAULT_BENAME
from lib389.idm.user import UserAccount, UserAccounts
from lib389.idm.organization import Organization
from lib389.idm.organizationalunit import OrganizationalUnit
from lib389.topologies import topology_st as topo
from lib389.idm.role import FilteredRoles, ManagedRoles, NestedRoles
from lib389.idm.domain import Domain
from lib389.dbgen import dbgen_users
from lib389.tasks import ImportTask
from lib389.utils import get_default_db_lib
from lib389.rewriters import *
from lib389.backend import Backends

logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

pytestmark = pytest.mark.tier1

DNBASE = "o=acivattr,{}".format(DEFAULT_SUFFIX)
ENG_USER = "cn=enguser1,ou=eng,{}".format(DNBASE)
SALES_UESER = "cn=salesuser1,ou=sales,{}".format(DNBASE)
ENG_MANAGER = "cn=engmanager1,ou=eng,{}".format(DNBASE)
SALES_MANAGER = "cn=salesmanager1,ou=sales,{}".format(DNBASE)
SALES_OU = "ou=sales,{}".format(DNBASE)
ENG_OU = "ou=eng,{}".format(DNBASE)
FILTERROLESALESROLE = "cn=FILTERROLESALESROLE,{}".format(DNBASE)
FILTERROLEENGROLE = "cn=FILTERROLEENGROLE,{}".format(DNBASE)


def test_filterrole(topo, request):
    """Test Filter Role

    :id: 8ada4064-786b-11e8-8634-8c16451d917b
    :setup: server
    :steps:
        1. Add test entry
        2. Add ACI
        3. Search nsconsole role
    :expectedresults:
        1. Entry should be added
        2. Operation should  succeed
        3. Operation should  succeed
    """
    Organization(topo.standalone).create(properties={"o": "acivattr"}, basedn=DEFAULT_SUFFIX)
    properties = {
        'ou': 'eng',
    }

    ou_ou = OrganizationalUnit(topo.standalone, "ou=eng,o=acivattr,{}".format(DEFAULT_SUFFIX))
    ou_ou.create(properties=properties)
    properties = {'ou': 'sales'}
    ou_ou = OrganizationalUnit(topo.standalone, "ou=sales,o=acivattr,{}".format(DEFAULT_SUFFIX))
    ou_ou.create(properties=properties)

    roles = FilteredRoles(topo.standalone, DNBASE)
    roles.create(properties={'cn': 'FILTERROLEENGROLE', 'nsRoleFilter': 'cn=eng*'})
    roles.create(properties={'cn': 'FILTERROLESALESROLE', 'nsRoleFilter': 'cn=sales*'})

    properties = {
        'uid': 'salesuser1',
        'cn': 'salesuser1',
        'sn': 'user',
        'uidNumber': '1000',
        'gidNumber': '2000',
        'homeDirectory': '/home/' + 'salesuser1',
        'userPassword': PW_DM
    }
    user = UserAccount(topo.standalone,
                       'cn=salesuser1,ou=sales,o=acivattr,{}'.format(DEFAULT_SUFFIX))
    user.create(properties=properties)

    properties = {
        'uid': 'salesmanager1',
        'cn': 'salesmanager1',
        'sn': 'user',
        'uidNumber': '1000',
        'gidNumber': '2000',
        'homeDirectory': '/home/' + 'salesmanager1',
        'userPassword': PW_DM,
    }
    user = UserAccount(topo.standalone,
                       'cn=salesmanager1,ou=sales,o=acivattr,{}'.format(DEFAULT_SUFFIX))
    user.create(properties=properties)

    properties = {
        'uid': 'enguser1',
        'cn': 'enguser1',
        'sn': 'user',
        'uidNumber': '1000',
        'gidNumber': '2000',
        'homeDirectory': '/home/' + 'enguser1',
        'userPassword': PW_DM
    }
    user = UserAccount(topo.standalone,
                       'cn=enguser1,ou=eng,o=acivattr,{}'.format(DEFAULT_SUFFIX))
    user.create(properties=properties)

    properties = {
        'uid': 'engmanager1',
        'cn': 'engmanager1',
        'sn': 'user',
        'uidNumber': '1000',
        'gidNumber': '2000',
        'homeDirectory': '/home/' + 'engmanager1',
        'userPassword': PW_DM
    }
    user = UserAccount(topo.standalone,
                       'cn=engmanager1,ou=eng,o=acivattr,{}'.format(DEFAULT_SUFFIX))
    user.create(properties=properties)

    # user with cn=sales* will automatically memeber of nsfilterrole
    # cn=filterrolesalesrole,o=acivattr,dc=example,dc=com
    assert UserAccount(topo.standalone,
                       'cn=salesuser1,ou=sales,o=acivattr,dc=example,dc=com').\
               get_attr_val_utf8('nsrole') == 'cn=filterrolesalesrole,o=acivattr,dc=example,dc=com'
    # same goes to SALES_MANAGER
    assert UserAccount(topo.standalone, SALES_MANAGER).get_attr_val_utf8(
        'nsrole') == 'cn=filterrolesalesrole,o=acivattr,dc=example,dc=com'
    # user with cn=eng* will automatically memeber of nsfilterrole
    # cn=filterroleengrole,o=acivattr,dc=example,dc=com
    assert UserAccount(topo.standalone, 'cn=enguser1,ou=eng,o=acivattr,dc=example,dc=com').\
               get_attr_val_utf8('nsrole') == 'cn=filterroleengrole,o=acivattr,dc=example,dc=com'
    # same goes to ENG_MANAGER
    assert UserAccount(topo.standalone, ENG_MANAGER).get_attr_val_utf8(
        'nsrole') == 'cn=filterroleengrole,o=acivattr,dc=example,dc=com'
    for dn_dn in [ENG_USER, SALES_UESER, ENG_MANAGER, SALES_MANAGER,
                  FILTERROLESALESROLE, FILTERROLEENGROLE, ENG_OU,
                  SALES_OU, DNBASE]:
        UserAccount(topo.standalone, dn_dn).delete()

    def fin():
        topo.standalone.restart()
        try:
            filtered_roles = FilteredRoles(topo.standalone, DEFAULT_SUFFIX)
            for i in filtered_roles.list():
                i.delete()
        except:
            pass
        topo.standalone.config.set('nsslapd-ignore-virtual-attrs', 'on')

    request.addfinalizer(fin)


def test_managedrole(topo, request):
    """Test Managed Role

    :id: d52a9c00-3bf6-11e9-9b7b-8c16451d917b
    :setup: server
    :steps:
        1. Add test entry
        2. Add ACI
        3. Search managed role entries
    :expectedresults:
        1. Entry should be added
        2. Operation should  succeed
        3. Operation should  succeed
    """
    # Create Managed role entry
    roles = ManagedRoles(topo.standalone, DEFAULT_SUFFIX)
    role = roles.create(properties={"cn": 'ROLE1'})

    # Create user and Assign the role to the entry
    uas = UserAccounts(topo.standalone, DEFAULT_SUFFIX, rdn=None)
    uas.create(properties={
        'uid': 'Fail',
        'cn': 'Fail',
        'sn': 'user',
        'uidNumber': '1000',
        'gidNumber': '2000',
        'homeDirectory': '/home/' + 'Fail',
        'nsRoleDN': role.dn,
        'userPassword': PW_DM
        })

    # Create user and do not Assign any role to the entry
    uas.create(
        properties={
            'uid': 'Success',
            'cn': 'Success',
            'sn': 'user',
            'uidNumber': '1000',
            'gidNumber': '2000',
            'homeDirectory': '/home/' + 'Success',
            'userPassword': PW_DM
        })

    # Assert that Manage role entry is created and its searchable
    assert ManagedRoles(topo.standalone, DEFAULT_SUFFIX).list()[0].dn \
           == 'cn=ROLE1,dc=example,dc=com'

    # Set an aci that will deny  ROLE1 manage role
    Domain(topo.standalone, DEFAULT_SUFFIX).\
        add('aci', '(targetattr="*")(version 3.0; aci "role aci";'
                   ' deny(all) roledn="ldap:///{}";)'.format(role.dn),)
    # Add self user modification and anonymous aci
    ANON_ACI = "(targetattr=\"*\")(version 3.0; acl \"Anonymous Read access\"; allow (read,search,compare) userdn = \"ldap:///anyone\";)"
    suffix = Domain(topo.standalone, DEFAULT_SUFFIX)
    suffix.add('aci', ANON_ACI)

    # Crate a connection with cn=Fail which is member of ROLE1
    conn = UserAccount(topo.standalone, "uid=Fail,{}".format(DEFAULT_SUFFIX)).bind(PW_DM)
    # Access denied to ROLE1 members
    assert not ManagedRoles(conn, DEFAULT_SUFFIX).list()

    # Now create a connection with cn=Success which is not a member of ROLE1
    conn = UserAccount(topo.standalone, "uid=Success,{}".format(DEFAULT_SUFFIX)).bind(PW_DM)
    # Access allowed here
    assert ManagedRoles(conn, DEFAULT_SUFFIX).list()

    for i in uas.list():
        i.delete()

    for i in roles.list():
        i.delete()

    def fin():
        topo.standalone.restart()
        try:
            role = ManagedRoles(topo.standalone, DEFAULT_SUFFIX).get('ROLE1')
            role.delete()
        except:
            pass
        topo.standalone.config.set('nsslapd-ignore-virtual-attrs', 'on')

    request.addfinalizer(fin)

@pytest.fixture(scope="function")
def _final(request, topo):
    """
    Removes and Restores ACIs after the test.
    """
    aci_list = Domain(topo.standalone, DEFAULT_SUFFIX).get_attr_vals('aci')

    def finofaci():
        """
        Removes and Restores ACIs and other users after the test.
        And restore nsslapd-ignore-virtual-attrs to default
        """
        domain = Domain(topo.standalone, DEFAULT_SUFFIX)
        domain.remove_all('aci')

        managed_roles = ManagedRoles(topo.standalone, DEFAULT_SUFFIX)
        nested_roles = NestedRoles(topo.standalone, DEFAULT_SUFFIX)
        users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)

        for i in managed_roles.list() + nested_roles.list() + users.list():
            i.delete()

        for i in aci_list:
            domain.add("aci", i)

        topo.standalone.config.set('nsslapd-ignore-virtual-attrs', 'on')

    request.addfinalizer(finofaci)


def test_nestedrole(topo, _final):
    """Test Nested Role

    :id: 867b40c0-7fcf-4332-afc7-bd01025b77f2
    :setup: Standalone server
    :steps:
        1. Add test entry
        2. Add ACI
        3. Search managed role entries
    :expectedresults:
        1. Entry should be added
        2. Operation should  succeed
        3. Operation should  succeed
    """
    # Create Managed role entry
    managed_roles = ManagedRoles(topo.standalone, DEFAULT_SUFFIX)
    managed_role1 = managed_roles.create(properties={"cn": 'managed_role1'})
    managed_role2 = managed_roles.create(properties={"cn": 'managed_role2'})

    # Create nested role entry
    nested_roles = NestedRoles(topo.standalone, DEFAULT_SUFFIX)
    nested_role = nested_roles.create(properties={"cn": 'nested_role',
                                                  "nsRoleDN": [managed_role1.dn, managed_role2.dn]})

    # Create user and assign managed role to it
    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    user1 = users.create_test_user(uid=1, gid=1)
    user1.set('nsRoleDN', managed_role1.dn)
    user1.set('userPassword', PW_DM)

    # Create another user and assign managed role to it
    user2 = users.create_test_user(uid=2, gid=2)
    user2.set('nsRoleDN', managed_role2.dn)
    user2.set('userPassword', PW_DM)

    # Create another user and do not assign any role to it
    user3 = users.create_test_user(uid=3, gid=3)
    user3.set('userPassword', PW_DM)

    # Create a ACI with deny access to nested role entry
    Domain(topo.standalone, DEFAULT_SUFFIX).\
        add('aci', f'(targetattr="*")(version 3.0; aci '
                   f'"role aci"; deny(all) roledn="ldap:///{nested_role.dn}";)')

    # Create connection with 'uid=test_user_1,ou=People,dc=example,dc=com' member of managed_role1
    # and search while bound as the user
    conn = users.get('test_user_1').bind(PW_DM)
    assert not UserAccounts(conn, DEFAULT_SUFFIX).list()

    # Create connection with 'uid=test_user_2,ou=People,dc=example,dc=com' member of managed_role2
    # and search while bound as the user
    conn = users.get('test_user_2').bind(PW_DM)
    assert not UserAccounts(conn, DEFAULT_SUFFIX).list()

    # Create connection with 'uid=test_user_3,ou=People,dc=example,dc=com' and
    # search while bound as the user
    conn = users.get('test_user_3').bind(PW_DM)
    assert UserAccounts(conn, DEFAULT_SUFFIX).list()

def test_vattr_on_filtered_role(topo, request):
    """Test nsslapd-ignore-virtual-attrs configuration attribute
       The attribute is ON by default. If a filtered role is
       added it is moved to OFF

    :id: 88b3ad3c-f39a-4eb7-a8c9-07c685f11908
    :setup: Standalone instance
    :steps:
         1. Check the attribute nsslapd-ignore-virtual-attrs is present in cn=config
         2. Check the default value of attribute nsslapd-ignore-virtual-attrs should be ON
         3. Create a filtered role
         4. Check the value of nsslapd-ignore-virtual-attrs should be OFF
         5. Check a message "roles_cache_trigger_update_role - Because of virtual attribute.." in error logs
    :expectedresults:
         1. This should be successful
         2. This should be successful
         3. This should be successful
         4. This should be successful
         5. This should be successful
    """

    log.info("Check the attribute nsslapd-ignore-virtual-attrs is present in cn=config")
    assert topo.standalone.config.present('nsslapd-ignore-virtual-attrs')

    log.info("Check the default value of attribute nsslapd-ignore-virtual-attrs should be ON")
    assert topo.standalone.config.get_attr_val_utf8('nsslapd-ignore-virtual-attrs') == "on"

    log.info("Create a filtered role")
    try:
        Organization(topo.standalone).create(properties={"o": "acivattr"}, basedn=DEFAULT_SUFFIX)
    except:
        pass
    roles = FilteredRoles(topo.standalone, DNBASE)
    roles.create(properties={'cn': 'FILTERROLEENGROLE', 'nsRoleFilter': 'cn=eng*'})

    log.info("Check the default value of attribute nsslapd-ignore-virtual-attrs should be OFF")
    assert topo.standalone.config.present('nsslapd-ignore-virtual-attrs', 'off')

    topo.standalone.stop()
    assert topo.standalone.searchErrorsLog("roles_cache_trigger_update_role - Because of virtual attribute definition \(role\), nsslapd-ignore-virtual-attrs was set to \'off\'")

    def fin():
        topo.standalone.restart()
        try:
            filtered_roles = FilteredRoles(topo.standalone, DEFAULT_SUFFIX)
            for i in filtered_roles.list():
                i.delete()
        except:
            pass
        topo.standalone.config.set('nsslapd-ignore-virtual-attrs', 'on')

    request.addfinalizer(fin)

def test_vattr_on_filtered_role_restart(topo, request):
    """Test nsslapd-ignore-virtual-attrs configuration attribute
    If it exists a filtered role definition at restart then
    nsslapd-ignore-virtual-attrs should be set to 'off'

    :id: 972183f7-d18f-40e0-94ab-580e7b7d78d0
    :setup: Standalone instance
    :steps:
         1. Check the attribute nsslapd-ignore-virtual-attrs is present in cn=config
         2. Check the default value of attribute nsslapd-ignore-virtual-attrs should be ON
         3. Create a filtered role
         4. Check the value of nsslapd-ignore-virtual-attrs should be OFF
         5. restart the instance
         6. Check the presence of virtual attribute is detected
         7. Check the value of nsslapd-ignore-virtual-attrs should be OFF
    :expectedresults:
         1. This should be successful
         2. This should be successful
         3. This should be successful
         4. This should be successful
         5. This should be successful
         6. This should be successful
         7. This should be successful
    """

    log.info("Check the attribute nsslapd-ignore-virtual-attrs is present in cn=config")
    assert topo.standalone.config.present('nsslapd-ignore-virtual-attrs')

    log.info("Check the default value of attribute nsslapd-ignore-virtual-attrs should be ON")
    assert topo.standalone.config.get_attr_val_utf8('nsslapd-ignore-virtual-attrs') == "on"

    log.info("Create a filtered role")
    try:
        Organization(topo.standalone).create(properties={"o": "acivattr"}, basedn=DEFAULT_SUFFIX)
    except:
        pass
    roles = FilteredRoles(topo.standalone, DNBASE)
    roles.create(properties={'cn': 'FILTERROLEENGROLE', 'nsRoleFilter': 'cn=eng*'})

    log.info("Check the default value of attribute nsslapd-ignore-virtual-attrs should be OFF")
    assert topo.standalone.config.present('nsslapd-ignore-virtual-attrs', 'off')

    
    log.info("Check the virtual attribute definition is found (after a required delay)")
    topo.standalone.restart()
    time.sleep(5)
    assert topo.standalone.searchErrorsLog("Found a role/cos definition in")
    assert topo.standalone.searchErrorsLog("roles_cache_trigger_update_role - Because of virtual attribute definition \(role\), nsslapd-ignore-virtual-attrs was set to \'off\'")

    log.info("Check the default value of attribute nsslapd-ignore-virtual-attrs should be OFF")
    assert topo.standalone.config.present('nsslapd-ignore-virtual-attrs', 'off')

    def fin():
        topo.standalone.restart()
        try:
            filtered_roles = FilteredRoles(topo.standalone, DEFAULT_SUFFIX)
            for i in filtered_roles.list():
                i.delete()
        except:
            pass
        topo.standalone.config.set('nsslapd-ignore-virtual-attrs', 'on')

    request.addfinalizer(fin)


def test_vattr_on_managed_role(topo, request):
    """Test nsslapd-ignore-virtual-attrs configuration attribute
       The attribute is ON by default. If a managed role is
       added it is moved to OFF

    :id: 664b722d-c1ea-41e4-8f6c-f9c87a212346
    :setup: Standalone instance
    :steps:
         1. Check the attribute nsslapd-ignore-virtual-attrs is present in cn=config
         2. Check the default value of attribute nsslapd-ignore-virtual-attrs should be ON
         3. Create a managed role
         4. Check the value of nsslapd-ignore-virtual-attrs should be OFF
         5. Check a message "roles_cache_trigger_update_role - Because of virtual attribute.." in error logs
    :expectedresults:
         1. This should be successful
         2. This should be successful
         3. This should be successful
         4. This should be successful
         5. This should be successful
    """

    log.info("Check the attribute nsslapd-ignore-virtual-attrs is present in cn=config")
    assert topo.standalone.config.present('nsslapd-ignore-virtual-attrs')

    log.info("Check the default value of attribute nsslapd-ignore-virtual-attrs should be ON")
    assert topo.standalone.config.get_attr_val_utf8('nsslapd-ignore-virtual-attrs') == "on"

    log.info("Create a managed role")
    roles = ManagedRoles(topo.standalone, DEFAULT_SUFFIX)
    role = roles.create(properties={"cn": 'ROLE1'})

    log.info("Check the default value of attribute nsslapd-ignore-virtual-attrs should be OFF")
    assert topo.standalone.config.present('nsslapd-ignore-virtual-attrs', 'off')

    topo.standalone.stop()
    assert topo.standalone.searchErrorsLog("roles_cache_trigger_update_role - Because of virtual attribute definition \(role\), nsslapd-ignore-virtual-attrs was set to \'off\'")

    def fin():
        topo.standalone.restart()
        try:
            filtered_roles = ManagedRoles(topo.standalone, DEFAULT_SUFFIX)
            for i in filtered_roles.list():
                i.delete()
        except:
            pass
        topo.standalone.config.set('nsslapd-ignore-virtual-attrs', 'on')

    request.addfinalizer(fin)

def test_managed_and_filtered_role_rewrite(topo, request):
    """Test that filter components containing 'nsrole=xxx'
    are reworked if xxx is either a filtered role or a managed
    role.

    :id: e30ff5ed-4f8b-48db-bb88-66f150fca31f
    :setup: server
    :steps:
        1. Setup nsrole rewriter
        2. Add a 'nsroleDN' indexes for managed roles
        3. Create an 90K ldif files
           This is large so that unindex search will last long
        4. import/restart the instance
        5. Create a managed role and add 4 entries in that role
        6. Check that a search 'nsrole=managed_role' is fast
        7. Create a filtered role that use an indexed attribute (givenName)
        8. Check that a search 'nsrole=filtered_role' is fast
    :expectedresults:
        1. Operation should  succeed
        2. Operation should  succeed
        3. Operation should  succeed
        4. Operation should  succeed
        5. Operation should  succeed
        6. Operation should  succeed
        7. Operation should  succeed
        8. Operation should  succeed
    """
    # Setup nsrole rewriter
    rewriters = Rewriters(topo.standalone)
    rewriter = rewriters.ensure_state(properties={"cn": "nsrole", "nsslapd-libpath": 'libroles-plugin'})
    try:
        rewriter.add('nsslapd-filterrewriter', "role_nsRole_filter_rewriter")
    except:
        pass

    # Create an index for nsRoleDN that is used by managed role
    attrname = 'nsRoleDN'
    backends = Backends(topo.standalone)
    backend = backends.get(DEFAULT_BENAME)
    indexes = backend.get_indexes()
    try:
        index = indexes.create(properties={
            'cn': attrname, 
            'nsSystemIndex': 'false',
            'nsIndexType': ['eq', 'pres']
            })
    except:
        pass

    # Build LDIF file
    bdb_values = {
      'wait30': 30
    }

    # Note: I still sometime get failure with a 60s timeout so lets use 90s
    mdb_values = {
      'wait30': 90
    }

    if get_default_db_lib() == 'bdb':
        values = bdb_values
    else:
        values = mdb_values

    ldif_dir = topo.standalone.get_ldif_dir()
    import_ldif = ldif_dir + '/perf_import.ldif'

    RDN="userNew"
    PARENT="ou=people,%s" % DEFAULT_SUFFIX
    dbgen_users(topo.standalone, 90000, import_ldif, DEFAULT_SUFFIX, entry_name=RDN, generic=True, parent=PARENT)

    # online import
    import_task = ImportTask(topo.standalone)
    import_task.import_suffix_from_ldif(ldiffile=import_ldif, suffix=DEFAULT_SUFFIX)
    # Check for up to 120sec that the completion
    for i in range(1, 12):
        if len(topo.standalone.ds_error_log.match('.*import userRoot: Import complete.  Processed 9000.*')) > 0:
            break
        time.sleep(10)
    import_complete = topo.standalone.ds_error_log.match('.*import userRoot: Import complete.  Processed 9000.*')
    assert (len(import_complete) == 1)

    # Restart server
    topo.standalone.restart()

    # Create Managed role entry
    managed_roles = ManagedRoles(topo.standalone, DEFAULT_SUFFIX)
    role = managed_roles.create(properties={"cn": 'MANAGED_ROLE'})

    # Assign managed role to 4 entries out of the 90K
    for i in range(1, 5):
        dn = "uid=%s0000%d,%s" % (RDN, i, PARENT)
        topo.standalone.modify_s(dn, [(ldap.MOD_REPLACE, 'nsRoleDN', [role.dn.encode()])])

    
    # Now check that search is fast, evaluating only 4 entries
    search_start = time.time()
    entries = topo.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, "(nsrole=%s)" % role.dn)
    duration = time.time() - search_start
    log.info("Duration of the search was %f", duration)
    assert(len(entries) == 4)
    assert (duration < 1)

    # Restart server to refresh entrycache
    topo.standalone.restart()

    # Create Filtered Role entry
    # it uses 'givenName' attribute that is indexed (eq) by default
    filtered_roles = FilteredRoles(topo.standalone, DEFAULT_SUFFIX)
    role = filtered_roles.create(properties={'cn': 'FILTERED_ROLE', 'nsRoleFilter': 'givenName=Technical'})

    # Now check that search is fast
    search_start = time.time()
    entries = topo.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, "(nsrole=%s)" % role.dn)
    duration = time.time() - search_start
    log.info("Duration of the search was %f", duration)
    assert (duration < 1)

    def fin():
        topo.standalone.restart()
        try:
            managed_roles = ManagedRoles(topo.standalone, DEFAULT_SUFFIX)
            for i in managed_roles.list():
                i.delete()
            filtered_roles = FilteredRoles(topo.standalone, DEFAULT_SUFFIX)
            for i in filtered_roles.list():
                i.delete()
        except:
            pass
        os.remove(import_ldif)

    request.addfinalizer(fin)

def test_not_such_entry_role_rewrite(topo, request):
    """Test that filter components containing 'nsrole=xxx'
       ,where xxx does not refer to any role definition,
       replace the component by 'nsuniqueid=-1'

    :id: b098dda5-fc77-46c4-84a7-5d0c7035bb77
    :setup: server
    :steps:
        1. Setup nsrole rewriter
        2. Add a 'nsroleDN' indexes for managed roles
        3. Create an 90K ldif files
           This is large so that unindex search will last long
        4. import/restart the instance
        5. Create a managed role and add 4 entries in that role
        6. Enable plugin log level to capture role plugin message
        7. Check that a search is fast "(OR(nsrole=managed_role)(nsrole=not_existing_role))"
        8. Stop the instance
        9. Check that a message like this was logged: replace (nsrole=not_existing_role) by (nsuniqueid=-1)
    :expectedresults:
        1. Operation should  succeed
        2. Operation should  succeed
        3. Operation should  succeed
        4. Operation should  succeed
        5. Operation should  succeed
        6. Operation should  succeed
        7. Operation should  succeed
        8. Operation should  succeed
        9. Operation should  succeed
    """
    # Setup nsrole rewriter
    rewriters = Rewriters(topo.standalone)
    rewriter = rewriters.ensure_state(properties={"cn": "nsrole", "nsslapd-libpath": 'libroles-plugin'})
    try:
        rewriter.add('nsslapd-filterrewriter', "role_nsRole_filter_rewriter")
    except:
        pass

    # Create an index for nsRoleDN that is used by managed role
    attrname = 'nsRoleDN'
    backends = Backends(topo.standalone)
    backend = backends.get(DEFAULT_BENAME)
    indexes = backend.get_indexes()
    try:
        index = indexes.create(properties={
            'cn': attrname, 
            'nsSystemIndex': 'false',
            'nsIndexType': ['eq', 'pres']
            })
    except:
        pass

    # Build LDIF file
    bdb_values = {
      'wait60': 60
    }

    # Note: I still sometime get failure with a 60s timeout so lets use 90s
    mdb_values = {
      'wait60': 90
    }

    if get_default_db_lib() == 'bdb':
        values = bdb_values
    else:
        values = mdb_values

    ldif_dir = topo.standalone.get_ldif_dir()
    import_ldif = ldif_dir + '/perf_import.ldif'

    RDN="userNew"
    PARENT="ou=people,%s" % DEFAULT_SUFFIX
    dbgen_users(topo.standalone, 91000, import_ldif, DEFAULT_SUFFIX, entry_name=RDN, generic=True, parent=PARENT)

    # online import
    import_task = ImportTask(topo.standalone)
    import_task.import_suffix_from_ldif(ldiffile=import_ldif, suffix=DEFAULT_SUFFIX)
    # Check for up to 120sec that the completion
    for i in range(1, 12):
        if len(topo.standalone.ds_error_log.match('.*import userRoot: Import complete.  Processed 9100.*')) > 0:
            break
        time.sleep(10)
    import_complete = topo.standalone.ds_error_log.match('.*import userRoot: Import complete.  Processed 9100.*')
    assert (len(import_complete) == 1)

    # Restart server
    topo.standalone.restart()

    # Create Managed role entry
    managed_roles = ManagedRoles(topo.standalone, DEFAULT_SUFFIX)
    role = managed_roles.create(properties={"cn": 'MANAGED_ROLE'})

    # Assign managed role to 4 entries out of the 90K
    for i in range(1, 5):
        dn = "uid=%s0000%d,%s" % (RDN, i, PARENT)
        topo.standalone.modify_s(dn, [(ldap.MOD_REPLACE, 'nsRoleDN', [role.dn.encode()])])

    # Enable plugin level to check message
    topo.standalone.config.loglevel(vals=(ErrorLog.DEFAULT,ErrorLog.PLUGIN))
    
    # Now check that search is fast, evaluating only 4 entries
    search_start = time.time()
    entries = topo.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, "(|(nsrole=%s)(nsrole=cn=not_such_entry_role,%s))" % (role.dn, DEFAULT_SUFFIX))
    duration = time.time() - search_start
    log.info("Duration of the search was %f", duration)
    assert(len(entries) == 4)
    assert (duration < 1)

    # Restart server to refresh entrycache
    topo.standalone.stop()

    # Check that when the role does not exist it is translated into 'nsuniqueid=-1'
    pattern = ".*replace \(nsRole=cn=not_such_entry_role,dc=example,dc=com\) by \(nsuniqueid=-1\).*"
    assert topo.standalone.ds_error_log.match(pattern)

    def fin():
        topo.standalone.restart()
        try:
            managed_roles = ManagedRoles(topo.standalone, DEFAULT_SUFFIX)
            for i in managed_roles.list():
                i.delete()
        except:
            pass
        os.remove(import_ldif)

    request.addfinalizer(fin)

if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
