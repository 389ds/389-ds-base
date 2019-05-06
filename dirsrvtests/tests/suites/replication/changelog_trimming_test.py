import logging
import pytest
import os
import ldap
import time
from lib389._constants import *
from lib389.properties import *
from lib389.topologies import topology_m1 as topo
from lib389.changelog import Changelog5
from lib389.idm.domain import Domain

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def do_mods(master, num):
    """Perform a num of mods on the default suffix
    """
    domain = Domain(master, DEFAULT_SUFFIX)
    for i in range(num):
        domain.replace('description', 'change %s' % i)

@pytest.fixture(scope="module")
def setup_max_entries(topo, request):
    """Configure logging and changelog max entries
    """
    master = topo.ms["master1"]

    master.config.loglevel((ErrorLog.REPLICA,), 'error')

    cl = Changelog5(master)
    cl.set_max_entries('2')
    cl.set_trim_interval('300')

@pytest.fixture(scope="module")
def setup_max_age(topo, request):
    """Configure logging and changelog max age
    """
    master = topo.ms["master1"]
    master.config.loglevel((ErrorLog.REPLICA,), 'error')

    cl = Changelog5(master)
    cl.set_max_age('5')
    cl.set_trim_interval('300')

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
    cl = Changelog5(master)

    # Do mods to build if cl entries
    do_mods(master, 10)
    time.sleep(6)  # 5 seconds + 1 for good measure

    if master.searchErrorsLog("Trimmed") is True:
        log.fatal('Trimming event unexpectedly occurred')
        assert False

    cl.set_trim_interval('5')

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
    cl = Changelog5(master)

    # reset errors log
    master.deleteErrorLogs()

    # Do mods to build if cl entries
    do_mods(master, 10)

    if master.searchErrorsLog("Trimmed") is True:
        log.fatal('Trimming event unexpectedly occurred')
        assert False

    cl.set_trim_interval('5')

    time.sleep(6)  # Trimming should have occured

    if master.searchErrorsLog("Trimmed") is False:
        log.fatal('Trimming event did not occur')
        assert False


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

