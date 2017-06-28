from subprocess import check_output

import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st

from lib389._constants import DEFAULT_SUFFIX, DEFAULT_SECURE_PORT

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


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


def test_ticket48798(topology_st):
    """
    Test DH param sizes offered by DS.

    """

    # Create a CA
    # This is a trick. The nss db that ships with DS is broken fundamentally.
    ## THIS ASSUMES old nss format. SQLite will bite us!
    for f in ('key3.db', 'cert8.db', 'key4.db', 'cert9.db', 'secmod.db', 'pkcs11.txt'):
        try:
            os.remove("%s/%s" % (topology_st.standalone.confdir, f))
        except:
            pass

    # Check if the db exists. Should be false.
    assert (topology_st.standalone.nss_ssl._db_exists() is False)
    time.sleep(0.5)

    # Create it. Should work.
    assert (topology_st.standalone.nss_ssl.reinit() is True)
    time.sleep(0.5)

    # Check if the db exists. Should be true
    assert (topology_st.standalone.nss_ssl._db_exists() is True)
    time.sleep(0.5)

    # Check if ca exists. Should be false.
    assert (topology_st.standalone.nss_ssl._rsa_ca_exists() is False)
    time.sleep(0.5)

    # Create it. Should work.
    assert (topology_st.standalone.nss_ssl.create_rsa_ca() is True)
    time.sleep(0.5)

    # Check if ca exists. Should be true
    assert (topology_st.standalone.nss_ssl._rsa_ca_exists() is True)
    time.sleep(0.5)

    # Check if we have a server cert / key. Should be false.
    assert (topology_st.standalone.nss_ssl._rsa_key_and_cert_exists() is False)
    time.sleep(0.5)

    # Create it. Should work.
    assert (topology_st.standalone.nss_ssl.create_rsa_key_and_cert() is True)
    time.sleep(0.5)

    # Check if server cert and key exist. Should be true.
    assert (topology_st.standalone.nss_ssl._rsa_key_and_cert_exists() is True)
    time.sleep(0.5)

    topology_st.standalone.config.enable_ssl(secport=DEFAULT_SECURE_PORT, secargs={'nsSSL3Ciphers': '+all'})

    topology_st.standalone.restart(30)

    # Confirm that we have a connection, and that it has DH

    # Open a socket to the port.
    # Check the security settings.
    size = check_socket_dh_param_size(topology_st.standalone.host, DEFAULT_SECURE_PORT)

    assert (size == 2048)

    # Now toggle the settings.
    mod = [(ldap.MOD_REPLACE, 'allowWeakDHParam', 'on')]
    dn_enc = 'cn=encryption,cn=config'
    topology_st.standalone.modify_s(dn_enc, mod)

    topology_st.standalone.restart(30)

    # Check the DH params are less than 1024.
    size = check_socket_dh_param_size(topology_st.standalone.host, DEFAULT_SECURE_PORT)

    assert (size == 1024)

    log.info('Test complete')


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
