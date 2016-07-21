import os
import sys
import time
import ldap
import logging
import pytest

import nss

from lib389 import DirSrv, Entry, tools, tasks
from lib389.tools import DirSrvTools
from lib389._constants import *
from lib389.properties import *
from lib389.tasks import *
from lib389.utils import *

from subprocess import check_output

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


class TopologyStandalone(object):
    def __init__(self, standalone):
        standalone.open()
        self.standalone = standalone


@pytest.fixture(scope="module")
def topology(request):
    # Creating standalone instance ...
    standalone = DirSrv(verbose=False)
    args_instance[SER_HOST] = HOST_STANDALONE
    args_instance[SER_PORT] = PORT_STANDALONE
    args_instance[SER_SERVERID_PROP] = SERVERID_STANDALONE
    args_instance[SER_CREATION_SUFFIX] = DEFAULT_SUFFIX
    args_standalone = args_instance.copy()
    standalone.allocate(args_standalone)
    instance_standalone = standalone.exists()
    if instance_standalone:
        standalone.delete()
    standalone.create()
    standalone.open()

    # Delete each instance in the end
    def fin():
        standalone.delete()
    request.addfinalizer(fin)

    return TopologyStandalone(standalone)

def check_socket_dh_param_size(hostname, port):
    ### You know why we have to do this?
    # Because TLS and SSL suck. Hard. They are impossible. It's all terrible, burn it all down.
    cmd = "echo quit | openssl s_client -connect {HOSTNAME}:{PORT} -msg -cipher DH | grep -A 1 ServerKeyExchange".format(
        HOSTNAME=hostname,
        PORT=port)
    output = check_output(cmd, shell=True)
    dhheader = output.split('\n')[1]
    # Get rid of all the other whitespace.
    dhheader = dhheader.replace(' ', '')
    # Example is 0c00040b0100ffffffffffffffffadf8
    # We need the bits 0100 here. Which means 256 bytes aka 256 * 8, for 2048 bit.
    dhheader = dhheader[8:12]
    # make it an int, and times 8
    i = int(dhheader, 16) * 8
    return i


def test_ticket48798(topology):
    """
    Test DH param sizes offered by DS.

    """

    # Create a CA
    # This is a trick. The nss db that ships with DS is broken fundamentally.
    ## THIS ASSUMES old nss format. SQLite will bite us!
    for f in ('key3.db', 'cert8.db', 'key4.db', 'cert9.db', 'secmod.db', 'pkcs11.txt'):
        try:
            os.remove("%s/%s" % (topology.standalone.confdir, f ))
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

    topology.standalone.config.enable_ssl(secport=DEFAULT_SECURE_PORT, secargs={'nsSSL3Ciphers': '+all'} )

    topology.standalone.restart(30)

    # Confirm that we have a connection, and that it has DH

    # Open a socket to the port.
    # Check the security settings.
    size = check_socket_dh_param_size(topology.standalone.host, DEFAULT_SECURE_PORT)

    assert(size == 2048)

    # Now toggle the settings.
    mod = [(ldap.MOD_REPLACE, 'allowWeakDHParam', 'on')]
    dn_enc = 'cn=encryption,cn=config'
    topology.standalone.modify_s(dn_enc, mod)

    topology.standalone.restart(30)

    # Check the DH params are less than 1024.
    size = check_socket_dh_param_size(topology.standalone.host, DEFAULT_SECURE_PORT)

    assert(size == 1024)

    log.info('Test complete')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
