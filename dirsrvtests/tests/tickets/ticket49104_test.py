# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import subprocess

import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st

# Skip on older versions
pytestmark = [pytest.mark.tier2,
              pytest.mark.skipif(ds_is_older('1.3.6'), reason="Not implemented")]
log = logging.getLogger(__name__)

def test_ticket49104_setup(topology_st):
    """
    Generate an ldif file having 10K entries and import it.
    """
    # Generate a test ldif (100k entries)
    ldif_dir = topology_st.standalone.get_ldif_dir()
    import_ldif = ldif_dir + '/49104.ldif'
    try:
        topology_st.standalone.buildLDIF(100000, import_ldif)
    except OSError as e:
        log.fatal('ticket 49104: failed to create test ldif,\
                  error: %s - %s' % (e.errno, e.strerror))
        assert False

    # Online
    try:
        topology_st.standalone.tasks.importLDIF(suffix=DEFAULT_SUFFIX,
                                                input_file=import_ldif,
                                                args={TASK_WAIT: True})
    except ValueError:
        log.fatal('ticket 49104: Online import failed')
        assert False

def test_ticket49104(topology_st):
    """
    Run dbscan with valgrind changing the truncate size.
    If there is no Invalid report, we can claim the test has passed.
    """
    log.info("Test ticket 49104 -- dbscan crashes by memory corruption")
    myvallog = '/tmp/val49104.out'
    if os.path.exists(myvallog):
        os.remove(myvallog)
    prog = os.path.join(topology_st.standalone.get_bin_dir(), 'dbscan-bin')
    valcmd = 'valgrind --tool=memcheck --leak-check=yes --num-callers=40 --log-file=%s ' % myvallog
    if topology_st.standalone.has_asan():
        valcmd = ''
    id2entry = os.path.join(topology_st.standalone.dbdir, DEFAULT_BENAME, 'id2entry.db')

    for i in range(20, 30):
        cmd = valcmd + '%s -f %s -t %d -R' % (prog, id2entry , i)
        log.info('Running script: %s' % cmd)
        proc = subprocess.Popen(cmd.split(), stdout=subprocess.PIPE)
        outs = ''
        try:
            outs = proc.communicate()
        except OSError as e:
            log.exception('dbscan: error executing (%s): error %d - %s' %
            (cmd, e.errno, e.strerror))
            raise e

        # If we have asan, this fails in other spectacular ways instead
        if not topology_st.standalone.has_asan():
            grep = 'egrep "Invalid read|Invalid write" %s' % myvallog
            p = os.popen(grep, "r")
            l = p.readline()
            if 'Invalid' in l:
                log.fatal('ERROR: valgrind reported invalid read/write: %s' % l)
                assert False

    log.info('ticket 49104 - PASSED')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
