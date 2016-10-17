# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

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

from lib389.plugins import *

DEBUGGING = False

if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)


log = logging.getLogger(__name__)


class TopologyStandalone(object):
    """The DS Topology Class"""
    def __init__(self, standalone):
        """Init"""
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):
    """Create DS Deployment"""

    # Creating standalone instance ...
    if DEBUGGING:
        standalone = DirSrv(verbose=True)
    else:
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
        """If we are debugging just stop the instances, otherwise remove
        them
        """
        if DEBUGGING:
            standalone.stop(60)
        else:
            standalone.delete()

    request.addfinalizer(fin)

    # Clear out the tmp dir
    standalone.clearTmpDir(__file__)

    return TopologyStandalone(standalone)


def test_plugin_management(topology):
    """
    This tests the new plugin management and enablement features.
    """


    if DEBUGGING:
        # Add debugging steps(if any)...
        pass

    for plugin in topology.standalone.plugins.list():
        print(plugin)


    # Make sure that when we get the plugin for refint, it's the correct
    # type
    refintplugin = topology.standalone.plugins.get('libreferint-plugin')
    assert(isinstance(refintplugin, ReferentialIntegrityPlugin))

    # Set and configure a simple plugin (IE no extra config)

    rolesplugin = topology.standalone.plugins.get('libroles-plugin')
    assert(isinstance(rolesplugin, Plugin))

    # Neither of these should fail
    rolesplugin.disable()
    rolesplugin.enable()

    # Prove that we can set and configure a complex plugin

    uniqplugin = topology.standalone.plugins.get('attribute uniqueness')
    assert(isinstance(uniqplugin, AttributeUniquenessPlugin))

    uniqplugin.disable()
    uniqplugin.add_unique_attribute('sn')
    uniqplugin.enable_all_subtrees()
    uniqplugin.enable()

    # Test compat with the old api

    topology.standalone.plugins.enable('attribute uniqueness')

    # Show the status

    assert(uniqplugin.status())

    log.info('Test PASSED')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)


