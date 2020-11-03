# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import pytest
import os
import ldap
import time
from lib389._constants import *
from lib389.topologies import topology_st as topo
from lib389.idm.user import UserAccount, TEST_USER_PROPERTIES
from lib389.idm.domain import Domain

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

BIND_DN2 = 'uid=tuser,ou=People,dc=example,dc=com'
BIND_RDN2 = 'tuser'
BIND_DN = 'uid=tuser1,ou=People,dc=example,dc=com'
BIND_RDN = 'tuser1'
SRCH_FILTER = "uid=tuser1"
SRCH_FILTER2 = "uid=tuser"

aci_list_A = ['(targetattr != "userPassword") (version 3.0; acl "Anonymous access"; allow (read, search, compare)userdn = "ldap:///anyone";)',
              '(targetattr = "*") (version 3.0;acl "allow tuser";allow (all)(userdn = "ldap:///uid=tuser5,ou=People,dc=example,dc=com");)',
              '(targetattr != "uid || mail") (version 3.0; acl "deny-attrs"; deny (all) (userdn = "ldap:///anyone");)',
              '(targetfilter = "(inetUserStatus=1)") ( version 3.0; acl "deny-specific-entry"; deny(all) (userdn = "ldap:///anyone");)']

aci_list_B = ['(targetattr != "userPassword") (version 3.0; acl "Anonymous access"; allow (read, search, compare)userdn = "ldap:///anyone";)',
              '(targetattr != "uid || mail") (version 3.0; acl "deny-attrs"; deny (all) (userdn = "ldap:///anyone");)',
              '(targetfilter = "(inetUserStatus=1)") ( version 3.0; acl "deny-specific-entry"; deny(all) (userdn = "ldap:///anyone");)']


@pytest.fixture(scope="module")
def aci_setup(topo):
    topo.standalone.log.info("Add {}".format(BIND_DN))
    user = UserAccount(topo.standalone, BIND_DN)
    user_props = TEST_USER_PROPERTIES.copy()
    user_props.update({'sn': BIND_RDN,
                       'cn': BIND_RDN,
                       'uid': BIND_RDN,
                       'inetUserStatus': '1',
                       'objectclass': 'extensibleObject',
                       'userpassword': PASSWORD})
    user.create(properties=user_props, basedn=SUFFIX)

    topo.standalone.log.info("Add {}".format(BIND_DN2))
    user2 = UserAccount(topo.standalone, BIND_DN2)
    user_props = TEST_USER_PROPERTIES.copy()
    user_props.update({'sn': BIND_RDN2,
                       'cn': BIND_RDN2,
                       'uid': BIND_RDN2,
                       'userpassword': PASSWORD})
    user2.create(properties=user_props, basedn=SUFFIX)


def test_multi_deny_aci(topo, aci_setup):
    """Test that mutliple deny rules work, and that they the cache properly
    stores the result

    :id: 294c366d-850e-459e-b5a0-3cc828ec3aca
    :setup: Standalone Instance
    :steps:
        1. Add aci_list_A aci's and verify two searches on the same connection
           behave the same
        2. Add aci_list_B aci's and verify search fails as expected
    :expectedresults:
        1. Both searches do not return any entries
        2. Seaches do not return any entries
    """

    if DEBUGGING:
        # Maybe add aci logging?
        pass

    suffix = Domain(topo.standalone, DEFAULT_SUFFIX)

    for run in range(2):
        topo.standalone.log.info("Pass " + str(run + 1))

        # Test ACI List A
        topo.standalone.log.info("Testing two searches behave the same...")
        topo.standalone.simple_bind_s(DN_DM, PASSWORD)
        suffix.set('aci', aci_list_A, ldap.MOD_REPLACE)
        time.sleep(1)

        topo.standalone.simple_bind_s(BIND_DN, PASSWORD)
        entries = topo.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, SRCH_FILTER)
        if entries and entries[0]:
            topo.standalone.log.fatal("Incorrectly got an entry returned from search 1")
            assert False

        entries = topo.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, SRCH_FILTER)
        if entries and entries[0]:
            topo.standalone.log.fatal("Incorrectly got an entry returned from search 2")
            assert False

        entries = topo.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, SRCH_FILTER2)
        if entries is None or len(entries) == 0:
            topo.standalone.log.fatal("Failed to get entry as good user")
            assert False

        entries = topo.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, SRCH_FILTER2)
        if entries is None or len(entries) == 0:
            topo.standalone.log.fatal("Failed to get entry as good user")
            assert False

        # Bind a different user who has rights
        topo.standalone.simple_bind_s(BIND_DN2, PASSWORD)
        entries = topo.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, SRCH_FILTER2)
        if entries is None or len(entries) == 0:
            topo.standalone.log.fatal("Failed to get entry as good user")
            assert False

        entries = topo.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, SRCH_FILTER2)
        if entries is None or len(entries) == 0:
            topo.standalone.log.fatal("Failed to get entry as good user (2)")
            assert False

        if run > 0:
            # Second pass
            topo.standalone.restart()

        # Reset ACI's and do the second test
        topo.standalone.log.info("Testing search does not return any entries...")
        topo.standalone.simple_bind_s(DN_DM, PASSWORD)
        suffix.set('aci', aci_list_B, ldap.MOD_REPLACE)
        time.sleep(1)

        topo.standalone.simple_bind_s(BIND_DN, PASSWORD)
        entries = topo.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, SRCH_FILTER)
        if entries and entries[0]:
            topo.standalone.log.fatal("Incorrectly got an entry returned from search 1")
            assert False

        entries = topo.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, SRCH_FILTER)
        if entries and entries[0]:
            topo.standalone.log.fatal("Incorrectly got an entry returned from search 2")
            assert False

        if run > 0:
            # Second pass
            topo.standalone.restart()

        # Bind as different user who has rights
        topo.standalone.simple_bind_s(BIND_DN2, PASSWORD)
        entries = topo.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, SRCH_FILTER2)
        if entries is None or len(entries) == 0:
            topo.standalone.log.fatal("Failed to get entry as good user")
            assert False

        entries = topo.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, SRCH_FILTER2)
        if entries is None or len(entries) == 0:
            topo.standalone.log.fatal("Failed to get entry as good user (2)")
            assert False

        entries = topo.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, SRCH_FILTER)
        if entries and entries[0]:
            topo.standalone.log.fatal("Incorrectly got an entry returned from search 1")
            assert False

        entries = topo.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, SRCH_FILTER)
        if entries and entries[0]:
            topo.standalone.log.fatal("Incorrectly got an entry returned from search 2")
            assert False

        # back to user 1
        topo.standalone.simple_bind_s(BIND_DN, PASSWORD)
        entries = topo.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, SRCH_FILTER2)
        if entries is None or len(entries) == 0:
            topo.standalone.log.fatal("Failed to get entry as user1")
            assert False

        entries = topo.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, SRCH_FILTER2)
        if entries is None or len(entries) == 0:
            topo.standalone.log.fatal("Failed to get entry as user1 (2)")
            assert False

        entries = topo.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, SRCH_FILTER)
        if entries and entries[0]:
            topo.standalone.log.fatal("Incorrectly got an entry returned from search 1")
            assert False

        entries = topo.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, SRCH_FILTER)
        if entries and entries[0]:
            topo.standalone.log.fatal("Incorrectly got an entry returned from search 2")
            assert False

    topo.standalone.log.info("Test PASSED")


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])

