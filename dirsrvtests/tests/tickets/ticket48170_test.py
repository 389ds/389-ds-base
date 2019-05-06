# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
from lib389.utils import *
from lib389.topologies import topology_st

pytestmark = pytest.mark.tier2

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


def test_ticket48170(topology_st):
    '''
    Attempt to add a nsIndexType wikth an invalid value:  "eq,pres"
    '''

    INDEX_DN = 'cn=cn,cn=index,cn=userroot,cn=ldbm database,cn=plugins,cn=config'
    REJECTED = False
    try:
        topology_st.standalone.modify_s(INDEX_DN, [(ldap.MOD_ADD, 'nsINdexType', b'eq,pres')])
    except ldap.UNWILLING_TO_PERFORM:
        log.info('Index update correctly rejected')
        REJECTED = True

    if not REJECTED:
        log.fatal('Invalid nsIndexType value was incorrectly accepted.')
        assert False

    log.info('Test complete')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
