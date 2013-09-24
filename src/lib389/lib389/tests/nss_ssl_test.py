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

DEBUGGING = os.getenv('DEBUGGING', False)

if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)

log = logging.getLogger(__name__)

def test_nss(topo):
    """
    Build a nss db, create a ca, and check that it is correct.
    """

    standalone = topo.standalone

    # This is a trick. The nss db that ships with DS is broken fundamentally.
    # THIS ASSUMES old nss format. SQLite will bite us!
    for f in ('key3.db', 'cert8.db', 'key4.db', 'cert9.db', 'secmod.db', 'pkcs11.txt'):
        try:
            os.remove("%s/%s" % (standalone.confdir, f))
        except:
            pass


    # Check if the db exists. Should be false.
    assert(standalone.nss_ssl._db_exists() is False)
    # Create it. Should work.
    assert(standalone.nss_ssl.reinit() is True)
    # Check if the db exists. Should be true
    assert(standalone.nss_ssl._db_exists() is True)

    # Check if ca exists. Should be false.
    assert(standalone.nss_ssl._rsa_ca_exists() is False)
    # Create it. Should work.
    assert(standalone.nss_ssl.create_rsa_ca() is True)
    # Check if ca exists. Should be true
    assert(standalone.nss_ssl._rsa_ca_exists() is True)

    # Check if we have a server cert / key. Should be false.
    assert(standalone.nss_ssl._rsa_key_and_cert_exists() is False)
    # Create it. Should work.
    assert(standalone.nss_ssl.create_rsa_key_and_cert() is True)
    # Check if server cert and key exist. Should be true.
    assert(standalone.nss_ssl._rsa_key_and_cert_exists() is True)

    # Check making users certs. They should never conflict
    for user in ('william', 'noriko', 'mark'):
        assert(standalone.nss_ssl._rsa_user_exists(user) is False)
        # Create the user cert
        assert(standalone.nss_ssl.create_rsa_user(user) is True)
        # Assert it exists now
        assert(standalone.nss_ssl._rsa_user_exists(user) is True)


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -vv %s" % CURRENT_FILE)
