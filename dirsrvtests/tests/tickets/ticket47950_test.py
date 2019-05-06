# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging

import pytest
from lib389.tasks import *
from lib389.topologies import topology_st

from lib389._constants import (defaultProperties, DEFAULT_SUFFIX, ReplicaRole,
                               REPLICAID_MASTER_1, REPLICATION_BIND_DN, REPLICATION_BIND_PW,
                               REPLICATION_BIND_METHOD, REPLICATION_TRANSPORT, RA_NAME,
                               RA_BINDDN, RA_BINDPW, RA_METHOD, RA_TRANSPORT_PROT,
                               DN_DM, PASSWORD, REPLICA_ID, RA_CONSUMER_PORT)

pytestmark = pytest.mark.tier2

log = logging.getLogger(__name__)

USER1_DN = "uid=user1,%s" % DEFAULT_SUFFIX
USER2_DN = "uid=user2,%s" % DEFAULT_SUFFIX


def test_ticket47950(topology_st):
    """
        Testing nsslapd-plugin-binddn-tracking does not cause issues around
        access control and reconfiguring replication/repl agmt.
    """

    log.info('Testing Ticket 47950 - Testing nsslapd-plugin-binddn-tracking')

    #
    # Turn on bind dn tracking
    #
    try:
        topology_st.standalone.modify_s("cn=config", [(ldap.MOD_REPLACE, 'nsslapd-plugin-binddn-tracking', 'on')])
        log.info('nsslapd-plugin-binddn-tracking enabled.')
    except ldap.LDAPError as e:
        log.error('Failed to enable bind dn tracking: ' + e.message['desc'])
        assert False

    #
    # Add two users
    #
    try:
        topology_st.standalone.add_s(Entry((USER1_DN, {
            'objectclass': "top person inetuser".split(),
            'userpassword': "password",
            'sn': "1",
            'cn': "user 1"})))
        log.info('Added test user %s' % USER1_DN)
    except ldap.LDAPError as e:
        log.error('Failed to add %s: %s' % (USER1_DN, e.message['desc']))
        assert False

    try:
        topology_st.standalone.add_s(Entry((USER2_DN, {
            'objectclass': "top person inetuser".split(),
            'sn': "2",
            'cn': "user 2"})))
        log.info('Added test user %s' % USER2_DN)
    except ldap.LDAPError as e:
        log.error('Failed to add user1: ' + e.message['desc'])
        assert False

    #
    # Add an aci
    #
    try:
        acival = '(targetattr ="cn")(version 3.0;acl "Test bind dn tracking"' + \
                 ';allow (all) (userdn = "ldap:///%s");)' % USER1_DN

        topology_st.standalone.modify_s(DEFAULT_SUFFIX, [(ldap.MOD_ADD, 'aci', acival)])
        log.info('Added aci')
    except ldap.LDAPError as e:
        log.error('Failed to add aci: ' + e.message['desc'])
        assert False

    #
    # Make modification as user
    #
    try:
        topology_st.standalone.simple_bind_s(USER1_DN, "password")
        log.info('Bind as user %s successful' % USER1_DN)
    except ldap.LDAPError as e:
        log.error('Failed to bind as user1: ' + e.message['desc'])
        assert False

    try:
        topology_st.standalone.modify_s(USER2_DN, [(ldap.MOD_REPLACE, 'cn', 'new value')])
        log.info('%s successfully modified user %s' % (USER1_DN, USER2_DN))
    except ldap.LDAPError as e:
        log.error('Failed to update user2: ' + e.message['desc'])
        assert False

    #
    # Setup replica and create a repl agmt
    #
    try:
        topology_st.standalone.simple_bind_s(DN_DM, PASSWORD)
        log.info('Bind as %s successful' % DN_DM)
    except ldap.LDAPError as e:
        log.error('Failed to bind as rootDN: ' + e.message['desc'])
        assert False

    try:
        topology_st.standalone.replica.enableReplication(suffix=DEFAULT_SUFFIX, role=ReplicaRole.MASTER,
                                                         replicaId=REPLICAID_MASTER_1)
        log.info('Successfully enabled replication.')
    except ValueError:
        log.error('Failed to enable replication')
        assert False

    properties = {RA_NAME: r'test plugin internal bind dn',
                  RA_BINDDN: defaultProperties[REPLICATION_BIND_DN],
                  RA_BINDPW: defaultProperties[REPLICATION_BIND_PW],
                  RA_METHOD: defaultProperties[REPLICATION_BIND_METHOD],
                  RA_TRANSPORT_PROT: defaultProperties[REPLICATION_TRANSPORT]}

    try:
        repl_agreement = topology_st.standalone.agreement.create(suffix=DEFAULT_SUFFIX, host="127.0.0.1",
                                                                 port="7777", properties=properties)
        log.info('Successfully created replication agreement')
    except InvalidArgumentError as e:
        log.error('Failed to create replication agreement: ' + e.message['desc'])
        assert False

    #
    # modify replica
    #
    try:
        properties = {REPLICA_ID: "7"}
        topology_st.standalone.replica.setProperties(DEFAULT_SUFFIX, None, None, properties)
        log.info('Successfully modified replica')
    except ldap.LDAPError as e:
        log.error('Failed to update replica config: ' + e.message['desc'])
        assert False

    #
    # modify repl agmt
    #
    try:
        properties = {RA_CONSUMER_PORT: "8888"}
        topology_st.standalone.agreement.setProperties(None, repl_agreement, None, properties)
        log.info('Successfully modified replication agreement')
    except ValueError:
        log.error('Failed to update replica agreement: ' + repl_agreement)
        assert False


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
