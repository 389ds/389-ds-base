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
from lib389.backend import Backend, Backends
from lib389.idm.user import UserAccounts
from lib389.replica import Changelog, ReplicationManager, Replicas
from lib389.utils import *
from lib389._constants import *
from lib389.cli_base import FakeArgs
from lib389.topologies import topology_m2, topology_m3
from lib389.cli_ctl.health import health_check_run
from lib389.paths import Paths

CMD_OUTPUT = 'No issues found.'
JSON_OUTPUT = '[]'

ds_paths = Paths()
log = logging.getLogger(__name__)


def run_healthcheck_and_flush_log(topology, instance, searched_code, json, searched_code2=None):
    args = FakeArgs()
    args.instance = instance.serverid
    args.verbose = instance.verbose
    args.list_errors = False
    args.list_checks = False
    args.check = ['replication', 'backends:userroot:cl_trimming']
    args.dry_run = False

    if json:
        log.info('Use healthcheck with --json option')
        args.json = json
        health_check_run(instance, topology.logcap.log, args)
        assert topology.logcap.contains(searched_code)
        log.info('Healthcheck returned searched code: %s' % searched_code)

        if searched_code2 is not None:
            assert topology.logcap.contains(searched_code2)
            log.info('Healthcheck returned searched code: %s' % searched_code2)
    else:
        log.info('Use healthcheck without --json option')
        args.json = json
        health_check_run(instance, topology.logcap.log, args)
        assert topology.logcap.contains(searched_code)
        log.info('Healthcheck returned searched code: %s' % searched_code)

        if searched_code2 is not None:
            assert topology.logcap.contains(searched_code2)
            log.info('Healthcheck returned searched code: %s' % searched_code2)

    log.info('Clear the log')
    topology.logcap.flush()


def set_changelog_trimming(instance):
    log.info('Get the changelog enteries')
    inst_changelog = Changelog(instance, suffix=DEFAULT_SUFFIX)

    log.info('Set nsslapd-changelogmaxage to 30d')
    inst_changelog.add('nsslapd-changelogmaxage', '30')


@pytest.mark.ds50873
@pytest.mark.bz1685160
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

    M1 = topology_m2.ms['master1']
    M2 = topology_m2.ms['master2']

    set_changelog_trimming(M1)

    log.info('Set nsds5replicaport for the replication agreement to an unreachable port')
    repl = ReplicationManager(DEFAULT_SUFFIX)
    repl.wait_for_replication(M1, M2)

    replica_m1 = Replicas(M1).get(DEFAULT_SUFFIX)
    agmt_m1 = replica_m1.get_agreements().list()[0]
    agmt_m1.replace('nsds5replicaport', '4389')

    run_healthcheck_and_flush_log(topology_m2, M1, RET_CODE, json=False)
    run_healthcheck_and_flush_log(topology_m2, M1, RET_CODE, json=True)

    log.info('Set nsds5replicaport for the replication agreement to a reachable port')
    agmt_m1.replace('nsDS5ReplicaPort', '{}'.format(M2.port))
    repl.wait_for_replication(M1, M2)

    run_healthcheck_and_flush_log(topology_m2, M1, CMD_OUTPUT, json=False)
    run_healthcheck_and_flush_log(topology_m2, M1, JSON_OUTPUT, json=True)


@pytest.mark.ds50873
@pytest.mark.bz1685160
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

    M1 = topology_m2.ms['master1']

    RET_CODE = 'DSCLLE0001'

    log.info('Get the changelog entries for M1')
    changelog_m1 = Changelog(M1, suffix=DEFAULT_SUFFIX)

    log.info('Check nsslapd-changelogmaxage value')
    if changelog_m1.get_attr_val('nsslapd-changelogmaxage') is not None:
        changelog_m1.remove_all('nsslapd-changelogmaxage')

    time.sleep(3)

    run_healthcheck_and_flush_log(topology_m2, M1, RET_CODE, json=False)
    run_healthcheck_and_flush_log(topology_m2, M1, RET_CODE, json=True)

    set_changelog_trimming(M1)

    run_healthcheck_and_flush_log(topology_m2, M1, CMD_OUTPUT, json=False)
    run_healthcheck_and_flush_log(topology_m2, M1, JSON_OUTPUT, json=True)


@pytest.mark.ds50873
@pytest.mark.bz1685160
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

    M1 = topology_m2.ms['master1']
    M2 = topology_m2.ms['master2']

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

    run_healthcheck_and_flush_log(topology_m2, M1, RET_CODE, json=False)
    run_healthcheck_and_flush_log(topology_m2, M1, RET_CODE, json=True)


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

    inst = topology_m2.ms['master1']

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


@pytest.mark.ds50873
@pytest.mark.bz1685160
@pytest.mark.xfail(ds_is_older("1.4.1"), reason="Not implemented")
def test_healthcheck_replication_out_of_sync_broken(topology_m3):
    """Check if HealthCheck returns DSREPLLE0001 code

    :id: b5ae7cae-de0f-4206-95a4-f81538764bea
    :setup: 3 MMR topology
    :steps:
        1. Create a 3 masters full-mesh topology, on M2 and M3 don’t set nsds5BeginReplicaRefresh:start
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

    M1 = topology_m3.ms['master1']
    M2 = topology_m3.ms['master2']
    M3 = topology_m3.ms['master3']

    log.info('Break master2 and master3')
    replicas = Replicas(M2)
    replica = replicas.list()[0]
    replica.replace('nsds5ReplicaBindDNGroup', 'cn=repl')

    replicas = Replicas(M3)
    replica = replicas.list()[0]
    replica.replace('nsds5ReplicaBindDNGroup', 'cn=repl')

    log.info('Perform update on master1')
    test_users_m1 = UserAccounts(M1, DEFAULT_SUFFIX)
    test_users_m1.create_test_user(1005, 2000)

    run_healthcheck_and_flush_log(topology_m3, M1, RET_CODE, json=False)
    run_healthcheck_and_flush_log(topology_m3, M1, RET_CODE, json=True)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
