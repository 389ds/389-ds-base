# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import time
import subprocess
import pytest
import logging
import os

from lib389 import DEFAULT_SUFFIX
from lib389.cli_idm.account import list, get_dn, lock, unlock, delete, modify, rename, entry_status, \
    subtree_status, reset_password, change_password
from lib389.topologies import topology_st
from lib389.cli_base import FakeArgs
from lib389.utils import ds_is_older
from lib389.idm.user import nsUserAccounts

pytestmark = pytest.mark.tier0

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


def check_value_in_log_and_reset(topology, content_list=None, content_list2=None, check_value=None, check_value_not=None):
        if content_list2 is not None:
            log.info('Check if content is present in output')
            for item in content_list + content_list2:
                assert topology.logcap.contains(item)

        if content_list is not None:
            log.info('Check if content is present in output')
            for item in content_list:
                assert topology.logcap.contains(item)

        if check_value is not None:
            log.info('Check if value is present in output')
            assert topology.logcap.contains(check_value)

        if check_value_not is not None:
            log.info('Check if value is not present in output')
            assert not topology.logcap.contains(check_value_not)

        log.info('Reset the log for next test')
        topology.logcap.flush()


@pytest.fixture(scope="function")
def create_test_user(topology_st, request):
    log.info('Create test user')
    users = nsUserAccounts(topology_st.standalone, DEFAULT_SUFFIX)
    test_user = users.create_test_user()

    def fin():
        log.info('Delete test user')
        if test_user.exists():
            test_user.delete()

    request.addfinalizer(fin)


@pytest.mark.bz1862971
@pytest.mark.ds4281
@pytest.mark.skipif(ds_is_older("1.4.2"), reason="Not implemented")
def test_dsidm_account_entry_status_with_lock(topology_st, create_test_user):
    """ Test dsidm account entry-status option with account lock/unlock

    :id: d911bbf2-3a65-42a4-ad76-df1114caa396
    :setup: Standalone instance
    :steps:
         1. Create user account
         2. Run dsidm account entry status
         3. Run dsidm account lock
         4. Run dsidm account entry status
         5. Run dsidm account unlock
         6. Run dsidm account entry status
    :expectedresults:
         1. Success
         2. The state message should be Entry State: activated
         3. Success
         4. The state message should be Entry State: directly locked through nsAccountLock
         5. Success
         6. The state message should be Entry State: activated
    """

    standalone = topology_st.standalone
    users = nsUserAccounts(standalone, DEFAULT_SUFFIX)
    test_user = users.get('test_user_1000')

    entry_list = ['Entry DN: {}'.format(test_user.dn),
                  'Entry Creation Date',
                  'Entry Modification Date']

    state_lock = 'Entry State: directly locked through nsAccountLock'
    state_unlock= 'Entry State: activated'

    lock_msg = 'Entry {} is locked'.format(test_user.dn)
    unlock_msg = 'Entry {} is unlocked'.format(test_user.dn)

    args = FakeArgs()
    args.dn = test_user.dn

    log.info('Test dsidm account entry-status')
    entry_status(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=entry_list, check_value=state_unlock)

    log.info('Test dsidm account lock')
    lock(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value=lock_msg)

    log.info('Test dsidm account entry-status with locked account')
    entry_status(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=entry_list, check_value=state_lock)

    log.info('Test dsidm account unlock')
    unlock(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, check_value=unlock_msg)

    log.info('Test dsidm account entry-status with unlocked account')
    entry_status(standalone, DEFAULT_SUFFIX, topology_st.logcap.log, args)
    check_value_in_log_and_reset(topology_st, content_list=entry_list, check_value=state_unlock)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
