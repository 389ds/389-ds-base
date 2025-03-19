# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import pytest
import os
import random
import re
import string
import subprocess
import threading
import time
from contextlib import suppress, AbstractContextManager
from lib389.backend import Backend, Backends
from lib389.idm.user import UserAccounts
from lib389.replica import Changelog, ReplicationManager, Replicas
from lib389.utils import *
from lib389._constants import *
from lib389.cli_base import FakeArgs
from lib389.cli_ctl.health import health_check_run
from lib389.topologies import topology_m2, topology_m3
from lib389.paths import Paths

CMD_OUTPUT = 'No issues found.'
JSON_OUTPUT = '[]'

LOGIC_DICT = {
        False: ( "not ", "", lambda x: x ),
        True:  ( "", "not ", lambda x: not x )
    }

ds_paths = Paths()
log = logging.getLogger(__name__)


class LoadInstance(AbstractContextManager):
    @staticmethod
    def create_test_user(inst):
        users = UserAccounts(inst, DEFAULT_SUFFIX)
        uid = str(20000 + int(inst.serverid[8:]))
        properties = {
            'uid': f'testuser_{inst.serverid}',
            'cn' : f'testuser_{inst.serverid}',
            'sn' : 'user_{inst.serverid}',
            'uidNumber' : uid,
            'gidNumber' : uid,
            'homeDirectory' : f'/home/testuser_{inst.serverid}'
        }
        return users.ensure_state(properties=properties)

    def __init__(self, inst):
        self.inst = inst
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self.loader)
        self.user = LoadInstance.create_test_user(inst)

    def loader(self):
        while not self.stop.is_set():
            value = ''.join(random.choices(string.ascii_uppercase + string.digits, k=10))
            self.user.replace('description', value)
            #log.info(f'Modified {self.user.dn} description with {value} on {self.inst.serverid}')
            time.sleep(0.001)

    def __exit__(self, *args):
        self.stop.set()
        self.thread.join()
        self.user.delete()

    def __enter__(self):
        self.thread.start()
        return self


class BreakReplication(AbstractContextManager):
    def __init__(self, inst):
        self.replica = Replicas(inst).list()[0]
        self.oldval = None

    def __exit__(self, *args):
        self.replica.replace('nsds5ReplicaBindDNGroup', self.oldval)

    def __enter__(self):
        self.oldval = self.replica.get_attr_val_utf8('nsds5ReplicaBindDNGroup')
        self.replica.replace('nsds5ReplicaBindDNGroup', 'cn=repl')
        return self


def assert_is_in_result(result, searched_code, isnot=False):
    # Assert if searched_code is not in logcap
    if searched_code is None:
        return
    
    # Handle positive and negative tests:
    nomatch, match, f = LOGIC_DICT[bool(isnot)]
    try:
        assert f(re.search(re.escape(searched_code), result))
        log.info(f'Searched code {searched_code} is {match}in healthcheck output')
    except AssertionError as exc:
        log.error(f'{searched_code} is {nomatch}in healthcheck output:  {result}')
        raise


def run_healthcheck_and_check_result(topology, instance, searched_code, json, searched_code2=None, isnot=False):
    cmd = [ 'dsctl', ]
    if json:
        cmd.append('--json')
        if searched_code == CMD_OUTPUT:
            searched_code = JSON_OUTPUT
    cmd.append(instance.serverid)
    cmd.extend(['healthcheck', '--check', 'replication' , 'backends:userroot:cl_trimming'])

    result = subprocess.run(cmd, capture_output=True, universal_newlines=True)
    log.info(f'Running: {cmd}')
    log.info(f'Stdout: {result.stdout}')
    log.info(f'Stderr: {result.stdout}')
    log.info(f'Return code: {result.returncode}')
    stdout = result.stdout

    # stdout should not be empty
    assert stdout is not None
    assert len(stdout) > 0
    assert_is_in_result(stdout, searched_code, isnot=isnot)
    assert_is_in_result(stdout, searched_code2, isnot=isnot)



def set_changelog_trimming(instance):
    log.info('Get the changelog enteries')
    inst_changelog = Changelog(instance, suffix=DEFAULT_SUFFIX)

    log.info('Set nsslapd-changelogmaxage to 30d')
    inst_changelog.set_max_age('30d')


@pytest.mark.xfail(ds_is_older("1.4.1"), reason="Not implemented")
def test_healthcheck_replication_replica_not_reachable(topology_m2):
    """Check if HealthCheck returns DSREPLLE0005 code

    :id: d452a564-7b82-4c1a-b331-a71abbd82a10
    :setup: Replicated topology
    :steps:
        1. Create a replicated topology
        2. On M1, set nsds5replicaport for the replication agreement to an unreachable port on the replica
        3. Use HealthCheck without --json option
        4. Use HealthCheck with --json option
        5. On M1, set nsds5replicaport for the replication agreement to a reachable port number
        6. Use HealthCheck without --json option
        7. Use HealthCheck with --json option
    :expectedresults:
        1. Success
        2. Success
        3. Healthcheck reports DSREPLLE0005 code and related details
        4. Healthcheck reports DSREPLLE0005 code and related details
        5. Success
        6. Healthcheck reports no issue found
        7. Healthcheck reports no issue found
    """

    RET_CODE = 'DSREPLLE0005'

    M1 = topology_m2.ms['supplier1']
    M2 = topology_m2.ms['supplier2']

    set_changelog_trimming(M1)

    log.info('Set nsds5replicaport for the replication agreement to an unreachable port')
    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.wait_for_replication(M1, M2)

    replica_m1 = Replicas(M1).get(DEFAULT_SUFFIX)
    agmt_m1 = replica_m1.get_agreements().list()[0]
    agmt_m1.replace('nsds5replicaport', '4389')
    # Should generates updates here to insure that we starts a new replication session
    # and really try to connect to the consumer
    with suppress(Exception):
        repl.wait_for_replication(M1, M2, timeout=5)

    run_healthcheck_and_check_result(topology_m2, M1, RET_CODE, json=False)
    run_healthcheck_and_check_result(topology_m2, M1, RET_CODE, json=True)

    log.info('Set nsds5replicaport for the replication agreement to a reachable port')
    agmt_m1.replace('nsDS5ReplicaPort', '{}'.format(M2.port))
    repl.wait_for_replication(M1, M2)

    run_healthcheck_and_check_result(topology_m2, M1, CMD_OUTPUT, json=False)
    run_healthcheck_and_check_result(topology_m2, M1, JSON_OUTPUT, json=True)


@pytest.mark.xfail(ds_is_older("1.4.1"), reason="Not implemented")
def test_healthcheck_changelog_trimming_not_configured(topology_m2):
    """Check if HealthCheck returns DSCLLE0001 code

    :id: c2165032-88ba-4978-a4ca-2fecfd8c35d8
    :setup: Replicated topology
    :steps:
        1. Create a replicated topology
        2. On M1, check that value of nsslapd-changelogmaxage from cn=changelog5,cn=config is None
        3. Use HealthCheck without --json option
        4. Use HealthCheck with --json option
        5. On M1, set nsslapd-changelogmaxage to 30d
        6. Use HealthCheck without --json option
        7. Use HealthCheck with --json option
    :expectedresults:
        1. Success
        2. Success
        3. Healthcheck reports DSCLLE0001 code and related details
        4. Healthcheck reports DSCLLE0001 code and related details (json)
        5. Success
        6. Healthcheck reports no issue found
        7. Healthcheck reports no issue found (json)
    """

    M1 = topology_m2.ms['supplier1']

    RET_CODE = 'DSCLLE0001'

    log.info('Get the changelog entries for M1')
    changelog_m1 = Changelog(M1, suffix=DEFAULT_SUFFIX)

    log.info('Check nsslapd-changelogmaxage value')
    if changelog_m1.get_attr_val('nsslapd-changelogmaxage') is not None:
        changelog_m1.remove_all('nsslapd-changelogmaxage')

    time.sleep(3)

    run_healthcheck_and_check_result(topology_m2, M1, RET_CODE, json=False)
    run_healthcheck_and_check_result(topology_m2, M1, RET_CODE, json=True)

    set_changelog_trimming(M1)

    run_healthcheck_and_check_result(topology_m2, M1, CMD_OUTPUT, json=False)
    run_healthcheck_and_check_result(topology_m2, M1, JSON_OUTPUT, json=True)


@pytest.mark.xfail(ds_is_older("1.4.1"), reason="Not implemented")
def test_healthcheck_replication_presence_of_conflict_entries(topology_m2):
    """Check if HealthCheck returns DSREPLLE0002 code

    :id: 43abc6c6-2075-42eb-8fa3-aa092ff64cba
    :setup: Replicated topology
    :steps:
        1. Create a replicated topology
        2. Create conflict entries : different entries renamed to the same dn
        3. Use HealthCheck without --json option
        4. Use HealthCheck with --json option
    :expectedresults:
        1. Success
        2. Success
        3. Healthcheck reports DSREPLLE0002 code and related details
        4. Healthcheck reports DSREPLLE0002 code and related details
    """

    RET_CODE = 'DSREPLLE0002'

    M1 = topology_m2.ms['supplier1']
    M2 = topology_m2.ms['supplier2']

    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.wait_for_replication(M1, M2)

    topology_m2.pause_all_replicas()

    log.info("Create conflict entries")
    test_users_m1 = UserAccounts(M1, DEFAULT_SUFFIX)
    test_users_m2 = UserAccounts(M2, DEFAULT_SUFFIX)
    user_num = 1000
    test_users_m1.create_test_user(user_num, 2000)
    test_users_m2.create_test_user(user_num, 2000)

    topology_m2.resume_all_replicas()

    repl.test_replication_topology(topology_m2)

    run_healthcheck_and_check_result(topology_m2, M1, RET_CODE, json=False)
    run_healthcheck_and_check_result(topology_m2, M1, RET_CODE, json=True)


def test_healthcheck_non_replicated_suffixes(topology_m2):
    """Check if backend lint function unexpectedly throws exception

    :id: f922edf8-c527-4802-9f42-0b75bf97098a
    :setup: 2 MMR topology
    :steps:
        1. Create a new suffix: cn=changelog
        2. Call healthcheck (there should not be any exceptions raised)
    :expectedresults:
        1. Success
        2. Success
    """

    inst = topology_m2.ms['supplier1']

    # Create second suffix
    backends = Backends(inst)
    backends.create(properties={'nsslapd-suffix': "cn=changelog",
                                'name': 'changelog'})

    # Call healthcheck
    args = FakeArgs()
    args.instance = inst.serverid
    args.verbose = inst.verbose
    args.list_errors = False
    args.list_checks = False
    args.check = ['backends']
    args.dry_run = False
    args.json = False

    health_check_run(inst, topology_m2.logcap.log, args)


def test_healthcheck_replica_busy(topology_m3):
    """Check that HealthCheck does not returns DSREPLLE0003 code when a replicva is busy

    :id: b7c4a5aa-ef98-11ef-87f5-482ae39447e5
    :setup: 3 MMR topology
    :steps:
        1. Create a 3 suppliers full-mesh topology
        2. Generate constant modify load on S1 and S2
        3. Wait a bit to ensure stable replication flow
        4. Perform a modify on S3
        5. Use HealthCheck on S3 without --json option
        6. Use HealthCheck on S3 with --json option
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Healthcheck should not reports DSREPLLE0003 code and related details
        6. Healthcheck should not reports DSREPLLE0003 code and related details
    """

    RET_CODE = 'DSREPLLE0003'
    # Is DSREPLLE0003 ignored if replica is busy ?
    ignored = not ds_is_older("2.7")

    S1 = topology_m3.ms['supplier1']
    S2 = topology_m3.ms['supplier2']
    S3 = topology_m3.ms['supplier3']
    with LoadInstance(S1), LoadInstance(S2):
        # Wait a bit to let replication starts
        time.sleep(10)
        # Create user on S3 then remove it:
        LoadInstance(S3).user.delete()
        # S3 agrements should now be in the replica busy state
        run_healthcheck_and_check_result(topology_m3, S3, RET_CODE, json=False, isnot=ignored)
        run_healthcheck_and_check_result(topology_m3, S3, RET_CODE, json=True, isnot=ignored)


@pytest.mark.xfail(ds_is_older("1.4.1"), reason="Not implemented")
def test_healthcheck_replication_out_of_sync_broken(topology_m3):
    """Check if HealthCheck returns DSREPLLE0001 code

    :id: b5ae7cae-de0f-4206-95a4-f81538764bea
    :setup: 3 MMR topology
    :steps:
        1. Create a 3 suppliers full-mesh topology, on M2 and M3 don’t set nsds5BeginReplicaRefresh:start
        2. Perform modifications on M1
        3. Use HealthCheck without --json option
        4. Use HealthCheck with --json option
    :expectedresults:
        1. Success
        2. Success
        3. Healthcheck reports DSREPLLE0001 code and related details
        4. Healthcheck reports DSREPLLE0001 code and related details
    """

    RET_CODE = 'DSREPLLE0001'

    S1 = topology_m3.ms['supplier1']
    S2 = topology_m3.ms['supplier2']
    S3 = topology_m3.ms['supplier3']

    log.info('Break supplier2 and supplier3')
    with BreakReplication(S2), BreakReplication(S3):
        time.sleep(1)
        log.info('Perform update on supplier1')
        test_users_m1 = UserAccounts(S1, DEFAULT_SUFFIX)
        test_users_m1.create_test_user(1005, 2000)

        time.sleep(3)
        run_healthcheck_and_check_result(topology_m3, S1, RET_CODE, json=False)
        run_healthcheck_and_check_result(topology_m3, S1, RET_CODE, json=True)


def test_healthcheck_replication_out_of_sync_not_broken(topology_m3):
    """Check that HealthCheck returns no issues when replication is in progress

    :id: 8305000d-ba4d-4c00-8331-be0e8bd92150
    :setup: 3 MMR topology
    :steps:
        1. Create a 3 suppliers full-mesh topology, all replicas being synchronized
        2. Generate constant load on two supplier
        3. Use HealthCheck without --json option
        4. Use HealthCheck with --json option
    :expectedresults:
        1. Success
        2. Success
        3. Healthcheck reports no issue found
        4. Healthcheck reports no issue found
    """

    RET_CODE = CMD_OUTPUT

    S1 = topology_m3.ms['supplier1']
    S2 = topology_m3.ms['supplier2']
    S3 = topology_m3.ms['supplier3']

    with LoadInstance(S1), LoadInstance(S2):
        # Wait a bit to let replication starts
        time.sleep(10)
        run_healthcheck_and_check_result(topology_m3, S1, RET_CODE, json=False)
        run_healthcheck_and_check_result(topology_m3, S1, RET_CODE, json=True)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
