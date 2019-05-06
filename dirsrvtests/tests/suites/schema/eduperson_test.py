# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#


import os
import logging
import pytest
import ldap

from lib389.idm.user import UserAccounts
from lib389.topologies import topology_st as topology
from lib389._constants import DEFAULT_SUFFIX

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv('DEBUGGING', False)

if DEBUGGING is not False:
    DEBUGGING = True

if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)

log = logging.getLogger(__name__)


def test_account_locking(topology):
    """Test the eduperson schema works

    :id: f2f15449-a822-4ec6-b4ea-bd6db6240a6c

    :setup: Standalone instance

    :steps:
        1. Add a common user
        2. Extend the user with eduPerson objectClass
        3. Add attributes in eduPerson

    :expectedresults:
        1. User should be added with its properties
        2. User should be extended with eduPerson as the objectClass
        3. eduPerson should be added
    """

    if DEBUGGING:
        # Add debugging steps(if any)...
        pass

    users = UserAccounts(topology.standalone, DEFAULT_SUFFIX)

    user_properties = {
        'uid': 'testuser',
        'cn' : 'testuser',
        'sn' : 'user',
        'uidNumber' : '1000',
        'gidNumber' : '2000',
        'homeDirectory' : '/home/testuser',
    }
    testuser = users.create(properties=user_properties)

    # Extend the user with eduPerson
    testuser.add('objectClass', 'eduPerson')

    # now add eduPerson attrs
    testuser.add('eduPersonAffiliation', 'value') # From 2002
    testuser.add('eduPersonNickName', 'value') # From 2002
    testuser.add('eduPersonOrgDN', 'ou=People,%s' % DEFAULT_SUFFIX) # From 2002
    testuser.add('eduPersonOrgUnitDN', 'ou=People,%s' % DEFAULT_SUFFIX) # From 2002
    testuser.add('eduPersonPrimaryAffiliation', 'value') # From 2002
    testuser.add('eduPersonPrincipalName', 'value') # From 2002
    testuser.add('eduPersonEntitlement', 'value') # From 2002
    testuser.add('eduPersonPrimaryOrgUnitDN', 'ou=People,%s' % DEFAULT_SUFFIX) # From 2002
    testuser.add('eduPersonScopedAffiliation', 'value') # From 2003
    testuser.add('eduPersonTargetedID', 'value') # From 2003
    testuser.add('eduPersonAssurance', 'value') # From 2008
    testuser.add('eduPersonPrincipalNamePrior', 'value') # From 2012
    testuser.add('eduPersonUniqueId', 'value') # From 2013
    testuser.add('eduPersonOrcid', 'value') # From 2016

    log.info('Test PASSED')


