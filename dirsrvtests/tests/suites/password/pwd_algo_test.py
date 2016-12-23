# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2016 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st

USER_DN = 'uid=user,ou=People,%s' % DEFAULT_SUFFIX

logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def _test_bind(inst, password):
    result = True
    userconn = ldap.initialize("ldap://%s:%s" % (HOST_STANDALONE, PORT_STANDALONE))
    try:
        userconn.simple_bind_s(USER_DN, password)
        userconn.unbind_s()
    except ldap.INVALID_CREDENTIALS:
        result = False
    return result


def _test_algo(inst, algo_name):
    inst.config.set('passwordStorageScheme', algo_name)

    # Create the user with a password
    inst.add_s(Entry((
        USER_DN, {
            'objectClass': 'top account simplesecurityobject'.split(),
            'uid': 'user',
            'userpassword': 'Secret123'
        })))

    # Make sure when we read the userPassword field, it is the correct ALGO
    pw_field = inst.search_s(USER_DN, ldap.SCOPE_BASE, '(objectClass=*)', ['userPassword'])[0]

    if algo_name != 'CLEAR':
        assert (algo_name.lower() in pw_field.getValue('userPassword').lower())
    # Now make sure a bind works
    assert (_test_bind(inst, 'Secret123'))
    # Bind with a wrong shorter password, should fail
    assert (not _test_bind(inst, 'Wrong'))
    # Bind with a wrong longer password, should fail
    assert (not _test_bind(inst, 'This is even more wrong'))
    # Bind with a wrong exact length password.
    assert (not _test_bind(inst, 'Alsowrong'))
    # Bind with a subset password, should fail
    assert (not _test_bind(inst, 'Secret'))
    if algo_name != 'CRYPT':
        # Bind with a subset password that is 1 char shorter, to detect off by 1 in clear
        assert (not _test_bind(inst, 'Secret12'))
        # Bind with a superset password, should fail
        assert (not _test_bind(inst, 'Secret123456'))
    # Delete the user
    inst.delete_s(USER_DN)
    # done!


def test_pwd_algo_test(topology_st):
    """Assert that all of our password algorithms correctly PASS and FAIL varying
    password conditions.
    """

    for algo in (
            'CLEAR', 'CRYPT', 'MD5', 'SHA', 'SHA256', 'SHA384', 'SHA512', 'SMD5', 'SSHA', 'SSHA256', 'SSHA384',
            'SSHA512'):
        _test_algo(topology_st.standalone, algo)

    log.info('Test PASSED')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
