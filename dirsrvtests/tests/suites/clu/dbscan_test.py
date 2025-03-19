# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2024 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import os
import pytest
import re
import subprocess
import sys

from lib389 import DirSrv
from lib389.topologies import topology_m2 as topo_m2
from lib389.cli_ctl.dblib import DbscanHelper
from difflib import context_diff

pytestmark = pytest.mark.tier0

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

DEBUGGING = os.getenv("DEBUGGING", default=False)


def log_export_file(filename):
    with open(filename, 'r') as file:
        log.debug(f'=========== Dump of {filename} ================')
        for line in file:
            log.debug(line.rstrip('\n'))
        log.debug(f'=========== Enf of {filename} =================')


@pytest.fixture(scope='module')
def helper(topo_m2, request):
    inst = topo_m2.ms["supplier1"]
    if sys.version_info < (3,5):
        pytest.skip('requires python version >= 3.5')
    dbsh = DbscanHelper(inst, log=log)
    dbsh.resync()
    if '--do-it' not in dbsh.options:
       pytest.skip('Not supported with this dbscan version')
    inst.stop()
    return dbsh


def test_dbscan_destructive_actions(helper, request):
    """Test that dbscan remove/import actions

    :id: f40b0c42-660a-11ef-9544-083a88554478
    :setup: Stopped standalone instance
    :steps:
         1. Export cn instance with dbscan
         2. Run dbscan --remove ...
         3. Check the error message about missing --do-it
         4. Check that cn instance is still present
         5. Run dbscan -I import_file ...
         6. Check it was properly imported
         7. Check that cn instance is still present
         8. Run dbscan --remove ... --doit
         9. Check the error message about missing --do-it
         10. Check that cn instance is still present
         11. Run dbscan -I import_file ... --do-it
         12. Check it was properly imported
         13. Check that cn instance is still present
         14. Export again the database
         15. Check that content of export files are the same
    :expectedresults:
         1. Success
         2. dbscan return code should be 1 (error)
         3. Error message should be present
         4. cn instance should be present
         5. dbscan return code should be 1 (error)
         6. Error message should be present
         7. cn instance should be present
         8. dbscan return code should be 0 (success)
         9. Error message should not be present
         10. cn instance should not be present
         11. dbscan return code should be 0 (success)
         12. Error message should not be present
         13. cn instance should be present
         14. Success
         15. Export files content should be the same
    """

    # Export cn instance with dbscan
    export_cn = f'{helper.ldif_dir}/dbscan_cn.data'
    export_cn2 = f'{helper.ldif_dir}/dbscan_cn2.data'
    cndbi = helper.get_dbi('replication_changelog')
    inst = helper.inst
    dblib = helper.dblib
    exportok = False
    def fin():
        if exportok and os.path.exists(export_cn):
            try:
                helper.resync()
                dbi = helper.get_dbi('replication_changelog')
            except KeyError:
                # Restore cn if it was exported successfully but does not exists any more
                helper.dbscan(['-D', dblib, '-f', cndbi, '-I', export_cn, '--do-it'])
            if not DEBUGGING:
                os.remove(export_cn)
        if os.path.exists(export_cn2) and not DEBUGGING:
            os.remove(export_cn2)

    fin()
    request.addfinalizer(fin)
    helper.dbscan(['-D', dblib,  '-f', cndbi, '-X', export_cn])
    exportok = True

    expected_msg = "without specifying '--do-it' parameter."

    # Run dbscan --remove ...
    result = helper.dbscan(['-D', helper.dblib, '--remove', '-f', cndbi], expected_rc=1)

    # Check the error message about missing --do-it
    assert expected_msg in result.stdout

    # Check that cn instance is still present
    helper.resync()
    assert helper.get_dbi('replication_changelog') == cndbi

    # Run dbscan -I import_file ...
    result = helper.dbscan(['-D', helper.dblib, '-f', cndbi, '-I', export_cn],
                           expected_rc=1)

    # Check the error message about missing --do-it
    assert expected_msg in result.stdout

    # Check that cn instance is still present
    helper.resync()
    assert helper.get_dbi('replication_changelog') == cndbi

    # Run dbscan --remove ... --doit
    result = helper.dbscan(['-D', helper.dblib, '--remove', '-f', cndbi, '--do-it'],
                           expected_rc=0)

    # Check the error message about missing --do-it
    assert expected_msg not in result.stdout

    # Check that cn instance is still present
    helper.resync()
    with pytest.raises(KeyError):
        helper.get_dbi('replication_changelog')

    # Run dbscan -I import_file ... --do-it
    result = helper.dbscan(['-D', helper.dblib, '-f', cndbi,
                           '-I', export_cn, '--do-it'], expected_rc=0)

    # Check the error message about missing --do-it
    assert expected_msg not in result.stdout

    # Check that cn instance is still present
    helper.resync()
    assert helper.get_dbi('replication_changelog') == cndbi

    # Export again the database
    helper.dbscan(['-D', dblib,  '-f', cndbi, '-X', export_cn2])

    # Check that content of export files are the same
    with open(export_cn) as f1:
        f1lines = f1.readlines()
    with open(export_cn2) as f2:
        f2lines = f2.readlines()
    diffs = list(context_diff(f1lines, f2lines))
    if len(diffs) > 0:
        log.debug("Export file differences are:")
        for d in diffs:
            log.debug(d)
        log_export_file(export_cn)
        log_export_file(export_cn2)
        assert diffs is None


def test_dbscan_changelog_dump(helper, request):
    """Test that dbscan remove/import actions

    :id: b6cf7922-d4c7-11ef-a028-482ae39447e5
    :setup: Stopped standalone instance
    :steps:
         1. Export chanmgelog instance with dbscan
         2. Check that replace kweyword is present in the changelog
    :expectedresults:
         1. Success
         2. Success
    """

    # Export changelog with dbscan
    cldbi = helper.get_dbi('replication_changelog')
    inst = helper.inst
    dblib = helper.dblib
    exportok = False
    result = helper.dbscan(['-D', dblib,  '-f', cldbi])
    log.info(result.stdout)
    assert 'replace: description' in result.stdout


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
