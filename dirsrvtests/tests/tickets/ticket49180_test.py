# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import threading

import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_m4
from lib389.replica import ReplicationManager

from lib389._constants import (DEFAULT_SUFFIX, SUFFIX)

from lib389 import DirSrv

pytestmark = pytest.mark.tier2

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


def remove_supplier4_agmts(msg, topology_m4):
    """Remove all the repl agmts to supplier4. """

    log.info('%s: remove all the agreements to supplier 4...' % msg)
    for num in range(1, 4):
        try:
            topology_m4.ms["supplier{}".format(num)].agreement.delete(DEFAULT_SUFFIX,
                                                                    topology_m4.ms["supplier4"].host,
                                                                    topology_m4.ms["supplier4"].port)
        except ldap.LDAPError as e:
            log.fatal('{}: Failed to delete agmt(m{} -> m4), error: {}'.format(msg, num, str(e)))
            assert False


def restore_supplier4(topology_m4):
    """In our tests will always be removing supplier 4, so we need a common
    way to restore it for another test
    """

    log.info('Restoring supplier 4...')

    # Enable replication on supplier 4
    M4 = topology_m4.ms["supplier4"]
    M1 = topology_m4.ms["supplier1"]
    repl = ReplicationManager(SUFFIX)
    repl.join_supplier(M1, M4)
    repl.ensure_agreement(M4, M1)
    repl.ensure_agreement(M1, M4)

    # Test Replication is working
    for num in range(2, 5):
        if topology_m4.ms["supplier1"].testReplication(DEFAULT_SUFFIX, topology_m4.ms["supplier{}".format(num)]):
            log.info('Replication is working m1 -> m{}.'.format(num))
        else:
            log.fatal('restore_supplier4: Replication is not working from m1 -> m{}.'.format(num))
            assert False
        time.sleep(1)

    # Check replication is working from supplier 4 to supplier1...
    if topology_m4.ms["supplier4"].testReplication(DEFAULT_SUFFIX, topology_m4.ms["supplier1"]):
        log.info('Replication is working m4 -> m1.')
    else:
        log.fatal('restore_supplier4: Replication is not working from m4 -> 1.')
        assert False
    time.sleep(5)

    log.info('Supplier 4 has been successfully restored.')


def test_ticket49180(topology_m4):

    log.info('Running test_ticket49180...')

    log.info('Check that replication works properly on all suppliers')
    agmt_nums = {"supplier1": ("2", "3", "4"),
                 "supplier2": ("1", "3", "4"),
                 "supplier3": ("1", "2", "4"),
                 "supplier4": ("1", "2", "3")}

    for inst_name, agmts in agmt_nums.items():
        for num in agmts:
            if not topology_m4.ms[inst_name].testReplication(DEFAULT_SUFFIX, topology_m4.ms["supplier{}".format(num)]):
                log.fatal(
                    'test_replication: Replication is not working between {} and supplier {}.'.format(inst_name,
                                                                                                    num))
                assert False

    # Disable supplier 4
    log.info('test_clean: disable supplier 4...')
    topology_m4.ms["supplier4"].replica.disableReplication(DEFAULT_SUFFIX)

    # Remove the agreements from the other suppliers that point to supplier 4
    remove_supplier4_agmts("test_clean", topology_m4)

    # Cleanup - restore supplier 4
    restore_supplier4(topology_m4)

    attr_errors = os.popen('egrep "attrlist_replace" %s  | wc -l' % topology_m4.ms["supplier1"].errlog)
    ecount = int(attr_errors.readline().rstrip())
    log.info("Errors found on m1: %d" % ecount)
    assert (ecount == 0)

    attr_errors = os.popen('egrep "attrlist_replace" %s  | wc -l' % topology_m4.ms["supplier2"].errlog)
    ecount = int(attr_errors.readline().rstrip())
    log.info("Errors found on m2: %d" % ecount)
    assert (ecount == 0)

    attr_errors = os.popen('egrep "attrlist_replace" %s  | wc -l' % topology_m4.ms["supplier3"].errlog)
    ecount = int(attr_errors.readline().rstrip())
    log.info("Errors found on m3: %d" % ecount)
    assert (ecount == 0)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
