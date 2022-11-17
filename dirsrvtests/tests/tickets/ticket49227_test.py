# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import time
import ldap
import logging
import pytest
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st as topo

pytestmark = pytest.mark.tier2

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

DEFAULT_LEVEL = b"16384"
COMB_LEVEL = b"73864"  # 65536+8192+128+8 = 73864
COMB_DEFAULT_LEVEL = b"90248"  # 65536+8192+128+8+16384 = 90248


def set_level(topo, level):
    ''' Set the error log level
    '''
    try:
        topo.standalone.modify_s("cn=config", [(ldap.MOD_REPLACE, 'nsslapd-errorlog-level', ensure_bytes(level))])
        time.sleep(1)
    except ldap.LDAPError as e:
        log.fatal('Failed to set loglevel to %s - error: %s' % (level, str(e)))
        assert False


def get_level(topo):
    ''' Set the error log level
    '''
    try:
        config = topo.standalone.search_s("cn=config", ldap.SCOPE_BASE, "objectclass=top")
        time.sleep(1)
        return config[0].getValue('nsslapd-errorlog-level')
    except ldap.LDAPError as e:
        log.fatal('Failed to get loglevel - error: %s' % (str(e)))
        assert False


def get_log_size(topo):
    ''' Get the errors log size
    '''
    statinfo = os.stat(topo.standalone.errlog)
    return statinfo.st_size


def test_ticket49227(topo):
    """Set the error log to varying levels, and make sure a search for that value
    reflects the expected value (not the bitmasked value.
    """
    log_size = get_log_size(topo)

    # Check the default level
    level = get_level(topo)
    if level != DEFAULT_LEVEL:
        log.fatal('Incorrect default logging level: %s' % (level))
        assert False

    # Set connection logging
    set_level(topo, '8')
    level = get_level(topo)
    if level != b'8':
        log.fatal('Incorrect connection logging level: %s' % (level))
        assert False

    # Check the actual log
    new_size = get_log_size(topo)
    if new_size == log_size:
        # Size should be different
        log.fatal('Connection logging is not working')
        assert False

    # Set default logging using zero
    set_level(topo, '0')
    log_size = get_log_size(topo)
    level = get_level(topo)
    if level != DEFAULT_LEVEL:
        log.fatal('Incorrect default logging level: %s' % (level))
        assert False

    # Check the actual log
    new_size = get_log_size(topo)
    if new_size != log_size:
        # Size should be the size
        log.fatal('Connection logging is still on')
        assert False

    # Set default logging using the default value
    set_level(topo, DEFAULT_LEVEL)
    level = get_level(topo)
    if level != DEFAULT_LEVEL:
        log.fatal('Incorrect default logging level: %s' % (level))
        assert False

    # Check the actual log
    new_size = get_log_size(topo)
    if new_size != log_size:
        # Size should be the size
        log.fatal('Connection logging is still on')
        assert False

    # Set a combined level that includes the default level
    set_level(topo, COMB_DEFAULT_LEVEL)
    level = get_level(topo)
    if level != COMB_DEFAULT_LEVEL:
        log.fatal('Incorrect combined logging level with default level: %s expected %s' %
                  (level, COMB_DEFAULT_LEVEL))
        assert False

    # Set a combined level that does not includes the default level
    set_level(topo, COMB_LEVEL)
    level = get_level(topo)
    if level != COMB_LEVEL:
        log.fatal('Incorrect combined logging level without default level: %s expected %s' %
                  (level, COMB_LEVEL))
        assert False

    # Check our level is present after a restart - previous level was COMB_LEVEL
    topo.standalone.restart()
    log_size = get_log_size(topo)  # Grab the log size for our next check
    level = get_level(topo)  # This should trigger connection logging
    if level != COMB_LEVEL:
        log.fatal('Incorrect combined logging level with default level: %s expected %s' %
                  (level, COMB_LEVEL))
        assert False

    # Now check the actual levels are still working
    new_size = get_log_size(topo)
    if new_size == log_size:
        # Size should be different
        log.fatal('Combined logging is not working')
        assert False


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

