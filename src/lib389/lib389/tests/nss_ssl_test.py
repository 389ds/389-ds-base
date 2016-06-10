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

INSTANCE_PORT = 54321
INSTANCE_SERVERID = 'nss_ssl'
# INSTANCE_PREFIX   = None


class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):
    standalone = DirSrv(verbose=False)
    standalone.log.debug("Instance allocated")
    args = {SER_HOST: LOCALHOST,
            SER_PORT: INSTANCE_PORT,
            # SER_DEPLOYED_DIR:  INSTANCE_PREFIX,
            SER_SERVERID_PROP: INSTANCE_SERVERID}
    standalone.allocate(args)
    if standalone.exists():
        standalone.delete()
    standalone.create()
    standalone.open()

    def fin():
        standalone.delete()
    request.addfinalizer(fin)

    return TopologyStandalone(standalone)


def test_nss(topology):
    """
    Build a nss db, create a ca, and check that it is correct.
    """

    # This is a trick. The nss db that ships with DS is broken fundamentally.
    # THIS ASSUMES old nss format. SQLite will bite us!
    for f in ('key3.db', 'cert8.db', 'key4.db', 'cert9.db', 'secmod.db', 'pkcs11.txt'):
        try:
            os.remove("%s/%s" % (topology.standalone.confdir, f))
        except:
            pass

    # Check if the db exists. Should be false.
    assert(topology.standalone.nss_ssl._db_exists() is False)
    # Create it. Should work.
    assert(topology.standalone.nss_ssl.reinit() is True)
    # Check if the db exists. Should be true
    assert(topology.standalone.nss_ssl._db_exists() is True)

    # Check if ca exists. Should be false.
    assert(topology.standalone.nss_ssl._rsa_ca_exists() is False)
    # Create it. Should work.
    assert(topology.standalone.nss_ssl.create_rsa_ca() is True)
    # Check if ca exists. Should be true
    assert(topology.standalone.nss_ssl._rsa_ca_exists() is True)

    # Check if we have a server cert / key. Should be false.
    assert(topology.standalone.nss_ssl._rsa_key_and_cert_exists() is False)
    # Create it. Should work.
    assert(topology.standalone.nss_ssl.create_rsa_key_and_cert() is True)
    # Check if server cert and key exist. Should be true.
    assert(topology.standalone.nss_ssl._rsa_key_and_cert_exists() is True)


if __name__ == "__main__":
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s -vv %s" % CURRENT_FILE)
