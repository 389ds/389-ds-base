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
from lib389._constants import PW_DM, DBSCAN
from lib389.topologies import topology_m2 as topo_m2
from difflib import context_diff

pytestmark = pytest.mark.tier0

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

DEBUGGING = os.getenv("DEBUGGING", default=False)


class CalledProcessUnexpectedReturnCode(subprocess.CalledProcessError):
    def __init__(self, result, expected_rc):
        super().__init__(cmd=result.args, returncode=result.returncode, output=result.stdout, stderr=result.stderr)
        self.expected_rc = expected_rc
        self.result = result

    def __str__(self):
        return f'Command {self.result.args} returned {self.result.returncode} instead of {self.expected_rc}'


class DbscanPaths:
    @staticmethod
    def list_instances(inst, dblib, dbhome):
        # compute db instance pathnames
        instances = dbscan(['-D', dblib, '-L', dbhome], inst=inst).stdout
        dbis = []
        if dblib == 'bdb':
            pattern = r'^ (.*) $'
            prefix = f'{dbhome}/'
        else:
            pattern = r'^ (.*) flags:'
            prefix = f''
        for match in re.finditer(pattern, instances, flags=re.MULTILINE):
            dbis.append(prefix+match.group(1))
        return dbis

    @staticmethod
    def list_options(inst):
        # compute supported options
        options = []
        usage = dbscan(['-h'], inst=inst, expected_rc=None).stdout
        pattern = r'^\s+(?:(-[^-,]+), +)?(--[^ ]+).*$'
        for match in re.finditer(pattern, usage, flags=re.MULTILINE):
            for idx in range(1,3):
                if not match.group(idx) is None:
                    options.append(match.group(idx))
        return options

    def __init__(self, inst):
        dblib = inst.get_db_lib()
        dbhome = inst.ds_paths.db_home_dir
        self.inst = inst
        self.dblib = dblib
        self.dbhome = dbhome
        self.options = DbscanPaths.list_options(inst)
        self.dbis = DbscanPaths.list_instances(inst, dblib, dbhome)
        self.ldif_dir = inst.ds_paths.ldif_dir

    def get_dbi(self, attr, backend='userroot'):
        for dbi in self.dbis:
            if f'{backend}/{attr}.'.lower() in dbi.lower():
                return dbi
        raise KeyError(f'Unknown dbi {backend}/{attr}')

    def __repr__(self):
        attrs = ['inst', 'dblib', 'dbhome', 'ldif_dir', 'options', 'dbis' ]
        res = ", ".join(map(lambda x: f'{x}={self.__dict__[x]}', attrs))
        return f'DbscanPaths({res})'


def dbscan(args, inst=None, expected_rc=0):
    if inst is None:
        prefix = os.environ.get('PREFIX', "")
        prog = f'{prefix}/bin/dbscan'
    else:
        prog = os.path.join(inst.ds_paths.bin_dir, DBSCAN)
    args.insert(0, prog)
    output = subprocess.run(args, encoding='utf-8', stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    log.debug(f'{args} result is {output.returncode} output is {output.stdout}')
    if expected_rc != output.returncode and not expected_rc is None:
        raise CalledProcessUnexpectedReturnCode(output, expected_rc)
    return output


def log_export_file(filename):
    with open(filename, 'r') as file:
        log.debug(f'=========== Dump of {filename} ================')
        for line in file:
            log.debug(line.rstrip('\n'))
        log.debug(f'=========== Enf of {filename} =================')


@pytest.fixture(scope='module')
def paths(topo_m2, request):
    inst = topo_m2.ms["supplier1"]
    if sys.version_info < (3,5):
        pytest.skip('requires python version >= 3.5')
    paths = DbscanPaths(inst)
    if not '--do-it' in paths.options:
       pytest.skip('Not supported with this dbscan version')
    inst.stop()
    return paths


def test_dbscan_destructive_actions(paths, request):
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
    export_cn = f'{paths.ldif_dir}/dbscan_cn.data'
    export_cn2 = f'{paths.ldif_dir}/dbscan_cn2.data'
    cndbi = paths.get_dbi('replication_changelog')
    inst = paths.inst
    dblib = paths.dblib
    exportok = False
    def fin():
        if os.path.exists(export_cn):
            # Restore cn if it was exported successfully but does not exists any more
            if exportok and not cndbi in DbscanPaths.list_instances(inst, dblib, paths.dbhome):
                    dbscan(['-D', dblib, '-f', cndbi, '-I', export_cn, '--do-it'], inst=inst)
            if not DEBUGGING:
                os.remove(export_cn)
        if os.path.exists(export_cn) and not DEBUGGING:
            os.remove(export_cn2)

    fin()
    request.addfinalizer(fin)
    dbscan(['-D', dblib,  '-f', cndbi, '-X', export_cn], inst=inst)
    exportok = True

    expected_msg = "without specifying '--do-it' parameter."

    # Run dbscan --remove ...
    result = dbscan(['-D', paths.dblib, '--remove', '-f', cndbi],
                    inst=paths.inst, expected_rc=1)

    # Check the error message about missing --do-it
    assert expected_msg in result.stdout

    # Check that cn instance is still present
    curdbis = DbscanPaths.list_instances(paths.inst, paths.dblib, paths.dbhome)
    assert cndbi in curdbis

    # Run dbscan -I import_file ...
    result = dbscan(['-D', paths.dblib, '-f', cndbi, '-I', export_cn],
                    inst=paths.inst, expected_rc=1)

    # Check the error message about missing --do-it
    assert expected_msg in result.stdout

    # Check that cn instance is still present
    curdbis = DbscanPaths.list_instances(paths.inst, paths.dblib, paths.dbhome)
    assert cndbi in curdbis

    # Run dbscan --remove ... --doit
    result = dbscan(['-D', paths.dblib, '--remove', '-f', cndbi, '--do-it'],
                    inst=paths.inst, expected_rc=0)

    # Check the error message about missing --do-it
    assert not expected_msg in result.stdout

    # Check that cn instance is still present
    curdbis = DbscanPaths.list_instances(paths.inst, paths.dblib, paths.dbhome)
    assert not cndbi in curdbis

    # Run dbscan -I import_file ... --do-it
    result = dbscan(['-D', paths.dblib, '-f', cndbi,
                     '-I', export_cn, '--do-it'],
                    inst=paths.inst, expected_rc=0)

    # Check the error message about missing --do-it
    assert not expected_msg in result.stdout

    # Check that cn instance is still present
    curdbis = DbscanPaths.list_instances(paths.inst, paths.dblib, paths.dbhome)
    assert cndbi in curdbis

    # Export again the database
    dbscan(['-D', dblib,  '-f', cndbi, '-X', export_cn2], inst=inst)

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


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
