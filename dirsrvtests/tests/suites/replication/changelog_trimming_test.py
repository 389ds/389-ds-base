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
from lib389.utils import ensure_bytes, ds_supports_new_changelog

pytestmark = pytest.mark.tier1

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

CHANGELOG = 'cn=changelog,{}'.format(DN_USERROOT_LDBM)
MAXAGE = 'nsslapd-changelogmaxage'
MAXENTRIES = 'nsslapd-changelogmaxentries'
TRIMINTERVAL = 'nsslapd-changelogtrim-interval'

def do_mods(master, num):
    """Perform a num of mods on the default suffix
    """
    domain = Domain(master, DEFAULT_SUFFIX)
    for i in range(num):
        domain.replace('description', 'change %s' % i)

def set_value(master, attr, val):
    """
    Helper function to add/replace attr: val and check the added value
    """
    try:
        master.modify_s(CHANGELOG, [(ldap.MOD_REPLACE, attr, ensure_bytes(val))])
    except ldap.LDAPError as e:
        log.error('Failed to add ' + attr + ': ' + val + ' to ' + plugin + ': error {}'.format(get_ldap_error_msg(e,'desc')))
        assert False

@pytest.fixture(scope="module")
def setup_max_entries(topo, request):
    """Configure logging and changelog max entries
    """
    master = topo.ms["master1"]

    master.config.loglevel((ErrorLog.REPLICA,), 'error')

    if ds_supports_new_changelog():
        set_value(master, MAXENTRIES, '2')
        set_value(master, TRIMINTERVAL, '300')
    else:
        cl = Changelog5(master)
        cl.set_trim_interval('300')

@pytest.fixture(scope="module")
def setup_max_age(topo, request):
    """Configure logging and changelog max age
    """
    master = topo.ms["master1"]
    master.config.loglevel((ErrorLog.REPLICA,), 'error')

    if ds_supports_new_changelog():
        set_value(master, MAXAGE, '5')
        set_value(master, TRIMINTERVAL, '300')
    else:
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
    log.info("Testing changelog trimming interval with max age...")

    master = topo.ms["master1"]
    if not ds_supports_new_changelog():
        cl = Changelog5(master)

    # Do mods to build if cl entries
    do_mods(master, 10)

    time.sleep(1)  # Trimming should not have occurred
    if master.searchErrorsLog("Trimmed") is True:
        log.fatal('Trimming event unexpectedly occurred')
        assert False

    if ds_supports_new_changelog():
        set_value(master, TRIMINTERVAL, '5')
    else:
        cl.set_trim_interval('5')

    time.sleep(3)  # Trimming should not have occurred
    if master.searchErrorsLog("Trimmed") is True:
        log.fatal('Trimming event unexpectedly occurred')
        assert False

    time.sleep(3)  # Trimming should have occurred
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
    if not ds_supports_new_changelog():
        cl = Changelog5(master)

    # reset errors log
    master.deleteErrorLogs()

    # Do mods to build if cl entries
    do_mods(master, 10)

    time.sleep(1)  # Trimming should have occurred
    if master.searchErrorsLog("Trimmed") is True:
        log.fatal('Trimming event unexpectedly occurred')
        assert False

    if ds_supports_new_changelog():
        set_value(master, TRIMINTERVAL, '5')
    else:
        cl.set_trim_interval('5')

    time.sleep(6)  # Trimming should have occurred
    if master.searchErrorsLog("Trimmed") is False:
        log.fatal('Trimming event did not occur')
        assert False


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
