# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
'''
Created on Dec 12, 2019

@author: tbordaz
'''
import logging
import subprocess
import pytest
from lib389 import Entry
from lib389.utils import *
from lib389.plugins import *
from lib389._constants import *
from lib389.idm.user import UserAccounts, UserAccount
from lib389.idm.group import Groups
from lib389.topologies import topology_st as topo

pytestmark = pytest.mark.tier1

log = logging.getLogger(__name__)

ESCAPED_RDN_BASE = "foo\\,oo"
def _user_get_dn(no):
    uid = '%s%d' % (ESCAPED_RDN_BASE, no)
    dn = 'uid=%s,%s' % (uid, SUFFIX)
    return (uid, dn)

def add_escaped_user(server, no):
    (uid, dn) = _user_get_dn(no)
    log.fatal('Adding user (%s): ' % dn)
    server.add_s(Entry((dn, {'objectclass': ['top', 'person', 'organizationalPerson', 'inetOrgPerson'],
                             'uid': [uid],
                             'sn' : [uid],
                             'cn' : [uid]})))
    return dn

@pytest.mark.ds50020
def test_referential_false_failure(topo):
    """On MODRDN referential integrity can erronously fail

    :id: f77aeb80-c4c4-471b-8c1b-4733b714778b
    :setup: Standalone Instance
    :steps:
        1. Configure the plugin
        2. Create a group
            - 1rst member the one that will be move
            - more than 128 members
            - last member is a DN containing escaped char
        3. Rename the 1rst member
    :expectedresults:
        1. should succeed
        2. should succeed
        3. should succeed
    """

    inst = topo[0]

    # stop the plugin, and start it
    plugin = ReferentialIntegrityPlugin(inst)
    plugin.disable()
    plugin.enable()

    ############################################################################
    # Configure plugin
    ############################################################################
    GROUP_CONTAINER = "ou=groups,%s" % DEFAULT_SUFFIX
    plugin.replace('referint-membership-attr', 'member')
    plugin.replace('nsslapd-plugincontainerscope', GROUP_CONTAINER)

    ############################################################################
    # Creates a group with members having escaped DN
    ############################################################################
    # Add some users and a group
    users = UserAccounts(inst, DEFAULT_SUFFIX, None)
    user1 = users.create_test_user(uid=1001)
    user2 = users.create_test_user(uid=1002)

    groups = Groups(inst, GROUP_CONTAINER, None)
    group = groups.create(properties={'cn': 'group'})
    group.add('member', user2.dn)
    group.add('member', user1.dn)

    # Add more than 128 members so that referint follows the buggy path
    for i in range(130):
        escaped_user = add_escaped_user(inst, i)
        group.add('member', escaped_user)

    ############################################################################
    # Check that the MODRDN succeeds
    ###########################################################################
    # Here we need to restart so that member values are taken in the right order
    # the last value is the escaped one
    inst.restart()

    # Here if the bug is fixed, referential is able to update the member value
    inst.rename_s(user1.dn, 'uid=new_test_user_1001', newsuperior=SUFFIX, delold=0)


