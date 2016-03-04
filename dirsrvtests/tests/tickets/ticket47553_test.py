# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
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

CONTAINER_1_OU = 'test_ou_1'
CONTAINER_2_OU = 'test_ou_2'
CONTAINER_1 = 'ou=%s,dc=example,dc=com' % CONTAINER_1_OU
CONTAINER_2 = 'ou=%s,dc=example,dc=com' % CONTAINER_2_OU
USER_CN = 'test_user'
USER_PWD = 'Secret123'
USER = 'cn=%s,%s' % (USER_CN, CONTAINER_1)


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


@pytest.fixture(scope="module")
def env_setup(topology):
    """Adds two containers, one user and two ACI rules"""

    try:
        log.info("Add a container: %s" % CONTAINER_1)
        topology.standalone.add_s(Entry((CONTAINER_1,
                                         {'objectclass': 'top',
                                          'objectclass': 'organizationalunit',
                                          'ou': CONTAINER_1_OU,
                                          })))

        log.info("Add a container: %s" % CONTAINER_2)
        topology.standalone.add_s(Entry((CONTAINER_2,
                                         {'objectclass': 'top',
                                          'objectclass': 'organizationalunit',
                                          'ou': CONTAINER_2_OU,
                                          })))

        log.info("Add a user: %s" % USER)
        topology.standalone.add_s(Entry((USER,
                                         {'objectclass': 'top person'.split(),
                                          'cn': USER_CN,
                                          'sn': USER_CN,
                                          'userpassword': USER_PWD
                                          })))
    except ldap.LDAPError as e:
        log.error('Failed to add object to database: %s' % e.message['desc'])
        assert False

    ACI_TARGET = '(targetattr="*")'
    ACI_ALLOW = '(version 3.0; acl "All rights for %s"; allow (all) ' % USER
    ACI_SUBJECT = 'userdn="ldap:///%s";)' % USER
    ACI_BODY = ACI_TARGET + ACI_ALLOW + ACI_SUBJECT
    mod = [(ldap.MOD_ADD, 'aci', ACI_BODY)]

    try:
        log.info("Add an ACI 'allow (all)' by %s to the %s" % (USER,
                                                               CONTAINER_1))
        topology.standalone.modify_s(CONTAINER_1, mod)

        log.info("Add an ACI 'allow (all)' by %s to the %s" % (USER,
                                                               CONTAINER_2))
        topology.standalone.modify_s(CONTAINER_2, mod)
    except ldap.LDAPError as e:
        log.fatal('Failed to add ACI: error (%s)' % (e.message['desc']))
        assert False


def test_ticket47553(topology, env_setup):
    """Tests, that MODRDN operation is allowed,
    if user has ACI right '(all)' under superior entries,
    but doesn't have '(modrdn)'
    """

    log.info("Bind as %s" % USER)
    try:
        topology.standalone.simple_bind_s(USER, USER_PWD)
    except ldap.LDAPError as e:
        log.error('Bind failed for %s, error %s' % (USER, e.message['desc']))
        assert False

    log.info("User MODRDN operation from %s to %s" % (CONTAINER_1,
                                                      CONTAINER_2))
    try:
        topology.standalone.rename_s(USER, "cn=%s" % USER_CN,
                                     newsuperior=CONTAINER_2, delold=1)
    except ldap.LDAPError as e:
        log.error('MODRDN failed for %s, error %s' % (USER, e.message['desc']))
        assert False

    try:
        log.info("Check there is no user in %s" % CONTAINER_1)
        entries = topology.standalone.search_s(CONTAINER_1,
                                               ldap.SCOPE_ONELEVEL,
                                               'cn=%s' % USER_CN)
        assert not entries

        log.info("Check there is our user in %s" % CONTAINER_2)
        entries = topology.standalone.search_s(CONTAINER_2,
                                               ldap.SCOPE_ONELEVEL,
                                               'cn=%s' % USER_CN)
        assert entries
    except ldap.LDAPError as e:
        log.fatal('Search failed, error: ' + e.message['desc'])
        assert False


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    # -v for additional verbose
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
