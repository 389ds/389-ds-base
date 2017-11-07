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
from lib389.topologies import topology_m2

from lib389._constants import DN_CONFIG, DEFAULT_SUFFIX

from lib389.idm.user import UserAccounts, TEST_USER_PROPERTIES

from lib389.config import LDBMConfig

USER_DN = 'uid=test_user,%s' % DEFAULT_SUFFIX

logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


@pytest.fixture(scope="module")
def big_file():
    TEMP_BIG_FILE = ''
    # 1024*1024=1048576
    # B for 1 MiB
    # Big for 3 MiB
    for x in range(1048576):
        TEMP_BIG_FILE += '+'

    return TEMP_BIG_FILE


def test_maxbersize_repl(topology_m2, big_file):
    """maxbersize is ignored in the replicated operations.

    :id: ad57de60-7d56-4323-bbca-5556e5cdb126
    :setup: MMR with two masters, test user,
            1 MiB big value for any attribute
    :steps:
        1. Set maxbersize attribute to a small value (20KiB) on master2
        2. Add the big value to master2
        3. Add the big value to master1
        4. Check if the big value was successfully replicated to master2
    :expectedresults:
        1. maxbersize should be successfully set
        2. Adding the big value to master2 failed
        3. Adding the big value to master1 succeed
        4. The big value is successfully replicated to master2
    """

    users_m1 = UserAccounts(topology_m2.ms["master1"], DEFAULT_SUFFIX)
    users_m2 = UserAccounts(topology_m2.ms["master2"], DEFAULT_SUFFIX)

    user_m1 = users_m1.create(properties=TEST_USER_PROPERTIES)
    time.sleep(2)
    user_m2 = users_m2.get(dn=user_m1.dn)

    log.info("Set nsslapd-maxbersize: 20K to master2")
    topology_m2.ms["master2"].config.set('nsslapd-maxbersize', '20480')

    topology_m2.ms["master2"].restart()

    log.info('Try to add attribute with a big value to master2 - expect to FAIL')
    with pytest.raises(ldap.SERVER_DOWN):
        user_m2.add('jpegphoto', big_file)

    topology_m2.ms["master2"].restart()
    topology_m2.ms["master1"].restart()

    log.info('Try to add attribute with a big value to master1 - expect to PASS')
    user_m1.add('jpegphoto', big_file)

    time.sleep(2)

    log.info('Check if a big value was successfully added to master1')

    photo_m1 = user_m1.get_attr_vals('jpegphoto')

    log.info('Check if a big value was successfully replicated to master2')
    photo_m2 = user_m2.get_attr_vals('jpegphoto')

    assert photo_m2 == photo_m1

def test_config_listen_backport_size(topology_m2):
    """Check that nsslapd-listen-backlog-size acted as expected

    :id: a4385d58-a6ab-491e-a604-6df0e8ed91cd
    :setup: MMR with two masters
    :steps:
        1. Search for nsslapd-listen-backlog-size
        2. Set nsslapd-listen-backlog-size to a positive value
        3. Set nsslapd-listen-backlog-size to a negative value
        4. Set nsslapd-listen-backlog-size to an invalid value
        5. Set nsslapd-listen-backlog-size back to a default value
    :expectedresults:
        1. Search should be successful
        2. nsslapd-listen-backlog-size should be successfully set
        3. nsslapd-listen-backlog-size should be successfully set
        4. Modification with an invalid value should throw an error
        5. nsslapd-listen-backlog-size should be successfully set
    """

    default_val = topology_m2.ms["master1"].config.get_attr_val_bytes('nsslapd-listen-backlog-size')

    topology_m2.ms["master1"].config.replace('nsslapd-listen-backlog-size', '256')

    topology_m2.ms["master1"].config.replace('nsslapd-listen-backlog-size', '-1')

    with pytest.raises(ldap.LDAPError):
        topology_m2.ms["master1"].config.replace('nsslapd-listen-backlog-size', 'ZZ')

    topology_m2.ms["master1"].config.replace('nsslapd-listen-backlog-size', default_val)


def test_config_deadlock_policy(topology_m2):
    """Check that nsslapd-db-deadlock-policy acted as expected

    :ID: a24e25fd-bc15-47fa-b018-372f6a2ec59c
    :setup: MMR with two masters
    :steps:
        1. Search for nsslapd-db-deadlock-policy and check if
           it contains a default value
        2. Set nsslapd-db-deadlock-policy to a positive value
        3. Set nsslapd-db-deadlock-policy to a negative value
        4. Set nsslapd-db-deadlock-policy to an invalid value
        5. Set nsslapd-db-deadlock-policy back to a default value
    :expectedresults:
        1. Search should be a successful and should contain a default value
        2. nsslapd-db-deadlock-policy should be successfully set
        3. nsslapd-db-deadlock-policy should be successfully set
        4. Modification with an invalid value should throw an error
        5. nsslapd-db-deadlock-policy should be successfully set
    """

    default_val = b'9'

    ldbmconfig = LDBMConfig(topology_m2.ms["master1"])

    deadlock_policy = ldbmconfig.get_attr_val_bytes('nsslapd-db-deadlock-policy')
    assert deadlock_policy == default_val


    # Try a range of valid values
    for val in ('0', '5', '9'):
        ldbmconfig.replace('nsslapd-db-deadlock-policy', val)

    # Try a range of invalid values
    for val in ('-1', '10'):
        with pytest.raises(ldap.LDAPError):
            ldbmconfig.replace('nsslapd-db-deadlock-policy', val)

    # Cleanup - undo what we've done
    ldbmconfig.replace('nsslapd-db-deadlock-policy', deadlock_policy)


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
