# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2023 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

import ldap
import logging
import pytest
from lib389._constants import DEFAULT_SUFFIX, PASSWORD
from lib389.topologies import topology_st as topo
from lib389.idm.user import UserAccount, UserAccounts
from lib389.idm.account import Anonymous

log = logging.getLogger(__name__)
DN = "uid=common,ou=people," + DEFAULT_SUFFIX
HOME_DIR = '/home/common'

@pytest.fixture(scope="function")
def setup_test(topo, request):
    """Setup test environment"""
    log.info("Add nsslapd-haproxy-trusted-ip attribute")
    topo.standalone.config.set('nsslapd-haproxy-trusted-ip', '192.168.0.1')
    assert topo.standalone.config.present('nsslapd-haproxy-trusted-ip', '192.168.0.1')

    log.info("Add a user")
    users = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    try:
        users.create(properties={
            'uid': 'common',
            'cn': 'common',
            'sn': 'common',
            'uidNumber': '3000',
            'gidNumber': '4000',
            'homeDirectory': HOME_DIR,
            'description': 'test haproxy with this user',
            'userPassword': PASSWORD
        })
    except ldap.ALREADY_EXISTS:
        log.info("User already exists")
        pass


def test_haproxy_trust_ip_attribute(topo, setup_test):
    """Test nsslapd-haproxy-trusted-ip attribute set and delete

    :id: 8a0789a6-3ede-40e2-966c-9a2c87eaac05
    :setup: Standalone instance with nsslapd-haproxy-trusted-ip attribute and a user
    :steps:
        1. Check that nsslapd-haproxy-trusted-ip attribute is present
        2. Delete nsslapd-haproxy-trusted-ip attribute
        3. Check that nsslapd-haproxy-trusted-ip attribute is not present
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """

    log.info("Check that nsslapd-haproxy-trusted-ip attribute is present")
    assert topo.standalone.config.present('nsslapd-haproxy-trusted-ip', '192.168.0.1')
    # Check nsslapd-haproxy-trusted-ip attribute is present in schema
    assert topo.standalone.schema.query_attributetype('nsslapd-haproxy-trusted-ip') is not None

    log.info("Delete nsslapd-haproxy-trusted-ip attribute")
    topo.standalone.config.remove_all('nsslapd-haproxy-trusted-ip')

    log.info("Check that nsslapd-haproxy-trusted-ip attribute is not present")
    assert not topo.standalone.config.present('nsslapd-haproxy-trusted-ip', '192.168.0.1')


def test_binds_with_haproxy_trust_ip_attribute(topo, setup_test):
    """Test that non-proxy binds are not blocked when nsslapd-haproxy-trusted-ip attribute is set

    :id: 14273c16-fed9-497e-8ebb-09e3dabc7914
    :setup: Standalone instance with nsslapd-haproxy-trusted-ip attribute and a user
    :steps:
        1. Try to bind as anonymous user
        2. Try to bind as a user
        3. Check that userPassword is correct and we can get it
    :expectedresults:
        1. Success
        2. Success
        3. Success
    """

    log.info("Bind as anonymous user")
    Anonymous(topo.standalone).bind()

    log.info("Bind as a user")
    user_entry = UserAccount(topo.standalone, DN)
    user_conn = user_entry.bind(PASSWORD)

    log.info("Check that userPassword is correct and we can get it")
    user_entry = UserAccount(user_conn, DN)
    home = user_entry.get_attr_val_utf8('homeDirectory')
    assert home == HOME_DIR
