import time
import ldap
import logging
import pytest
import os
import re
from lib389._constants import DEFAULT_SUFFIX, SUFFIX, LOG_REPLICA, LOG_DEFAULT
from lib389.config import Config
from lib389 import DirSrv, Entry
from lib389.topologies import topology_m4 as topo
from lib389.replica import Replicas, ReplicationManager
from lib389.idm.user import UserAccounts, UserAccount
from lib389.tasks import *
from lib389.utils import *

pytestmark = pytest.mark.tier2

USER_CN = "test_user"


def add_user(server, no, desc='dummy'):
    user = UserAccounts(server, DEFAULT_SUFFIX)
    users = user.create_test_user(uid=no)
    users.add('description', [desc])
    users.add('objectclass', 'userSecurityInformation')


def pattern_errorlog(server, log_pattern):
    for i in range(10):
        time.sleep(5)
        found = server.ds_error_log.match(log_pattern)
        if found == '' or found:
            return found
            break


def fractional_server_to_replica(server, replica):
    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.ensure_agreement(server, replica)
    replica_server = Replicas(server).get(DEFAULT_SUFFIX)
    agmt_server = replica_server.get_agreements().list()[0]
    agmt_server.replace_many(
        ('nsDS5ReplicatedAttributeListTotal', '(objectclass=*) $ EXCLUDE telephoneNumber'),
        ('nsDS5ReplicatedAttributeList', '(objectclass=*) $ EXCLUDE telephoneNumber'),
        ('nsds5ReplicaStripAttrs', 'modifiersname modifytimestamp'),
    )


def count_pattern_accesslog(server, log_pattern):
    count = 0
    server.config.set('nsslapd-accesslog-logbuffering', 'off')
    if server.ds_access_log.match(log_pattern):
        count = count + 1

    return count


def test_ticket_49463(topo):
    """Specify a test case purpose or name here

    :id: 2a68e8be-387d-4ac7-9452-1439e8483c13
    :setup: Fill in set up configuration here
    :steps:
        1. Enable fractional replication
        2. Enable replication logging
        3. Check that replication is working fine
        4. Generate skipped updates to create keep alive entries
        5. Remove M3 from the topology
        6. issue cleanAllRuv FORCE that will run on M1 then propagated M2 and M4
        7. Check that Number DEL keep alive '3' is <= 1
        8. Check M1 is the originator of cleanAllRuv and M2/M4 the propagated ones
        9. Check replication M1,M2 and M4 can recover
        10. Remove M4 from the topology
        11. Issue cleanAllRuv not force  while M2 is stopped (that hangs the cleanAllRuv)
        12. Check that nsds5ReplicaCleanRUV is correctly encoded on M1 (last value: 1)
        13. Check that nsds5ReplicaCleanRUV encoding survives M1 restart
        14. Check that nsds5ReplicaCleanRUV encoding is valid on M2 (last value: 0)
        15. Check that (for M4 cleanAllRUV) M1 is Originator and M2 propagation
    :expectedresults:
        1. No report of failure when the RUV is updated
    """

    # Step 1 - Configure fractional (skip telephonenumber) replication
    M1 = topo.ms["supplier1"]
    M2 = topo.ms["supplier2"]
    M3 = topo.ms["supplier3"]
    M4 = topo.ms["supplier4"]
    repl = ReplicationManager(DEFAULT_SUFFIX)
    fractional_server_to_replica(M1, M2)
    fractional_server_to_replica(M1, M3)
    fractional_server_to_replica(M1, M4)

    fractional_server_to_replica(M2, M1)
    fractional_server_to_replica(M2, M3)
    fractional_server_to_replica(M2, M4)

    fractional_server_to_replica(M3, M1)
    fractional_server_to_replica(M3, M2)
    fractional_server_to_replica(M3, M4)

    fractional_server_to_replica(M4, M1)
    fractional_server_to_replica(M4, M2)
    fractional_server_to_replica(M4, M3)

    # Step 2 - enable internal op logging and replication debug
    for i in (M1, M2, M3, M4):
        i.config.loglevel(vals=[256 + 4], service='access')
        i.config.loglevel(vals=[LOG_REPLICA, LOG_DEFAULT], service='error')

    # Step 3 - Check that replication is working fine
    add_user(M1, 11, desc="add to M1")
    add_user(M2, 21, desc="add to M2")
    add_user(M3, 31, desc="add to M3")
    add_user(M4, 41, desc="add to M4")

    for i in (M1, M2, M3, M4):
        for j in (M1, M2, M3, M4):
            if i == j:
                continue
            repl.wait_for_replication(i, j)

    # Step 4 - Generate skipped updates to create keep alive entries
    for i in (M1, M2, M3, M4):
        cn = '%s_%d' % (USER_CN, 11)
        dn = 'uid=%s,ou=People,%s' % (cn, SUFFIX)
        users = UserAccount(i, dn)
        for j in range(110):
            users.set('telephoneNumber', str(j))

    # Step 5 - Remove M3 from the topology
    M3.stop()
    M1.agreement.delete(suffix=SUFFIX, consumer_host=M3.host, consumer_port=M3.port)
    M2.agreement.delete(suffix=SUFFIX, consumer_host=M3.host, consumer_port=M3.port)
    M4.agreement.delete(suffix=SUFFIX, consumer_host=M3.host, consumer_port=M3.port)

    # Step 6 - Then issue cleanAllRuv FORCE that will run on M1, M2 and M4
    M1.tasks.cleanAllRUV(suffix=SUFFIX, replicaid='3',
                         force=True, args={TASK_WAIT: True})

    # Step 7 - Count the number of received DEL of the keep alive 3
    for i in (M1, M2, M4):
        i.restart()
    regex = re.compile(".*DEL dn=.cn=repl keep alive 3.*")
    for i in (M1, M2, M4):
        count = count_pattern_accesslog(M1, regex)
        log.debug("count on %s = %d" % (i, count))

        # check that DEL is replicated once (If DEL is kept in the fix)
        # check that DEL is is not replicated (If DEL is finally no long done in the fix)
        assert ((count == 1) or (count == 0))

    # Step 8 - Check that M1 is Originator of cleanAllRuv and M2, M4 propagation
    regex = re.compile(".*Original task deletes Keep alive entry .3.*")
    assert pattern_errorlog(M1, regex)

    regex = re.compile(".*Propagated task does not delete Keep alive entry .3.*")
    assert pattern_errorlog(M2, regex)
    assert pattern_errorlog(M4, regex)

    # Step 9 - Check replication M1,M2 and M4 can recover
    add_user(M1, 12, desc="add to M1")
    add_user(M2, 22, desc="add to M2")
    for i in (M1, M2, M4):
        for j in (M1, M2, M4):
            if i == j:
                continue
            repl.wait_for_replication(i, j)

    # Step 10 - Remove M4 from the topology
    M4.stop()
    M1.agreement.delete(suffix=SUFFIX, consumer_host=M4.host, consumer_port=M4.port)
    M2.agreement.delete(suffix=SUFFIX, consumer_host=M4.host, consumer_port=M4.port)

    # Step 11 - Issue cleanAllRuv not force  while M2 is stopped (that hangs the cleanAllRuv)
    M2.stop()
    M1.tasks.cleanAllRUV(suffix=SUFFIX, replicaid='4',
                         force=False, args={TASK_WAIT: False})

    # Step 12
    # CleanAllRuv is hanging waiting for M2 to restart
    # Check that nsds5ReplicaCleanRUV is correctly encoded on M1
    replicas = Replicas(M1)
    replica = replicas.list()[0]
    time.sleep(0.5)
    replica.present('nsds5ReplicaCleanRUV')
    log.info("M1: nsds5ReplicaCleanRUV=%s" % replica.get_attr_val_utf8('nsds5replicacleanruv'))
    regex = re.compile("^4:.*:no:1$")
    assert regex.match(replica.get_attr_val_utf8('nsds5replicacleanruv'))

    # Step 13
    # Check that it encoding survives restart
    M1.restart()
    assert replica.present('nsds5ReplicaCleanRUV')
    assert regex.match(replica.get_attr_val_utf8('nsds5replicacleanruv'))

    # Step 14 - Check that nsds5ReplicaCleanRUV encoding is valid on M2
    M1.stop()
    M2.start()
    replicas = Replicas(M2)
    replica = replicas.list()[0]
    M1.start()
    time.sleep(0.5)
    if replica.present('nsds5ReplicaCleanRUV'):
        log.info("M2: nsds5ReplicaCleanRUV=%s" % replica.get_attr_val_utf8('nsds5replicacleanruv'))
        regex = re.compile("^4:.*:no:0$")
        assert regex.match(replica.get_attr_val_utf8('nsds5replicacleanruv'))

    # time to run cleanAllRuv
    for i in (M1, M2):
        for j in (M1, M2):
            if i == j:
                continue
            repl.wait_for_replication(i, j)

    # Step 15 - Check that M1 is Originator of cleanAllRuv and M2 propagation
    regex = re.compile(".*Original task deletes Keep alive entry .4.*")
    assert pattern_errorlog(M1, regex)

    regex = re.compile(".*Propagated task does not delete Keep alive entry .4.*")
    assert pattern_errorlog(M2, regex)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
    
