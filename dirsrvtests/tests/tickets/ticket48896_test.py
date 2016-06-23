# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
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

CONFIG_DN = 'cn=config'
UID = 'buser123'
TESTDN = 'uid=%s,' % UID + DEFAULT_SUFFIX

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

def check_attr_val(topology, dn, attr, expected):
    try:
        centry = topology.standalone.search_s(dn, ldap.SCOPE_BASE, 'cn=*')
        if centry:
            val = centry[0].getValue(attr)
            if val == expected:
                log.info('Default value of %s is %s' % (attr, expected))
            else:
                log.info('Default value of %s is not %s, but %s' % (attr, expected, val))
                assert False
        else:
            log.fatal('Failed to get %s' % dn)
            assert False
    except ldap.LDAPError as e:
        log.fatal('Failed to search ' + dn + ': ' + e.message['desc'])
        assert False

def replace_pw(server, curpw, newpw, expstr, rc):
    log.info('Binding as {%s, %s}' % (TESTDN, curpw))
    server.simple_bind_s(TESTDN, curpw)

    hit = 0
    log.info('Replacing password: %s -> %s, which should %s' % (curpw, newpw, expstr))
    try:
        server.modify_s(TESTDN, [(ldap.MOD_REPLACE, 'userPassword', newpw)])
    except Exception as e:
        log.info("Exception (expected): %s" % type(e).__name__)
        hit = 1
        assert isinstance(e, rc)

    if (0 != rc) and (0 == hit):
        log.info('Expected to fail with %d, but passed' % rc)
        assert False

    log.info('PASSED')

def test_ticket48896(topology):
    """
    """
    log.info('Testing Ticket 48896 - Default Setting for passwordMinTokenLength does not work')

    log.info("Setting global password policy with password syntax.")
    topology.standalone.simple_bind_s(DN_DM, PASSWORD)
    topology.standalone.modify_s(CONFIG_DN, [(ldap.MOD_REPLACE, 'passwordCheckSyntax', 'on'),
                                             (ldap.MOD_REPLACE, 'nsslapd-pwpolicy-local', 'on')])

    config = topology.standalone.search_s(CONFIG_DN, ldap.SCOPE_BASE, 'cn=*')
    mintokenlen = config[0].getValue('passwordMinTokenLength')
    history = config[0].getValue('passwordInHistory')

    log.info('Default passwordMinTokenLength == %s' % mintokenlen)
    log.info('Default passwordInHistory == %s' % history)

    log.info('Adding a user.')
    curpw = 'password'
    topology.standalone.add_s(Entry((TESTDN,
                                     {'objectclass': "top person organizationalPerson inetOrgPerson".split(),
                                      'cn': 'test user',
                                      'sn': 'user',
                                      'userPassword': curpw})))

    newpw = 'Abcd012+'
    exp = 'be ok'
    rc = 0
    replace_pw(topology.standalone, curpw, newpw, exp, rc)

    curpw = 'Abcd012+'
    newpw = 'user'
    exp = 'fail'
    rc = ldap.CONSTRAINT_VIOLATION
    replace_pw(topology.standalone, curpw, newpw, exp, rc)

    curpw = 'Abcd012+'
    newpw = UID
    exp = 'fail'
    rc = ldap.CONSTRAINT_VIOLATION
    replace_pw(topology.standalone, curpw, newpw, exp, rc)

    curpw = 'Abcd012+'
    newpw = 'Tuse!1234'
    exp = 'fail'
    rc = ldap.CONSTRAINT_VIOLATION
    replace_pw(topology.standalone, curpw, newpw, exp, rc)

    curpw = 'Abcd012+'
    newpw = 'Tuse!0987'
    exp = 'fail'
    rc = ldap.CONSTRAINT_VIOLATION
    replace_pw(topology.standalone, curpw, newpw, exp, rc)

    curpw = 'Abcd012+'
    newpw = 'Tabc!1234'
    exp = 'fail'
    rc = ldap.CONSTRAINT_VIOLATION
    replace_pw(topology.standalone, curpw, newpw, exp, rc)

    curpw = 'Abcd012+'
    newpw = 'Direc+ory389'
    exp = 'be ok'
    rc = 0
    replace_pw(topology.standalone, curpw, newpw, exp, rc)

    log.info('SUCCESS')

if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode

    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
