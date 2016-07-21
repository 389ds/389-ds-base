# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
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
logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)

installation1_prefix = None


class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):
    global installation1_prefix
    if installation1_prefix:
        args_instance[SER_DEPLOYED_DIR] = installation1_prefix

    # Creating standalone instance ...
    standalone = DirSrv(verbose=False)
    args_instance[SER_HOST] = HOST_STANDALONE
    args_instance[SER_PORT] = PORT_STANDALONE
    args_instance[SER_SERVERID_PROP] = SERVERID_STANDALONE
    args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
    args_standalone = args_instance.copy()
    standalone.allocate(args_standalone)
    instance_standalone = standalone.exists()
    if instance_standalone:
        standalone.delete()
    standalone.create()
    standalone.open()

    def fin():
        standalone.delete()
    request.addfinalizer(fin)

    return TopologyStandalone(standalone)


def test_clu_init(topology):
    '''
    Write any test suite initialization here(if needed)
    '''

    return


def test_clu_pwdhash(topology):
    '''
    Test the pwdhash script
    '''

    log.info('Running test_clu_pwdhash...')

    cmd = 'pwdhash -s ssha testpassword'

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


def run_isolated():
    '''
    This test is for the simple scripts that don't have a lot of options or
    points of failure.  Scripts that do, should have their own individual tests.
    '''
    global installation1_prefix
    installation1_prefix = None

    topo = topology(True)
    test_clu_init(topo)
    test_clu_pwdhash(topo)


if __name__ == '__main__':
    run_isolated()

