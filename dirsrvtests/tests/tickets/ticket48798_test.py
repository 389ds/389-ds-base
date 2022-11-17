# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2022 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
from subprocess import check_output

import pytest
from lib389.tasks import *
from lib389.utils import *
from lib389.topologies import topology_st
from lib389.config import Encryption

from lib389._constants import DEFAULT_SUFFIX, DEFAULT_SECURE_PORT

pytestmark = pytest.mark.tier2

logging.getLogger(__name__).setLevel(logging.DEBUG)
log = logging.getLogger(__name__)


def check_socket_dh_param_size(hostname, port):
    ### You know why we have to do this?
    # Because TLS and SSL suck. Hard. They are impossible. It's all terrible, burn it all down.
    cmd = "echo quit | openssl s_client -connect {HOSTNAME}:{PORT} -msg -cipher DH | grep -A 1 ServerKeyExchange".format(
        HOSTNAME=hostname,
        PORT=port)
    output = check_output(cmd, shell=True)
    dhheader = output.split(b'\n')[1]
    # Get rid of all the other whitespace.
    dhheader = dhheader.replace(b' ', b'')
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
    topology_st.standalone.enable_tls()

    # Confirm that we have a connection, and that it has DH

    # Open a socket to the port.
    # Check the security settings.
    size = check_socket_dh_param_size(topology_st.standalone.host, topology_st.standalone.sslport)

    assert size == 2048

    # Now toggle the settings.
    enc = Encryption(topology_st.standalone)
    enc.set('allowWeakDHParam', 'on')

    topology_st.standalone.restart()

    # Check the DH params are less than 1024.
    size = check_socket_dh_param_size(topology_st.standalone.host, topology_st.standalone.sslport)

    assert size == 1024

if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
