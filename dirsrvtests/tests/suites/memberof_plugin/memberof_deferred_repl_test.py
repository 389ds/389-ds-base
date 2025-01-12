# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2024 Red Hat, Inc.
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
from lib389._constants import DEFAULT_SUFFIX, AGMT_ATTR_LIST
from lib389.topologies import topology_m2 as topo_m2
from lib389.agreement import Agreements
from lib389.replica import Replicas
from lib389.plugins import MemberOfPlugin
from lib389.idm.user import UserAccounts
from lib389.idm.group import Groups

log = logging.getLogger(__name__)


def test_repl_deferred_updates(topo_m2):
    """Test memberOf plugin deferred updates work in different types of
    replicated environments

    :id: f7b20a60-7e52-411d-8693-cd7235df8e84
    :setup: 2 Supplier Instances
    :steps:
        1. Enable memberOf with deferred updates on Supplier 1
        2. Test deferred updates are replicated to Supplier 2
        3. Enable memberOf with deferred updates on Supplier 2
        4. Edit both agreements to strip memberOf updates
        5. Test that supplier 2 will update memberOf after receving replicated
           group update
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
    """
    s1 = topo_m2.ms["supplier1"]
    s2 = topo_m2.ms["supplier2"]

    # Setup - create users and groups
    s1_users = UserAccounts(s1, DEFAULT_SUFFIX)
    s2_users = UserAccounts(s2, DEFAULT_SUFFIX)
    user_dn_list = []
    for idx in range(5):
        USER_NAME = f'user_{idx}'
        user = s1_users.create(properties={
            'uid': USER_NAME,
            'sn': USER_NAME,
            'cn': USER_NAME,
            'uidNumber': f'{idx}',
            'gidNumber': f'{idx}',
            'homeDirectory': f'/home/{USER_NAME}'
        })
        user_dn_list.append(user.dn)

    groups = Groups(s1, DEFAULT_SUFFIX)
    group = groups.create(properties={'cn': 'group'})

    #
    # Configure MO plugin Supplier 1, we're testing that deferred updates are
    # replicated
    #
    memberof = MemberOfPlugin(s1)
    memberof.enable()
    memberof.set_autoaddoc('nsMemberOf')
    memberof.set_memberofdeferredupdate('on')
    #s1.config.set('nsslapd-errorlog','/dev/shm/slapd-supplier1/errors')
    #s1.setLogLevel(65536)
    #s1.setAccessLogLevel(260)
    #s1.config.set('nsslapd-plugin-logging', 'on')
    #s1.config.set('nsslapd-auditlog-logging-enabled',  'on')
    s1.restart()

    # Update group
    for dn in user_dn_list:
        group.add('member', dn)

    # Check memberOf was added to all users in S1 and S2
    for count in range(10):
        log.debug(f"Phase 1 - pass: {count}")
        all_good = True
        time.sleep(2)
        # Check supplier 1
        users_s1 = s1_users.list()
        for user in users_s1:
            memberof = user.get_attr_vals('memberof')
            log.debug("Checking %s" % user.dn)
            log.debug("memberof: %s" % str(memberof))

        for user in users_s1:
            if not user.present('memberof'):
                log.debug("missing  memberof: %s !!!!" % user.dn)
                all_good = False
                break
            else:
                log.debug("Checking memberof: %s" % user.dn)
        if not all_good:
            continue

        # Supplier 1 is good, now check Supplier 2 ...
        users_s2 = s2_users.list()
        for user in users_s2:
            if not user.present('memberof'):
                all_good = False
                break

        # If we are all good then we can break out
        if all_good:
            break

    assert all_good

    #
    # New test that when a supplier receives a group update (memberOf is not
    # replicated in this test) that it updates memberOf locally from the
    # replicated group update
    #

    # Exclude memberOf from replication
    replica = Replicas(s1).get(DEFAULT_SUFFIX)
    agmt = Agreements(s1, replica.dn).list()[0]
    agmt.replace(AGMT_ATTR_LIST, '(objectclass=*) $ EXCLUDE memberOf')
    agmt = Agreements(s2, replica.dn).list()[0]
    agmt.replace(AGMT_ATTR_LIST, '(objectclass=*) $ EXCLUDE memberOf')

    # enable MO plugin on Supplier 2
    memberof = MemberOfPlugin(s2)
    memberof.enable()
    memberof.set_autoaddoc('nsMemberOf')
    memberof.set_memberofdeferredupdate('on')
    s1.restart()
    s2.restart()

    # Remove members
    group.remove_all('member')

    # Check memberOf is removed from users on S1 and S2
    all_good = True
    for count in range(10):
        log.debug(f"Phase 2 - pass: {count}")
        all_good = True
        time.sleep(2)
        # Check supplier 1
        users_s1 = s1_users.list()
        for user in users_s1:
            if user.present('memberof'):
                all_good = False
                break
        if not all_good:
            continue

        # Supplier 1 is good, now check Supplier 2 ...
        users_s2 = s2_users.list()
        for user in users_s2:
            if user.present('memberof'):
                all_good = False
                break

        # If we are all good then we can break out
        if all_good:
            break

    assert all_good


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])

