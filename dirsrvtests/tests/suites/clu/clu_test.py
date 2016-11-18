# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import os
import sys
import time
import ldap
import logging
import pytest
from lib389 import DirSrv, Entry, tools, tasks
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


def test_clu_pwdhash(topology_st):
    """Test the pwdhash script"""

    log.info('Running test_clu_pwdhash...')

    cmd = '%s -s ssha testpassword' % os.path.join(topology_st.standalone.get_bin_dir(), 'pwdhash')

    p = os.popen(cmd)
    result = p.readline()
    p.close()

    if not result:
        log.fatal('test_clu_pwdhash: Failed to run pwdhash')
        assert False

    if len(result) < 20:
        log.fatal('test_clu_pwdhash: Encrypted password is too short')
        assert False

    log.info('pwdhash generated: ' + result)
    log.info('test_clu_pwdhash: PASSED')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
