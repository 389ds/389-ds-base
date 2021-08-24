# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
import os
import logging
import subprocess

from lib389.topologies import topology_st as topo

pytestmark = pytest.mark.tier0

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

def test_restore_config(topo):
    """
    Check that if a dse.ldif and backup are removed, that the server still starts.

    :id: e1c38fa7-30bc-46f2-a934-f8336f387581
    :setup: Standalone instance
    :steps:
        1. Stop the instance
        2. Delete 'dse.ldif'
        3. Start the instance
    :expectedresults:
        1. Steps 1 and 2 succeed.
        2. Server will succeed to start with restored cfg.
    """
    topo.standalone.stop()

    dse_path = topo.standalone.get_config_dir()

    log.info(dse_path)

    for i in ('dse.ldif', 'dse.ldif.startOK'):
        p = os.path.join(dse_path, i)
        d = os.path.join(dse_path, i + '.49298')
        os.rename(p, d)

    # This will pass.
    topo.standalone.start()

def test_removed_config(topo):
    """
    Check that if a dse.ldif and backup are removed, that the server
    exits better than "segfault".

    :id: b45272d1-c197-473e-872f-07257fcb2ec0
    :setup: Standalone instance
    :steps:
        1. Stop the instance
        2. Delete 'dse.ldif', 'dse.ldif.bak', 'dse.ldif.startOK'
        3. Start the instance
    :expectedresults:
        1. Steps 1 and 2 succeed.
        2. Server will fail to start, but will not crash.
    """
    topo.standalone.stop()

    dse_path = topo.standalone.get_config_dir()

    log.info(dse_path)

    for i in ('dse.ldif', 'dse.ldif.bak', 'dse.ldif.startOK'):
        p = os.path.join(dse_path, i)
        d = os.path.join(dse_path, i + '.49298')
        os.rename(p, d)

    # We actually can't check the log output, because it can't read dse.ldif,
    # don't know where to write it yet! All we want is the server fail to
    # start here, rather than infinite run + segfault.
    with pytest.raises(subprocess.CalledProcessError):
        topo.standalone.start()

    # Restore the files so that setup-ds.l can work
    for i in ('dse.ldif', 'dse.ldif.bak', 'dse.ldif.startOK'):
        p = os.path.join(dse_path, i)
        d = os.path.join(dse_path, i + '.49298')
        os.rename(d, p)

