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
from lib389.idm.directorymanager import DirectoryManager

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

USER_ACI = '(targetattr="userpassword")(version 3.0; acl "pwp test"; allow (all) userdn="ldap:///self";)'
TOKEN = 'test_user123'

user_properties = {
    'uid': 'Test_user123',
    'cn': 'test_user123',
    'sn': 'test_user123',
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
        1. Create user, setup global password policy
        2. Bind as user, change password to 'Abcd012+'
        3. Bind as user with 'Abcd012+', attempt changes to 'user', 'us123', 'Tuse!1234', 'Tuse!0987', 'Tabc!1234'
        4. For each passwordMinTokenLength 4, 6, 10: change settings, rebind as user, attempt password with token of that length from TOKEN
        5. Cleanup - delete user
    :expectedresults:
        1. User created, password policy enabled and set
        2. Success
        3. All attempts fail with CONSTRAINT_VIOLATION
        4. All attempts fail with CONSTRAINT_VIOLATION
        5. User successfully deleted
    """
    user = pwd_setup(topo)
    dm = DirectoryManager(topo.standalone)

    try:
        # Verify that the user can change their password
        user.rebind(PASSWORD)
        user.replace('userpassword', 'Abcd012+')

        # Verify that the default password policy is enforced
        user.rebind('Abcd012+')
        for new_password in ['user', 'us123', 'Tuse!1234', 'Tuse!0987', 'Tabc!1234']:
            log.info(f"Testing password {new_password}")
            with pytest.raises(ldap.CONSTRAINT_VIOLATION):
                user.replace('userpassword', new_password)

        # Verify that the password policy is enforced for different token lengths
        for length in ['4', '6', '10']:
            dm.rebind(PASSWORD)
            topo.standalone.config.set('passwordMinTokenLength', length)
            user.rebind('Abcd012+')
            time.sleep(1)

            with pytest.raises(ldap.CONSTRAINT_VIOLATION):
                passwd = TOKEN[:int(length)]
                log.info("Testing password len {} token ({})".format(length, passwd))
                user.replace('userpassword', passwd)

    finally:
        # Cleanup
        user.delete()


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

