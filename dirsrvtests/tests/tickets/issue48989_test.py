import os
import time
import ldap
import logging
import pytest
from lib389.topologies import topology_st
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from lib389.utils import *

DEBUGGING = False

if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def test_bytessent_overflow(topology_st):
    """
    Issue 48989 - Add 10k entries and run search until the value of bytessent is
    bigger than 2^32 or resets to 0
    """

    # Create users
    topology_st.standalone.ldclt.create_users('ou=People,%s' %
        DEFAULT_SUFFIX, min=0, max=10000)
    bytessent = int(topology_st.standalone.search_s(
        'cn=monitor', ldap.SCOPE_BASE, attrlist=['bytessent'])[0].getValue('bytessent'))
    bytessent_old = bytessent

    while bytessent < 4300000000:
        # Do searches
        topology_st.standalone.search_s(DEFAULT_SUFFIX,
                                        ldap.SCOPE_SUBTREE,
                                        filterstr='(objectClass=*)')

        # Read bytessent value from cn=monitor
        bytessent = int(topology_st.standalone.search_s(
            'cn=monitor', ldap.SCOPE_BASE, attrlist=['bytessent'])[0].getValue('bytessent'))

        if bytessent > bytessent_old:
            bytessent_old = bytessent
        else:
            # If it overflows - test failed
            assert(bytessent > 4294967295)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)


