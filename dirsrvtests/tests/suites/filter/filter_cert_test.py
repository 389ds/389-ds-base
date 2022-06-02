# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ----


"""
verify and testing  Filter from a search
"""

import os
import pytest

from lib389._constants import DEFAULT_SUFFIX
from lib389.topologies import topology_st as topo
from lib389.idm.user import UserAccounts
from lib389.idm.account import Accounts
from lib389.nss_ssl import NssSsl
from lib389.utils import search_filter_escape_bytes

pytestmark = pytest.mark.tier1


def test_positive(topo):
    """Test User certificate field

        :id: e984ac40-63d1-4176-ad1e-0cbe71391b5f
        :setup: Standalone
        :steps:
            1. Create entries with userCertificate field.
            2. Try to search/filter them with userCertificate field.
        :expectedresults:
            1. Pass
            2. Pass
    """
    # SETUP TLS
    topo.standalone.stop()
    NssSsl(topo.standalone).reinit()
    NssSsl(topo.standalone).create_rsa_ca()
    NssSsl(topo.standalone).create_rsa_key_and_cert()
    # Create  user
    NssSsl(topo.standalone).create_rsa_user('testuser1')
    NssSsl(topo.standalone).create_rsa_user('testuser2')
    # Creating cert users
    topo.standalone.start()
    users_people = UserAccounts(topo.standalone, DEFAULT_SUFFIX)
    for count in range(1, 3):
        user = users_people.create_test_user(uid=count, gid=count)
        tls_locs = NssSsl(topo.standalone).get_rsa_user(f'testuser{count}')
        #  {'ca': ca_path, 'key': key_path, 'crt': crt_path}
        user.enroll_certificate(tls_locs['crt_der_path'])

    assert Accounts(topo.standalone, DEFAULT_SUFFIX).filter("(usercertificate=*)")
    assert Accounts(topo.standalone, DEFAULT_SUFFIX).filter("(userCertificate;binary=*)")
    user1_cert = users_people.list()[0].get_attr_val("userCertificate;binary")
    assert Accounts(topo.standalone, DEFAULT_SUFFIX).filter(
        f'(userCertificate;binary={search_filter_escape_bytes(user1_cert)})')[0].dn == \
           'uid=test_user_1,ou=people,dc=example,dc=com'
    user2_cert = users_people.list()[1].get_attr_val("userCertificate;binary")
    assert Accounts(topo.standalone, DEFAULT_SUFFIX).filter(
        f'(userCertificate;binary={search_filter_escape_bytes(user2_cert)})')[0].dn == \
           'uid=test_user_2,ou=people,dc=example,dc=com'


if __name__ == '__main__':
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -v %s" % CURRENT_FILE)
