import ldap
import logging
import pytest
import os
import time
from lib389._constants import DEFAULT_SUFFIX, PASSWORD
from lib389.idm.domain import Domain
from lib389.idm.user import UserAccounts
from lib389.topologies import topology_st as topo

log = logging.getLogger(__name__)

def test_expired_user_has_no_privledge(topo):
    """Specify a test case purpose or name here

    :id: 3df86b45-9929-414b-9bf6-06c25301d207
    :setup: Standalone Instance
    :steps:
        1. Set short password expiration time
        2. Add user and wait for expiration time to run out
        3. Set one aci that allows authenticated users full access
        4. Bind as user (password should be expired)
        5. Attempt modify
    :expectedresults:
        1. Success
        2. Success
        3. Success
        4. Success
        5. Success
    """

    # Configured password epxiration
    topo.standalone.config.replace_many(('passwordexp', 'on'), ('passwordmaxage', '1'))

    # Set aci
    suffix = Domain(topo.standalone, DEFAULT_SUFFIX)
    ACI_TEXT = '(targetattr="*")(version 3.0; acl "test aci"; allow (all) (userdn="ldap:///all");)'
    suffix.replace('aci', ACI_TEXT)

    # Add user
    user = UserAccounts(topo.standalone, DEFAULT_SUFFIX, rdn=None).create_test_user()
    user.replace('userpassword', PASSWORD)
    time.sleep(2)

    # Bind as user with expired password.  Need to use raw ldap calls because
    # lib389 will close the connection when an error 49 is encountered.
    ldap_object = ldap.initialize(topo.standalone.toLDAPURL())
    with pytest.raises(ldap.INVALID_CREDENTIALS):
        res_type, res_data, res_msgid, res_ctrls = ldap_object.simple_bind_s(
            user.dn, PASSWORD)

    # Try modify
    with pytest.raises(ldap.INSUFFICIENT_ACCESS):
        modlist = [ (ldap.MOD_REPLACE, 'description', b'Should not work!') ]
        ldap_object.modify_ext_s(DEFAULT_SUFFIX, modlist)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main(["-s", CURRENT_FILE])
