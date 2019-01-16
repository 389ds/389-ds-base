# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import pytest, os
from lib389._constants import PW_DM, DEFAULT_SUFFIX
from lib389.idm.user import UserAccount
from lib389.idm.organization import Organization
from lib389.idm.organizationalunit import OrganizationalUnit
from lib389.topologies import topology_st as topo
from lib389.idm.nsrole import nsFilterRoles


DNBASE = "o=acivattr,{}".format(DEFAULT_SUFFIX)
ENG_USER = "cn=enguser1,ou=eng,{}".format(DNBASE)
SALES_UESER = "cn=salesuser1,ou=sales,{}".format(DNBASE)
ENG_MANAGER = "cn=engmanager1,ou=eng,{}".format(DNBASE)
SALES_MANAGER = "cn=salesmanager1,ou=sales,{}".format(DNBASE)
SALES_OU = "ou=sales,{}".format(DNBASE)
ENG_OU = "ou=eng,{}".format(DNBASE)
FILTERROLESALESROLE = "cn=FILTERROLESALESROLE,{}".format(DNBASE)
FILTERROLEENGROLE = "cn=FILTERROLEENGROLE,{}".format(DNBASE)


def test_nsrole(topo):
    '''
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
    '''
    Organization(topo.standalone).create(properties={"o": "acivattr"}, basedn=DEFAULT_SUFFIX)
    properties = {
        'ou': 'eng',
    }

    ou = OrganizationalUnit(topo.standalone, "ou=eng,o=acivattr,{}".format(DEFAULT_SUFFIX))
    ou.create(properties=properties)
    properties = {'ou': 'sales'}
    ou = OrganizationalUnit(topo.standalone, "ou=sales,o=acivattr,{}".format(DEFAULT_SUFFIX))
    ou.create(properties=properties)

    roles = nsFilterRoles(topo.standalone, DNBASE)
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
    user = UserAccount(topo.standalone,'cn=enguser1,ou=eng,o=acivattr,{}'.format(DEFAULT_SUFFIX))
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

    #user with cn=sales* will automatically memeber of nsfilterrole cn=filterrolesalesrole,o=acivattr,dc=example,dc=com
    assert UserAccount(topo.standalone, 'cn=salesuser1,ou=sales,o=acivattr,dc=example,dc=com').get_attr_val_utf8(
        'nsrole') == 'cn=filterrolesalesrole,o=acivattr,dc=example,dc=com'
    # same goes to SALES_MANAGER
    assert UserAccount(topo.standalone, SALES_MANAGER).get_attr_val_utf8(
        'nsrole') == 'cn=filterrolesalesrole,o=acivattr,dc=example,dc=com'
    # user with cn=eng* will automatically memeber of nsfilterrole cn=filterroleengrole,o=acivattr,dc=example,dc=com
    assert UserAccount(topo.standalone, 'cn=enguser1,ou=eng,o=acivattr,dc=example,dc=com').get_attr_val_utf8(
        'nsrole') == 'cn=filterroleengrole,o=acivattr,dc=example,dc=com'
    # same goes to ENG_MANAGER
    assert UserAccount(topo.standalone, ENG_MANAGER).get_attr_val_utf8(
        'nsrole') == 'cn=filterroleengrole,o=acivattr,dc=example,dc=com'
    for DN in [ENG_USER, SALES_UESER, ENG_MANAGER, SALES_MANAGER, FILTERROLESALESROLE, FILTERROLEENGROLE, ENG_OU,
               SALES_OU, DNBASE]:
        UserAccount(topo.standalone, DN).delete()


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
