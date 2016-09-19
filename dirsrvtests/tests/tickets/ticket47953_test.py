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
import shutil
from lib389 import DirSrv, Entry, tools, tasks
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *

log = logging.getLogger(__name__)

class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):
    '''
        This fixture is used to standalone topology for the 'module'.
    '''
    standalone = DirSrv(verbose=False)

    # Args for the standalone instance
    args_instance[SER_HOST] = HOST_STANDALONE
    args_instance[SER_PORT] = PORT_STANDALONE
    args_instance[SER_SERVERID_PROP] = SERVERID_STANDALONE
    args_standalone = args_instance.copy()
    standalone.allocate(args_standalone)

    # Get the status of the instance and restart it if it exists
    instance_standalone = standalone.exists()

    # Remove the instance
    if instance_standalone:
        standalone.delete()

    # Create the instance
    standalone.create()

    # Used to retrieve configuration information (dbdir, confdir...)
    standalone.open()

    def fin():
        standalone.delete()
    #request.addfinalizer(fin)

    # Here we have standalone instance up and running
    return TopologyStandalone(standalone)


def test_ticket47953(topology):
    """
        Test that we can delete an aci that has an invalid syntax.
        Sart by importing an ldif with a "bad" aci, then simply try
        to remove that value without error.
    """

    log.info('Testing Ticket 47953 - Test we can delete aci that has invalid syntax')

    #
    # Import an invalid ldif
    #
    ldif_file = (topology.standalone.getDir(__file__, DATA_DIR) +
                 "ticket47953/ticket47953.ldif")
    try:
        ldif_dir = topology.standalone.get_ldif_dir()
        shutil.copy(ldif_file, ldif_dir)
        ldif_file = ldif_dir + '/ticket47953.ldif'
    except:
        log.fatal('Failed to copy ldif to instance ldif dir')
        assert False
    importTask = Tasks(topology.standalone)
    args = {TASK_WAIT: True}
    try:
        importTask.importLDIF(DEFAULT_SUFFIX, None, ldif_file, args)
    except ValueError:
        assert False

    time.sleep(2)

    #
    # Delete the invalid aci
    #
    acival = '(targetattr ="fffff")(version 3.0;acl "Directory Administrators Group"' + \
             ';allow (all) (groupdn = "ldap:///cn=Directory Administrators, dc=example,dc=com");)'

    log.info('Attempting to remove invalid aci...')
    try:
        topology.standalone.modify_s(DEFAULT_SUFFIX, [(ldap.MOD_DELETE, 'aci', acival)])
        log.info('Removed invalid aci.')
    except ldap.LDAPError as e:
        log.error('Failed to remove invalid aci: ' + e.message['desc'])
        assert False


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
