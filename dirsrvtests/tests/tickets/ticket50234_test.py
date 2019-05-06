# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2019 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import time
import ldap
import pytest

from lib389.topologies import topology_st

from lib389._constants import DEFAULT_SUFFIX

from lib389.idm.user import UserAccount, UserAccounts
from lib389.idm.organizationalunit import OrganizationalUnit

pytestmark = pytest.mark.tier2

log = logging.getLogger(__name__)

def test_ticket50234(topology_st):
    """
    The fix for ticket 50234


    The test sequence is:
    - create more than 10 entries with objectclass organizational units ou=org{}
    - add an Account in one of them, eg below ou=org5
    - do searches with search base ou=org5 and search filter "objectclass=organizationalunit"
    - a subtree search should return 1 entry, the base entry
    - a onelevel search should return no entry
    """

    log.info('Testing Ticket 50234 - onelvel search returns not matching entry')

    for i in range(1,15):
        ou = OrganizationalUnit(topology_st.standalone, "ou=Org{},{}".format(i, DEFAULT_SUFFIX))
        ou.create(properties={'ou': 'Org'.format(i)})

    properties = {
            'uid': 'Jeff Vedder',
            'cn': 'Jeff Vedder',
            'sn': 'user',
            'uidNumber': '1000',
            'gidNumber': '2000',
            'homeDirectory': '/home/' + 'JeffVedder',
            'userPassword': 'password'
        }
    user = UserAccount(topology_st.standalone, "cn=Jeff Vedder,ou=org5,{}".format(DEFAULT_SUFFIX))
    user.create(properties=properties)

    # in a subtree search the entry used as search base matches the filter and shoul be returned
    ent = topology_st.standalone.getEntry("ou=org5,{}".format(DEFAULT_SUFFIX), ldap.SCOPE_SUBTREE, "(objectclass=organizationalunit)")

    # in a onelevel search the only child is an useraccount which does not match the filter
    # no entry should be returned, which would cause getEntry to raise an exception we need to handle
    found = 1
    try:
        ent = topology_st.standalone.getEntry("ou=org5,{}".format(DEFAULT_SUFFIX), ldap.SCOPE_ONELEVEL, "(objectclass=organizationalunit)")
    except ldap.NO_SUCH_OBJECT:
        found = 0
    assert (found == 0)

if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
