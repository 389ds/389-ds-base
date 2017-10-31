import logging
import pytest
import os
import ldap
import time
from lib389._constants import *
from lib389.properties import *
from lib389.topologies import create_topology

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def do_mods(master, num):
    """Perform a num of mods on the default suffix
    """
    for i in xrange(num):
        try:
            master.modify_s(DEFAULT_SUFFIX, [(ldap.MOD_REPLACE,
                                              "description",
                                              "new")])
        except ldap.LDAPError as e:
            log.fatal("Failed to make modify: " + str(e))
            assert False


@pytest.fixture(scope="module")
def setup_max_entries(topo, request):
    """Configure logging and changelog max entries
    """
    master = topo.ms["master1"]

    master.config.loglevel((LOG_REPLICA,), 'error')
    try:
        master.modify_s(DN_CHANGELOG, [(ldap.MOD_REPLACE, CL_MAX_ENTRIES, "2"),
                                       (ldap.MOD_REPLACE, CL_TRIM_INTERVAL, "300")])
    except ldap.LDAPError as e:
        log.fatal("Failed to set change log config: " + str(e))
        assert False


@pytest.fixture(scope="module")
def setup_max_age(topo, request):
    """Configure logging and changelog max age
    """
    master = topo.ms["master1"]
    master.config.loglevel((LOG_REPLICA,), 'error')
    try:
        master.modify_s(DN_CHANGELOG, [(ldap.MOD_REPLACE, CL_MAXAGE, "5"),
                                       (ldap.MOD_REPLACE, CL_TRIM_INTERVAL, "300")])
    except ldap.LDAPError as e:
        log.fatal("Failed to set change log config: " + str(e))
        assert False


@pytest.fixture(scope="module")
def topo(request):
    """Create a topology with 1 masters"""

    topology = create_topology({
        ReplicaRole.MASTER: 1,
        })
    # You can write replica test here. Just uncomment the block and choose instances
    # replicas = Replicas(topology.ms["master1"])
    # replicas.test(DEFAULT_SUFFIX, topology.cs["consumer1"])

    def fin():
        """If we are debugging just stop the instances, otherwise remove them"""

        if DEBUGGING:
            map(lambda inst: inst.stop(), topology.all_insts.values())
        else:
            map(lambda inst: inst.delete(), topology.all_insts.values())

    request.addfinalizer(fin)

    return topology


def test_max_age(topo, setup_max_age):
    """Test changing the trimming interval works with max age

    :id: b5de04a5-4d92-49ea-a725-1d278a1c647c
    :setup: single master
    :steps:
        1. Perform modification to populate changelog
        2. Adjust the changelog trimming interval
        3. Check is trimming occurrs within the new interval

    :expectedresults:
        1. Modifications are successful
        2. The changelog trimming interval is correctly lowered
        3. Trimming occurs

    """
    log.info("Testing changelog triming interval with max age...")

    master = topo.ms["master1"]

    # Do mods to build if cl entries
    do_mods(master, 10)
    time.sleep(6)  # 5 seconds + 1 for good measure

    if master.searchErrorsLog("Trimmed") is True:
        log.fatal('Trimming event unexpectedly occurred')
        assert False

    try:
        master.modify_s(DN_CHANGELOG, [(ldap.MOD_REPLACE, CL_TRIM_INTERVAL, "5")])
    except ldap.LDAPError as e:
        log.fatal("Failed to set chance log trim interval: " + str(e))
        assert False

    time.sleep(6)  # Trimming should have occured

    if master.searchErrorsLog("Trimmed") is False:
        log.fatal('Trimming event did not occur')
        assert False


def test_max_entries(topo, setup_max_entries):
    """Test changing the trimming interval works with max entries

    :id: b5de04a5-4d92-49ea-a725-1d278a1c647d
    :setup: single master
    :steps:
        1. Perform modification to populate changelog
        2. Adjust the changelog trimming interval
        3. Check is trimming occurrs within the new interval

    :expectedresults:
        1. Modifications are successful
        2. The changelog trimming interval is correctly lowered
        3. Trimming occurs

    """

    log.info("Testing changelog triming interval with max entries...")
    master = topo.ms["master1"]

    # reset errors log
    master.deleteErrorLogs()

    # Do mods to build if cl entries
    do_mods(master, 10)

    if master.searchErrorsLog("Trimmed") is True:
        log.fatal('Trimming event unexpectedly occurred')
        assert False

    try:
        master.modify_s(DN_CHANGELOG, [(ldap.MOD_REPLACE, CL_TRIM_INTERVAL, "5")])
    except ldap.LDAPError as e:
        log.fatal("Failed to set chance log trim interval: " + str(e))
        assert False

    time.sleep(6)  # Trimming should have occured

    if master.searchErrorsLog("Trimmed") is False:
        log.fatal('Trimming event did not occur')
        assert False


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

