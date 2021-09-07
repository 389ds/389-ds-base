# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import time
import subprocess
import pytest

from lib389.cli_ctl.dbgen import *
from lib389.cos import CosClassicDefinitions, CosPointerDefinitions, CosIndirectDefinitions, CosTemplates
from lib389.idm.account import Accounts
from lib389.idm.group import Groups
from lib389.idm.role import ManagedRoles, FilteredRoles, NestedRoles
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st
from lib389.cli_base import FakeArgs

pytestmark = pytest.mark.tier0

LOG_FILE = '/tmp/dbgen.log'
logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


@pytest.fixture(scope="function")
def set_log_file_and_ldif(topology_st, request):
    global ldif_file
    ldif_file = get_ldif_dir(topology_st.standalone) + '/created.ldif'

    fh = logging.FileHandler(LOG_FILE)
    fh.setLevel(logging.DEBUG)
    log.addHandler(fh)

    def fin():
        log.info('Delete files')
        #os.remove(LOG_FILE)
        #os.remove(ldif_file)

    request.addfinalizer(fin)


def run_offline_import(instance, ldif_file):
    log.info('Stopping the server and running offline import...')
    instance.stop()
    assert instance.ldif2db(bename=DEFAULT_BENAME, suffixes=[DEFAULT_SUFFIX], encrypt=None, excludeSuffixes=None,
                              import_file=ldif_file)
    instance.start()


def run_ldapmodify_from_file(instance, ldif_file, output_to_check=None):
    LDAP_MOD = '/usr/bin/ldapmodify'
    log.info('Add entries from ldif file with ldapmodify')
    result = subprocess.check_output([LDAP_MOD, '-cx', '-D', DN_DM, '-w', PASSWORD,
                                      '-h', instance.host, '-p', str(instance.port), '-af', ldif_file])
    if output_to_check is not None:
        assert output_to_check in ensure_str(result)


def check_value_in_log_and_reset(content_list):
    with open(LOG_FILE, 'r+') as f:
        file_content = f.read()
        log.info('Check if content is present in output')
        for item in content_list:
            assert item in file_content

        log.info('Reset log file for next test')
        f.truncate(0)


@pytest.mark.ds50545
@pytest.mark.bz1798394
@pytest.mark.skipif(ds_is_older("1.4.3"), reason="Not implemented")
def test_dsconf_dbgen_users(topology_st, set_log_file_and_ldif):
    """Test ldifgen (formerly dbgen) tool to create ldif with users

    :id: 426b5b94-9923-454d-a736-7e71ca985e98
    :setup: Standalone instance
    :steps:
         1. Create DS instance
         2. Run ldifgen to generate ldif with users
         3. Import generated ldif to database
         4. Check it was properly imported
    :expectedresults:
         1. Success
         2. Success
         3. Success
         4. Success
    """

    standalone = topology_st.standalone

    args = FakeArgs()
    args.suffix = DEFAULT_SUFFIX
    args.parent = 'ou=people,dc=example,dc=com'
    args.number = 1000
    args.rdn_cn = False
    args.generic = True
    args.start_idx = 50
    args.localize = False
    args.ldif_file = ldif_file

    content_list = ['Generating LDIF with the following options:',
                    'suffix={}'.format(args.suffix),
                    'parent={}'.format(args.parent),
                    'number={}'.format(args.number),
                    'rdn-cn={}'.format(args.rdn_cn),
                    'generic={}'.format(args.generic),
                    'start-idx={}'.format(args.start_idx),
                    'localize={}'.format(args.localize),
                    'ldif-file={}'.format(args.ldif_file),
                    'Writing LDIF',
                    'Successfully created LDIF file: {}'.format(args.ldif_file)]

    log.info('Run ldifgen to create users ldif')
    dbgen_create_users(standalone, log, args)

    log.info('Check if file exists')
    assert os.path.exists(ldif_file)

    check_value_in_log_and_reset(content_list)

    log.info('Get number of accounts before import')
    accounts = Accounts(standalone, DEFAULT_SUFFIX)
    count_account = len(accounts.filter('(uid=*)'))

    run_offline_import(standalone, ldif_file)

    log.info('Check that accounts are imported')
    assert len(accounts.filter('(uid=*)')) > count_account


@pytest.mark.ds50545
@pytest.mark.bz1798394
@pytest.mark.skipif(ds_is_older("1.4.3"), reason="Not implemented")
def test_dsconf_dbgen_groups(topology_st, set_log_file_and_ldif):
    """Test ldifgen (formerly dbgen) tool to create ldif with group

            :id: 97207413-9a93-4065-a5ec-63aa93801a3f
            :setup: Standalone instance
            :steps:
                 1. Create DS instance
                 2. Run ldifgen to generate ldif with group
                 3. Import generated ldif to database
                 4. Check it was properly imported
            :expectedresults:
                 1. Success
                 2. Success
                 3. Success
                 4. Success
            """
    LDAP_RESULT = 'adding new entry "cn=myGroup-1,ou=groups,dc=example,dc=com"'

    standalone = topology_st.standalone

    args = FakeArgs()
    args.NAME = 'myGroup'
    args.parent = 'ou=groups,dc=example,dc=com'
    args.suffix = DEFAULT_SUFFIX
    args.number = 1
    args.num_members = 1000
    args.create_members = True
    args.member_attr = 'uniquemember'
    args.member_parent = 'ou=people,dc=example,dc=com'
    args.ldif_file = ldif_file

    content_list = ['Generating LDIF with the following options:',
                    'NAME={}'.format(args.NAME),
                    'number={}'.format(args.number),
                    'suffix={}'.format(args.suffix),
                    'num-members={}'.format(args.num_members),
                    'create-members={}'.format(args.create_members),
                    'member-parent={}'.format(args.member_parent),
                    'member-attr={}'.format(args.member_attr),
                    'ldif-file={}'.format(args.ldif_file),
                    'Writing LDIF',
                    'Successfully created LDIF file: {}'.format(args.ldif_file)]

    log.info('Run ldifgen to create group ldif')
    dbgen_create_groups(standalone, log, args)

    log.info('Check if file exists')
    assert os.path.exists(ldif_file)

    check_value_in_log_and_reset(content_list)

    log.info('Get number of accounts before import')
    accounts = Accounts(standalone, DEFAULT_SUFFIX)
    count_account = len(accounts.filter('(uid=*)'))

    # Groups, COS, Roles and modification ldifs are designed to be used by ldapmodify, not ldif2db
    # ldapmodify will complain about already existing parent which causes subprocess to return exit code != 0
    with pytest.raises(subprocess.CalledProcessError):
        run_ldapmodify_from_file(standalone, ldif_file, LDAP_RESULT)

    log.info('Check that accounts are imported')
    assert len(accounts.filter('(uid=*)')) > count_account

    log.info('Check that group is imported')
    groups = Groups(standalone, DEFAULT_SUFFIX)
    assert groups.exists(args.NAME + '-1')
    new_group = groups.get(args.NAME + '-1')
    new_group.present('uniquemember', 'uid=group_entry1-0152,ou=people,dc=example,dc=com')


@pytest.mark.ds50545
@pytest.mark.bz1798394
@pytest.mark.skipif(ds_is_older("1.4.3"), reason="Not implemented")
def test_dsconf_dbgen_cos_classic(topology_st, set_log_file_and_ldif):
    """Test ldifgen (formerly dbgen) tool to create a COS definition

        :id: 8557f994-8a91-4f8a-86f6-9cb826a0b8fd
        :setup: Standalone instance
        :steps:
             1. Create DS instance
             2. Run ldifgen to generate ldif with classic COS definition
             3. Import generated ldif to database
             4. Check it was properly imported
        :expectedresults:
             1. Success
             2. Success
             3. Success
             4. Success
        """

    LDAP_RESULT = 'adding new entry "cn=My_Postal_Def,ou=cos definitions,dc=example,dc=com"'

    standalone = topology_st.standalone

    args = FakeArgs()
    args.type = 'classic'
    args.NAME = 'My_Postal_Def'
    args.parent = 'ou=cos definitions,dc=example,dc=com'
    args.create_parent = True
    args.cos_specifier = 'businessCategory'
    args.cos_attr = ['postalcode', 'telephonenumber']
    args.cos_template = 'cn=sales,cn=classicCoS,dc=example,dc=com'
    args.ldif_file = ldif_file

    content_list = ['Generating LDIF with the following options:',
                    'NAME={}'.format(args.NAME),
                    'type={}'.format(args.type),
                    'parent={}'.format(args.parent),
                    'create-parent={}'.format(args.create_parent),
                    'cos-specifier={}'.format(args.cos_specifier),
                    'cos-template={}'.format(args.cos_template),
                    'cos-attr={}'.format(args.cos_attr),
                    'ldif-file={}'.format(args.ldif_file),
                    'Writing LDIF',
                    'Successfully created LDIF file: {}'.format(args.ldif_file)]

    log.info('Run ldifgen to create COS definition ldif')
    dbgen_create_cos_def(standalone, log, args)

    log.info('Check if file exists')
    assert os.path.exists(ldif_file)

    check_value_in_log_and_reset(content_list)

    # Groups, COS, Roles and modification ldifs are designed to be used by ldapmodify, not ldif2db
    run_ldapmodify_from_file(standalone, ldif_file, LDAP_RESULT)

    log.info('Check that COS definition is imported')
    cos_def = CosClassicDefinitions(standalone, args.parent)
    assert cos_def.exists(args.NAME)
    new_cos = cos_def.get(args.NAME)
    assert new_cos.present('cosTemplateDN', args.cos_template)
    assert new_cos.present('cosSpecifier', args.cos_specifier)
    assert new_cos.present('cosAttribute', args.cos_attr[0])
    assert new_cos.present('cosAttribute', args.cos_attr[1])


@pytest.mark.ds50545
@pytest.mark.bz1798394
@pytest.mark.skipif(ds_is_older("1.4.3"), reason="Not implemented")
def test_dsconf_dbgen_cos_pointer(topology_st, set_log_file_and_ldif):
    """Test ldifgen (formerly dbgen) tool to create a COS definition

        :id: 6b26ca6d-226a-4f93-925e-faf95cc20214
        :setup: Standalone instance
        :steps:
             1. Create DS instance
             2. Run ldifgen to generate ldif with pointer COS definition
             3. Import generated ldif to database
             4. Check it was properly imported
        :expectedresults:
             1. Success
             2. Success
             3. Success
             4. Success
        """

    LDAP_RESULT = 'adding new entry "cn=My_Postal_Def_pointer,ou=cos pointer definitions,dc=example,dc=com"'

    standalone = topology_st.standalone

    args = FakeArgs()
    args.type = 'pointer'
    args.NAME = 'My_Postal_Def_pointer'
    args.parent = 'ou=cos pointer definitions,dc=example,dc=com'
    args.create_parent = True
    args.cos_specifier = None
    args.cos_attr = ['postalcode', 'telephonenumber']
    args.cos_template = 'cn=sales,cn=pointerCoS,dc=example,dc=com'
    args.ldif_file = ldif_file

    content_list = ['Generating LDIF with the following options:',
                    'NAME={}'.format(args.NAME),
                    'type={}'.format(args.type),
                    'parent={}'.format(args.parent),
                    'create-parent={}'.format(args.create_parent),
                    'cos-template={}'.format(args.cos_template),
                    'cos-attr={}'.format(args.cos_attr),
                    'ldif-file={}'.format(args.ldif_file),
                    'Writing LDIF',
                    'Successfully created LDIF file: {}'.format(args.ldif_file)]

    log.info('Run ldifgen to create COS definition ldif')
    dbgen_create_cos_def(standalone, log, args)

    log.info('Check if file exists')
    assert os.path.exists(ldif_file)

    check_value_in_log_and_reset(content_list)

    # Groups, COS, Roles and modification ldifs are designed to be used by ldapmodify, not ldif2db
    run_ldapmodify_from_file(standalone, ldif_file, LDAP_RESULT)

    log.info('Check that COS definition is imported')
    cos_def = CosPointerDefinitions(standalone, args.parent)
    assert cos_def.exists(args.NAME)
    new_cos = cos_def.get(args.NAME)
    assert new_cos.present('cosTemplateDN', args.cos_template)
    assert new_cos.present('cosAttribute', args.cos_attr[0])
    assert new_cos.present('cosAttribute', args.cos_attr[1])


@pytest.mark.ds50545
@pytest.mark.bz1798394
@pytest.mark.skipif(ds_is_older("1.4.3"), reason="Not implemented")
def test_dsconf_dbgen_cos_indirect(topology_st, set_log_file_and_ldif):
    """Test ldifgen (formerly dbgen) tool to create a COS definition

        :id: ab4b799e-e801-432a-a61d-badad2628203
        :setup: Standalone instance
        :steps:
             1. Create DS instance
             2. Run ldifgen to generate ldif with indirect COS definition
             3. Import generated ldif to database
             4. Check it was properly imported
        :expectedresults:
             1. Success
             2. Success
             3. Success
             4. Success
        """

    LDAP_RESULT = 'adding new entry "cn=My_Postal_Def_indirect,ou=cos indirect definitions,dc=example,dc=com"'

    standalone = topology_st.standalone

    args = FakeArgs()
    args.type = 'indirect'
    args.NAME = 'My_Postal_Def_indirect'
    args.parent = 'ou=cos indirect definitions,dc=example,dc=com'
    args.create_parent = True
    args.cos_specifier = 'businessCategory'
    args.cos_attr = ['postalcode', 'telephonenumber']
    args.cos_template = None
    args.ldif_file = ldif_file

    content_list = ['Generating LDIF with the following options:',
                    'NAME={}'.format(args.NAME),
                    'type={}'.format(args.type),
                    'parent={}'.format(args.parent),
                    'create-parent={}'.format(args.create_parent),
                    'cos-specifier={}'.format(args.cos_specifier),
                    'cos-attr={}'.format(args.cos_attr),
                    'ldif-file={}'.format(args.ldif_file),
                    'Writing LDIF',
                    'Successfully created LDIF file: {}'.format(args.ldif_file)]

    log.info('Run ldifgen to create COS definition ldif')
    dbgen_create_cos_def(standalone, log, args)

    log.info('Check if file exists')
    assert os.path.exists(ldif_file)

    check_value_in_log_and_reset(content_list)

    # Groups, COS, Roles and modification ldifs are designed to be used by ldapmodify, not ldif2db
    run_ldapmodify_from_file(standalone, ldif_file, LDAP_RESULT)

    log.info('Check that COS definition is imported')
    cos_def = CosIndirectDefinitions(standalone, args.parent)
    assert cos_def.exists(args.NAME)
    new_cos = cos_def.get(args.NAME)
    assert new_cos.present('cosIndirectSpecifier', args.cos_specifier)
    assert new_cos.present('cosAttribute', args.cos_attr[0])
    assert new_cos.present('cosAttribute', args.cos_attr[1])


@pytest.mark.ds50545
@pytest.mark.bz1798394
@pytest.mark.skipif(ds_is_older("1.4.3"), reason="Not implemented")
def test_dsconf_dbgen_cos_template(topology_st, set_log_file_and_ldif):
    """Test ldifgen (formerly dbgen) tool to create a COS template

        :id: 544017c7-4a82-4e7d-a047-00b68a28e070
        :setup: Standalone instance
        :steps:
             1. Create DS instance
             2. Run ldifgen to generate ldif with COS template
             3. Import generated ldif to database
             4. Check it was properly imported
        :expectedresults:
             1. Success
             2. Success
             3. Success
             4. Success
        """

    LDAP_RESULT = 'adding new entry "cn=My_Template,ou=cos templates,dc=example,dc=com"'

    standalone = topology_st.standalone

    args = FakeArgs()
    args.NAME = 'My_Template'
    args.parent = 'ou=cos templates,dc=example,dc=com'
    args.create_parent = True
    args.cos_priority = 1
    args.cos_attr_val = 'postalcode:12345'
    args.ldif_file = ldif_file

    content_list = ['Generating LDIF with the following options:',
                    'NAME={}'.format(args.NAME),
                    'parent={}'.format(args.parent),
                    'create-parent={}'.format(args.create_parent),
                    'cos-priority={}'.format(args.cos_priority),
                    'cos-attr-val={}'.format(args.cos_attr_val),
                    'ldif-file={}'.format(args.ldif_file),
                    'Writing LDIF',
                    'Successfully created LDIF file: {}'.format(args.ldif_file)]

    log.info('Run ldifgen to create COS template ldif')
    dbgen_create_cos_tmp(standalone, log, args)

    log.info('Check if file exists')
    assert os.path.exists(ldif_file)

    check_value_in_log_and_reset(content_list)

    # Groups, COS, Roles and modification ldifs are designed to be used by ldapmodify, not ldif2db
    run_ldapmodify_from_file(standalone, ldif_file, LDAP_RESULT)

    log.info('Check that COS template is imported')
    cos_temp = CosTemplates(standalone, args.parent)
    assert cos_temp.exists(args.NAME)
    new_cos = cos_temp.get(args.NAME)
    assert new_cos.present('cosPriority', str(args.cos_priority))
    assert new_cos.present('postalcode', '12345')


@pytest.mark.ds50545
@pytest.mark.bz1798394
@pytest.mark.skipif(ds_is_older("1.4.3"), reason="Not implemented")
def test_dsconf_dbgen_managed_role(topology_st, set_log_file_and_ldif):
    """Test ldifgen (formerly dbgen) tool to create a managed role

        :id: 10e77b41-0bc1-4ad5-a144-2c5107455b92
        :setup: Standalone instance
        :steps:
             1. Create DS instance
             2. Run ldifgen to generate ldif with managed role
             3. Import generated ldif to database
             4. Check it was properly imported
        :expectedresults:
             1. Success
             2. Success
             3. Success
             4. Success
        """

    LDAP_RESULT = 'adding new entry "cn=My_Managed_Role,ou=managed roles,dc=example,dc=com"'

    standalone = topology_st.standalone

    args = FakeArgs()

    args.NAME = 'My_Managed_Role'
    args.parent = 'ou=managed roles,dc=example,dc=com'
    args.create_parent = True
    args.type = 'managed'
    args.filter = None
    args.role_dn = None
    args.ldif_file = ldif_file

    content_list = ['Generating LDIF with the following options:',
                    'NAME={}'.format(args.NAME),
                    'parent={}'.format(args.parent),
                    'create-parent={}'.format(args.create_parent),
                    'type={}'.format(args.type),
                    'ldif-file={}'.format(args.ldif_file),
                    'Writing LDIF',
                    'Successfully created LDIF file: {}'.format(args.ldif_file)]

    log.info('Run ldifgen to create managed role ldif')
    dbgen_create_role(standalone, log, args)

    log.info('Check if file exists')
    assert os.path.exists(ldif_file)

    check_value_in_log_and_reset(content_list)

    # Groups, COS, Roles and modification ldifs are designed to be used by ldapmodify, not ldif2db
    run_ldapmodify_from_file(standalone, ldif_file, LDAP_RESULT)

    log.info('Check that managed role is imported')
    roles = ManagedRoles(standalone, DEFAULT_SUFFIX)
    assert roles.exists(args.NAME)


@pytest.mark.ds50545
@pytest.mark.bz1798394
@pytest.mark.skipif(ds_is_older("1.4.3"), reason="Not implemented")
def test_dsconf_dbgen_filtered_role(topology_st, set_log_file_and_ldif):
    """Test ldifgen (formerly dbgen) tool to create a filtered role

        :id: cb3c8ea8-4234-40e2-8810-fb6a25973927
        :setup: Standalone instance
        :steps:
             1. Create DS instance
             2. Run ldifgen to generate ldif with filtered role
             3. Import generated ldif to database
             4. Check it was properly imported
        :expectedresults:
             1. Success
             2. Success
             3. Success
             4. Success
        """

    LDAP_RESULT = 'adding new entry "cn=My_Filtered_Role,ou=filtered roles,dc=example,dc=com"'

    standalone = topology_st.standalone

    args = FakeArgs()

    args.NAME = 'My_Filtered_Role'
    args.parent = 'ou=filtered roles,dc=example,dc=com'
    args.create_parent = True
    args.type = 'filtered'
    args.filter = '"objectclass=posixAccount"'
    args.role_dn = None
    args.ldif_file = ldif_file

    content_list = ['Generating LDIF with the following options:',
                    'NAME={}'.format(args.NAME),
                    'parent={}'.format(args.parent),
                    'create-parent={}'.format(args.create_parent),
                    'type={}'.format(args.type),
                    'filter={}'.format(args.filter),
                    'ldif-file={}'.format(args.ldif_file),
                    'Writing LDIF',
                    'Successfully created LDIF file: {}'.format(args.ldif_file)]

    log.info('Run ldifgen to create filtered role ldif')
    dbgen_create_role(standalone, log, args)

    log.info('Check if file exists')
    assert os.path.exists(ldif_file)

    check_value_in_log_and_reset(content_list)

    # Groups, COS, Roles and modification ldifs are designed to be used by ldapmodify, not ldif2db
    run_ldapmodify_from_file(standalone, ldif_file, LDAP_RESULT)

    log.info('Check that filtered role is imported')
    roles = FilteredRoles(standalone, DEFAULT_SUFFIX)
    assert roles.exists(args.NAME)
    new_role = roles.get(args.NAME)
    assert new_role.present('nsRoleFilter', args.filter)


@pytest.mark.ds50545
@pytest.mark.bz1798394
@pytest.mark.skipif(ds_is_older("1.4.3"), reason="Not implemented")
def test_dsconf_dbgen_nested_role(topology_st, set_log_file_and_ldif):
    """Test ldifgen (formerly dbgen) tool to create a nested role

        :id: 97fff0a8-3103-4adb-be04-2799ff58d8f4
        :setup: Standalone instance
        :steps:
             1. Create DS instance
             2. Run ldifgen to generate ldif with nested role
             3. Import generated ldif to database
             4. Check it was properly imported
        :expectedresults:
             1. Success
             2. Success
             3. Success
             4. Success
        """

    LDAP_RESULT = 'adding new entry "cn=My_Nested_Role,ou=nested roles,dc=example,dc=com"'

    standalone = topology_st.standalone

    args = FakeArgs()
    args.NAME = 'My_Nested_Role'
    args.parent = 'ou=nested roles,dc=example,dc=com'
    args.create_parent = True
    args.type = 'nested'
    args.filter = None
    args.role_dn = ['cn=some_role,ou=roles,dc=example,dc=com']
    args.ldif_file = ldif_file

    content_list = ['Generating LDIF with the following options:',
                    'NAME={}'.format(args.NAME),
                    'parent={}'.format(args.parent),
                    'create-parent={}'.format(args.create_parent),
                    'type={}'.format(args.type),
                    'role-dn={}'.format(args.role_dn),
                    'ldif-file={}'.format(args.ldif_file),
                    'Writing LDIF',
                    'Successfully created LDIF file: {}'.format(args.ldif_file)]

    log.info('Run ldifgen to create nested role ldif')
    dbgen_create_role(standalone, log, args)

    log.info('Check if file exists')
    assert os.path.exists(ldif_file)

    check_value_in_log_and_reset(content_list)

    # Groups, COS, Roles and modification ldifs are designed to be used by ldapmodify, not ldif2db
    run_ldapmodify_from_file(standalone, ldif_file, LDAP_RESULT)

    log.info('Check that nested role is imported')
    roles = NestedRoles(standalone, DEFAULT_SUFFIX)
    assert roles.exists(args.NAME)
    new_role = roles.get(args.NAME)
    assert new_role.present('nsRoleDN', args.role_dn[0])


@pytest.mark.ds50545
@pytest.mark.bz1798394
@pytest.mark.skipif(ds_is_older("1.4.3"), reason="Not implemented")
def test_dsconf_dbgen_mod_ldif_mixed(topology_st, set_log_file_and_ldif):
    """Test ldifgen (formerly dbgen) tool to create mixed modification ldif

        :id: 4a2e0901-2b48-452e-a4a0-507735132c8d
        :setup: Standalone instance
        :steps:
             1. Create DS instance
             2. Run ldifgen to generate modification ldif
             3. Import generated ldif to database
             4. Check it was properly imported
        :expectedresults:
             1. Success
             2. Success
             3. Success
             4. Success
        """

    standalone = topology_st.standalone

    args = FakeArgs()
    args.parent = DEFAULT_SUFFIX
    args.create_users = True
    args.delete_users = True
    args.create_parent = False
    args.num_users = 1000
    args.add_users = 100
    args.del_users = 999
    args.modrdn_users = 100
    args.mod_users = 10
    args.mod_attrs = ['cn', 'uid', 'sn']
    args.randomize = False
    args.ldif_file = ldif_file

    content_list = ['Generating LDIF with the following options:',
                    'create-users={}'.format(args.create_users),
                    'parent={}'.format(args.parent),
                    'create-parent={}'.format(args.create_parent),
                    'delete-users={}'.format(args.delete_users),
                    'num-users={}'.format(args.num_users),
                    'add-users={}'.format(args.add_users),
                    'del-users={}'.format(args.del_users),
                    'modrdn-users={}'.format(args.modrdn_users),
                    'mod-users={}'.format(args.mod_users),
                    'mod-attrs={}'.format(args.mod_attrs),
                    'randomize={}'.format(args.randomize),
                    'ldif-file={}'.format(args.ldif_file),
                    'Writing LDIF',
                    'Successfully created LDIF file: {}'.format(args.ldif_file)]

    log.info('Run ldifgen to create modification ldif')
    dbgen_create_mods(standalone, log, args)

    log.info('Check if file exists')
    assert os.path.exists(ldif_file)

    check_value_in_log_and_reset(content_list)

    log.info('Get number of accounts before import')
    accounts = Accounts(standalone, DEFAULT_SUFFIX)
    count_account = len(accounts.filter('(uid=*)'))

    # Groups, COS, Roles and modification ldifs are designed to be used by ldapmodify, not ldif2db
    # ldapmodify will complain about a lot of changes done which causes subprocess to return exit code != 0
    with pytest.raises(subprocess.CalledProcessError):
        run_ldapmodify_from_file(standalone, ldif_file)

    log.info('Check that some accounts are imported')
    assert len(accounts.filter('(uid=*)')) > count_account


@pytest.mark.ds50545
@pytest.mark.bz1798394
@pytest.mark.skipif(ds_is_older("1.4.3"), reason="Not implemented")
def test_dsconf_dbgen_nested_ldif(topology_st, set_log_file_and_ldif):
    """Test ldifgen (formerly dbgen) tool to create nested ldif

        :id: 9c281c28-4169-45e0-8c07-c5502d9a7585
        :setup: Standalone instance
        :steps:
             1. Create DS instance
             2. Run ldifgen to generate nested ldif
             3. Import generated ldif to database
             4. Check it was properly imported
        :expectedresults:
             1. Success
             2. Success
             3. Success
             4. Success
        """

    standalone = topology_st.standalone

    args = FakeArgs()
    args.suffix = DEFAULT_SUFFIX
    args.node_limit = 100
    args.num_users = 600
    args.ldif_file = ldif_file

    content_list = ['Generating LDIF with the following options:',
                    'suffix={}'.format(args.suffix),
                    'node-limit={}'.format(args.node_limit),
                    'num-users={}'.format(args.num_users),
                    'ldif-file={}'.format(args.ldif_file),
                    'Writing LDIF',
                    'Successfully created nested LDIF file ({}) containing 6 nodes/subtrees'.format(args.ldif_file)]

    log.info('Run ldifgen to create nested ldif')
    dbgen_create_nested(standalone, log, args)

    log.info('Check if file exists')
    assert os.path.exists(ldif_file)

    check_value_in_log_and_reset(content_list)

    log.info('Get number of accounts before import')
    accounts = Accounts(standalone, DEFAULT_SUFFIX)
    count_account = len(accounts.filter('(uid=*)'))
    count_ou = len(accounts.filter('(ou=*)'))

    # Groups, COS, Roles and modification ldifs are designed to be used by ldapmodify, not ldif2db
    # ldapmodify will complain about already existing suffix which causes subprocess to return exit code != 0
    with pytest.raises(subprocess.CalledProcessError):
        run_ldapmodify_from_file(standalone, ldif_file)

    standalone.restart()

    log.info('Check that accounts are imported')
    assert len(accounts.filter('(uid=*)')) > count_account
    assert len(accounts.filter('(ou=*)')) > count_ou


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
