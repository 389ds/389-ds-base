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


class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):
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
        # This is useful for analysing the test env.
        #standalone.db2ldif(bename=DEFAULT_BENAME, suffixes=[DEFAULT_SUFFIX], excludeSuffixes=[], encrypt=False, \
        #    repl_data=True, outputfile='%s/ldif/%s.ldif' % (standalone.dbdir,SERVERID_STANDALONE ))
        #standalone.clearBackupFS()
        #standalone.backupFS()
        standalone.delete()
    request.addfinalizer(fin)

    # Clear out the tmp dir
    standalone.clearTmpDir(__file__)

    return TopologyStandalone(standalone)


def test_ticket48665(topology):
    """
    This tests deletion of certain cn=config values.

    First, it should be able to delete, and not crash the server.

    Second, we might be able to delete then add to replace values.

    We should also still be able to mod replace the values and keep the server alive.
    """
    #topology.standalone.config.enable_log('audit')
    #topology.standalone.config.enable_log('auditfail')
    # This will trigger a mod delete then add.
    try:
        modlist = [(ldap.MOD_DELETE, 'nsslapd-cachememsize', None), (ldap.MOD_ADD, 'nsslapd-cachememsize', '1')]
        topology.standalone.modify_s("cn=%s,cn=ldbm database,cn=plugins,cn=config" % DEFAULT_BENAME,
            modlist)
    except:
        pass
    # Check the server has not commited seppuku.
    result = topology.standalone.whoami_s()
    assert(DN_DM.lower() in result.lower())

    # This has a magic hack to determine if we are in cn=config.
    try:
        topology.standalone.modify_s(DEFAULT_BENAME, [(ldap.MOD_REPLACE,
                                                       'nsslapd-cachememsize', '1')])
    except ldap.LDAPError as e:
        log.fatal('Failed to change nsslapd-cachememsize ' + e.message['desc'])

    # Check the server has not commited seppuku.
    result = topology.standalone.whoami_s()
    assert(DN_DM.lower() in result.lower())

    # Now try with mod_replace. This should be okay.

    modlist = [(ldap.MOD_REPLACE, 'nsslapd-cachememsize', '1')]
    topology.standalone.modify_s("cn=%s,cn=ldbm database,cn=plugins,cn=config" % DEFAULT_BENAME,
        modlist)
    # Check the server has not commited seppuku.
    result = topology.standalone.whoami_s()
    assert(DN_DM.lower() in result.lower())

    log.info('Test complete')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
