# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import pytest
from lib389.dseldif import DSEldif
from lib389.topologies import topology_st as topo

logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def test_maxbersize_repl(topo):
    """Check that instance starts when nsslapd-errorlog-maxlogsize
    nsslapd-errorlog-logmaxdiskspace are set in certain order

    :id: 743e912c-2be4-4f5f-9c2a-93dcb18f51a0
    :setup: MMR with two masters
    :steps:
        1. Stop the instance
        2. Set nsslapd-errorlog-maxlogsize before/after
           nsslapd-errorlog-logmaxdiskspace
        3. Start the instance
        4. Check the error log for errors
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. The error log should contain no errors
    """

    inst = topo.standalone
    dse_ldif = DSEldif(inst)

    inst.stop()
    log.info("Set nsslapd-errorlog-maxlogsize before nsslapd-errorlog-logmaxdiskspace")
    dse_ldif.replace('cn=config', 'nsslapd-errorlog-maxlogsize', '300')
    dse_ldif.replace('cn=config', 'nsslapd-errorlog-logmaxdiskspace', '500')
    inst.start()
    log.info("Assert no init_dse_file errors in the error log")
    assert not inst.ds_error_log.match('.*ERR - init_dse_file.*')

    inst.stop()
    log.info("Set nsslapd-errorlog-maxlogsize after nsslapd-errorlog-logmaxdiskspace")
    dse_ldif.replace('cn=config', 'nsslapd-errorlog-logmaxdiskspace', '500')
    dse_ldif.replace('cn=config', 'nsslapd-errorlog-maxlogsize', '300')
    inst.start()
    log.info("Assert no init_dse_file errors in the error log")
    assert not inst.ds_error_log.match('.*ERR - init_dse_file.*')
