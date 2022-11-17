# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
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

from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.idm.services import ServiceAccounts

from lib389.topologies import topology_st as topology

DEBUGGING = os.getenv('DEBUGGING', False)

if DEBUGGING is not False:
    DEBUGGING = True

if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)

log = logging.getLogger(__name__)

def test_services(topology):
    """
    Test and assert that a simple service account can be bound to and created.

    These are really useful in simple tests.
    """
    ous = OrganizationalUnits(topology.standalone, DEFAULT_SUFFIX)
    services = ServiceAccounts(topology.standalone, DEFAULT_SUFFIX)

    # Create the OU for them.
    ous.create(properties={
            'ou': 'Services',
            'description': 'Computer Service accounts which request DS bind',
        })
    # Now, we can create the services from here.
    service = services.create(properties={
        'cn': 'testbind',
        'userPassword': 'Password1'
        })

    conn = service.bind('Password1')
    conn.unbind_s()



