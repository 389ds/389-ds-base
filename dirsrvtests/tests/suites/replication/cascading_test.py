# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2018 Red Hat, Inc.
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
from lib389._constants import *
from lib389.replica import ReplicationManager
from lib389.plugins import MemberOfPlugin
from lib389.agreement import Agreements
from lib389.idm.user import UserAccount, TEST_USER_PROPERTIES
from lib389.idm.group import Groups
from lib389.topologies import topology_m1h1c1 as topo

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

BIND_DN = 'uid=tuser1,ou=People,dc=example,dc=com'
BIND_RDN = 'tuser1'


def config_memberof(server):
    """Configure memberOf plugin and configure fractional
    to prevent total init to send memberof
    """

    memberof = MemberOfPlugin(server)
    memberof.enable()
    memberof.set_autoaddoc('nsMemberOf')
    server.restart()
    agmts = Agreements(server)
    for agmt in agmts.list():
        log.info('update %s to add nsDS5ReplicatedAttributeListTotal' % agmt.dn)
        agmt.replace_many(('nsDS5ReplicatedAttributeListTotal', '(objectclass=*) $ EXCLUDE '),
                          ('nsDS5ReplicatedAttributeList', '(objectclass=*) $ EXCLUDE memberOf'))


def test_basic_with_hub(topo):
    """Check that basic operations work in cascading replication, this includes
    testing plugins that perform internal operatons, and replicated password
    policy state attributes.

    :id: 4ac85552-45bc-477b-89a4-226dfff8c6cc
    :setup: 1 supplier, 1 hub, 1 consumer
    :steps:
        1. Enable memberOf plugin and set password account lockout settings
        2. Restart the instance
        3. Add a user
        4. Add a group
        5. Test that the replication works
        6. Add the user as a member to the group
        7. Test that the replication works
        8. Issue bad binds to update passwordRetryCount
        9. Test that replicaton works
        10. Check that passwordRetyCount was replicated
    :expectedresults:
        1. Should be a success
        2. Should be a success
        3. Should be a success
        4. Should be a success
        5. Should be a success
        6. Should be a success
        7. Should be a success
        8. Should be a success
        9. Should be a success
        10. Should be a success
    """

    repl_manager = ReplicationManager(DEFAULT_SUFFIX)
    supplier = topo.ms["supplier1"]
    consumer = topo.cs["consumer1"]
    hub = topo.hs["hub1"]

    for inst in topo:
        config_memberof(inst)
        inst.config.set('passwordlockout', 'on')
        inst.config.set('passwordlockoutduration', '60')
        inst.config.set('passwordmaxfailure', '3')
        inst.config.set('passwordIsGlobalPolicy', 'on')

    # Create user
    user1 = UserAccount(supplier, BIND_DN)
    user_props = TEST_USER_PROPERTIES.copy()
    user_props.update({'sn': BIND_RDN,
                       'cn': BIND_RDN,
                       'uid': BIND_RDN,
                       'inetUserStatus': '1',
                       'objectclass': 'extensibleObject',
                       'userpassword': PASSWORD})
    user1.create(properties=user_props, basedn=SUFFIX)

    # Create group
    groups = Groups(supplier, DEFAULT_SUFFIX)
    group = groups.create(properties={'cn': 'group'})

    # Test replication
    repl_manager.test_replication(supplier, consumer)

    # Trigger memberOf plugin by adding user to group
    group.replace('member', user1.dn)

    # Test replication once more
    repl_manager.test_replication(supplier, consumer)

    # Issue bad password to update passwordRetryCount
    try:
        supplier.simple_bind_s(user1.dn, "badpassword")
    except:
        pass

    # Test replication one last time
    supplier.simple_bind_s(DN_DM, PASSWORD)
    repl_manager.test_replication(supplier, consumer)

    # Finally check if passwordRetyCount was replicated to the hub and consumer
    user1 = UserAccount(hub, BIND_DN)
    count = user1.get_attr_val_int('passwordRetryCount')
    if count is None:
        log.fatal('PasswordRetyCount was not replicated to hub')
        assert False
    if int(count) != 1:
        log.fatal('PasswordRetyCount has unexpected value: {}'.format(count))
        assert False

    user1 = UserAccount(consumer, BIND_DN)
    count = user1.get_attr_val_int('passwordRetryCount')
    if count is None:
        log.fatal('PasswordRetyCount was not replicated to consumer')
        assert False
    if int(count) != 1:
        log.fatal('PasswordRetyCount has unexpected value: {}'.format(count))
        assert False


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])

