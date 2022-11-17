# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
from lib389.utils import *
from lib389.topologies import topology_st

from lib389._constants import DEFAULT_SUFFIX, HOST_STANDALONE, PORT_STANDALONE

pytestmark = pytest.mark.tier2

DEBUGGING = os.getenv('DEBUGGING', False)

if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)

log = logging.getLogger(__name__)


def _attr_present(conn, name):
    results = conn.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(%s=*)' % name, [name, ])
    if DEBUGGING:
        print(results)
    if len(results) > 0:
        return True
    return False


def test_ticket48354(topology_st):
    """
    Test that we cannot view ACIs, userPassword, or certain other attributes as anonymous.
    """

    if DEBUGGING:
        # Add debugging steps(if any)...
        pass

    # Do an anonymous bind
    conn = ldap.initialize("ldap://%s:%s" % (HOST_STANDALONE, PORT_STANDALONE))
    conn.simple_bind_s()

    # Make sure that we cannot see:
    # * userPassword
    assert (not _attr_present(conn, 'userPassword'))
    # * aci
    assert (not _attr_present(conn, 'aci'))
    # * anything else?

    conn.unbind_s()

    log.info('Test PASSED')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
