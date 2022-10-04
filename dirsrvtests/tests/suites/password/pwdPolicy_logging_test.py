import ldap
import logging
import pytest
import os
import time
from lib389._constants import DEFAULT_SUFFIX, PASSWORD
from lib389.topologies import topology_st as topo
from lib389.pwpolicy import PwPolicyManager
from lib389.idm.organizationalunit import OrganizationalUnits
from lib389.idm.user import UserAccount, UserAccounts, TEST_USER_PROPERTIES
from lib389.idm.domain import Domain

log = logging.getLogger(__name__)

LOCAL_RDN = 'ou=People'
GLOBAL_RDN = 'ou=Global'
LOCAL_BIND_DN ="uid=local_user,ou=people," + DEFAULT_SUFFIX
GLOBAL_BIND_DN ="uid=global_user,ou=global," + DEFAULT_SUFFIX


def create_entries(inst):
    # Create local user
    users = UserAccounts(inst, DEFAULT_SUFFIX, rdn=LOCAL_RDN)
    user_props = TEST_USER_PROPERTIES.copy()
    user_props.update({'uid': 'local_user', 'cn': 'local_user', 'userpassword': PASSWORD})
    users.create(properties=user_props)

    # Create new OU
    ou_global = OrganizationalUnits(inst, DEFAULT_SUFFIX).create(properties={'ou': 'Global'})

    # Create global user
    users = UserAccounts(inst, DEFAULT_SUFFIX, rdn=GLOBAL_RDN)
    user_props = TEST_USER_PROPERTIES.copy()
    user_props.update({'uid': 'global_user', 'cn': 'global_user', 'userpassword': PASSWORD})
    users.create(properties=user_props)

    # Add aci
    aci = '(targetattr="*")(version 3.0; acl "pwp test"; allow (all) userdn="ldap:///self";)'
    suffix = Domain(inst, DEFAULT_SUFFIX)
    suffix.add('aci', aci)


def create_policies(inst):
    # Configure subtree policy
    pwp = PwPolicyManager(inst)
    subtree_policy_props = {
        'passwordCheckSyntax': 'on',
        'passwordMinLength': '6',
        'passwordChange': 'on',
        'passwordLockout': 'on',
        'passwordMaxFailure': '2',
    }
    pwp.create_subtree_policy(f'{LOCAL_RDN},{DEFAULT_SUFFIX}', subtree_policy_props)

    # Configure global policy
    inst.config.replace('nsslapd-pwpolicy-local', 'on')
    inst.config.replace('passwordCheckSyntax', 'on')
    inst.config.replace('passwordMinLength', '8')
    inst.config.replace('passwordChange', 'on')
    inst.config.replace('passwordLockout', 'on')
    inst.config.replace('passwordMaxFailure', '5')
    time.sleep(1)


def test_debug_logging(topo):
    """Enable password policy logging

    :id: cc152c65-94e0-4716-a77c-abdd2deec00d
    :setup: Standalone Instance
    :steps:
        1. Set password policy logging level
        2. Add database entries
        3. Configure local and global policies
        4. Test syntax checking on local policy
        5. Test syntax checking on global policy
        6. Test account lockout on local policy
        7. Test account lockout on global policy

    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
        6. Success
        7. Success
    """

    inst = topo.standalone

    # Enable password policy debug logging
    inst.config.replace('nsslapd-errorlog-level', '1048576')

    # Create entries and password policies
    create_entries(inst)
    create_policies(inst)

    # Setup bind connections

    local_conn = UserAccounts(inst, DEFAULT_SUFFIX, rdn=LOCAL_RDN).get('local_user').bind(PASSWORD)
    local_user = UserAccount(local_conn, LOCAL_BIND_DN)
    global_conn = UserAccounts(inst, DEFAULT_SUFFIX, rdn=GLOBAL_RDN).get('global_user').bind(PASSWORD)
    global_user = UserAccount(global_conn, GLOBAL_BIND_DN)

    # Test syntax checking on local policy
    passwd_val = "passw"  # len 5 which is less than configured 6
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        local_user.replace('userpassword', passwd_val)
    time.sleep(1)

    err_msg = "PWDPOLICY_DEBUG - invalid password syntax - password must be at least 6 characters long: Entry " + \
              "\\(uid=local_user,ou=people,dc=example,dc=com\\) Policy \\(cn="
    assert inst.searchErrorsLog(err_msg)

    # Test syntax checking on global policy
    passwd_val = "passwod"  # len 7 which is less than configured 8
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        global_user.replace('userpassword', passwd_val)
    time.sleep(1)

    err_msg = "PWDPOLICY_DEBUG - invalid password syntax - password must be at least 8 characters long: Entry " + \
              "\\(uid=global_user,ou=global,dc=example,dc=com\\) Policy \\(Global\\)"
    assert inst.searchErrorsLog(err_msg)

    # Test account lock is logging for local policy
    for i in range(2):
        with pytest.raises(ldap.INVALID_CREDENTIALS):
            local_user.bind("bad")
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        local_user.bind("bad")

    err_msg = "PWDPOLICY_DEBUG - Account is locked: Entry " + \
              "\\(uid=local_user,ou=people,dc=example,dc=com\\) Policy \\(cn="
    assert inst.searchErrorsLog(err_msg)

    # Test account lock is logging for global policy
    for i in range(5):
        with pytest.raises(ldap.INVALID_CREDENTIALS):
            global_user.bind("bad")
    with pytest.raises(ldap.CONSTRAINT_VIOLATION):
        global_user.bind("bad")

    err_msg = "PWDPOLICY_DEBUG - Account is locked: Entry " + \
              "\\(uid=global_user,ou=global,dc=example,dc=com\\) Policy \\(Global\\)"
    assert inst.searchErrorsLog(err_msg)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
