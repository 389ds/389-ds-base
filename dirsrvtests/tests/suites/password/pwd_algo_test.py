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
from lib389._constants import DEFAULT_SUFFIX, HOST_STANDALONE, DN_DM, PORT_STANDALONE
from lib389.idm.user import UserAccounts

logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

def _test_bind(user, password):
    result = True
    try:
        userconn = user.bind(password)
        userconn.unbind_s()
    except ldap.INVALID_CREDENTIALS:
        result = False
    return result


def _test_algo(inst, algo_name):
    inst.config.set('passwordStorageScheme', algo_name)

    users = UserAccounts(inst, DEFAULT_SUFFIX)

    user = users.create(properties={
        'uid': 'user',
        'cn' : 'user',
        'sn' : 'user',
        'uidNumber' : '1000',
        'gidNumber' : '2000',
        'homeDirectory' : '/home/user',
        'userpassword': 'Secret123'
    })

    # Make sure when we read the userPassword field, it is the correct ALGO
    pw_field = user.get_attr_val_utf8('userPassword')

    if algo_name != 'CLEAR' and algo_name != 'DEFAULT':
        assert (algo_name[:5].lower() in pw_field.lower())
    # Now make sure a bind works
    assert (_test_bind(user, 'Secret123'))
    # Bind with a wrong shorter password, should fail
    assert (not _test_bind(user, 'Wrong'))
    # Bind with a wrong longer password, should fail
    assert (not _test_bind(user, 'This is even more wrong'))
    # Bind with a wrong exact length password.
    assert (not _test_bind(user, 'Alsowrong'))
    # Bind with a subset password, should fail
    assert (not _test_bind(user, 'Secret'))
    if not algo_name.startswith('CRYPT'):
        # Bind with a subset password that is 1 char shorter, to detect off by 1 in clear
        assert (not _test_bind(user, 'Secret12'))
        # Bind with a superset password, should fail
        assert (not _test_bind(user, 'Secret123456'))
    # Delete the user
    user.delete()
    # done!

@pytest.mark.parametrize("algo",
    ('CLEAR', 'CRYPT', 'CRYPT-MD5', 'CRYPT-SHA256', 'CRYPT-SHA512',
     'MD5', 'SHA', 'SHA256', 'SHA384', 'SHA512', 'SMD5', 'SSHA',
     'SSHA256', 'SSHA384', 'SSHA512', 'PBKDF2_SHA256', 'DEFAULT',) )
def test_pwd_algo_test(topology_st, algo):
    """Assert that all of our password algorithms correctly PASS and FAIL varying
    password conditions.
    """
    if algo == 'DEFAULT':
       if ds_is_older('1.4.0'):
          pytest.skip("Not implemented")
    _test_algo(topology_st.standalone, algo)
    log.info('Test %s PASSED' % algo)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
