# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import pytest
import codecs
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_m2

from lib389._constants import DATA_DIR, DEFAULT_SUFFIX, VALGRIND_INVALID_STR

# Skip on older versions
pytestmark = [pytest.mark.tier2,
              pytest.mark.skipif(ds_is_older('1.3.6'), reason="Not implemented")]

DEBUGGING = os.getenv('DEBUGGING', False)

if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)

ds_paths = Paths()


@pytest.mark.skipif(not ds_paths.asan_enabled, reason="Don't run if ASAN is not enabled")
def test_ticket49121(topology_m2):
    """
    Creating some users.
    Deleting quite a number of attributes which may or may not be in the entry.
    The attribute type names are to be long.
    Under the conditions, it did not estimate the size of string format entry
    shorter than the real size and caused the Invalid write / server crash.
    """

    utf8file = os.path.join(topology_m2.ms["supplier1"].getDir(__file__, DATA_DIR), "ticket49121/utf8str.txt")
    utf8obj = codecs.open(utf8file, 'r', 'utf-8')
    utf8strorig = utf8obj.readline()
    utf8str = ensure_bytes(utf8strorig).rstrip(b'\n')
    utf8obj.close()
    assert (utf8str)

    # Get the sbin directory so we know where to replace 'ns-slapd'
    sbin_dir = topology_m2.ms["supplier1"].get_sbin_dir()
    log.info('sbin_dir: %s' % sbin_dir)

    # stop M1 to do the next updates
    topology_m2.ms["supplier1"].stop(30)
    topology_m2.ms["supplier2"].stop(30)

    # wait for the servers shutdown
    time.sleep(5)

    # start M1 to do the next updates
    topology_m2.ms["supplier1"].start()
    topology_m2.ms["supplier2"].start()

    for idx in range(1, 10):
        try:
            USER_DN = 'CN=user%d,ou=People,%s' % (idx, DEFAULT_SUFFIX)
            log.info('adding user %s...' % (USER_DN))
            topology_m2.ms["supplier1"].add_s(Entry((USER_DN,
                                                   {'objectclass': 'top person extensibleObject'.split(' '),
                                                    'cn': 'user%d' % idx,
                                                    'sn': 'SN%d-%s' % (idx, utf8str)})))
        except ldap.LDAPError as e:
            log.fatal('Failed to add user (%s): error %s' % (USER_DN, e.args[0]['desc']))
            assert False

    for i in range(1, 3):
        time.sleep(3)
        for idx in range(1, 10):
            try:
                USER_DN = 'CN=user%d,ou=People,%s' % (idx, DEFAULT_SUFFIX)
                log.info('[%d] modify user %s - replacing attrs...' % (i, USER_DN))
                topology_m2.ms["supplier1"].modify_s(
                    USER_DN, [(ldap.MOD_REPLACE, 'cn', b'user%d' % idx),
                              (ldap.MOD_REPLACE, 'ABCDEFGH_ID', [b'239001ad-06dd-e011-80fa-c00000ad5174',
                                                                 b'240f0878-c552-e411-b0f3-000006040037']),
                              (ldap.MOD_REPLACE, 'attr1', b'NEW_ATTR'),
                              (ldap.MOD_REPLACE, 'attr20000000000000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr30000000000000000000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr40000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr50000000000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr600000000000000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr7000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr8000000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr900000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr1000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr110000000000000', None),
                              (ldap.MOD_REPLACE, 'attr120000000000000', None),
                              (ldap.MOD_REPLACE, 'attr130000000000000000000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr140000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr150000000000000000000000000000000000000000000000000000000000000',
                               None),
                              (ldap.MOD_REPLACE, 'attr1600000000000000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr17000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr18000000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr1900000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr2000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr210000000000000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr220000000000000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr230000000000000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr240000000000000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr25000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr260000000000000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE,
                               'attr270000000000000000000000000000000000000000000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr280000000000000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr29000000000000000000000000000000000000000000000000000000000',
                               None),
                              (ldap.MOD_REPLACE, 'attr3000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr310000000000000000000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr320000000000000000000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr330000000000000000000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr340000000000000000000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr350000000000000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr360000000000000000000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr370000000000000000000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr380000000000000000000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr390000000000000000000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr4000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr410000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr420000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr430000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr440000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr4500000000000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr460000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr470000000000000000000000000000000000000000000000000000000000',
                               None),
                              (ldap.MOD_REPLACE, 'attr480000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr49000000000000000000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr5000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr510000000000000', None),
                              (ldap.MOD_REPLACE, 'attr520000000000000', None),
                              (ldap.MOD_REPLACE, 'attr530000000000000', None),
                              (ldap.MOD_REPLACE, 'attr540000000000000', None),
                              (ldap.MOD_REPLACE, 'attr550000000000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr5600000000000000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr57000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr58000000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr5900000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr6000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr6100000000000000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr6200000000000000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr6300000000000000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr6400000000000000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE,
                               'attr65000000000000000000000000000000000000000000000000000000000000000000000000000000',
                               None),
                              (ldap.MOD_REPLACE, 'attr6600000000000000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr6700000000000000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr6800000000000000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr690000000000000000000000000000000000000000000000000000000000',
                               None),
                              (ldap.MOD_REPLACE, 'attr7000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr71000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr72000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr73000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr74000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr750000000000000000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr7600000000000000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr77000000000000000000000000000000', None),
                              (
                              ldap.MOD_REPLACE, 'attr78000000000000000000000000000000000000000000000000000000000000000',
                              None),
                              (ldap.MOD_REPLACE, 'attr79000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr800000000000000000000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr81000000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr82000000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr83000000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr84000000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr85000000000000000000000000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr8600000000000000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr87000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr88000000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr89000000000000000000000000000000000', None),
                              (ldap.MOD_REPLACE, 'attr9000000000000000000000000000000000000000000000000000', None)])
            except ldap.LDAPError as e:
                log.fatal('Failed to modify user - deleting attrs (%s): error %s' % (USER_DN, e.args[0]['desc']))

    # Stop supplier2
    topology_m2.ms["supplier1"].stop(30)
    topology_m2.ms["supplier2"].stop(30)

    # start M1 to do the next updates
    topology_m2.ms["supplier1"].start()
    topology_m2.ms["supplier2"].start()

    log.info('Testcase PASSED')
    if DEBUGGING:
        # Add debugging steps(if any)...
        pass


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
