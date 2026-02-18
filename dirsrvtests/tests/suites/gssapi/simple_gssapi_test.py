# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---

from lib389.topologies import topology_st_gssapi, gssapi_ack
from lib389.idm.user import UserAccounts

from lib389.saslmap import SaslMappings

from lib389._constants import DEFAULT_SUFFIX

import ldap
import subprocess
import os
import pytest

pytestmark = pytest.mark.tier1

@pytest.fixture(scope='module')
def testuser(topology_st_gssapi):
    # Create a user
    users = UserAccounts(topology_st_gssapi.standalone, DEFAULT_SUFFIX)
    user = users.create(properties={
        'uid': 'testuser',
        'cn' : 'testuser',
        'sn' : 'user',
        'uidNumber' : '1000',
        'gidNumber' : '2000',
        'homeDirectory' : '/home/testuser'
    })
    # Give them a krb princ
    user.create_keytab()
    # Make krb5 config readable by everyone for the tests to work
    os.chmod(user._instance.realm.krb5confrealm, 0o644)
    return user

@gssapi_ack
def test_gssapi_bind(topology_st_gssapi, testuser):
    """Test that we can bind with GSSAPI

    :id: 894a4c27-3d4c-4ba3-aa33-2910032e3783

    :setup: standalone gssapi instance

    :steps:
        1. Bind with sasl/gssapi
    :expectedresults:
        1. Bind succeeds

    """
    conn = testuser.bind_gssapi()
    assert(conn.whoami_s() == "dn: %s" % testuser.dn.lower())

@gssapi_ack
def test_invalid_sasl_map(topology_st_gssapi, testuser):
    """Test that auth fails when we can not map a user.

    :id: dd4218eb-9237-4611-ba2f-1781391cadd1

    :setup: standalone gssapi instance

    :steps:
        1. Invalidate a sasl map
        2. Attempt to bind
    :expectedresults:
        1. The sasl map is invalid.
        2. The bind fails.
    """
    saslmaps = SaslMappings(topology_st_gssapi.standalone)
    saslmap = saslmaps.get('suffix map')
    saslmap.set('nsSaslMapFilterTemplate', '(invalidattr=\\1)')

    with pytest.raises(ldap.INVALID_CREDENTIALS):
        conn = testuser.bind_gssapi()

    saslmap.set('nsSaslMapFilterTemplate', '(uid=\\1)')

@gssapi_ack
def test_missing_user(topology_st_gssapi):
    """Test that binding with no user does not work.

    :id: 109b5ab8-6556-4222-92d6-398476a50d30

    :setup: standalone gssapi instance

    :steps:
        1. Create a principal with a name that is not mappable
        2. Attempt to bind
    :expectedresults:
        1. The principal is created
        2. The bind fails.
    """
    # Make a principal and bind with no user.
    st = topology_st_gssapi.standalone
    st.realm.create_principal("doesnotexist")
    st.realm.create_keytab("doesnotexist", "/tmp/doesnotexist.keytab")
    # Now try to bind.
    subprocess.call(['kdestroy', '-A'])
    os.environ["KRB5_CLIENT_KTNAME"] = "/tmp/doesnotexist.keytab"

    conn = ldap.initialize(st.toLDAPURL())
    sasltok = ldap.sasl.gssapi()

    with pytest.raises(ldap.INVALID_CREDENTIALS):
        conn.sasl_interactive_bind_s('', sasltok)

@gssapi_ack
def test_support_mech(topology_st_gssapi, testuser):
    """Test allowed sasl mechs works when GSSAPI is allowed

    :id: 6ec80aca-00c4-4141-b96b-3ae8837fc751

    :setup: standalone gssapi instance

    :steps:
        1. Add GSSAPI to allowed sasl mechanisms.
        2. Attempt to bind
    :expectedresults:
        1. The allowed mechs are changed.
        2. The bind succeeds.
    """
    topology_st_gssapi.standalone.config.set('nsslapd-allowed-sasl-mechanisms', 'GSSAPI EXTERNAL ANONYMOUS')
    conn = testuser.bind_gssapi()
    assert(conn.whoami_s() == "dn: %s" % testuser.dn.lower())

@gssapi_ack
def test_rejected_mech(topology_st_gssapi, testuser):
    """Test allowed sasl mechs fail when GSSAPI is not allowed.

    :id: 7896c756-6f65-4390-a844-12e2eec19675

    :setup: standalone gssapi instance

    :steps:
        1. Add GSSAPI to allowed sasl mechanisms.
        2. Attempt to bind
    :expectedresults:
        1. The allowed mechs are changed.
        2. The bind fails.
    """
    topology_st_gssapi.standalone.config.set('nsslapd-allowed-sasl-mechanisms', 'EXTERNAL ANONYMOUS')
    with pytest.raises(ldap.STRONG_AUTH_NOT_SUPPORTED):
        conn = testuser.bind_gssapi()
    topology_st_gssapi.standalone.config.set('nsslapd-allowed-sasl-mechanisms', 'GSSAPI EXTERNAL ANONYMOUS')

