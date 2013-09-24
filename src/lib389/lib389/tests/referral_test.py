# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#

import ldap
import logging

from lib389.referral import Referrals, Referral

from lib389.topologies import topology_st

from lib389._constants import DEFAULT_SUFFIX

log = logging.getLogger(__name__)

def test_referral(topology_st):
    standalone = topology_st.standalone

    rs = Referrals(standalone, DEFAULT_SUFFIX)

    r = rs.create(properties={
        'cn': 'testref',
        'ref': 'ldap://localhost:38901/ou=People,dc=example,dc=com'
    })

    r_all = rs.list()
    assert(len(r_all) == 1)
    r2 = r_all[0]
    assert(r2.present('ref', 'ldap://localhost:38901/ou=People,dc=example,dc=com'))


