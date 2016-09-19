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
from lib389 import DirSrv, Entry, tools
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from ldap.controls.readentry import PreReadControl,PostReadControl


SCOPE_IN_CN  = 'in'
SCOPE_OUT_CN = 'out'
SCOPE_IN_DN  = 'cn=%s,%s' % (SCOPE_IN_CN, SUFFIX)
SCOPE_OUT_DN = 'cn=%s,%s' % (SCOPE_OUT_CN, SUFFIX)

PROVISIONING_CN = "provisioning"
PROVISIONING_DN = "cn=%s,%s" % (PROVISIONING_CN, SCOPE_IN_DN)

ACTIVE_CN = "accounts"
STAGE_CN  = "staged users"
DELETE_CN = "deleted users"
ACTIVE_DN = "cn=%s,%s" % (ACTIVE_CN, SCOPE_IN_DN)
STAGE_DN  = "cn=%s,%s" % (STAGE_CN, PROVISIONING_DN)
DELETE_DN  = "cn=%s,%s" % (DELETE_CN, PROVISIONING_DN)

STAGE_USER_CN = "stage guy"
STAGE_USER_DN = "cn=%s,%s" % (STAGE_USER_CN, STAGE_DN)

ACTIVE_USER_CN = "active guy"
ACTIVE_USER_DN = "cn=%s,%s" % (ACTIVE_USER_CN, ACTIVE_DN)

OUT_USER_CN = "out guy"
OUT_USER_DN = "cn=%s,%s" % (OUT_USER_CN, SCOPE_OUT_DN)

STAGE_GROUP_CN = "stage group"
STAGE_GROUP_DN = "cn=%s,%s" % (STAGE_GROUP_CN, STAGE_DN)

ACTIVE_GROUP_CN = "active group"
ACTIVE_GROUP_DN = "cn=%s,%s" % (ACTIVE_GROUP_CN, ACTIVE_DN)

OUT_GROUP_CN = "out group"
OUT_GROUP_DN = "cn=%s,%s" % (OUT_GROUP_CN, SCOPE_OUT_DN)

INDIRECT_ACTIVE_GROUP_CN = "indirect active group"
INDIRECT_ACTIVE_GROUP_DN = "cn=%s,%s" % (INDIRECT_ACTIVE_GROUP_CN, ACTIVE_DN)

INITIAL_DESC = "inital description"
FINAL_DESC = "final description"

log = logging.getLogger(__name__)

class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):
    '''
        This fixture is used to standalone topology for the 'module'.
    '''
    standalone = DirSrv(verbose=False)

    # Args for the standalone instance
    args_instance[SER_HOST] = HOST_STANDALONE
    args_instance[SER_PORT] = PORT_STANDALONE
    args_instance[SER_SERVERID_PROP] = SERVERID_STANDALONE
    args_standalone = args_instance.copy()
    standalone.allocate(args_standalone)

    # Get the status of the instance and restart it if it exists
    instance_standalone = standalone.exists()

    # Remove the instance
    if instance_standalone:
        standalone.delete()

    # Create the instance
    standalone.create()

    # Used to retrieve configuration information (dbdir, confdir...)
    standalone.open()

    def fin():
        standalone.delete()
    request.addfinalizer(fin)

    # Here we have standalone instance up and running
    return TopologyStandalone(standalone)


def _header(topology, label):
    topology.standalone.log.info("\n\n###############################################")
    topology.standalone.log.info("#######")
    topology.standalone.log.info("####### %s" % label)
    topology.standalone.log.info("#######")
    topology.standalone.log.info("###############################################")


def _add_user(topology, type='active'):
    if type == 'active':
        topology.standalone.add_s(Entry((ACTIVE_USER_DN, {
                                                'objectclass': "top person inetuser".split(),
                                                'sn': ACTIVE_USER_CN,
                                                'cn': ACTIVE_USER_CN,
                                                'description': INITIAL_DESC})))
    elif type == 'stage':
        topology.standalone.add_s(Entry((STAGE_USER_DN, {
                                                'objectclass': "top person inetuser".split(),
                                                'sn': STAGE_USER_CN,
                                                'cn': STAGE_USER_CN})))
    else:
        topology.standalone.add_s(Entry((OUT_USER_DN, {
                                        'objectclass': "top person inetuser".split(),
                                        'sn': OUT_USER_CN,
                                        'cn': OUT_USER_CN})))


def test_ticket47920_init(topology):
    topology.standalone.add_s(Entry((SCOPE_IN_DN, {
                                                        'objectclass': "top nscontainer".split(),
                                                        'cn': SCOPE_IN_DN})))
    topology.standalone.add_s(Entry((ACTIVE_DN, {
                                                        'objectclass': "top nscontainer".split(),
                                                        'cn': ACTIVE_CN})))

    # add users
    _add_user(topology, 'active')


def test_ticket47920_mod_readentry_ctrl(topology):
    _header(topology, 'MOD: with a readentry control')

    topology.standalone.log.info("Check the initial value of the entry")
    ent = topology.standalone.getEntry(ACTIVE_USER_DN, ldap.SCOPE_BASE, "(objectclass=*)", ['description'])
    assert ent.hasAttr('description')
    assert ent.getValue('description') == INITIAL_DESC

    pr = PostReadControl(criticality=True, attrList=['cn', 'description'])
    _, _, _, resp_ctrls = topology.standalone.modify_ext_s(ACTIVE_USER_DN, [(ldap.MOD_REPLACE, 'description', [FINAL_DESC])], serverctrls=[pr])

    assert resp_ctrls[0].dn == ACTIVE_USER_DN
    assert 'description' in resp_ctrls[0].entry
    assert 'cn' in resp_ctrls[0].entry
    print(resp_ctrls[0].entry['description'])

    ent = topology.standalone.getEntry(ACTIVE_USER_DN, ldap.SCOPE_BASE, "(objectclass=*)", ['description'])
    assert ent.hasAttr('description')
    assert ent.getValue('description') == FINAL_DESC


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
