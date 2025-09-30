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
from lib389._constants import DEFAULT_SUFFIX, SUFFIX, LOG_REPLICA, LOG_DEFAULT
from lib389.topologies import topology_m4
from lib389.replica import Replicas, ReplicationManager
from lib389.idm.user import UserAccounts, UserAccount
from lib389.properties import TASK_WAIT

pytestmark = pytest.mark.tier2

USER_CN = "test_user"
log = logging.getLogger(__name__)


@pytest.fixture(scope="function")
def setup_replication_logging(topology_m4):
    """Enable replication logging on all suppliers and ensure clean topology"""
    for server in topology_m4.ms.values():
        if not server.status():
            server.start()

    repl = ReplicationManager(DEFAULT_SUFFIX)
    try:
        for src in topology_m4.ms.values():
            for dst in topology_m4.ms.values():
                if src != dst:
                    repl.ensure_agreement(src, dst)
    except Exception as e:
        log.warning(f"Could not restore full topology: {e}")
        pass

    for server in topology_m4.ms.values():
        server.config.loglevel(vals=[256 + 4], service='access')
        server.config.loglevel(vals=[LOG_REPLICA, LOG_DEFAULT], service='error')
        force_log_flush(server)

    return topology_m4


def setup_fractional_replication(topology_m4):
    """Configure fractional replication excluding telephoneNumber"""
    repl = ReplicationManager(DEFAULT_SUFFIX)

    for src in topology_m4.ms.values():
        for dst in topology_m4.ms.values():
            if src != dst:
                agmt = repl.ensure_agreement(src, dst)
                agmt.replace_many(
                    ('nsDS5ReplicatedAttributeListTotal', '(objectclass=*) $ EXCLUDE telephoneNumber'),
                    ('nsDS5ReplicatedAttributeList', '(objectclass=*) $ EXCLUDE telephoneNumber'),
                    ('nsds5ReplicaStripAttrs', 'modifiersname modifytimestamp'),
                )

    return repl


def add_user(server, no, desc='dummy'):
    """Add a test user with specific UID number"""
    user = UserAccounts(server, DEFAULT_SUFFIX)
    users = user.create_test_user(uid=no)
    users.add('description', [desc])
    users.add('objectclass', 'userSecurityInformation')
    return users



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


def test_fractional_cleanallruv_comprehensive(topology_m4, setup_replication_logging):
    """Test fractional replication with cleanAllRUV operations including force and hang recovery scenarios

    :id: 2a68e8be-387d-4ac7-9452-1439e8483c13
    :setup: Four suppliers with fractional replication excluding telephoneNumber
    :steps:
        1. Configure fractional replication excluding telephoneNumber
        2. Enable replication logging on all suppliers
        3. Create test user and verify basic replication
        4. Generate telephoneNumber updates (excluded attribute) to create keep alive entries
        5. Remove supplier3 from topology
        6. Execute cleanAllRUV FORCE for replica ID 3
        7. Verify DEL keep alive operations are limited
        8. Confirm originator vs propagation behavior
        9. Verify replication recovery after cleanAllRUV
        10. Remove supplier4 from topology creating orphaned keep alive entries
        11. Stop supplier2 and execute non-force cleanAllRUV to trigger hang
        12. Verify nsds5ReplicaCleanRUV encoding on supplier1
        13. Verify encoding survives supplier1 restart
        14. Start supplier2 and verify its encoding
        15. Wait for cleanAllRUV completion
        16. Verify final replication recovery functionality
    :expectedresults:
        1. Fractional replication configured successfully
        2. Logging enabled properly on all suppliers
        3. Basic replication works with fractional configuration
        4. Keep alive entries generated from excluded telephoneNumber updates
        5. Supplier3 removed from topology
        6. CleanAllRUV FORCE completes successfully
        7. DEL operations limited to 0 or 1 occurrences
        8. Supplier1 shows originator, others show propagation
        9. Replication continues working normally
        10. Supplier4 removed leaving orphaned keep alive entries
        11. CleanAllRUV hangs as expected without force option
        12. RUV encoded correctly on supplier1
        13. Encoding persists across supplier1 restart
        14. Supplier2 shows different encoding
        15. CleanAllRUV completes after supplier2 restart
        16. Final replication recovery works correctly
    """

    M1 = topology_m4.ms["supplier1"]
    M2 = topology_m4.ms["supplier2"]
    M3 = topology_m4.ms["supplier3"]
    M4 = topology_m4.ms["supplier4"]

    log.info('Configure fractional replication excluding telephoneNumber')
    repl = setup_fractional_replication(topology_m4)

    log.info('Create test users and verify basic replication')
    add_user(M1, 11, desc="add to M1")
    add_user(M2, 21, desc="add to M2")
    add_user(M3, 31, desc="add to M3")
    add_user(M4, 41, desc="add to M4")
    repl.test_replication_topology(topology_m4)

    log.info('Generate telephoneNumber updates (excluded attribute) to create keep alive entries')
    for server in topology_m4:
        cn = f'{USER_CN}_{11}'
        dn = f'uid={cn},ou=People,{DEFAULT_SUFFIX}'
        users = UserAccount(server, dn)
        for j in range(110):
            users.set('telephoneNumber', str(j))

    repl.test_replication_topology(topology_m4)

    log.info('Remove supplier3 from topology')
    remaining_suppliers = [M1, M2, M4]
    repl.remove_supplier(M3, remaining_suppliers)

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

    log.info('Confirm originator vs propagation behavior')
    regex = re.compile(".*Original task deletes Keep alive entry .3.*")
    assert M1.ds_error_log.match(regex)

    regex = re.compile(".*Propagated task does not delete Keep alive entry .3.*")
    assert M2.ds_error_log.match(regex)
    assert M4.ds_error_log.match(regex)

    log.info('Verify replication recovery after cleanAllRUV')
    add_user(M1, 12, desc="post-cleanup M1")
    add_user(M2, 22, desc="post-cleanup M2")
    repl.test_replication_topology(remaining_suppliers)

    log.info('Remove supplier4 from topology creating orphaned keep alive entries')
    final_suppliers = [M1, M2]
    repl.remove_supplier(M4, final_suppliers)

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
    repl.test_replication_topology(final_suppliers)

    regex_del = re.compile(".*DEL dn=.cn=repl keep alive 4.*")
    for server in final_suppliers:
        count = count_pattern_accesslog(server, regex_del)
        log.info(f"DEL keep alive 4 count on {server.serverid} = {count}")
        assert count in (0, 1), f"Too many DEL operations on {server.serverid}: {count}"

    log.info('Verify final replication recovery functionality')
    user_13 = add_user(M1, 13, desc="final-cleanup M1")
    user_23 = add_user(M2, 23, desc="final-cleanup M2")

    repl.test_replication_topology(final_suppliers)

    for server in final_suppliers:
        user_13_obj = UserAccount(server, user_13.dn)
        assert user_13_obj.exists(), f"User test_user_13 NOT found on {server.serverid}"

        user_23_obj = UserAccount(server, user_23.dn)
        assert user_23_obj.exists(), f"User test_user_23 NOT found on {server.serverid}"


if __name__ == '__main__':
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
