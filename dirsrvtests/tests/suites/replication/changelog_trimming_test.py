import logging
import pytest
import os
import ldap
import time
from lib389._constants import *
from lib389.properties import *
from lib389.topologies import topology_m1 as topo
from lib389.replica import Changelog5
from lib389.idm.domain import Domain
from lib389.utils import ds_supports_new_changelog

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def do_mods(supplier, num):
    """Perform a num of mods on the default suffix
    """
    domain = Domain(supplier, DEFAULT_SUFFIX)
    for i in range(num):
        domain.replace('description', 'change %s' % i)

@pytest.fixture(scope="module")
def setup_max_entries(topo, request):
    """Configure logging and changelog max entries
    """
    supplier = topo.ms["supplier1"]

    supplier.config.loglevel((ErrorLog.REPLICA,), 'error')

    if ds_supports_new_changelog():
        set_value(supplier, MAXENTRIES, '2')
        set_value(supplier, TRIMINTERVAL, '300')
    else:
        cl = Changelog5(supplier)
        cl.set_trim_interval('300')

@pytest.fixture(scope="module")
def setup_max_age(topo, request):
    """Configure logging and changelog max age
    """
    supplier = topo.ms["supplier1"]
    supplier.config.loglevel((ErrorLog.REPLICA,), 'error')

    if ds_supports_new_changelog():
        set_value(supplier, MAXAGE, '5')
        set_value(supplier, TRIMINTERVAL, '300')
    else:
        cl = Changelog5(supplier)
        cl.set_max_age('5')
        cl.set_trim_interval('300')

def test_max_age(topo, setup_max_age):
    """Test changing the trimming interval works with max age

    :id: b5de04a5-4d92-49ea-a725-1d278a1c647c
    :setup: single supplier
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

    supplier = topo.ms["supplier1"]
    if not ds_supports_new_changelog():
        cl = Changelog5(supplier)

    # Do mods to build if cl entries
    do_mods(supplier, 10)

    time.sleep(1)  # Trimming should not have occurred
    if supplier.searchErrorsLog("Trimmed") is True:
        log.fatal('Trimming event unexpectedly occurred')
        assert False

    if ds_supports_new_changelog():
        set_value(supplier, TRIMINTERVAL, '5')
    else:
        cl.set_trim_interval('5')

    time.sleep(3)  # Trimming should not have occurred
    if supplier.searchErrorsLog("Trimmed") is True:
        log.fatal('Trimming event unexpectedly occurred')
        assert False

    time.sleep(3)  # Trimming should have occurred
    if supplier.searchErrorsLog("Trimmed") is False:
        log.fatal('Trimming event did not occur')
        assert False


def test_max_entries(topo, setup_max_entries):
    """Test changing the trimming interval works with max entries

    :id: b5de04a5-4d92-49ea-a725-1d278a1c647d
    :setup: single supplier
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
    supplier = topo.ms["supplier1"]
    if not ds_supports_new_changelog():
        cl = Changelog5(supplier)

    # reset errors log
    supplier.deleteErrorLogs()

    # Do mods to build if cl entries
    do_mods(supplier, 10)

    time.sleep(1)  # Trimming should have occurred
    if supplier.searchErrorsLog("Trimmed") is True:
        log.fatal('Trimming event unexpectedly occurred')
        assert False

    if ds_supports_new_changelog():
        set_value(supplier, TRIMINTERVAL, '5')
    else:
        cl.set_trim_interval('5')

    time.sleep(6)  # Trimming should have occurred
    if supplier.searchErrorsLog("Trimmed") is False:
        log.fatal('Trimming event did not occur')
        assert False


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
