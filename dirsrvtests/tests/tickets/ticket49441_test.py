# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import pytest
import os
import ldap
from lib389._constants import *
from lib389.topologies import topology_st as topo
from lib389.utils import *

pytestmark = pytest.mark.tier2

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def test_ticket49441(topo):
    """Import ldif with large indexed binary attributes, the server should not
    crash

    :id: 4e5df145-cbd1-4955-8f77-6a7eaa14beba
    :setup: standalone topology
    :steps:
        1. Add indexes for binary attributes
        2. Perform online import
        3. Verify server is still running
    :expectedresults:
        1. Indexes are successfully added
        2. Import succeeds
        3. Server is still running
    """

    log.info('Position ldif files, and add indexes...')
    ldif_dir = topo.standalone.get_ldif_dir() + "binary.ldif"
    ldif_file = (topo.standalone.getDir(__file__, DATA_DIR) +
                 "ticket49441/binary.ldif")
    shutil.copyfile(ldif_file, ldif_dir)
    args = {INDEX_TYPE: ['eq', 'pres']}
    for attr in ('usercertificate', 'authorityrevocationlist',
                 'certificaterevocationlist', 'crosscertificatepair',
                 'cacertificate'):
        try:
            topo.standalone.index.create(suffix=DEFAULT_SUFFIX,
                                         be_name='userroot',
                                         attr=attr, args=args)
        except ldap.LDAPError as e:
            log.fatal("Failed to add index '{}' error: {}".format(attr, str(e)))
            raise e

    log.info('Import LDIF with large indexed binary attributes...')
    try:
        topo.standalone.tasks.importLDIF(suffix=DEFAULT_SUFFIX,
                                         input_file=ldif_dir,
                                         args={TASK_WAIT: True})
    except:
        log.fatal('Import failed!')
        assert False

    log.info('Verify server is still running...')
    try:
        topo.standalone.search_s("", ldap.SCOPE_BASE, "objectclass=*")
    except ldap.LDAPError as e:
        log.fatal('Server  is not alive: ' + str(e))
        assert False

    log.info('Test PASSED')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

