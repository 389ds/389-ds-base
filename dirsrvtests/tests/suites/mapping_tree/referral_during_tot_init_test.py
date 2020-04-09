# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2020 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import ldap
import pytest
from lib389.topologies import topology_m2
from lib389._constants import (DEFAULT_SUFFIX)
from lib389.agreement import Agreements
from lib389.idm.user import (TEST_USER_PROPERTIES, UserAccounts)
from lib389.dbgen import dbgen_users
from lib389.utils import ds_is_older

pytestmark = pytest.mark.tier1

@pytest.mark.skipif(ds_is_older("1.4.0.0"), reason="Not implemented")
def test_referral_during_tot(topology_m2):

    master1 = topology_m2.ms["master1"]
    master2 = topology_m2.ms["master2"]

    users = UserAccounts(master2, DEFAULT_SUFFIX)
    u = users.create(properties=TEST_USER_PROPERTIES)
    u.set('userPassword', 'password')
    binddn = u.dn
    bindpw = 'password'

    # Create a bunch of entries on master1
    ldif_dir = master1.get_ldif_dir()
    import_ldif = ldif_dir + '/ref_during_tot_import.ldif'
    dbgen_users(master1, 10000, import_ldif, DEFAULT_SUFFIX)

    master1.stop()
    master1.ldif2db(bename=None, excludeSuffixes=None, encrypt=False, suffixes=[DEFAULT_SUFFIX], import_file=import_ldif)
    master1.start()
    # Recreate the user on m1 also, so that if the init finishes first ew don't lose the user on m2
    users = UserAccounts(master1, DEFAULT_SUFFIX)
    u = users.create(properties=TEST_USER_PROPERTIES)
    u.set('userPassword', 'password')
    # Now export them to master2
    agmts = Agreements(master1)
    agmts.list()[0].begin_reinit()

    # While that's happening try to bind as a user to master 2
    # This should trigger the referral code.
    referred = False
    for i in range(0, 100):
        conn = ldap.initialize(master2.toLDAPURL())
        conn.set_option(ldap.OPT_REFERRALS, False)
        try:
            conn.simple_bind_s(binddn, bindpw)
            conn.unbind_s()
        except ldap.REFERRAL:
            referred = True
            break
    # Means we never go a referral, should not happen!
    assert referred

    # Done.
