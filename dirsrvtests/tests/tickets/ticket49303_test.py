# --- BEGIN COPYRIGHT BLOCK ---
# Copyright (C) 2017 Red Hat, Inc.
# All rights reserved.
#
# License: GPL (version 3 or any later version).
# See LICENSE for details.
# --- END COPYRIGHT BLOCK ---
#
import time
import logging
import os
import subprocess
import pytest
from lib389.topologies import topology_st as topo
from lib389.nss_ssl import NssSsl

from lib389._constants import SECUREPORT_STANDALONE1, HOST_STANDALONE1

pytestmark = pytest.mark.tier2

DEBUGGING = os.getenv("DEBUGGING", default=False)
if DEBUGGING:
    logging.getLogger(__name__).setLevel(logging.DEBUG)
else:
    logging.getLogger(__name__).setLevel(logging.INFO)
log = logging.getLogger(__name__)


def try_reneg(host, port):
    """
    Connect to the specified host and port with openssl, and attempt to
    initiate a renegotiation.  Returns true if successful, false if not.
    """

    cmd = [
        '/usr/bin/openssl',
        's_client',
        '-connect',
        '%s:%d' % (host, port),
    ]

    try:
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE,
                                stdin=subprocess.PIPE,
                                stderr=subprocess.PIPE)
    except ValueError as e:
        log.info("openssl failed: %s", e)
        proc.kill()

    # This 'R' command is intercepted by openssl and triggers a renegotiation
    proc.communicate(b'R\n')

    # We rely on openssl returning 0 if no errors occured, and 1 if any did
    # (for example, the server rejecting renegotiation and terminating the
    # connection)
    return proc.returncode == 0


def enable_ssl(server, ldapsport):
    server.stop()
    nss_ssl = NssSsl(dbpath=server.get_cert_dir())
    nss_ssl.reinit()
    nss_ssl.create_rsa_ca()
    nss_ssl.create_rsa_key_and_cert()
    server.start()
    server.config.set('nsslapd-secureport', '%s' % ldapsport)
    server.config.set('nsslapd-security', 'on')
    server.sslport = SECUREPORT_STANDALONE1
    server.restart()


def set_reneg(server, state):
    server.encryption.set('nsTLSAllowClientRenegotiation', state)
    time.sleep(1)
    server.restart()


def test_ticket49303(topo):
    """
    Test the nsTLSAllowClientRenegotiation setting.
    """
    sslport = SECUREPORT_STANDALONE1

    log.info("Ticket 49303 - Allow disabling of SSL renegotiation")

    # No value set, defaults to reneg allowed
    enable_ssl(topo.standalone, sslport)
    assert try_reneg(HOST_STANDALONE1, sslport) is True
    log.info("Renegotiation allowed by default - OK")

    # Turn reneg off
    set_reneg(topo.standalone, 'off')
    assert try_reneg(HOST_STANDALONE1, sslport) is False
    log.info("Renegotiation disallowed - OK")

    # Explicitly enable
    set_reneg(topo.standalone, 'on')
    assert try_reneg(HOST_STANDALONE1, sslport) is True
    log.info("Renegotiation explicitly allowed - OK")

    # Set to an invalid value, defaults to allowed
    set_reneg(topo.standalone, 'invalid')
    assert try_reneg(HOST_STANDALONE1, sslport) is True
    log.info("Renegotiation allowed when option is invalid - OK")

    log.info("Ticket 49303 - PASSED")


if __name__ == '__main__':
    # Run isolated
    # -s for DEBUG mode
    CURRENT_FILE = os.path.realpath(__file__)
    pytest.main("-s %s" % CURRENT_FILE)
