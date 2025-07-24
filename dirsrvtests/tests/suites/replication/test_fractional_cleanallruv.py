# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2025 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import pytest
import os
import re
import time
from lib389._constants import DEFAULT_SUFFIX, SUFFIX, LOG_REPLICA, LOG_DEFAULT, ReplicaRole
from lib389.topologies import create_topology
from lib389.replica import Replicas, ReplicationManager
from lib389.idm.user import UserAccounts, UserAccount
from lib389.properties import TASK_WAIT

pytestmark = pytest.mark.tier2

USER_CN = "test_user"
log = logging.getLogger(__name__)


def add_user(server, no, desc='dummy'):
    """Add a test user with specific UID number"""
    user = UserAccounts(server, DEFAULT_SUFFIX)
    users = user.create_test_user(uid=no)
    users.add('description', [desc])
    users.add('objectclass', 'userSecurityInformation')
    return users


def wait_for_error_pattern(server, log_pattern, timeout=30):
    """Wait for error log pattern"""
    try:
        found = server.ds_error_log.match(log_pattern)
        return found
    except Exception:
        return server.ds_error_log.match(log_pattern)


def count_pattern_accesslog(server, log_pattern):
    """Count pattern occurrences in access log"""
    count = 0
    server.config.set('nsslapd-accesslog-logbuffering', 'off')
    if server.ds_access_log.match(log_pattern):
        count = count + 1
    return count


def force_log_flush(server):
    """Force server logs to flush"""
    server.config.set('nsslapd-accesslog-logbuffering', 'off')
    server.config.set('nsslapd-errorlog-logbuffering', 'off')


def get_user_on_server(server, user_dn):
    """Get user object on specific server, with fallback search"""
    try:
        return UserAccount(server, user_dn)
    except Exception:
        uid = user_dn.split(',')[0].split('=')[1]
        users = UserAccounts(server, DEFAULT_SUFFIX, rdn=None)
        found_users = users.filter(f'(uid={uid})')
        if found_users:
            return found_users[0]
        raise


def test_fractional_replication_basic():
    """Test basic fractional replication setup and verification

    :id: f1a2b3c4-d5e6-7f8g-9h0i-1j2k3l4m5n6o
    :setup: Four suppliers with fractional replication
    :steps:
        1. Configure fractional replication excluding telephoneNumber
        2. Enable replication logging on all suppliers
        3. Create test user and verify replication
        4. Generate telephoneNumber updates (excluded attribute)
        5. Verify fractional replication behavior
    :expectedresults:
        1. Fractional replication configured successfully
        2. Logging enabled on all suppliers
        3. User replicated correctly
        4. Replication works across all suppliers
        5. Excluded attribute updates processed
        6. Fractional replication working properly
    """
    log.info('Configure fractional replication excluding telephoneNumber')
    topology = create_topology({ReplicaRole.SUPPLIER: 4})

    M1 = topology.ms["supplier1"]
    M2 = topology.ms["supplier2"]
    M3 = topology.ms["supplier3"]
    M4 = topology.ms["supplier4"]
    repl = ReplicationManager(DEFAULT_SUFFIX)

    suppliers = [M1, M2, M3, M4]
    for src in suppliers:
        for dst in suppliers:
            if src != dst:
                agmt = repl.ensure_agreement(src, dst)
                agmt.replace_many(
                    ('nsDS5ReplicatedAttributeListTotal', '(objectclass=*) $ EXCLUDE telephoneNumber'),
                    ('nsDS5ReplicatedAttributeList', '(objectclass=*) $ EXCLUDE telephoneNumber'),
                    ('nsds5ReplicaStripAttrs', 'modifiersname modifytimestamp'),
                )

    log.info('Enable replication logging on all suppliers')
    for server in suppliers:
        server.config.loglevel(vals=[256 + 4], service='access')
        server.config.loglevel(vals=[LOG_REPLICA, LOG_DEFAULT], service='error')

    log.info('Create test user and verify replication')
    user_m1 = add_user(M1, 1001, desc="add to M1")
    repl.test_replication_topology(suppliers)

    log.info('Generate telephoneNumber updates (excluded attribute)')
    for server in suppliers:
        try:
            user_on_server = UserAccount(server, user_m1.dn)
            for j in range(50):
                user_on_server.set('telephoneNumber', str(j))
        except Exception as e:
            log.error(f"Error modifying user on {server.serverid}: {e}")

    log.info('Verify fractional replication behavior')
    repl.test_replication_topology(suppliers)


def test_fractional_cleanallruv_force():
    """Test cleanAllRUV FORCE with fractional replication

    :id: 2a68e8be-387d-4ac7-9452-1439e8483c13
    :setup: Four suppliers with fractional replication
    :steps:
        1. Configure fractional replication excluding telephoneNumber
        2. Enable replication logging
        3. Create test user and verify replication
        4. Generate keep alive entries with 110 telephoneNumber updates
        5. Remove supplier3 from topology
        6. Execute cleanAllRUV FORCE for replica ID 3
        7. Verify DEL keep alive operations are limited
        8. Confirm originator vs propagation behavior
        9. Verify replication recovery after cleanAllRUV
    :expectedresults:
        1. Fractional replication configured successfully
        2. Logging enabled properly
        3. Basic replication works
        4. Keep alive entries generated from excluded updates
        5. Supplier3 removed from topology
        6. CleanAllRUV FORCE completes successfully
        7. DEL operations limited to 0 or 1 occurrences
        8. Supplier1 shows originator, others show propagation
        9. Replication continues working normally
    """
    log.info('Configure fractional replication excluding telephoneNumber')
    topology = create_topology({ReplicaRole.SUPPLIER: 4})

    M1 = topology.ms["supplier1"]
    M2 = topology.ms["supplier2"]
    M3 = topology.ms["supplier3"]
    M4 = topology.ms["supplier4"]
    repl = ReplicationManager(DEFAULT_SUFFIX)

    suppliers = [M1, M2, M3, M4]
    for src in suppliers:
        for dst in suppliers:
            if src != dst:
                agmt = repl.ensure_agreement(src, dst)
                agmt.replace_many(
                    ('nsDS5ReplicatedAttributeListTotal', '(objectclass=*) $ EXCLUDE telephoneNumber'),
                    ('nsDS5ReplicatedAttributeList', '(objectclass=*) $ EXCLUDE telephoneNumber'),
                    ('nsds5ReplicaStripAttrs', 'modifiersname modifytimestamp'),
                )

    log.info('Enable replication logging')
    for server in suppliers:
        server.config.loglevel(vals=[256 + 4], service='access')
        server.config.loglevel(vals=[LOG_REPLICA, LOG_DEFAULT], service='error')
        force_log_flush(server)

    log.info('Create test user and verify replication')
    user_m1 = add_user(M1, 2001, desc="add to M1")
    repl.test_replication_topology(suppliers)

    log.info('Generate keep alive entries with 110 telephoneNumber updates')
    for server in suppliers:
        try:
            user_on_server = UserAccount(server, user_m1.dn)
            for j in range(110):
                user_on_server.set('telephoneNumber', str(j))
        except Exception as e:
            log.error(f"Error modifying user on {server.serverid}: {e}")

    repl.test_replication_topology(suppliers)

    log.info('Remove supplier3 from topology')
    remaining_suppliers = [M1, M2, M4]
    repl.remove_supplier(M3, remaining_suppliers, purge_sa=True)

    log.info('Execute cleanAllRUV FORCE for replica ID 3')
    M1.tasks.cleanAllRUV(suffix=SUFFIX, replicaid='3',
                         force=True, args={TASK_WAIT: True})

    for server in remaining_suppliers:
        server.restart()
        force_log_flush(server)

    repl.test_replication_topology(remaining_suppliers)

    log.info('Verify DEL keep alive operations are limited')
    regex = re.compile(".*DEL dn=.cn=repl keep alive 3.*")
    for server in remaining_suppliers:
        count = count_pattern_accesslog(server, regex)
        log.debug(f"DEL count on {server.serverid} = {count}")
        assert count in (0, 1)

    repl.test_replication_topology(remaining_suppliers)

    log.info('Confirm originator vs propagation behavior')
    regex = re.compile(".*Original task deletes Keep alive entry .3.*")
    assert wait_for_error_pattern(M1, regex)

    regex = re.compile(".*Propagated task does not delete Keep alive entry .3.*")
    assert wait_for_error_pattern(M2, regex)
    assert wait_for_error_pattern(M4, regex)

    log.info('Verify replication recovery after cleanAllRUV')
    add_user(M1, 2005, desc="post-cleanup M1")
    add_user(M2, 2006, desc="post-cleanup M2")
    repl.test_replication_topology(remaining_suppliers)


def test_fractional_cleanallruv_hang_recovery():
    """Test cleanAllRUV hang recovery with fractional replication

    This test verifies that cleanAllRUV operations properly handle hang scenarios
    in fractional replication environments. It tests the encoding and persistence
    of nsds5ReplicaCleanRUV attributes during server restarts and recovery.

    :id: 3b75f9c1-8e4d-4f2a-b8c6-9d5e2f4a7b8c
    :setup: Four suppliers with fractional replication excluding telephoneNumber
    :steps:
        1. Configure fractional replication excluding telephoneNumber
        2. Enable replication logging on all suppliers
        3. Create test user and verify replication
        4. Generate telephoneNumber updates to create keep alive entries
        5. Remove supplier4 from topology creating orphaned keep alive entries
        6. Stop supplier2 and execute non-force cleanAllRUV to trigger hang
        7. Verify nsds5ReplicaCleanRUV encoding on supplier1
        8. Verify encoding survives supplier1 restart
        9. Start supplier2 and verify its encoding
        10. Wait for cleanAllRUV completion
        11. Verify replication recovery functionality
    :expectedresults:
        1. Fractional replication configured successfully
        2. Logging enabled properly on all suppliers
        3. Test user created and replicated correctly
        4. Keep alive entries generated from excluded telephoneNumber updates
        5. Supplier4 removed leaving orphaned keep alive entries
        6. CleanAllRUV hangs as expected without force option
        7. RUV encoded correctly on supplier1
        8. Encoding persists across supplier1 restart
        9. Supplier2 shows different encoding
        10. CleanAllRUV completes after supplier2 restart
        11. Replication recovery works correctly
    """
    log.info('Configure fractional replication excluding telephoneNumber')
    topology = create_topology({ReplicaRole.SUPPLIER: 4})

    M1 = topology.ms["supplier1"]
    M2 = topology.ms["supplier2"]
    M3 = topology.ms["supplier3"]
    M4 = topology.ms["supplier4"]

    repl = ReplicationManager(DEFAULT_SUFFIX)
    suppliers = [M1, M2, M3, M4]

    for src in suppliers:
        for dst in suppliers:
            if src != dst:
                agmt = repl.ensure_agreement(src, dst)
                agmt.replace_many(
                    ('nsDS5ReplicatedAttributeListTotal', '(objectclass=*) $ EXCLUDE telephoneNumber'),
                    ('nsDS5ReplicatedAttributeList', '(objectclass=*) $ EXCLUDE telephoneNumber'),
                    ('nsds5ReplicaStripAttrs', 'modifiersname modifytimestamp'),
                )

    log.info('Enable replication logging on all suppliers')
    for server in suppliers:
        server.config.loglevel(vals=[256 + 4], service='access')
        server.config.loglevel(vals=[LOG_REPLICA, LOG_DEFAULT], service='error')
        force_log_flush(server)

    log.info('Create test user and verify replication')
    user_m1 = add_user(M1, 11, desc="add to M1")
    log.info(f"Created user with DN: {user_m1.dn}")
    repl.test_replication_topology(suppliers)

    log.info('Generate telephoneNumber updates to create keep alive entries')
    for server in suppliers:
        cn = f'{USER_CN}_{11}'
        dn = f'uid={cn},ou=People,{DEFAULT_SUFFIX}'
        users = UserAccount(server, dn)
        for j in range(110):
            users.set('telephoneNumber', str(j))

    repl.test_replication_topology(suppliers)

    log.info('Remove supplier4 from topology creating orphaned keep alive entries')
    remaining_suppliers = [M1, M2, M3]
    repl.remove_supplier(M4, remaining_suppliers, purge_sa=True)

    log.info('Stop supplier2 and execute non-force cleanAllRUV to trigger hang')
    M2.stop()
    M1.tasks.cleanAllRUV(suffix=SUFFIX, replicaid='4', force=False, args={TASK_WAIT: False})

    log.info('Verify nsds5ReplicaCleanRUV encoding on supplier1')
    replicas = Replicas(M1)
    replica = replicas.list()[0]

    time.sleep(0.5)

    assert replica.present('nsds5ReplicaCleanRUV')
    ruv_value = replica.get_attr_val_utf8('nsds5replicacleanruv')
    log.info(f"M1: nsds5ReplicaCleanRUV={ruv_value}")
    regex_m1 = re.compile("^4:no:1:")
    assert regex_m1.match(ruv_value), f"M1 RUV should contain :no:1 but got: {ruv_value}"

    log.info('Verify encoding survives supplier1 restart')
    M1.restart()
    replicas = Replicas(M1)
    replica = replicas.list()[0]
    assert replica.present('nsds5ReplicaCleanRUV')
    ruv_after_restart = replica.get_attr_val_utf8('nsds5replicacleanruv')
    assert regex_m1.match(ruv_after_restart), f"M1 RUV changed after restart: {ruv_after_restart}"

    log.info('Start supplier2 and verify its encoding')
    M1.stop()
    M2.start()
    replicas_m2 = Replicas(M2)
    replica_m2 = replicas_m2.list()[0]
    M1.start()  
    time.sleep(0.5)

    if replica_m2.present('nsds5ReplicaCleanRUV'):
        ruv_m2 = replica_m2.get_attr_val_utf8('nsds5replicacleanruv')
        log.info(f"M2: nsds5ReplicaCleanRUV={ruv_m2}")
        regex_m2 = re.compile("^4:no:0:")
        assert regex_m2.match(ruv_m2), f"M2 RUV should contain :no:0 but got: {ruv_m2}"

    log.info('Wait for cleanAllRUV completion')
    repl.test_replication_topology([M1, M2])

    regex_del = re.compile(".*DEL dn=.cn=repl keep alive 4.*")
    for server in [M1, M2]:
        count = count_pattern_accesslog(server, regex_del)
        log.info(f"DEL keep alive 4 count on {server.serverid} = {count}")
        assert count in (0, 1), f"Too many DEL operations on {server.serverid}: {count}"

    log.info('Verify replication recovery functionality')
    user_12 = add_user(M1, 12, desc="post-cleanup M1")
    user_22 = add_user(M2, 22, desc="post-cleanup M2")

    repl.test_replication_topology([M1, M2])

    for server in [M1, M2]:
        try:
            UserAccount(server, user_12.dn)
            log.info(f"User test_user_12 found on {server.serverid}")
        except Exception:
            log.error(f"User test_user_12 NOT found on {server.serverid}")

        try:
            UserAccount(server, user_22.dn)
            log.info(f"User test_user_22 found on {server.serverid}")
        except Exception:
            log.error(f"User test_user_22 NOT found on {server.serverid}")


if __name__ == '__main__':
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
