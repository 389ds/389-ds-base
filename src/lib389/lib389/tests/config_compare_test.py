import os
import sys
import time
import ldap
import logging
import pytest
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from lib389.utils import *

from lib389.idm.group import Groups
from lib389.idm.user import UserAccounts, UserAccount

from lib389.topologies import topology_st, topology_i2
from lib389.config import Config

DEBUGGING = os.getenv('DEBUGGING', False)

if DEBUGGING is not False:
    DEBUGGING = True

if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)

log = logging.getLogger(__name__)


def test_config_compare(topology_i2):
    """
    Compare test between cn=config of two different Directory Server intance.
    """
    if DEBUGGING:
        # Add debugging steps(if any)...
        pass

    st1_config = topology_i2.ins.get('standalone1').config
    st2_config = topology_i2.ins.get('standalone2').config
    # 'nsslapd-port' attribute is expected to be same in cn=config comparison, 
    # but they are different in our testing environment 
    # as we are using 2 DS instances running, both running simultaneuosly.
    # Hence explicitly adding 'nsslapd-port' to compare_exclude.
    st1_config._compare_exclude.append('nsslapd-port')
    st2_config._compare_exclude.append('nsslapd-port')

    assert(Config.compare(st1_config, st2_config) == True)
    log.info("Test PASSED")


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
