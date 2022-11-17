# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import pytest
import os
import time
import ldap
from lib389._constants import *
from lib389.idm.user import UserAccounts
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.topologies import topology_st as topo

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

USER_DN = 'uid=Test_user1,ou=People,dc=example,dc=com'
USER_ACI = '(targetattr="userpassword")(version 3.0; acl "pwp test"; allow (all) userdn="ldap:///self";)'
TOKEN = 'test_user1'

user_properties = {
    'uid': 'Test_user1',
    'cn': 'test_user1',
    'sn': 'test_user1',
    'uidNumber': '1001',
    'gidNumber': '2001',
    'userpassword': PASSWORD,
    'description': 'userdesc',
    'homeDirectory': '/home/{}'.format('test_user')}


def pwd_setup(topo):
    ous = OrganizationalUnits(topo.standalone, DEFAULT_SUFFIX)
    ou = ous.get('people')
    ou.add('aci', USER_ACI)

    topo.standalone.config.replace_many(('passwordCheckSyntax', 'on'),
                                        ('passwordMinLength', '4'),
                                        ('passwordMinCategories', '1'))
    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    return users.create(properties=user_properties)


def test_token_lengths(topo):
    """Test that password token length is enforced for various lengths including
    the same length as the attribute being checked by the policy.

    :id: dae9d916-2a03-4707-b454-9e901d295b13
    :setup: Standalone instance
    :steps:
        1. Test token length rejects password of the same length as rdn value
    :expectedresults:
        1. Passwords are rejected
    """
    user = pwd_setup(topo)
    for length in ['4', '6', '10']:
        topo.standalone.simple_bind_s(DN_DM, PASSWORD)
        topo.standalone.config.set('passwordMinTokenLength', length)
        topo.standalone.simple_bind_s(USER_DN, PASSWORD)
        time.sleep(1)

        try:
            passwd = TOKEN[:int(length)]
            log.info("Testing password len {} token ({})".format(length, passwd))
            user.replace('userpassword', passwd)
            log.fatal('Password incorrectly allowed!')
            assert False
        except ldap.CONSTRAINT_VIOLATION as e:
            log.info('Password correctly rejected: ' + str(e))
        except ldap.LDAPError as e:
            log.fatal('Unexpected failure ' + str(e))
            assert False


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

