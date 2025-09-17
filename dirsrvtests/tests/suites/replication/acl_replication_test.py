# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
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
from lib389.idm.domain import Domain
from lib389.idm.directorymanager import DirectoryManager
from lib389.idm.account import Anonymous
from lib389.topologies import topology_m2
from lib389._constants import DEFAULT_SUFFIX, PASSWORD
from lib389.replica import ReplicationManager

pytestmark = pytest.mark.tier2

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def test_nscpentrywsi_access_control(topology_m2):
    """Test that nscpentrywsi attribute access is properly controlled by ACLs in replication.

    This test verifies nscpentrywsi attribute should only be visible to Directory Manager
    and not to regular users or anonymous binds in a replication environment.

    :id: b47869a1-8c2d-4f5e-9e6a-1b2c3d4e5f67
    :setup: Two supplier replication topology
    :steps:
        1. Enable ACL error logging on both suppliers
        2. Create a bind user on supplier1
        3. Wait for replication to supplier2
        4. Set anonymous ACI on both suppliers
        5. Create test entries on supplier1
        6. Wait for replication to supplier2
        7. Test Directory Manager can see nscpentrywsi attribute
        8. Test regular user cannot see nscpentrywsi attribute
        9. Test anonymous user cannot see nscpentrywsi attribute
        10. Verify results consistent across both suppliers
    :expectedresults:
        1. ACL logging enabled successfully
        2. Bind user created successfully
        3. Replication completed successfully
        4. Anonymous ACI set successfully
        5. Test entries created successfully
        6. Replication completed successfully
        7. Directory Manager can see nscpentrywsi attribute
        8. Regular user cannot see nscpentrywsi attribute
        9. Anonymous user cannot see nscpentrywsi attribute
        10. Results consistent across both suppliers
    """
    supplier1 = topology_m2.ms["supplier1"]
    supplier2 = topology_m2.ms["supplier2"]
    repl = ReplicationManager(DEFAULT_SUFFIX)

    log.info("Enable ACL error logging on both suppliers")
    supplier1.config.set('nsslapd-errorlog-level', '8192')
    supplier2.config.set('nsslapd-errorlog-level', '8192')

    log.info("Create bind user on supplier1")
    users = UserAccounts(supplier1, DEFAULT_SUFFIX)
    bind_user = users.create_test_user(uid=2000)
    bind_user.set('userPassword', PASSWORD)

    log.info("Wait for replication to supplier2")
    repl.test_replication(supplier1, supplier2)

    users2 = UserAccounts(supplier2, DEFAULT_SUFFIX)
    bind_user2 = users2.get('test_user_2000')
    assert bind_user2.exists()

    log.info("Set anonymous ACI on both suppliers")
    anonymous_aci = '(targetattr!="userPassword")(version 3.0; acl "Enable anonymous access"; allow (read, search, compare) userdn="ldap:///anyone";)'

    domain1 = Domain(supplier1, DEFAULT_SUFFIX)
    domain1.set('aci', anonymous_aci)

    domain2 = Domain(supplier2, DEFAULT_SUFFIX)
    domain2.set('aci', anonymous_aci)

    log.info("Create test entries on supplier1")
    test_users = []
    for i in range(3000, 3010):
        test_user = users.create_test_user(uid=i)
        test_users.append(test_user)

    log.info("Wait for replication to supplier2")
    repl.test_replication(supplier1, supplier2)

    log.info("Test Directory Manager access on supplier1")
    dm1 = DirectoryManager(supplier1)
    dm_conn1 = dm1.bind(PASSWORD)
    msgid = dm_conn1.search_ext(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 'objectclass=*', ['nscpentrywsi'])
    rtype, entries, rmsgid = dm_conn1.result2(msgid)
    nscpentrywsi_count = sum(1 for dn, attrs in entries if 'nscpentrywsi' in attrs)
    log.info(f"Directory Manager found {nscpentrywsi_count} entries with nscpentrywsi attribute")
    assert nscpentrywsi_count > 0, "Directory Manager should see nscpentrywsi attribute"
    dm_conn1.close()

    log.info("Test Directory Manager access on supplier2")
    dm2 = DirectoryManager(supplier2)
    dm_conn2 = dm2.bind(PASSWORD)
    msgid = dm_conn2.search_ext(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 'objectclass=*', ['nscpentrywsi'])
    rtype, entries, rmsgid = dm_conn2.result2(msgid)
    nscpentrywsi_count = sum(1 for dn, attrs in entries if 'nscpentrywsi' in attrs)
    log.info(f"Directory Manager found {nscpentrywsi_count} entries with nscpentrywsi attribute")
    assert nscpentrywsi_count > 0, "Directory Manager should see nscpentrywsi attribute"
    dm_conn2.close()

    log.info("Test regular user access on supplier1")
    user_conn1 = bind_user.bind(PASSWORD)
    msgid = user_conn1.search_ext(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 'objectclass=*', ['nscpentrywsi'])
    rtype, entries, rmsgid = user_conn1.result2(msgid)
    nscpentrywsi_count = sum(1 for dn, attrs in entries if 'nscpentrywsi' in attrs)
    log.info(f"Regular user found {nscpentrywsi_count} entries with nscpentrywsi attribute")
    assert nscpentrywsi_count == 0, "Regular user should not see nscpentrywsi attribute"
    user_conn1.close()

    log.info("Test regular user access on supplier2")
    user_conn2 = bind_user2.bind(PASSWORD)
    msgid = user_conn2.search_ext(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 'objectclass=*', ['nscpentrywsi'])
    rtype, entries, rmsgid = user_conn2.result2(msgid)
    nscpentrywsi_count = sum(1 for dn, attrs in entries if 'nscpentrywsi' in attrs)
    log.info(f"Regular user found {nscpentrywsi_count} entries with nscpentrywsi attribute")
    assert nscpentrywsi_count == 0, "Regular user should not see nscpentrywsi attribute"
    user_conn2.close()

    log.info("Test anonymous access on supplier1")
    anon1 = Anonymous(supplier1)
    anon_conn1 = anon1.bind()
    msgid = anon_conn1.search_ext(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 'objectclass=*', ['nscpentrywsi'])
    rtype, entries, rmsgid = anon_conn1.result2(msgid)
    nscpentrywsi_count = sum(1 for dn, attrs in entries if 'nscpentrywsi' in attrs)
    log.info(f"Anonymous user found {nscpentrywsi_count} entries with nscpentrywsi attribute")
    assert nscpentrywsi_count == 0, "Anonymous user should not see nscpentrywsi attribute"
    anon_conn1.close()

    log.info("Test anonymous access on supplier2")
    anon2 = Anonymous(supplier2)
    anon_conn2 = anon2.bind()
    msgid = anon_conn2.search_ext(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 'objectclass=*', ['nscpentrywsi'])
    rtype, entries, rmsgid = anon_conn2.result2(msgid)
    nscpentrywsi_count = sum(1 for dn, attrs in entries if 'nscpentrywsi' in attrs)
    log.info(f"Anonymous user found {nscpentrywsi_count} entries with nscpentrywsi attribute")
    assert nscpentrywsi_count == 0, "Anonymous user should not see nscpentrywsi attribute"
    anon_conn2.close()

    log.info("nscpentrywsi access control test completed successfully")


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
