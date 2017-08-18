# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import ldap
import pytest
from lib389.topologies import topology_m2
from lib389._constants import (DEFAULT_SUFFIX, HOST_MASTER_2, PORT_MASTER_2, TASK_WAIT)

from lib389.idm.user import (TEST_USER_PROPERTIES, UserAccounts)

def test_referral_during_tot(topology_m2):

    master1 = topology_m2.ms["master1"]
    master2 = topology_m2.ms["master2"]

    # Create a bunch of entries on master1
    ldif_dir = master1.get_ldif_dir()
    import_ldif = ldif_dir + '/ref_during_tot_import.ldif'
    master1.buildLDIF(10000, import_ldif)

    master1.stop()
    try:
        master1.ldif2db(bename=None, excludeSuffixes=None, encrypt=False, suffixes=[DEFAULT_SUFFIX], import_file=import_ldif)
    except:
        pass
    # master1.tasks.importLDIF(suffix=DEFAULT_SUFFIX, input_file=import_ldif, args={TASK_WAIT: True})
    master1.start()
    users = UserAccounts(master1, DEFAULT_SUFFIX, rdn='ou=Accounting')

    u = users.create(properties=TEST_USER_PROPERTIES)
    u.set('userPassword', 'password')

    binddn = u.dn
    bindpw = 'password'

    # Now export them to master2
    master1.agreement.init(DEFAULT_SUFFIX, HOST_MASTER_2, PORT_MASTER_2)

    # While that's happening try to bind as a user to master 2
    # This should trigger the referral code.
    for i in range(0, 100):
        conn = ldap.initialize(master2.toLDAPURL())
        conn.set_option(ldap.OPT_REFERRALS, False)
        try:
            conn.simple_bind_s(binddn, bindpw)
            conn.unbind_s()
        except ldap.REFERRAL:
            pass

    # Done.


