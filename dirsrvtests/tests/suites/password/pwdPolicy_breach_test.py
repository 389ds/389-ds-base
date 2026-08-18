# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2026 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import logging
import os
import pytest
import ldap
from test389.topologies import topology_st
from lib389._constants import DEFAULT_SUFFIX, PASSWORD, DN_DM
from lib389.idm.user import UserAccounts
from lib389.idm.domain import Domain

HIBP_NETWORK_TESTS = os.getenv('HIBP_NETWORK_TESTS')
USER_RDN = 'breachuser'
USER_DN = f'uid={USER_RDN},ou=People,{DEFAULT_SUFFIX}'
USER_SELF_MOD_ACI = '(targetattr="userpassword")(version 3.0; acl "pwp test"; allow (all) userdn="ldap:///self";)'

pytestmark = [
    pytest.mark.tier2,
    pytest.mark.skipif(HIBP_NETWORK_TESTS is None, reason="HIBP tests require network access. Set HIBP_NETWORK_TESTS=1")
]

logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


@pytest.fixture
def hibp_enabled(topology_st):
    """Skip test if HIBP feature is not compiled in."""
    try:
        topology_st.standalone.config.get_attr_val_utf8('passwordBreachCheck')
    except ldap.NO_SUCH_ATTRIBUTE:
        pytest.skip("HIBP feature not compiled in (requires --enable-hibp)")


@pytest.fixture(scope="module")
def setup_breach_policy(topology_st):
    """Enable HIBP breach checking in password policy."""
    inst = topology_st.standalone
    inst.simple_bind_s(DN_DM, PASSWORD)

    try:
        inst.config.get_attr_val_utf8('passwordBreachCheck')
    except ldap.NO_SUCH_ATTRIBUTE:
        pytest.skip("HIBP feature not compiled in (requires --enable-hibp)")

    log.info('Adding ACI to allow self-service password changes')
    suffix = Domain(inst, DEFAULT_SUFFIX)
    suffix.add('aci', USER_SELF_MOD_ACI)

    log.info('Enabling HIBP password breach checking')
    inst.config.set('passwordCheckSyntax', 'on')
    inst.config.set('passwordChange', 'on')
    inst.config.set('passwordBreachCheck', 'on')

    yield inst

    log.info('Disabling HIBP password breach checking')
    inst.simple_bind_s(DN_DM, PASSWORD)
    inst.config.set('passwordBreachCheck', 'off')


@pytest.fixture(scope="function")
def breach_user(topology_st):
    """Create a test user for each test."""
    inst = topology_st.standalone
    inst.simple_bind_s(DN_DM, PASSWORD)
    users = UserAccounts(inst, DEFAULT_SUFFIX)

    user = users.create(properties={
        'uid': USER_RDN,
        'cn': USER_RDN,
        'sn': USER_RDN,
        'uidNumber': '3000',
        'gidNumber': '4000',
        'homeDirectory': f'/home/{USER_RDN}',
        'userPassword': PASSWORD
    })

    yield user

    inst.simple_bind_s(DN_DM, PASSWORD)
    try:
        user.delete()
    except ldap.NO_SUCH_OBJECT:
        pass


def test_breached_password_rejected(topology_st, setup_breach_policy, breach_user):
    """Test that a known breached password is rejected.

    :id: 0fd8610d-eaed-423d-b96e-253da63d7ac4
    :customerscenario: True
    :setup: Standalone instance with passwordBreachCheck enabled
    :steps:
        1. Enable passwordBreachCheck
        2. Bind as the test user
        3. Attempt to set a known breached password ('password')
        4. Verify the operation is rejected with CONSTRAINT_VIOLATION
    :expectedresults:
        1. Password policy is configured
        2. Bound as user
        3. Password change attempt is made
        4. Operation fails with CONSTRAINT_VIOLATION
    """
    inst = setup_breach_policy

    log.info('Binding as test user for self-service password change')
    inst.simple_bind_s(USER_DN, PASSWORD)

    users = UserAccounts(inst, DEFAULT_SUFFIX)
    user = users.get(USER_RDN)

    log.info('Attempting to set known breached password "password"')
    try:
        user.reset_password('password')
        log.fatal('Breached password was unexpectedly accepted')
        assert False, 'Breached password should have been rejected'
    except ldap.CONSTRAINT_VIOLATION as e:
        log.info(f'Breached password correctly rejected: {e}')


def test_safe_password_accepted(topology_st, setup_breach_policy, breach_user):
    """Test that a safe password is accepted.

    :id: 62f7c432-2c09-48e6-8642-44881b2ca80a
    :customerscenario: True
    :setup: Standalone instance with passwordBreachCheck enabled
    :steps:
        1. Enable passwordBreachCheck
        2. Bind as the test user
        3. Set a unique, non-breached password
        4. Verify the operation succeeds
        5. Verify user can bind with new password
    :expectedresults:
        1. Password policy is configured
        2. Bound as user
        3. Password change succeeds
        4. Operation completes without error
        5. User can bind with new password
    """
    inst = setup_breach_policy

    log.info('Binding as test user for self-service password change')
    inst.simple_bind_s(USER_DN, PASSWORD)

    users = UserAccounts(inst, DEFAULT_SUFFIX)
    user = users.get(USER_RDN)

    safe_password = 'xK9#mQ2$vL7@nP4!wR8^tY1&zB5'
    log.info('Setting safe password')
    user.reset_password(safe_password)

    log.info('Verifying user can bind with new password')
    inst.simple_bind_s(USER_DN, safe_password)
    log.info('Safe password correctly accepted')


def test_breach_check_disabled(topology_st, hibp_enabled, breach_user):
    """Test that breached passwords are allowed when check is disabled.

    :id: eba14842-290c-4a84-a032-481c8ac6a623
    :customerscenario: True
    :setup: Standalone instance with passwordBreachCheck disabled
    :steps:
        1. Disable passwordBreachCheck and passwordCheckSyntax
        2. Bind as the test user
        3. Set a known breached password
        4. Verify the operation succeeds
        5. Verify user can bind with breached password
    :expectedresults:
        1. Password policy is configured
        2. Bound as user
        3. Password change succeeds
        4. Operation completes without error
        5. User can bind with breached password
    """
    inst = topology_st.standalone
    inst.simple_bind_s(DN_DM, PASSWORD)

    log.info('Ensuring passwordBreachCheck and passwordCheckSyntax are disabled')
    inst.config.set('passwordBreachCheck', 'off')
    inst.config.set('passwordCheckSyntax', 'off')

    log.info('Adding ACI to allow self-service password changes')
    suffix = Domain(inst, DEFAULT_SUFFIX)
    try:
        suffix.add('aci', USER_SELF_MOD_ACI)
    except ldap.TYPE_OR_VALUE_EXISTS:
        pass

    log.info('Binding as test user')
    inst.simple_bind_s(USER_DN, PASSWORD)

    users = UserAccounts(inst, DEFAULT_SUFFIX)
    user = users.get(USER_RDN)

    log.info('Setting known breached password "password"')
    user.reset_password('password')

    log.info('Verifying user can bind with breached password')
    inst.simple_bind_s(USER_DN, 'password')
    log.info('Breached password correctly allowed when check is disabled')

    log.info('Restoring password policy settings')
    inst.simple_bind_s(DN_DM, PASSWORD)
    inst.config.set('passwordBreachCheck', 'on')
    inst.config.set('passwordCheckSyntax', 'on')


def test_admin_can_set_breached_password(topology_st, setup_breach_policy, breach_user):
    """Test that admin (Directory Manager) can set breached passwords.

    :id: cef5588d-9c20-4ecd-b563-362925732db5
    :customerscenario: True
    :setup: Standalone instance with passwordBreachCheck enabled
    :steps:
        1. Enable passwordBreachCheck
        2. Bind as Directory Manager
        3. Set a breached password for the user
        4. Verify the operation succeeds (admin bypass)
    :expectedresults:
        1. Password policy is configured
        2. Bound as admin
        3. Password change succeeds
        4. Admin can set any password
    """
    inst = setup_breach_policy

    log.info('Binding as Directory Manager')
    inst.simple_bind_s(DN_DM, PASSWORD)

    log.info('Admin setting breached password for user')
    users = UserAccounts(inst, DEFAULT_SUFFIX)
    user = users.get(USER_RDN)
    user.reset_password('password')


def test_user_add_with_breached_password(topology_st, setup_breach_policy):
    """Test that admin can create a user with a breached password (admin bypass).

    :id: df37ef3d-8bc5-4adb-9dbf-a4024ff6a2cd
    :customerscenario: True
    :setup: Standalone instance with passwordBreachCheck enabled
    :steps:
        1. Enable passwordBreachCheck
        2. Bind as Directory Manager
        3. Create a new user with breached password
        4. Verify the operation succeeds (admin bypass)
        5. Delete the test user
    :expectedresults:
        1. Password policy is configured
        2. Bound as admin
        3. User creation succeeds
        4. Admin bypass allows breached password
        5. Cleanup succeeds
    """
    inst = setup_breach_policy

    log.info('Binding as Directory Manager')
    inst.simple_bind_s(DN_DM, PASSWORD)

    users = UserAccounts(inst, DEFAULT_SUFFIX)

    log.info('Admin creating user with breached password')
    user = users.create(properties={
        'uid': 'breachtest',
        'cn': 'breachtest',
        'sn': 'breachtest',
        'uidNumber': '3001',
        'gidNumber': '4000',
        'homeDirectory': '/home/breachtest',
        'userPassword': 'password'
    })

    user.delete()


def test_direct_modify_breached_password(topology_st, setup_breach_policy, breach_user):
    """Test HIBP check via direct LDAP modify operation.

    :id: 7d2a832b-abd8-4682-85d9-860661e2c82a
    :customerscenario: True
    :setup: Standalone instance with passwordBreachCheck enabled
    :steps:
        1. Enable passwordBreachCheck
        2. Bind as test user
        3. Use direct ldap modify_s to set breached password
        4. Verify operation is rejected with CONSTRAINT_VIOLATION
    :expectedresults:
        1. Password policy is configured
        2. Bound as user
        3. Modify operation is attempted
        4. Operation fails with CONSTRAINT_VIOLATION
    """
    inst = setup_breach_policy

    log.info('Binding as test user')
    inst.simple_bind_s(USER_DN, PASSWORD)

    log.info('Attempting direct LDAP modify with breached password')
    try:
        inst.modify_s(USER_DN, [(ldap.MOD_REPLACE, 'userPassword', b'password')])
        assert False, 'Breached password should have been rejected via direct modify'
    except ldap.CONSTRAINT_VIOLATION as e:
        log.info(f'Direct modify correctly rejected breached password: {e}')


def test_passwd_extop_breached_password(topology_st, setup_breach_policy, breach_user):
    """Test HIBP check via Password Modify Extended Operation.

    Note: passwd_s requires a secure connection, this test attempts the
    operation and skips if TLS is not configured.

    :id: 05e89150-0124-4656-b15f-4d9926dbed39
    :customerscenario: True
    :setup: Standalone instance with passwordBreachCheck enabled
    :steps:
        1. Enable passwordBreachCheck
        2. Bind as test user
        3. Use passwd_s (Password Modify ExtOp) to set breached password
        4. Verify operation is rejected with CONSTRAINT_VIOLATION
    :expectedresults:
        1. Password policy is configured
        2. Bound as user
        3. Password Modify ExtOp is attempted
        4. Operation fails with CONSTRAINT_VIOLATION (or skipped if TLS required)
    """
    inst = setup_breach_policy

    log.info('Binding as test user')
    inst.simple_bind_s(USER_DN, PASSWORD)

    log.info('Attempting Password Modify ExtOp with breached password')
    try:
        inst.passwd_s(USER_DN, PASSWORD, 'password')
        assert False, 'Breached password should have been rejected via passwd_s'
    except ldap.CONSTRAINT_VIOLATION as e:
        log.info(f'Password Modify ExtOp correctly rejected breached password: {e}')
    except ldap.CONFIDENTIALITY_REQUIRED:
        pytest.skip('Password Modify ExtOp requires secure connection - TLS not configured')


def test_rootpw_breached_password(topology_st, hibp_enabled):
    """Test HIBP check for root password changes.

    :id: 5290ec50-a12f-451e-a96c-056ff5df3ccf
    :customerscenario: True
    :setup: Standalone instance with passwordBreachCheck enabled
    :steps:
        1. Enable passwordBreachCheck
        2. Bind as Directory Manager
        3. Attempt to set nsslapd-rootpw to breached password
        4. Verify operation is rejected with CONSTRAINT_VIOLATION
        5. Verify setting non-breached rootpw succeeds
        6. Restore original root password
    :expectedresults:
        1. Password policy is configured
        2. Bound as admin
        3. Root password change is attempted
        4. Operation fails with CONSTRAINT_VIOLATION
        5. Non-breached password succeeds
        6. Original password restored
    """
    inst = topology_st.standalone
    inst.simple_bind_s(DN_DM, PASSWORD)

    log.info('Enabling passwordBreachCheck')
    inst.config.set('passwordBreachCheck', 'on')

    log.info('Attempting to set root password to breached value')
    try:
        inst.config.set('nsslapd-rootpw', 'password')
        assert False, 'Breached root password should have been rejected'
    except ldap.CONSTRAINT_VIOLATION as e:
        log.info(f'Root password breach check correctly rejected: {e}')

    log.info('Setting root password to non-breached value')
    safe_rootpw = 'zX9#kM2$wL7@nQ4!rT8^yB1&vC5'
    inst.config.set('nsslapd-rootpw', safe_rootpw)
    log.info('Non-breached root password accepted')

    log.info('Rebinding with new root password')
    inst.simple_bind_s(DN_DM, safe_rootpw)

    log.info('Disabling passwordBreachCheck before restoring original password')
    inst.config.set('passwordBreachCheck', 'off')

    log.info('Restoring original root password')
    inst.config.set('nsslapd-rootpw', PASSWORD)
    inst.simple_bind_s(DN_DM, PASSWORD)


if __name__ == '__main__':
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
