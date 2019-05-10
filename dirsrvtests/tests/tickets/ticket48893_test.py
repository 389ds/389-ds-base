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


def _attr_present(conn):
    results = conn.search_s('cn=config', ldap.SCOPE_SUBTREE, '(objectClass=*)')
    if DEBUGGING:
        print(results)
    if len(results) > 0:
        return True
    return False


def test_ticket48893(topology_st):
    """
    Test that anonymous has NO VIEW to cn=config
    """

    if DEBUGGING:
        # Add debugging steps(if any)...
        pass

    # Do an anonymous bind
    conn = ldap.initialize("ldap://%s:%s" % (HOST_STANDALONE, PORT_STANDALONE))
    conn.simple_bind_s()

    # Make sure that we cannot see what's in cn=config as anonymous
    assert (not _attr_present(conn))

    conn.unbind_s()

    log.info('Test PASSED')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
