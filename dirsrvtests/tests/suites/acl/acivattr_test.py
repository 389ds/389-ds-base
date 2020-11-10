# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import pytest, os, ldap
from lib389._constants import DEFAULT_SUFFIX, PW_DM
from lib389.idm.user import UserAccount
from lib389.idm.organization import Organization
from lib389.idm.organizationalunit import OrganizationalUnit
from lib389.cos import CosTemplate, CosClassicDefinition
from lib389.topologies import topology_st as topo
from lib389.idm.nscontainer import nsContainer
from lib389.idm.domain import Domain
from lib389.idm.role import FilteredRoles

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


@pytest.fixture(scope="function")
def aci_of_user(request, topo):
    aci_list = Domain(topo.standalone, DEFAULT_SUFFIX).get_attr_vals('aci')

    def finofaci():
        domain = Domain(topo.standalone, DEFAULT_SUFFIX)
        domain.set('aci', None)
        for i in aci_list:
            domain.add("aci", i)

    request.addfinalizer(finofaci)


@pytest.fixture(scope="function")
def _add_user(request, topo):
    org = Organization(topo.standalone).create(properties={"o": "acivattr"}, basedn=DEFAULT_SUFFIX)
    org.add('aci', '(targetattr="*")(targetfilter="(nsrole=*)")(version 3.0; aci "tester"; '
                   'allow(all) userdn="ldap:///cn=enguser1,ou=eng,o=acivattr,{}";)'.format(DEFAULT_SUFFIX))

    ou = OrganizationalUnit(topo.standalone, "ou=eng,o=acivattr,{}".format(DEFAULT_SUFFIX))
    ou.create(properties={'ou': 'eng'})

    ou = OrganizationalUnit(topo.standalone, "ou=sales,o=acivattr,{}".format(DEFAULT_SUFFIX))
    ou.create(properties={'ou': 'sales'})

    roles = FilteredRoles(topo.standalone, DNBASE)
    roles.create(properties={'cn':'FILTERROLEENGROLE', 'nsRoleFilter':'cn=eng*'})
    roles.create(properties={'cn': 'FILTERROLESALESROLE', 'nsRoleFilter': 'cn=sales*'})

    nsContainer(topo.standalone,
                'cn=cosClassicGenerateEmployeeTypeUsingnsroleTemplates,o=acivattr,{}'.format(DEFAULT_SUFFIX)).create(
        properties={'cn': 'cosTemplates'})

    properties = {'employeeType': 'EngType', 'cn':'"cn=filterRoleEngRole,o=acivattr,dc=example,dc=com",cn=cosClassicGenerateEmployeeTypeUsingnsroleTemplates,o=acivattr,dc=example,dc=com'}
    CosTemplate(topo.standalone,'cn="cn=filterRoleEngRole,o=acivattr,dc=example,dc=com",'
                                'cn=cosClassicGenerateEmployeeTypeUsingnsroleTemplates,o=acivattr,{}'.format(DEFAULT_SUFFIX)).\
        create(properties=properties)

    properties = {'employeeType': 'SalesType', 'cn': '"cn=filterRoleSalesRole,o=acivattr,dc=example,dc=com",cn=cosClassicGenerateEmployeeTypeUsingnsroleTemplates,o=acivattr,dc=example,dc=com'}
    CosTemplate(topo.standalone,
                'cn="cn=filterRoleSalesRole,o=acivattr,dc=example,dc=com",cn=cosClassicGenerateEmployeeTypeUsingnsroleTemplates,'
                'o=acivattr,{}'.format(DEFAULT_SUFFIX)).create(properties=properties)

    properties = {
        'cosTemplateDn': 'cn=cosClassicGenerateEmployeeTypeUsingnsroleTemplates,o=acivattr,{}'.format(DEFAULT_SUFFIX),
        'cosAttribute': 'employeeType', 'cosSpecifier': 'nsrole', 'cn': 'cosClassicGenerateEmployeeTypeUsingnsrole'}
    CosClassicDefinition(topo.standalone,
                         'cn=cosClassicGenerateEmployeeTypeUsingnsrole,o=acivattr,{}'.format(DEFAULT_SUFFIX)).create(
        properties=properties)

    properties = {
        'uid': 'salesuser1',
        'cn': 'salesuser1',
        'sn': 'user',
        'uidNumber': '1000',
        'gidNumber': '2000',
        'homeDirectory': '/home/' + 'salesuser1',
        'userPassword': PW_DM
    }
    user = UserAccount(topo.standalone, 'cn=salesuser1,ou=sales,o=acivattr,{}'.format(DEFAULT_SUFFIX))
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
    user = UserAccount(topo.standalone, 'cn=salesmanager1,ou=sales,o=acivattr,{}'.format(DEFAULT_SUFFIX))
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
    user = UserAccount(topo.standalone, 'cn=enguser1,ou=eng,o=acivattr,{}'.format(DEFAULT_SUFFIX))
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
    user = UserAccount(topo.standalone, 'cn=engmanager1,ou=eng,o=acivattr,{}'.format(DEFAULT_SUFFIX))
    user.create(properties=properties)

    def fin():
        for DN in [ENG_USER,SALES_UESER,ENG_MANAGER,SALES_MANAGER,FILTERROLESALESROLE,FILTERROLEENGROLE,ENG_OU,SALES_OU,
                   'cn="cn=filterRoleEngRole,o=acivattr,dc=example,dc=com",'
                   'cn=cosClassicGenerateEmployeeTypeUsingnsroleTemplates,o=acivattr,dc=example,dc=com',
                   'cn="cn=filterRoleSalesRole,o=acivattr,dc=example,dc=com",'
                   'cn=cosClassicGenerateEmployeeTypeUsingnsroleTemplates,o=acivattr,{}'.format(DEFAULT_SUFFIX), 'cn=cosClassicGenerateEmployeeTypeUsingnsroleTemplates,o=acivattr,{}'.format(DEFAULT_SUFFIX),
                   'cn=cosClassicGenerateEmployeeTypeUsingnsrole,o=acivattr,{}'.format(DEFAULT_SUFFIX), DNBASE]:
            UserAccount(topo.standalone, DN).delete()

    request.addfinalizer(fin)


REAL_EQ_ACI = '(targetattr="*")(targetfilter="(cn=engmanager1)") (version 3.0; acl "real-eq"; allow (all) userdn="ldap:///{}";)'.format(ENG_USER)
REAL_PRES_ACI = '(targetattr="*")(targetfilter="(cn=*)") (version 3.0; acl "real-pres"; allow (all) userdn="ldap:///{}";)'.format(ENG_USER)
REAL_SUB_ACI = '(targetattr="*")(targetfilter="(cn=eng*)") (version 3.0; acl "real-sub"; allow (all) userdn="ldap:///{}";)'.format(ENG_USER)
ROLE_EQ_ACI = '(targetattr="*")(targetfilter="(nsrole=cn=filterroleengrole,o=sun.com)") (version 3.0; acl "role-eq"; allow (all) userdn="ldap:///{}";)'.format(ENG_USER)
ROLE_PRES_ACI = '(targetattr="*")(targetfilter="(nsrole=*)") (version 3.0; acl "role-pres"; allow (all) userdn="ldap:///{}";)'.format(ENG_USER)
ROLE_SUB_ACI = '(targetattr="*")(targetfilter="(nsrole=cn=filterroleeng*)") (version 3.0; acl "role-sub"; allow (all) userdn="ldap:///{}";)'.format(ENG_USER)
COS_EQ_ACI = '(targetattr="*")(targetfilter="(employeetype=engtype)") (version 3.0; acl "cos-eq"; allow (all) userdn="ldap:///{}";)'.format(ENG_USER)
COS_PRES_ACI = '(targetattr="*")(targetfilter="(employeetype=*)") (version 3.0; acl "cos-pres"; allow (all) userdn="ldap:///{}";)'.format(ENG_USER)
COS_SUB_ACI = '(targetattr="*")(targetfilter="(employeetype=eng*)") (version 3.0; acl "cos-sub"; allow (all) userdn="ldap:///{}";)'.format(ENG_USER)
LDAPURL_ACI = '(targetattr="*")(version 3.0; acl "url"; allow (all) userdn="ldap:///o=acivattr,dc=example,dc=com??sub?(nsrole=*eng*)";)'


@pytest.mark.parametrize("user,entry,aci", [
    (ENG_USER, ENG_MANAGER, REAL_EQ_ACI),
    (ENG_USER, ENG_MANAGER, REAL_PRES_ACI),
    (ENG_USER, ENG_MANAGER, REAL_SUB_ACI),
    (ENG_USER, ENG_MANAGER, ROLE_PRES_ACI),
    (ENG_USER, ENG_MANAGER, ROLE_SUB_ACI),
    (ENG_USER, ENG_MANAGER, COS_EQ_ACI),
    (ENG_USER, ENG_MANAGER, COS_PRES_ACI),
    (ENG_USER, ENG_MANAGER, COS_SUB_ACI),
    (ENG_USER, ENG_MANAGER, LDAPURL_ACI),
], ids=[
    "(ENG_USER, ENG_MANAGER, REAL_EQ_ACI)",
    "(ENG_USER, ENG_MANAGER, REAL_PRES_ACI)",
    "(ENG_USER, ENG_MANAGER, REAL_SUB_ACI)",
    "(ENG_USER, ENG_MANAGER, ROLE_PRES_ACI)",
    '(ENG_USER, ENG_MANAGER, ROLE_SUB_ACI)',
    '(ENG_USER, ENG_MANAGER, COS_EQ_ACI)',
    '(ENG_USER, ENG_MANAGER, COS_PRES_ACI)',
    '(ENG_USER, ENG_MANAGER, COS_SUB_ACI)',
    '(ENG_USER, ENG_MANAGER, LDAPURL_ACI)',
])
def test_positive(topo, _add_user, aci_of_user, user, entry, aci):
    """Positive testing of ACLs

    :id: ba6d5e9c-786b-11e8-860d-8c16451d917b
    :parametrized: yes
    :setup: server
    :steps:
        1. Add test entry
        2. Add ACI
        3. ACI role should be followed
    :expectedresults:
        1. Entry should be added
        2. Operation should succeed
        3. Operation should succeed
    """
    # set aci
    Domain(topo.standalone, DNBASE).set("aci", aci)
    # create connection
    conn = UserAccount(topo.standalone, user).bind(PW_DM)
    # according to the aci , user will  be able to change description
    UserAccount(conn, entry).replace("description", "Fred")
    assert UserAccount(conn, entry).present('description')


@pytest.mark.parametrize("user,entry,aci", [
    (ENG_USER, SALES_MANAGER, REAL_EQ_ACI),
    (ENG_USER, SALES_OU, REAL_PRES_ACI),
    (ENG_USER, SALES_MANAGER, REAL_SUB_ACI),
    (ENG_USER, SALES_MANAGER, ROLE_EQ_ACI),
    (ENG_USER, SALES_OU, ROLE_PRES_ACI),
    (ENG_USER, SALES_MANAGER, ROLE_SUB_ACI),
    (ENG_USER, SALES_MANAGER, COS_EQ_ACI),
    (ENG_USER, SALES_OU, COS_PRES_ACI),
    (ENG_USER, SALES_MANAGER, COS_SUB_ACI),
    (SALES_UESER, SALES_MANAGER, LDAPURL_ACI),
    (ENG_USER, ENG_MANAGER, ROLE_EQ_ACI),
], ids=[

    "(ENG_USER, SALES_MANAGER, REAL_EQ_ACI)",
    "(ENG_USER, SALES_OU, REAL_PRES_ACI)",
    "(ENG_USER, SALES_MANAGER, REAL_SUB_ACI)",
    "(ENG_USER, SALES_MANAGER, ROLE_EQ_ACI)",
    "(ENG_USER, SALES_MANAGER, ROLE_PRES_ACI)",
    '(ENG_USER, SALES_MANAGER, ROLE_SUB_ACI)',
    '(ENG_USER, SALES_MANAGER, COS_EQ_ACI)',
    '(ENG_USER, SALES_MANAGER, COS_PRES_ACI)',
    '(ENG_USER, SALES_MANAGER, COS_SUB_ACI)',
    '(SALES_UESER, SALES_MANAGER, LDAPURL_ACI)',
    '(ENG_USER, ENG_MANAGER, ROLE_EQ_ACI)'


])
def test_negative(topo, _add_user, aci_of_user, user, entry, aci):
    """Negative testing of ACLs

    :id: c4c887c2-786b-11e8-a328-8c16451d917b
    :parametrized: yes
    :setup: server
    :steps:
        1. Add test entry
        2. Add ACI
        3. ACI role should be followed
    :expectedresults:
        1. Entry should be added
        2. Operation should succeed
        3. Operation should not succeed
    """
    # set aci
    Domain(topo.standalone, DNBASE).set("aci", aci)
    # create connection
    conn = UserAccount(topo.standalone, user).bind(PW_DM)
    # according to the aci , user will not be able to change description
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        UserAccount(conn, entry).replace("description", "Fred")


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
