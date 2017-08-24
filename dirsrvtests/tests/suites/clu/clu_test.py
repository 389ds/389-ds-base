# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


def test_clu_pwdhash(topology_st):
    """Test the pwdhash script output and encrypted  password length

    :id: faaafd01-6748-4451-9d2b-f3bd47902447

    :setup: Standalone instance

    :steps:
         1. Execute /usr/bin/pwdhash -s ssha testpassword command from command line
         2. Check if there is any output
         3. Check the length of the generated output

    :expectedresults:
         1. Execution should PASS
         2. There should be an output from the command
         3. Output length should not be less than 20
    """

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

