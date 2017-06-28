import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st

from lib389._constants import DEFAULT_SUFFIX, HOST_STANDALONE, PORT_STANDALONE

DEBUGGING = os.getenv('DEBUGGING', False)
USER_DN = 'uid=user,ou=People,%s' % DEFAULT_SUFFIX

if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)

# Skip on older versions
pytestmark = pytest.mark.skipif(ds_is_older('1.3.6'), reason="Not implemented")

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

    if DEBUGGING:
        print('Testing %s' % algo_name)

    # Create the user with a password
    inst.add_s(Entry((
        USER_DN, {
            'objectClass': 'top account simplesecurityobject'.split(),
            'uid': 'user',
            'userpassword': ['Secret123', ]
        })))

    # Make sure when we read the userPassword field, it is the correct ALGO
    pw_field = inst.search_s(USER_DN, ldap.SCOPE_BASE, '(objectClass=*)', ['userPassword'])[0]

    if DEBUGGING:
        print(pw_field.getValue('userPassword'))

    if algo_name != 'CLEAR':
        lalgo_name = algo_name.lower()
        lpw_algo_name = pw_field.getValue('userPassword').lower()
        assert (lpw_algo_name.startswith("{%s}" % lalgo_name))
    # Now make sure a bind works
    assert (_test_bind(inst, 'Secret123'))
    # Bind with a wrong shorter password, should fail
    assert (not _test_bind(inst, 'Wrong'))
    # Bind with a wrong longer password, should fail
    assert (not _test_bind(inst, 'This is even more wrong'))
    # Bind with a password that has the algo in the name
    assert (not _test_bind(inst, '{%s}SomeValues....' % algo_name))
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


def test_397(topology_st):
    """
    Assert that all of our password algorithms correctly PASS and FAIL varying
    password conditions.

    """
    if DEBUGGING:
        # Add debugging steps(if any)...
        log.info("ATTACH NOW")
        time.sleep(30)

    # Merge this to the password suite in the future

    for algo in ('PBKDF2_SHA256',):
        for i in range(0, 10):
            _test_algo(topology_st.standalone, algo)

    log.info('Test PASSED')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
