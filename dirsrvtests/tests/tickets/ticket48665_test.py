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

from lib389._constants import DEFAULT_SUFFIX, DEFAULT_BENAME

pytestmark = pytest.mark.tier2

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


def test_ticket48665(topology_st):
    """
    This tests deletion of certain cn=config values.

    First, it should be able to delete, and not crash the server.

    Second, we might be able to delete then add to replace values.

    We should also still be able to mod replace the values and keep the server alive.
    """
    # topology_st.standalone.config.enable_log('audit')
    # topology_st.standalone.config.enable_log('auditfail')
    # This will trigger a mod delete then add.

    topology_st.standalone.modify_s('cn=config,cn=ldbm database,cn=plugins,cn=config',
                                    [(ldap.MOD_REPLACE, 'nsslapd-cache-autosize', b'0')])

    try:
        modlist = [(ldap.MOD_DELETE, 'nsslapd-cachememsize', None), (ldap.MOD_ADD, 'nsslapd-cachememsize', b'1')]
        topology_st.standalone.modify_s("cn=%s,cn=ldbm database,cn=plugins,cn=config" % DEFAULT_BENAME,
                                        modlist)
    except:
        pass

    # Check the server has not commited seppuku.
    entries = topology_st.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(cn=*)')
    assert len(entries) > 0
    log.info('{} entries are returned from the server.'.format(len(entries)))

    # This has a magic hack to determine if we are in cn=config.
    try:
        topology_st.standalone.modify_s(DEFAULT_BENAME, [(ldap.MOD_REPLACE,
                                                          'nsslapd-cachememsize', b'1')])
    except ldap.LDAPError as e:
        log.fatal('Failed to change nsslapd-cachememsize ' + e.args[0]['desc'])

    # Check the server has not commited seppuku.
    entries = topology_st.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(cn=*)')
    assert len(entries) > 0
    log.info('{} entries are returned from the server.'.format(len(entries)))

    # Now try with mod_replace. This should be okay.

    modlist = [(ldap.MOD_REPLACE, 'nsslapd-cachememsize', b'1')]
    topology_st.standalone.modify_s("cn=%s,cn=ldbm database,cn=plugins,cn=config" % DEFAULT_BENAME,
                                    modlist)

    # Check the server has not commited seppuku.
    entries = topology_st.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_SUBTREE, '(cn=*)')
    assert len(entries) > 0
    log.info('{} entries are returned from the server.'.format(len(entries)))

    log.info('Test complete')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
