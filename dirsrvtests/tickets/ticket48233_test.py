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

    # Delete each instance in the end
    def fin():
        standalone.delete()
    request.addfinalizer(fin)

    # Clear out the tmp dir
    standalone.clearTmpDir(__file__)

    return TopologyStandalone(standalone)


def test_ticket48233(topology):
    """Test that ACI's that use IP restrictions do not crash the server at
       shutdown
    """

    # Add aci to restrict access my ip
    aci_text = ('(targetattr != "userPassword")(version 3.0;acl ' +
                '"Enable anonymous access - IP"; allow (read,compare,search)' +
                '(userdn = "ldap:///anyone") and (ip="127.0.0.1");)')

    try:
        topology.standalone.modify_s(DEFAULT_SUFFIX, [(ldap.MOD_ADD, 'aci', aci_text)])
    except ldap.LDAPError as e:
        log.error('Failed to add aci: (%s) error %s' % (aci_text, e.message['desc']))
        assert False
    time.sleep(1)

    # Anonymous search to engage the aci
    try:
        topology.standalone.simple_bind_s("", "")
    except ldap.LDAPError as e:
        log.error('Failed to anonymously bind -error %s' % (e.message['desc']))
        assert False

    try:
        entries = topology.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, 'objectclass=*')
        if not entries:
            log.fatal('Failed return an entries from search')
            assert False
    except ldap.LDAPError, e:
        log.fatal('Search failed: ' + e.message['desc'])
        assert False

    # Restart the server
    topology.standalone.restart(timeout=10)

    # Check for crash
    if topology.standalone.detectDisorderlyShutdown():
        log.fatal('Server crashed!')
        assert False

    log.info('Test complete')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)