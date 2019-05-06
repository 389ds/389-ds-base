# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import shutil

import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st

from lib389._constants import DATA_DIR, DEFAULT_SUFFIX

pytestmark = pytest.mark.tier2

log = logging.getLogger(__name__)


def test_ticket47953(topology_st):
    """
        Test that we can delete an aci that has an invalid syntax.
        Sart by importing an ldif with a "bad" aci, then simply try
        to remove that value without error.
    """

    log.info('Testing Ticket 47953 - Test we can delete aci that has invalid syntax')

    #
    # Import an invalid ldif
    #
    ldif_file = (topology_st.standalone.getDir(__file__, DATA_DIR) +
                 "ticket47953/ticket47953.ldif")
    try:
        ldif_dir = topology_st.standalone.get_ldif_dir()
        shutil.copy(ldif_file, ldif_dir)
        ldif_file = ldif_dir + '/ticket47953.ldif'
    except:
        log.fatal('Failed to copy ldif to instance ldif dir')
        assert False
    importTask = Tasks(topology_st.standalone)
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
        topology_st.standalone.modify_s(DEFAULT_SUFFIX, [(ldap.MOD_DELETE, 'aci', ensure_bytes(acival))])
        log.info('Removed invalid aci.')
    except ldap.LDAPError as e:
        log.error('Failed to remove invalid aci: ' + e.args[0]['desc'])
        assert False


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
