# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
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
from lib389.topologies import topology_st as topo

pytestmark = pytest.mark.tier2

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

USER_DN = 'uid=testuser,dc=example,dc=com'
acis = ['(targetattr != "tele*") (version 3.0;acl "test case";allow (read,compare,search)(userdn = "ldap:///anyone");)',
        '(targetattr != "TELE*") (version 3.0;acl "test case";allow (read,compare,search)(userdn = "ldap:///anyone");)',
        '(targetattr != "telephonenum*") (version 3.0;acl "test case";allow (read,compare,search)(userdn = "ldap:///anyone");)',
        '(targetattr != "TELEPHONENUM*") (version 3.0;acl "test case";allow (read,compare,search)(userdn = "ldap:///anyone");)']


def test_ticket49095(topo):
    """Check that target attrbiutes with wildcards are case insensitive
    """

    # Add an entry
    try:
        topo.standalone.add_s(Entry((USER_DN, {
            'objectclass': 'top extensibleObject'.split(),
            'uid': 'testuser',
            'telephonenumber': '555-555-5555'
        })))
    except ldap.LDAPError as e:
            log.fatal('Failed to add test user: ' + e.args[0]['desc'])
            assert False

    for aci in acis:
        # Add ACI
        try:
            topo.standalone.modify_s(DEFAULT_SUFFIX,
                          [(ldap.MOD_REPLACE, 'aci', ensure_bytes(aci))])

        except ldap.LDAPError as e:
            log.fatal('Failed to set aci: ' + aci + ': ' + e.args[0]['desc'])
            assert False

        # Set Anonymous Bind to test aci
        try:
            topo.standalone.simple_bind_s("", "")
        except ldap.LDAPError as e:
            log.fatal('Failed to bind anonymously: ' + e.args[0]['desc'])
            assert False

        # Search for entry - should not get any results
        try:
            entry = topo.standalone.search_s(DEFAULT_SUFFIX, ldap.SCOPE_BASE,
                                             'telephonenumber=*')
            if entry:
                log.fatal('The entry was incorrectly returned')
                assert False
        except ldap.LDAPError as e:
            log.fatal('Failed to search anonymously: ' + e.args[0]['desc'])
            assert False

        # Set root DN Bind so we can update aci's
        try:
            topo.standalone.simple_bind_s(DN_DM, PASSWORD)
        except ldap.LDAPError as e:
            log.fatal('Failed to bind anonymously: ' + e.args[0]['desc'])
            assert False

    log.info("Test Passed")


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)

