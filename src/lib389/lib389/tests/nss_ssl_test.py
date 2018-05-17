# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2015 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
from lib389._constants import *
from lib389 import DirSrv, Entry
import pytest
import os
import logging

from lib389.topologies import topology_st as topo

from lib389.nss_ssl import NssSsl

DEBUGGING = os.getenv('DEBUGGING', False)

if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)

log = logging.getLogger(__name__)


def test_external_ca():
    """Test the behaviour of our system ca database.

    :id: 321c85c1-9cf3-413f-9e26-99a8b327509c

    :steps:
        1. Create the system CA.
        2. Create an nss db
        3. Submit a CSR to the system ca
        4. Import the crt to the db
    :expectedresults:
        1. It works.
        2. It works.
        3. It works.
        4. It works.
    """
    # If it doesn't exist, create a cadb.
    ssca = NssSsl(dbpath='/tmp/lib389-ssca')
    ssca.reinit()
    ssca.create_rsa_ca()

    # Create certificate database.
    tlsdb = NssSsl(dbpath='/tmp/lib389-tlsdb')
    tlsdb.reinit()

    csr = tlsdb.create_rsa_key_and_csr()
    (ca, crt) = ssca.rsa_ca_sign_csr(csr)
    tlsdb.import_rsa_crt(ca, crt)


def test_nss_ssca_users(topo):
    """Validate that we can submit user certs to the ds ca for signing.

    :id: a47e47ed-2056-440b-8797-d13fa89098f6
    :steps:
        1. Find the ssca path.
        2. Assert it exists
        3. Create user certificates from the ssca
    :expectedresults:
        1. It works.
        2. It works.
        3. It works.
    """

    ssca = NssSsl(dbpath=topo.standalone.get_ssca_dir())

    if not ssca._db_exists():
        ssca.reinit()
        if not ssca._rsa_ca_exists():
            ssca.create_rsa_ca()

    # It better exist now!
    assert(ssca._rsa_ca_exists() is True)

    # Check making users certs. They should never conflict
    for user in ('william', 'noriko', 'mark'):
        # Create the user cert
        assert(ssca.create_rsa_user(user) is not None)
        # Assert it exists now
        assert(ssca._rsa_user_exists(user) is True)
    assert(ssca._rsa_user_exists('non_existen') is False)


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -vv %s" % CURRENT_FILE)

