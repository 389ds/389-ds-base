# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
import subprocess
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st as topo

from lib389._constants import (DEFAULT_SUFFIX, PLUGIN_MEMBER_OF, DN_DM, PASSWORD, SERVERID_STANDALONE,
                              SUFFIX)

pytestmark = pytest.mark.tier2

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

TEST_FILTER = '(objectClass=person'
TEST_BASEDN = 'dc=testdb,dc=com'
FILTER = '(objectClass=person)'
FIXUP_MEMOF = 'fixup-memberof.pl'


def test_ticket49072_basedn(topo):
    """memberOf fixup task does not validate args

    :id: dce9b898-119d-42b8-a236-1130e59bfe18
    :feature: memberOf
    :setup: Standalone instance, with memberOf plugin
    :steps: 1. Run fixup-memberOf.pl with invalid DN entry
            2. Check if error log reports "Failed to get be backend"
    :expectedresults: Fixup-memberOf.pl task should complete, but errors logged.
    """

    log.info("Ticket 49072 memberof fixup task with invalid basedn...")
    topo.standalone.plugins.enable(name=PLUGIN_MEMBER_OF)
    topo.standalone.restart(timeout=10)

    if ds_is_older('1.3'):
        inst_dir = topo.standalone.get_inst_dir()
        memof_task = os.path.join(inst_dir, FIXUP_MEMOF)
        try:
            output = subprocess.check_output([memof_task, '-D', DN_DM, '-w', PASSWORD, '-b', TEST_BASEDN, '-f', FILTER])
        except subprocess.CalledProcessError as err:
            output = err.output
    else:
        sbin_dir = topo.standalone.get_sbin_dir()
        memof_task = os.path.join(sbin_dir, FIXUP_MEMOF)
        try:
            output = subprocess.check_output(
                [memof_task, '-D', DN_DM, '-w', PASSWORD, '-b', TEST_BASEDN, '-Z', SERVERID_STANDALONE, '-f', FILTER])
        except subprocess.CalledProcessError as err:
            output = err.output
    log.info('output: {}'.format(output))
    expected = b"Successfully added task entry"
    assert expected in output
    log_entry = topo.standalone.ds_error_log.match('.*Failed to get be backend.*')
    log.info('Error log out: {}'.format(log_entry))
    assert topo.standalone.ds_error_log.match('.*Failed to get be backend.*')


def test_ticket49072_filter(topo):
    """memberOf fixup task does not validate args

    :id: dde9e893-119d-42c8-a236-1190e56bfe98
    :feature: memberOf
    :setup: Standalone instance, with memberOf plugin
    :steps: 1. Run fixup-memberOf.pl with invalid filter
            2. Check if error log reports "Bad search filter"
    :expectedresults: Fixup-memberOf.pl task should complete, but errors logged.
    """
    log.info("Ticket 49072 memberof fixup task with invalid filter...")
    log.info('Wait for 10 secs and check if task is completed')
    time.sleep(10)
    task_memof = 'cn=memberOf task,cn=tasks,cn=config'
    if topo.standalone.search_s(task_memof, ldap.SCOPE_SUBTREE, 'cn=memberOf_fixup*', ['dn:']):
        log.info('memberof task is still running, wait for +10 secs')
        time.sleep(10)

    if ds_is_older('1.3'):
        inst_dir = topo.standalone.get_inst_dir()
        memof_task = os.path.join(inst_dir, FIXUP_MEMOF)
        try:
            output = subprocess.check_output([memof_task, '-D', DN_DM, '-w', PASSWORD, '-b', SUFFIX, '-f', TEST_FILTER])
        except subprocess.CalledProcessError as err:
            output = err.output
    else:
        sbin_dir = topo.standalone.get_sbin_dir()
        memof_task = os.path.join(sbin_dir, FIXUP_MEMOF)
        try:
            output = subprocess.check_output(
                [memof_task, '-D', DN_DM, '-w', PASSWORD, '-b', SUFFIX, '-Z', SERVERID_STANDALONE, '-f', TEST_FILTER])
        except subprocess.CalledProcessError as err:
            output = err.output
    log.info('output: {}'.format(output))
    expected = b"Successfully added task entry"
    assert expected in output
    log_entry = topo.standalone.ds_error_log.match('.*Bad search filter.*')
    log.info('Error log out: {}'.format(log_entry))
    assert topo.standalone.ds_error_log.match('.*Bad search filter.*')

    log.info("Ticket 49072 complete: memberOf fixup task does not validate args")


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
