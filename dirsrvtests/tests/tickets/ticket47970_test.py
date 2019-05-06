# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging

import ldap.sasl
import pytest
from lib389.tasks import *
from lib389.topologies import topology_st

pytestmark = pytest.mark.tier2

log = logging.getLogger(__name__)

USER1_DN = "uid=user1,%s" % DEFAULT_SUFFIX
USER2_DN = "uid=user2,%s" % DEFAULT_SUFFIX


def test_ticket47970(topology_st):
    """
        Testing that a failed SASL bind does not trigger account lockout -
        which would attempt to update the passwordRetryCount on the root dse entry
    """

    log.info('Testing Ticket 47970 - Testing that a failed SASL bind does not trigger account lockout')

    #
    # Enable account lockout
    #
    try:
        topology_st.standalone.modify_s("cn=config", [(ldap.MOD_REPLACE, 'passwordLockout', b'on')])
        log.info('account lockout enabled.')
    except ldap.LDAPError as e:
        log.error('Failed to enable account lockout: ' + e.args[0]['desc'])
        assert False

    try:
        topology_st.standalone.modify_s("cn=config", [(ldap.MOD_REPLACE, 'passwordMaxFailure', b'5')])
        log.info('passwordMaxFailure set.')
    except ldap.LDAPError as e:
        log.error('Failed to to set passwordMaxFailure: ' + e.args[0]['desc'])
        assert False

    #
    # Perform SASL bind that should fail
    #
    failed_as_expected = False
    try:
        user_name = "mark"
        pw = "secret"
        auth_tokens = ldap.sasl.digest_md5(user_name, pw)
        topology_st.standalone.sasl_interactive_bind_s("", auth_tokens)
    except ldap.INVALID_CREDENTIALS as e:
        log.info("SASL Bind failed as expected")
        failed_as_expected = True

    if not failed_as_expected:
        log.error("SASL bind unexpectedly succeeded!")
        assert False

    #
    # Check that passwordRetryCount was not set on the root dse entry
    #
    try:
        entry = topology_st.standalone.search_s("", ldap.SCOPE_BASE,
                                                "passwordRetryCount=*",
                                                ['passwordRetryCount'])
    except ldap.LDAPError as e:
        log.error('Failed to search Root DSE entry: ' + e.args[0]['desc'])
        assert False

    if entry:
        log.error('Root DSE was incorrectly updated')
        assert False

    # We passed
    log.info('Root DSE was correctly not updated')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
